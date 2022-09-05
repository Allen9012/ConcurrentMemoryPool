#pragma once

#include "Common.h"



class CentralCache
{
public:
	//thread cache �������ģ�����CentralCache����Ͱ����


	//���õ���ģʽ
	static CentralCache* GetInstance()
	{
		return &_sInst;	
	}

	//��central cache �л�ȡһ�������Ķ����thread cache
	size_t FetchRangeObject(void*& start,void *& end, size_t batchNum,size_t byte_size);

	//��ȡspan����						//һ��������ֽ���
	Span* GetOneSpan(SpanList& list, size_t byte_size);


	//����span
	//�����ͷŵ�ʱ�򻹲�һ�����ͷŵ�һ��Span��
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	CentralCache(){}
public:
	//���ɿ�������
	CentralCache(const CentralCache&) = delete;
	

private:
	SpanList _spanLists[NFREELIST];  //spanͰ
	static CentralCache _sInst;  //��Ҫ��.h�ж��壬����ᱻ����ļ�����
};