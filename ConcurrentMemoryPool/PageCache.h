#pragma once
#include <unordered_map>

#include "Common.h"
#include "PageMap.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInstan;
	}

	//获取K页的span
	Span* NewSpan(size_t k);

	//计算页号和对象的映射
	Span* MapObjectToSpan(void* obj);

	//释放空闲span回到PageCache，合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

public:
	std::mutex _pagemtx; //大锁
	PageCache(const PageCache&) = delete;
private:
	SpanList _spanLists[NPAGES];  //分桶的连着不同的页数
	//页号和Span的映射
	// std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	fixed_length_pool::ObjectPool<Span> _spanPool;

	PageCache()
	{}


	static PageCache _sInstan;
};
