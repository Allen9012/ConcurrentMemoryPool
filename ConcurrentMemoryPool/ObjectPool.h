#pragma once
#include"Common.h"
//定长内存池
//用模板控制了大小
//template<size_t N>


namespace fixed_length_pool
{
	template <class T>
	class ObjectPool
	{
	public:
		T* New()
		{
			T* obj = nullptr;

			//优先还回来的内存块重复利用
			if (_freeList)
			{
				//头删操作
				void* next = *((void**)_freeList); //下一个节点的地址
				obj = (T*)_freeList;
				_freeList = next; //切片出去了
			}

			else
			{
				//一个对象都不够的话，就重新申请
				if (_remainBytes < sizeof(T))
				{
					_remainBytes = 128 * 1024;	//默认128字节，一次16页
					// _memory = (char*)malloc(_remainBytes);  //128KB
					_memory = (char*)::SystemAlloc(_remainBytes >> 13); //除8K换算出页数


					//逻辑上切一个T大小的对象
					if (_memory == nullptr)
						throw std::bad_alloc();
				}

				obj = (T*)_memory;
				const size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);

				_memory += objSize; //偏移
				_remainBytes -= objSize;
			}
			//定位new显示调用T的构造函数初始化
			new(obj)T;
			return obj;
		}

		void Delete(T* obj)
		{
			//调用析构函数
			obj->~T();
			*(void**)obj = _freeList;
			_freeList = obj;
		}

	private:
		char* _memory = nullptr; //指向大块内存的指针。
		int _remainBytes = 0; //大块内存切分过程中剩余字节数。
		void* _freeList = nullptr; //还回来过程中链接的自由链表的头指针。
	};


	// struct TreeNode
	// {
	// 	int _val;
	// 	TreeNode* _left;
	// 	TreeNode* _right;
	//
	// 	TreeNode()
	// 		: _val(0)
	// 		  , _left(nullptr)
	// 		  , _right(nullptr)
	// 	{
	// 	}
	// };
	//
	// void TestObjectPool()
	// {
	// 	// 申请释放的轮次
	// 	const size_t Rounds = 3;
	// 	// 每轮申请释放多少次
	// 	const size_t N = 100000;
	// 	size_t begin1 = clock();
	// 	vector<TreeNode*> v1;
	// 	v1.reserve(N);
	// 	for (size_t j = 0; j < Rounds; ++j)
	// 	{
	// 		for (int i = 0; i < N; ++i)
	// 		{
	// 			v1.push_back(new TreeNode);
	// 		}
	// 		for (int i = 0; i < N; ++i)
	// 		{
	// 			delete v1[i];
	// 		}
	// 		v1.clear();
	// 	}
	// 	size_t end1 = clock();
	// 	ObjectPool<TreeNode> TNPool;
	// 	size_t begin2 = clock();
	// 	std::vector<TreeNode*> v2;
	// 	v2.reserve(N);
	// 	for (size_t j = 0; j < Rounds; ++j)
	// 	{
	// 		for (int i = 0; i < N; ++i)
	// 		{
	// 			v2.push_back(TNPool.New());
	// 		}
	// 		for (int i = 0; i < 100000; ++i)
	// 		{
	// 			TNPool.Delete(v2[i]);
	// 		}
	// 		v2.clear();
	// 	}
	// 	size_t end2 = clock();
	// 	cout << "new cost time:" << end1 - begin1 << endl;
	// 	cout << "object pool cost time:" << end2 - begin2 << endl;
	// }
}
