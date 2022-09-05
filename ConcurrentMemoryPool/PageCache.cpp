#include "PageCache.h"


PageCache PageCache::_sInstan;

Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	// //���ʵ�ʱ��Ҫ����������ʹ��unique������Ϊ��������������Լ�������ʹ�÷���
	// std::unique_lock<std::mutex> lock(_pagemtx);
	//
	// auto ret = _idSpanMap.find(id);
	// if (ret != _idSpanMap.end())
	// {
	// 	return ret->second;
	// }
	// else
	// {
	// 	assert(false);
	// 	return nullptr;
	// }

	//ʹ�û�����֮�����ڲ���Ҫ������
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;

}


Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	//����128ҳ��
	if (k > NPAGES - 1)
	{
		//���л�ȡ
		void* ptr = SystemAlloc(k);
		// Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		// _idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId,  span);

		return span;
	}

	//��K��Ͱ������û��span
	if (!_spanLists[k].Empty())
	{
		//��pageͰ�����ó�������central cache
		Span* kSpan = _spanLists[k].PopFront();

		// ����id��span��ӳ�䣬����central cache����С���ڴ�ʱ�����Ҷ�Ӧ��span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			// _idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);

		}

		return kSpan;
	}

	//���һ�º���� Ͱ������û��span������еĻ������������з�
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//��
			// Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			//��nSpan��ͷ����kҳ����
			kSpan->_pageId = nSpan->_pageId; //�޸���ʼ��ַ
			kSpan->_n = k; //���kҳ

			nSpan->_pageId += k; //��ʼҳ���޸�
			nSpan->_n -= k; //ҳ���޸�
			//��ʣ�µĹҵ���Ӧӳ���λ��
			_spanLists[nSpan->_n].PushFront(nSpan);

			/*����ӳ��*/

			//�洢nSpan����λҳ�ź�ӳ�䣬������Ϊ��pagecache�ϲ���ʱ��ʹ�õ�
			//���кϲ�����
			// _idSpanMap[nSpan->_pageId] = nSpan;
			// _idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//����id��span��ӳ��,kSpan����Ҫȫ��ӳ�佨����,Ϊ��centralcacheʹ�õ�
			for (PAGE_ID j = 0; j < kSpan->_n; ++j)
			{
				// _idSpanMap[kSpan->_pageId + j] = kSpan;
				_idSpanMap.set(kSpan->_pageId + j,kSpan);

			}


			return kSpan;
		}
	}
	//�Ѿ�û���κ�Span��
	// Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();

	void* ptr = SystemAlloc(NPAGES - 1); //128
	//ָ��ת��ҳ��
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT; //��ʼҳ��
	bigSpan->_n = NPAGES - 1;
	//����
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	//������������ظ����ݹ�����Լ�s���൱�ڵ�һ����һ�������룬�ڶ��鷵��span
	return NewSpan(k);
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//����128ҳֱ���������
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT); //��id�õ�ָ���ַ
		SystemFree(ptr);
		// delete span;
		_spanPool.Delete(span);

		return;
	}


	//��ǰ�ϲ�
	while (1)
	{
		//��spanǰ���ҳ�����Խ��кϲ��������ڴ���Ƭ����
		PAGE_ID prevId = span->_pageId - 1;

		// auto ret = _idSpanMap.find(prevID);
		// //1. ǰ��ҳ��û�У����ϲ���
		// if (ret == _idSpanMap.end())
		// {
		// 	break;
		// }

		auto ret = (Span*)_idSpanMap.get(prevId);
		if(ret == nullptr)
		{
			break;	
		}

		//2. ǰ�����ڵ�span��ʹ�ã����ϲ���
		Span* prevSpan = ret;
		if (ret->_isUse == true)
		{
			break;
		}
		//3. �������spanҳ����Ҳ���ܹ���
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;
		//��Ӧ��Ͱ�����Ƴ�
		_spanLists[prevSpan->_n].Erase(prevSpan);
		// delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	// ���ϲ�
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		// auto ret = _idSpanMap.find(nextId);
		// //1. �����ҳ��û�У����ϲ���
		// if (ret == _idSpanMap.end())
		// {
		// 	break;
		// }
		Span* ret = (Span*)_idSpanMap.get(nextId);
		if(ret == nullptr)
		{
			break;
		}

		//2. �������ڵ�span��ʹ�ã����ϲ���
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		//3. �������spanҳ����Ҳ���ܹ���
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;
		//��Ӧ��Ͱ����ɾ��
		_spanLists[nextSpan->_n].Erase(nextSpan);
		// delete nextSpan;
		_spanPool.Delete(nextSpan);
	}
	//���룬Ȼ����ӳ�䣬��ʱ�Ѿ�����ʹ����,������˺ϲ���
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}
