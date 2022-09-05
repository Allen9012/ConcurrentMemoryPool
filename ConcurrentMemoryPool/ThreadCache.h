#pragma once
#include "Common.h"

class ThreadCache
{
public:
	//�����ڴ����
	void *Allocate(size_t size);
	//�ͷ��ڴ����
	void Deallocate(void *ptr, size_t size);
	//��CentralCache�ö���ĺ���
	void *FetchFromCentralCache(size_t index, size_t alignSize);
	//�õ������thread cache
	void ListTooLong(FreeList& list,size_t size);
private:
	FreeList _freeLists[NFREELIST];
};

//ÿһ���̶߳���һ��,ֻ�ڵ�ǰ�ļ��ɼ������������������
static _declspec(thread) ThreadCache *pTLSThreadCache = nullptr;
