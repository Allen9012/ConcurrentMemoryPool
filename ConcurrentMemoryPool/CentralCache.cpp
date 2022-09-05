#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_sInst;

//获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	//看看当前的Page桶里面还有没有剩余的span对象在List中
	Span* it = list.Begin();
	while (it != list.End())
	{
		//只要挂着有对象就返回
		if(it->_freeList != nullptr)
			return it;
		else
			it = it->_next;
	}

	//先把centralcache的锁解掉，这样的话如果其他线程释放内存回来的话，就不会阻塞
	list._mtx.unlock();

	//没有空闲的Span了，只能找其他的桶上面换Span，再不行就在page类中找堆
	/*这里要加一个页锁*/
	PageCache::GetInstance()->_pagemtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeAlignment::NumMovePage(size));
	span->_isUse = true;
	span->objSize = size;
	PageCache::GetInstance()->_pagemtx.unlock();

	//对获取的span进行切分，不需要加锁，因为这回使得其他线程访问不到这个span

	/*切分，然后挂载*/

	//1. 找到起始地址
	char* start = (char*)(span->_pageId << PAGE_SHIFT);

	//2. 计算span大块内存的大小（字节数）
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	//3. 切割 + 尾插
	/*把大块内存切成自由链表链接起来，先切下来一个做头节点，方便尾插*/
	span->_freeList = start;
	start += size;
	void* tail =span->_freeList;

	int i = 1;
	while(start < end)
	{
		i++;
		NextObj(tail) = start;
		tail = NextObj(tail); //tail往后走
		start += size;
	}

	/*这里原来是一个BUG*/
	//最后一个尾节点没有置空
	NextObj(tail) = nullptr;

	//恢复锁,切好span，再挂号的时候就把锁加回去
	list._mtx.lock();

	//不能忘记要插入span
	list.PushFront(span);

	return span;
}


size_t CentralCache::FetchRangeObject(void*& start,void *& end, size_t batchNum,size_t size)
{
	size_t index = SizeAlignment::Index(size);
	//加锁
	_spanLists[index]._mtx.lock();
	//实现锁内的逻辑

	Span* span = GetOneSpan(_spanLists[index],size);  //对应的Page桶里面获得span
	assert(span);
	assert(span->_freeList);

	start = span->_freeList;
	//end需要往后走batchNum - 1
	end = start;
	size_t i = 0;
	size_t actualNum = 1; //需要返回的参数

	//为防止span < batchNum这里使用while循环
	while(i < batchNum -1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}

	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum; //更新被分配给thread cache的计数

	/*//// 条件断点
	int j = 0;
	void* cur = start;
	while (cur)
	{
		cur = NextObj(cur);
		++j;
	}

	if (j != actualNum)
	{
		int x = 0;
	}*/

	//解锁
	_spanLists[index]._mtx.unlock();

	return actualNum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeAlignment::Index(size);

	//加锁
	_spanLists[index]._mtx.lock();

	//怎么知道哪块内存对应的哪个span？
	while (start)
	{
		void* next = NextObj(start);

		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		//头插来还
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount --;
		if (span->_useCount == 0)
		{
			//说明所有的小块内存都回来了
			//span可以回收给page cache，page cache可以再去做前后页的合并
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			//还给下一层，桶锁解锁，其他线程也可以还
			_spanLists[index]._mtx.unlock();

			//后面还要加一个Page的大锁
			PageCache::GetInstance()->_pagemtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			span->_isUse = false;
			PageCache::GetInstance()->_pagemtx.unlock();

			//解了大锁之后加上桶锁
			_spanLists[index]._mtx.lock();
		}
		start = next;
	}

	_spanLists[index]._mtx.unlock();
}