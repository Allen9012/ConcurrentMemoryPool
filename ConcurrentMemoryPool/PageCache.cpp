#include "PageCache.h"


PageCache PageCache::_sInstan;

Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	// //访问的时候要加锁，这里使用unique锁，因为出了作用域可以自己解锁，使用方便
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

	//使用基数树之后现在不需要加锁了
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;

}


Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	//大于128页就
	if (k > NPAGES - 1)
	{
		//堆中获取
		void* ptr = SystemAlloc(k);
		// Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		// _idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId,  span);

		return span;
	}

	//第K个桶里面有没有span
	if (!_spanLists[k].Empty())
	{
		//从page桶里面拿出来给到central cache
		Span* kSpan = _spanLists[k].PopFront();

		// 建立id和span的映射，方便central cache回收小块内存时，查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			// _idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);

		}

		return kSpan;
	}

	//检查一下后面的 桶里面有没有span，如果有的话，把它进行切分
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//切
			// Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			//在nSpan的头部切k页下来
			kSpan->_pageId = nSpan->_pageId; //修改起始地址
			kSpan->_n = k; //获得k页

			nSpan->_pageId += k; //起始页号修改
			nSpan->_n -= k; //页数修改
			//将剩下的挂到对应映射的位置
			_spanLists[nSpan->_n].PushFront(nSpan);

			/*建立映射*/

			//存储nSpan的首位页号和映射，这里是为了pagecache合并的时候使用的
			//进行合并查找
			// _idSpanMap[nSpan->_pageId] = nSpan;
			// _idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//建立id和span的映射,kSpan是需要全部映射建立的,为了centralcache使用的
			for (PAGE_ID j = 0; j < kSpan->_n; ++j)
			{
				// _idSpanMap[kSpan->_pageId + j] = kSpan;
				_idSpanMap.set(kSpan->_pageId + j,kSpan);

			}


			return kSpan;
		}
	}
	//已经没有任何Span了
	// Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();

	void* ptr = SystemAlloc(NPAGES - 1); //128
	//指针转化页号
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT; //初始页号
	bigSpan->_n = NPAGES - 1;
	//挂载
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	//尽量避免代码重复，递归调用自己s，相当于第一遍走一个堆申请，第二遍返回span
	return NewSpan(k);
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//大于128页直接向堆申请
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT); //从id拿到指针地址
		SystemFree(ptr);
		// delete span;
		_spanPool.Delete(span);

		return;
	}


	//向前合并
	while (1)
	{
		//对span前后的页，尝试进行合并，缓解内存碎片问题
		PAGE_ID prevId = span->_pageId - 1;

		// auto ret = _idSpanMap.find(prevID);
		// //1. 前面页号没有，不合并了
		// if (ret == _idSpanMap.end())
		// {
		// 	break;
		// }

		auto ret = (Span*)_idSpanMap.get(prevId);
		if(ret == nullptr)
		{
			break;	
		}

		//2. 前面相邻的span在使用，不合并了
		Span* prevSpan = ret;
		if (ret->_isUse == true)
		{
			break;
		}
		//3. 超过最大span页数，也不能管理
		if (prevSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;
		//对应的桶里面移除
		_spanLists[prevSpan->_n].Erase(prevSpan);
		// delete prevSpan;
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		// auto ret = _idSpanMap.find(nextId);
		// //1. 后面的页号没有，不合并了
		// if (ret == _idSpanMap.end())
		// {
		// 	break;
		// }
		Span* ret = (Span*)_idSpanMap.get(nextId);
		if(ret == nullptr)
		{
			break;
		}

		//2. 后面相邻的span在使用，不合并了
		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}

		//3. 超过最大span页数，也不能管理
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;
		//对应的桶里面删掉
		_spanLists[nextSpan->_n].Erase(nextSpan);
		// delete nextSpan;
		_spanPool.Delete(nextSpan);
	}
	//连入，然后建立映射，此时已经不在使用了,方便别人合并了
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}
