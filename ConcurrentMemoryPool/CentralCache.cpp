#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_sInst;

//��ȡһ���ǿյ�span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	//������ǰ��PageͰ���滹��û��ʣ���span������List��
	Span* it = list.Begin();
	while (it != list.End())
	{
		//ֻҪ�����ж���ͷ���
		if(it->_freeList != nullptr)
			return it;
		else
			it = it->_next;
	}

	//�Ȱ�centralcache��������������Ļ���������߳��ͷ��ڴ�����Ļ����Ͳ�������
	list._mtx.unlock();

	//û�п��е�Span�ˣ�ֻ����������Ͱ���滻Span���ٲ��о���page�����Ҷ�
	/*����Ҫ��һ��ҳ��*/
	PageCache::GetInstance()->_pagemtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeAlignment::NumMovePage(size));
	span->_isUse = true;
	span->objSize = size;
	PageCache::GetInstance()->_pagemtx.unlock();

	//�Ի�ȡ��span�����з֣�����Ҫ��������Ϊ���ʹ�������̷߳��ʲ������span

	/*�з֣�Ȼ�����*/

	//1. �ҵ���ʼ��ַ
	char* start = (char*)(span->_pageId << PAGE_SHIFT);

	//2. ����span����ڴ�Ĵ�С���ֽ�����
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	//3. �и� + β��
	/*�Ѵ���ڴ��г���������������������������һ����ͷ�ڵ㣬����β��*/
	span->_freeList = start;
	start += size;
	void* tail =span->_freeList;

	int i = 1;
	while(start < end)
	{
		i++;
		NextObj(tail) = start;
		tail = NextObj(tail); //tail������
		start += size;
	}

	/*����ԭ����һ��BUG*/
	//���һ��β�ڵ�û���ÿ�
	NextObj(tail) = nullptr;

	//�ָ���,�к�span���ٹҺŵ�ʱ��Ͱ����ӻ�ȥ
	list._mtx.lock();

	//��������Ҫ����span
	list.PushFront(span);

	return span;
}


size_t CentralCache::FetchRangeObject(void*& start,void *& end, size_t batchNum,size_t size)
{
	size_t index = SizeAlignment::Index(size);
	//����
	_spanLists[index]._mtx.lock();
	//ʵ�����ڵ��߼�

	Span* span = GetOneSpan(_spanLists[index],size);  //��Ӧ��PageͰ������span
	assert(span);
	assert(span->_freeList);

	start = span->_freeList;
	//end��Ҫ������batchNum - 1
	end = start;
	size_t i = 0;
	size_t actualNum = 1; //��Ҫ���صĲ���

	//Ϊ��ֹspan < batchNum����ʹ��whileѭ��
	while(i < batchNum -1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}

	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum; //���±������thread cache�ļ���

	/*//// �����ϵ�
	int j = 0;
	void* cur = start;
	while (cur)
	{
		cur = NextObj(cur);
		++j;
	}

	if (j != actualNum)
	{
		int x = 0;
	}*/

	//����
	_spanLists[index]._mtx.unlock();

	return actualNum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeAlignment::Index(size);

	//����
	_spanLists[index]._mtx.lock();

	//��ô֪���Ŀ��ڴ��Ӧ���ĸ�span��
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		//ͷ������
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount --;
		if (span->_useCount == 0)
		{
			//˵�����е�С���ڴ涼������
			//span���Ի��ո�page cache��page cache������ȥ��ǰ��ҳ�ĺϲ�
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			//������һ�㣬Ͱ�������������߳�Ҳ���Ի�
			_spanLists[index]._mtx.unlock();

			//���滹Ҫ��һ��Page�Ĵ���
			PageCache::GetInstance()->_pagemtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			span->_isUse = false;
			PageCache::GetInstance()->_pagemtx.unlock();

			//���˴���֮�����Ͱ��
			_spanLists[index]._mtx.lock();
		}
		start = next;
	}

	_spanLists[index]._mtx.unlock();
}