#include "Common.h"
#include "PageCache.h"
#include "ThreadCache.h"
#include"ObjectPool.h"

static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeAlignment::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;
		//加锁
		PageCache::GetInstance()->_pagemtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		PageCache::GetInstance()->_pagemtx.unlock();

		//如果是大于256的话不用通过三级缓存来取，直接就获取了size，体现了使用span的好处
		span->objSize = size;

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		if (pTLSThreadCache == nullptr)
		{
			static std::mutex tcMtx;
			static fixed_length_pool::ObjectPool<ThreadCache> tcPool;

			//创建这个threadcache的时候是需要加锁的，防止多个线程创建对象
			tcMtx.lock();
			// pTLSThreadCache = new ThreadCache;
			pTLSThreadCache = tcPool.New();
			tcMtx.unlock();
		}

		// cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;
		return pTLSThreadCache->Allocate(size);
	}
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);

	//这里释放的时候可以直接获取到span的size
	size_t size = span->objSize;  

	if (size > MAX_BYTES)
	{
		//指针转换成页号

		//还回去的时候要加锁
		PageCache::GetInstance()->_pagemtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pagemtx.unlock();
	}

	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}
