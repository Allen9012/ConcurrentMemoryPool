#include "ThreadCache.h"

#include "CentralCache.h"

void *ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//����ʼ�ĵ����㷨

	/* 1. �ʼ��ʱ�򲻻����������Ļ���Ҫ̫�࣬��Ϊһ��̫���˾ͻ��ò���
	 * 2. ����㲻Ҫ���size��С�� �ڴ�������ôbatchNum�ͻ���������������
	 * 3. sizeԽ��һ�������Ļ���Ҫ��batch��С
	 * 4. sizeԽС��һ�������Ļ���Ҫ��batch�ʹ�
	 */

	//						�Լ���һ����Сֵ							�����������Ϊһ����ȡ������
	size_t batchNum = min(_freeLists[index].MaxSize(),SizeAlignment::NumMoveSize(size));

	if(batchNum == _freeLists[index].MaxSize())
	{
		_freeLists->MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	//����ֵ��ʵ�ʷ��ص�ֵ
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObject(start, end, batchNum, size);

	assert(actualNum >= 1); //������һ��

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}


void *ThreadCache::Allocate(size_t size)
{
	//�������ݲ����Գ���256K
	assert(size <= MAX_BYTES);
	//��ȡ������ֽ�
	size_t alignSize = SizeAlignment::RoundUp(size);
	//���Ͱ���±�
	size_t index = SizeAlignment::Index(size);

	if (!_freeLists[index].Empty())                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

void ThreadCache::Deallocate(void *ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);

	//�ҵ���Ӧ��Ͱ���ȥ
	size_t index = SizeAlignment::Index(size);
	_freeLists[index].Push(ptr);

	// �������ȴ���һ������������ڴ�ʱ�Ϳ�ʼ��һ��list��central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index],size);
	}
}

void ThreadCache::ListTooLong(FreeList& list,size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}
