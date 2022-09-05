#pragma once
#include"Common.h"
//�����ڴ��
//��ģ������˴�С
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

			//���Ȼ��������ڴ���ظ�����
			if (_freeList)
			{
				//ͷɾ����
				void* next = *((void**)_freeList); //��һ���ڵ�ĵ�ַ
				obj = (T*)_freeList;
				_freeList = next; //��Ƭ��ȥ��
			}

			else
			{
				//һ�����󶼲����Ļ�������������
				if (_remainBytes < sizeof(T))
				{
					_remainBytes = 128 * 1024;	//Ĭ��128�ֽڣ�һ��16ҳ
					// _memory = (char*)malloc(_remainBytes);  //128KB
					_memory = (char*)::SystemAlloc(_remainBytes >> 13); //��8K�����ҳ��


					//�߼�����һ��T��С�Ķ���
					if (_memory == nullptr)
						throw std::bad_alloc();
				}

				obj = (T*)_memory;
				const size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);

				_memory += objSize; //ƫ��
				_remainBytes -= objSize;
			}
			//��λnew��ʾ����T�Ĺ��캯����ʼ��
			new(obj)T;
			return obj;
		}

		void Delete(T* obj)
		{
			//������������
			obj->~T();
			*(void**)obj = _freeList;
			_freeList = obj;
		}

	private:
		char* _memory = nullptr; //ָ�����ڴ��ָ�롣
		int _remainBytes = 0; //����ڴ��зֹ�����ʣ���ֽ�����
		void* _freeList = nullptr; //���������������ӵ����������ͷָ�롣
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
	// 	// �����ͷŵ��ִ�
	// 	const size_t Rounds = 3;
	// 	// ÿ�������ͷŶ��ٴ�
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
