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
		//����
		PageCache::GetInstance()->_pagemtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		PageCache::GetInstance()->_pagemtx.unlock();

		//����Ǵ���256�Ļ�����ͨ������������ȡ��ֱ�Ӿͻ�ȡ��size��������ʹ��span�ĺô�
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

			//�������threadcache��ʱ������Ҫ�����ģ���ֹ����̴߳�������
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

	//�����ͷŵ�ʱ�����ֱ�ӻ�ȡ��span��size
	size_t size = span->objSize;  

	if (size > MAX_BYTES)
	{
		//ָ��ת����ҳ��

		//����ȥ��ʱ��Ҫ����
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
