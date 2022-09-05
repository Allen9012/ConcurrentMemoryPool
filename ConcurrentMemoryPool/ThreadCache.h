#pragma once
#include "Common.h"

class ThreadCache
{
public:
	//申请内存对象
	void *Allocate(size_t size);
	//释放内存对象
	void Deallocate(void *ptr, size_t size);
	//从CentralCache拿对象的函数
	void *FetchFromCentralCache(size_t index, size_t alignSize);
	//拿掉过多的thread cache
	void ListTooLong(FreeList& list,size_t size);
private:
	FreeList _freeLists[NFREELIST];
};

//每一个线程都有一份,只在当前文件可见，避免出现链接问题
static _declspec(thread) ThreadCache *pTLSThreadCache = nullptr;
