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

	//��ȡKҳ��span
	Span* NewSpan(size_t k);

	//����ҳ�źͶ����ӳ��
	Span* MapObjectToSpan(void* obj);

	//�ͷſ���span�ص�PageCache���ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);

public:
	std::mutex _pagemtx; //����
	PageCache(const PageCache&) = delete;
private:
	SpanList _spanLists[NPAGES];  //��Ͱ�����Ų�ͬ��ҳ��
	//ҳ�ź�Span��ӳ��
	// std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	fixed_length_pool::ObjectPool<Span> _spanPool;

	PageCache()
	{}


	static PageCache _sInstan;
};
