#include "ThreadCache.h"

#include "CentralCache.h"

void *ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//慢开始的调节算法

	/* 1. 最开始的时候不会依次向中心缓存要太多，因为一下太多了就会用不完
	 * 2. 如果你不要这个size大小的 内存需求，那么batchNum就会向上增长到上限
	 * 3. size越大，一次向中心缓存要的batch就小
	 * 4. size越小，一次向中心缓存要的batch就大
	 */

	//						自己有一个最小值							这里的设置作为一个获取的上限
	size_t batchNum = min(_freeLists[index].MaxSize(),SizeAlignment::NumMoveSize(size));

	if(batchNum == _freeLists[index].MaxSize())
	{
		_freeLists->MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;

	//返回值是实际返回的值
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObject(start, end, batchNum, size);

	assert(actualNum >= 1); //至少有一个

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
	//申请内容不可以超过256K
	assert(size <= MAX_BYTES);
	//获取对齐的字节
	size_t alignSize = SizeAlignment::RoundUp(size);
	//获得桶的下标
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

	//找到对应的桶插进去
	size_t index = SizeAlignment::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时就开始还一段list给central cache
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
