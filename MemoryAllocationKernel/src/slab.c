#include"slab.h"
#include"buddyAllocator.h"
#include "cache.h"
#include "stdio.h"

void kmem_init(void* space, int block_num)
{
	metadata = (Kernel*)space;
	metadata->startMemory = space;
	metadata->numOfBlocks = block_num;
	metadata->buddy = createBuddyAllocator((void*)((char*)space + BLOCK_SIZE), block_num - 1);
	metadata->cacheListHead = 0;
	metadata->otherCaches = 0;
	kmem_cache_t* mainCache = kmem_cache_create("main cache\0", sizeof(kmem_cache_t), 0, 0);
	metadata->bufferCaches = (kmem_cache_t**)((char*)space + sizeof(Kernel));
	metadata->cacheListHead = mainCache;
	for (int i = 5; i < 18; i++)
	{
		char buffer[10];
		sprintf_s(buffer, 10, "size-%d\0", i);
		kmem_cache_t* cache = kmem_cache_create(buffer, (1 << i), 0, 0);
		metadata->bufferCaches[sizeToIndex(i)] = cache;
	}
}

kmem_cache_t* kmem_cache_create(const char* name, size_t size, void (*ctor)(void*), void (*dtor)(void*))
{
	if (metadata == 0) return 0;
	kmem_cache_t* cache = 0;
	if (metadata->cacheListHead == 0)
	{
		cache = (kmem_cache_t*)((char*)(metadata->startMemory) + BLOCK_SIZE - sizeof(kmem_cache_t) - 1);
		metadata->cacheListHead = cache;
	}
	else
	{
		cache = (kmem_cache_t*)kmem_cache_alloc(metadata->cacheListHead);
	}
	sprintf_s(cache->cacheName, 30, "%s", name);
	cache->constructor = ctor;
	cache->destructor = dtor;
	cache->objectSize = size;
	cache->mutex = CreateMutex(0, FALSE, 0);
	cache->offset = 0;
	cache->allocated = 0;
	cache->shrinked = 0;
	cache->slabHead[NOT_EMPTY] = 0;
	cache->slabHead[FULL] = 0;
	cache->slabHead[FREE] = 0;
	cache->errorCode = 0;

	unsigned order = 0;
	unsigned objects = 0;
	while (1)
	{
		unsigned i = 1 << order;
		objects = (i * BLOCK_SIZE - sizeof(Slab)) / (sizeof(int) + cache->objectSize);
		if (objects == 0)
		{
			order++;
			continue;
		}
		break;
	}
	cache->numObjects = objects;
	cache->blocksNeeded = 1 << order;
	unsigned usableSpace = cache->blocksNeeded * BLOCK_SIZE - sizeof(Slab) - sizeof(unsigned) * cache->numObjects;
	cache->offset = (usableSpace % cache->objectSize) / CACHE_L1_LINE_SIZE;
	cache->nextSlabOffset = 0;
	if (metadata->cacheListHead != cache)
	{
		cache->next = metadata->otherCaches;
		cache->prev = 0;
		if (metadata->otherCaches != 0)
			metadata->otherCaches->prev = cache;
		metadata->otherCaches = cache;
	}
	else
		allocateSlab(cache);
	return cache;
}


void* kmem_cache_alloc(kmem_cache_t* cache)
{
	if (cache == 0) return 0;
	WaitForSingleObject(cache->mutex, INFINITE);
	Slab* slab = 0;
	if (cache->slabHead[NOT_EMPTY] != 0)
		slab = cache->slabHead[NOT_EMPTY];
	else
	{
		if (cache->slabHead[FREE] == 0)
			allocateSlab(cache);
		slab = cache->slabHead[FREE];
		if (slab == 0)
		{
			cache->errorCode = -2;
			return 0;
		}
		cache->slabHead[FREE] = cache->slabHead[FREE]->next;
		slab->next = cache->slabHead[NOT_EMPTY];
		cache->slabHead[NOT_EMPTY] = slab;
	}
	void* space = (void*)((char*)(slab->freeSpace) + slab->head * cache->objectSize);
	int next = slab->freeBuffer[slab->head];
	slab->freeBuffer[slab->head] = -1;
	slab->head = next;
	slab->free--;
	if (slab->free == 0)
	{
		cache->slabHead[NOT_EMPTY] = cache->slabHead[NOT_EMPTY]->next;
		slab->next = cache->slabHead[FULL];
		cache->slabHead[FULL] = slab;
	}
	if (cache->constructor)
		cache->constructor(space);
	cache->allocated = 1;
	ReleaseMutex(cache->mutex);
	return space;
}

void kmem_cache_free(kmem_cache_t* cache, void* obj)
{
	if (cache == 0 || obj == 0)
	{
		if (cache != 0)
			cache->errorCode = -1;
		return;
	}
	WaitForSingleObject(cache->mutex, INFINITE);
	Slab* slab = findSlab(cache, obj);
	if (slab == 0)
	{
		cache->errorCode = -4;
		ReleaseMutex(cache->mutex);
		return;
	}
	int index = ((char*)obj - (char*)slab->freeSpace) / cache->objectSize;
	if (cache->destructor)
		cache->destructor(obj);
	if (cache->constructor)
		cache->constructor(obj);
	slab->freeBuffer[index] = slab->head;
	slab->head = index;
	if (slab->free++ == 0)
	{
		swapSlab(cache, slab, FULL, NOT_EMPTY);
	}
	if (slab->free == cache->numObjects)
	{
		swapSlab(cache, slab, NOT_EMPTY, FREE);
	}
	ReleaseMutex(cache->mutex);
}

void* kmalloc(size_t size)
{
	int index = sizeToIndex(highestOneBit(size)) + 1;
	if (index < 0 || index >= 13) 
	{
		WaitForSingleObject(metadata->cacheListHead->mutex, INFINITE);
		metadata->cacheListHead->errorCode = -10;
		ReleaseMutex(metadata->cacheListHead->mutex);
		return 0;
	}
	void* space = kmem_cache_alloc(metadata->bufferCaches[index]);
	return space;
}

int kmem_cache_shrink(kmem_cache_t* cache)
{
	if (cache == 0) return 0;
	WaitForSingleObject(cache->mutex, INFINITE);
	if (((cache->allocated & cache->shrinked) == 1) || cache->slabHead[FREE] == 0)
	{
		ReleaseMutex(cache->mutex);
		return 0;
	}
	cache->shrinked = 1;
	cache->allocated = 0;
	int blocksFree = 0;
	while (cache->slabHead[FREE] != 0)
	{
		Slab* slab = cache->slabHead[FREE];
		cache->slabHead[FREE] = cache->slabHead[FREE]->next;
		freeBlock(metadata->buddy, slab, highestOneBit(cache->blocksNeeded));
		blocksFree += cache->blocksNeeded;
	}
	return blocksFree;
}


void kfree(const void* objp)
{
	for (int i = 5; i < 18; i++)
	{
		kmem_cache_free(metadata->bufferCaches[sizeToIndex(i)], objp);
	}
}

void kmem_cache_destroy(kmem_cache_t* cache)
{
	if (cache == 0) return;
	WaitForSingleObject(cache->mutex, INFINITE);
	for (int i = 2; i >= 1; i--)
	{
		for (Slab* slab = cache->slabHead[i]; slab != 0;)
		{
			Slab* tmpSlab = slab;
			slab = slab->next;
			for (int j = 0; j < cache->numObjects; j++)
			{
				if (tmpSlab->freeBuffer[j] != -1)
					continue;
				void* obj = (void*)((char*)(tmpSlab->freeSpace) + cache->objectSize * j);
				if (cache->destructor)
					cache->destructor(obj);
				if (tmpSlab->free++ == 0)
				{
					swapSlab(cache, tmpSlab, FULL, NOT_EMPTY);
				}
				if (tmpSlab->free == cache->numObjects)
				{
					swapSlab(cache, tmpSlab, NOT_EMPTY, FREE);
					break;
				}
			}
		}
	}
	cache->shrinked = 0;
	ReleaseMutex(cache->mutex);
	kmem_cache_shrink(cache);
	WaitForSingleObject(cache->mutex, INFINITE);
	if (metadata->cacheListHead != cache)
	{
		if (cache->prev)
			cache->prev->next = cache->next;
		else
			metadata->otherCaches = cache->next;
		if (cache->next)
			cache->next->prev = cache->prev;
		ReleaseMutex(cache->mutex);
		kmem_cache_free(metadata->cacheListHead, cache);
	}
}


void kmem_cache_info(kmem_cache_t* cache)
{
	if (cache == 0)
	{
		printf_s("Pointer to a cache is 0!\n");
		return;
	}
	WaitForSingleObject(cache->mutex, INFINITE);
	int cacheSize = 0;
	int slabNum = 0;
	double occupancy = 0.0;
	for (int i = 0; i < 3; i++)
	{
		for (Slab* slab = cache->slabHead[i]; slab != 0; slab = slab->next)
		{
			slabNum++;
			occupancy = occupancy + (1 - (1.0 * slab->free) / cache->numObjects);
		}
	}
	cacheSize = slabNum * cache->blocksNeeded;
	occupancy = (occupancy / slabNum) * 100;
	printf_s("Cache name: %s\n Object size: %d\n Cache size: %d\n Slab number: %d\n Number of objects in one Slab: %d\n Cache occupancy: %f\n", cache->cacheName, cache->objectSize, cacheSize, slabNum, cache->numObjects, occupancy);
	ReleaseMutex(cache->mutex);
}

int kmem_cache_error(kmem_cache_t* cache)
{
	if (cache != 0)
	{
		WaitForSingleObject(cache->mutex, INFINITE);
		switch (cache->errorCode)
		{
		case 0:return 0;
		case -10:cache->errorCode = 0; printf_s("Given size is out of bounds!\n"); ReleaseMutex(cache->mutex); return -10;
		case -1:cache->errorCode = 0; printf_s("Given pointer to an object is 0!\n"); ReleaseMutex(cache->mutex); return -1;
		case -2:cache->errorCode = 0; printf_s("Not enough memory for a new slab!\n"); ReleaseMutex(cache->mutex); return -2;
		case -3:cache->errorCode = 0; printf_s("Memory full!\n"); ReleaseMutex(cache->mutex); return -3;
		//case -4:cache->errorCode = 0; printf_s("Object is not in proceeded cache!\n"); ReleaseMutex(cache->mutex); return -4;
		}
	}
	return 0;
}