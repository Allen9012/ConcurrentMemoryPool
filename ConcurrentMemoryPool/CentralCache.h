#pragma once

#include "Common.h"



class CentralCache
{
public:
	//thread cache 是无锁的，但是CentralCache是有桶锁的


	//采用单例模式
	static CentralCache* GetInstance()
	{
		return &_sInst;	
	}

	//从central cache 中获取一定数量的对象给thread cache
	size_t FetchRangeObject(void*& start,void *& end, size_t batchNum,size_t byte_size);

	//获取span对象						//一个对象的字节数
	Span* GetOneSpan(SpanList& list, size_t byte_size);


	//还给span
	//可能释放的时候还不一定是释放到一个Span中
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	CentralCache(){}
public:
	//不可拷贝构造
	CentralCache(const CentralCache&) = delete;
	

private:
	SpanList _spanLists[NFREELIST];  //span桶
	static CentralCache _sInst;  //不要在.h中定义，否则会被多个文件包含
};