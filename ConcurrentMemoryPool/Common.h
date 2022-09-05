#pragma once
#include <cassert>
#include <iostream>
#include <vector>
#include <time.h>
#include <thread>
#include <mutex>
#include <algorithm>
#include <exception>
#include"ObjectPool.h"

using std::vector;
using std::cout;
using std::endl;
using std::min;


//��̬����
static const size_t MAX_BYTES = 256 * 1024; //threadcache�������������ֽ��� 256KB
static const size_t NFREELIST = 208; //����Ͱ����
static const size_t NPAGES = 129; //pagebucket num
static const size_t PAGE_SHIFT = 13;   // ����8K��������13λ 2^13 Ĭ��������Ϊ1ҳ��С8K

#ifdef _WIN32
	#include <windows.h>
#else
	// ...
#endif


//�����������ҳ��
#ifdef _WIN64
typedef unsigned long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#else
// linux
#endif

//ֱ��ȥ�������밴ҳ����ռ�
inline static void *SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void *ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux��brk mmap��
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap��
#endif
}

static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

//�����зֺõ�С�������������

class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);
		// ͷ��
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	void* Pop()
	{
		assert(_freeList);
		//ͷɾ
		void* obj = _freeList;
		_freeList = NextObj(obj);
		_size--;
		return obj;
	}
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		/*//�����ϵ㣬������֤
		int i = 0;
		void* cur = start;
		while(cur)
		{
			cur = NextObj(cur);
			++i;
		}
		if(n != i)
		{//���ּ���֮��û��n��
			int x = 0;	
		}*/

		//�������������ɹ���ֱ�Ӽ�����n��
		_size += n;
	}

	//����ӿڵ������ò�Ҫһ��ȡ�꣬����һ��ȥһ�������ģ���������Լ�����
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);
		start = _freeList;
		end = start;

		for(size_t i = 0; i < n - 1 ; ++i)
		{
			end = NextObj(end);
		}

		//ֱ�������м��
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize()
	{
		return _maxSize;	
	}

	size_t Size()
	{
		return _size;	
	}

private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;
	size_t _size = 0;
};

// ��������С�Ķ���ӳ�����

class SizeAlignment
{
public:
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128]					8byte����	    freelist[0,16)
	// [128+1,1024]				16byte����	    freelist[16,72)
	// [1024+1,8*1024]			128byte����	    freelist[72,128)
	// [8*1024+1,64*1024]		1024byte����     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte����   freelist[184,208)
	/*static inline size_t _RoundUp(size_t size, size_t AlignNum)
	{
		size_t alignSize;

		if(size % 8 != 0)
		{
			if(size % AlignNum != 0)
			{
				//���뵽������
				alignSize = (size / alignSize + 1) * AlignNum ;
			}
			else
			{//���ö���
				alignSize = size;
			}
		}
		return alignSize;
	}*/

	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		else if (size <= 1024)
		{
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024)
		{
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024)
		{
			return _RoundUp(size, 8 * 1024);
		}
		else
		{
			return _RoundUp(size, 1 << PAGE_SHIFT); //��ҳΪ��λ
		}
	}

	/*
	size_t _Index(size_t bytes, size_t alignNum)
	{
		if (bytes % alignNum == 0)
		{
			return bytes / alignNum - 1;
		}
		else
		{
			return bytes / alignNum;
		}
	}
	*/

	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		//Ӧ���ǲ��������

		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// ÿ�������ж��ٸ���
		static int group_array[4] = {16, 56, 56, 56};
		if (bytes <= 128)
		{
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024)
		{
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024)
		{
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024)
		{
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else
		{
			assert(false);
		}

		return -1;
	}


	//�̻߳��������Ļ���һ�λ�ȡ��������������
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		//[2, 512] ,���������ƶ����ٸ�����ģ�������������ֵ
		//С����һ�����������޸�
		//�����һ�����������޵�
		size_t num = MAX_BYTES / size;

		//���ޣ�һ������ܴ������£�ֻȡһ������̫С�ˣ�
		if (num < 2)
		{
			num = 2;
		}

		//���ޣ�8�ֽڵĶ������λ�ȡ̫�ࣩ
		if (num > 512)
		{
			num = 512;
		}
		return num;
	}


	//������Ҫһ����ϵͳ��ȡ����ҳ��
	//�������� 8Bytes - 256KB
								//size->��������Ĵ�С
	static size_t NumMovePage(size_t size)
	{
		size_t num_limit = NumMoveSize(size); //��ȡ�ܻ�ȡ�Ķ��������
		size_t actual_Byte = num_limit * size;	//ʵ������Ķ�����ܴ�С

		size_t npage = actual_Byte >> PAGE_SHIFT;  //��ҳ��ת�� 8K
		if(npage == 0)
			npage = 1;
		return npage;
	}
};

//������ҳΪ��λ�Ĵ���ڴ�
struct Span
{
	PAGE_ID _pageId = 0; //����ڴ���ʼҳ��ҳ��
	size_t _n = 0; //ҳ������

	Span* _next = nullptr; //˫����ṹ
	Span* _prev = nullptr;
	size_t objSize = 0;  //�кõ�С������ڴ�Ĵ�С
	size_t _useCount = 0; //�кõ�С���ڴ棬�������thread cache�ļ���
	void* _freeList = nullptr; //�кõ�С���ڴ����������
	bool _isUse = false; //�Ƿ���ʹ��
};

//��ͷ˫������
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		//prev newspan pos
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);
		//�����ϵ�
		// if(pos == _head)
		// {
		// 	int x = 0;
		// }

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;	
	}


	void PushFront(Span* span)
	{
		Insert(Begin(),span);	
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	bool Empty()
	{
		return _head == _head->_next;	
	}

private:
	Span* _head;

public:
	std::mutex _mtx; //Ͱ��
};
