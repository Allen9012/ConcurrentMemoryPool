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


//静态变量
static const size_t MAX_BYTES = 256 * 1024; //threadcache允许申请的最大字节数 256KB
static const size_t NFREELIST = 208; //最多的桶的数
static const size_t NPAGES = 129; //pagebucket num
static const size_t PAGE_SHIFT = 13;   // 除以8K就是右移13位 2^13 默认这里认为1页大小8K

#ifdef _WIN32
	#include <windows.h>
#else
	// ...
#endif


//条配件编译解决页号
#ifdef _WIN64
typedef unsigned long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#else
// linux
#endif

//直接去堆上申请按页申请空间
inline static void *SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void *ptr = VirtualAlloc(0, kpage << 13, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// linux下brk mmap等
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
	// sbrk unmmap等
#endif
}

static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

//管理切分好的小对象的自由链表

class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);
		// 头插
		NextObj(obj) = _freeList;
		_freeList = obj;
		_size++;
	}

	void* Pop()
	{
		assert(_freeList);
		//头删
		void* obj = _freeList;
		_freeList = NextObj(obj);
		_size--;
		return obj;
	}
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;

		/*//条件断点，测试验证
		int i = 0;
		void* cur = start;
		while(cur)
		{
			cur = NextObj(cur);
			++i;
		}
		if(n != i)
		{//发现计算之后没有n个
			int x = 0;	
		}*/

		//这里假想了这里成功了直接加上了n个
		_size += n;
	}

	//这里接口的设计最好不要一次取完，而是一次去一定数量的，这个可以自己控制
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);
		start = _freeList;
		end = start;

		for(size_t i = 0; i < n - 1 ; ++i)
		{
			end = NextObj(end);
		}

		//直接跳过中间的
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

// 计算对象大小的对齐映射规则

class SizeAlignment
{
public:
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]					8byte对齐	    freelist[0,16)
	// [128+1,1024]				16byte对齐	    freelist[16,72)
	// [1024+1,8*1024]			128byte对齐	    freelist[72,128)
	// [8*1024+1,64*1024]		1024byte对齐     freelist[128,184)
	// [64*1024+1,256*1024]		8*1024byte对齐   freelist[184,208)
	/*static inline size_t _RoundUp(size_t size, size_t AlignNum)
	{
		size_t alignSize;

		if(size % 8 != 0)
		{
			if(size % AlignNum != 0)
			{
				//对齐到对齐数
				alignSize = (size / alignSize + 1) * AlignNum ;
			}
			else
			{//正好对齐
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
			return _RoundUp(size, 1 << PAGE_SHIFT); //以页为单位
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
		//应该是不会溢出的

		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
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


	//线程缓存向中心缓存一次获取对象数量的上限
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		//[2, 512] ,依次批量移动多少个对象的（慢启动）上限值
		//小对象一次批量的上限高
		//大对象一次批量的上限低
		size_t num = MAX_BYTES / size;

		//下限（一个对象很大的情况下，只取一个对象太小了）
		if (num < 2)
		{
			num = 2;
		}

		//上限（8字节的对象依次会取太多）
		if (num > 512)
		{
			num = 512;
		}
		return num;
	}


	//计算需要一次向系统获取几个页？
	//单个对象 8Bytes - 256KB
								//size->单个对象的大小
	static size_t NumMovePage(size_t size)
	{
		size_t num_limit = NumMoveSize(size); //获取能获取的对象的上限
		size_t actual_Byte = num_limit * size;	//实际申请的对象的总大小

		size_t npage = actual_Byte >> PAGE_SHIFT;  //和页的转换 8K
		if(npage == 0)
			npage = 1;
		return npage;
	}
};

//管理以页为单位的大块内存
struct Span
{
	PAGE_ID _pageId = 0; //大块内存起始页的页号
	size_t _n = 0; //页的数量

	Span* _next = nullptr; //双链表结构
	Span* _prev = nullptr;
	size_t objSize = 0;  //切好的小对象的内存的大小
	size_t _useCount = 0; //切好的小块内存，被分配给thread cache的计数
	void* _freeList = nullptr; //切好的小块内存的自由链表
	bool _isUse = false; //是否在使用
};

//带头双向链表
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
		//条件断点
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
	std::mutex _mtx; //桶锁
};
