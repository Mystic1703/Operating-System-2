#include "cache.h"
#include "slab.h"
void allocateSlab(kmem_cache_t* cache)
{
	if (cache == 0) return;
	unsigned size = highestOneBit(cache->blocksNeeded);
	void* slabSpace = allocateBlock(metadata->buddy, size);
	if (slabSpace == 0) 
	{
		cache->errorCode = -3;
		return;
	}
	Slab* slab = (Slab*)slabSpace;
	slab->free = cache->numObjects;
	slab->freeBuffer = (int*)((char*)slabSpace + sizeof(Slab));
	for (int i = 0; i < slab->free; i++)
		slab->freeBuffer[i] = i + 1;
	slab->freeBuffer[slab->free - 1] = -1;
	slab->freeSpace = (void*)((char*)slab + sizeof(Slab) + slab->free * sizeof(int) + CACHE_L1_LINE_SIZE * cache->nextSlabOffset);
	if(cache->offset != 0)
		cache->nextSlabOffset = (cache->nextSlabOffset + 1) % cache->offset;
	slab->head = 0;
	slab->next = cache->slabHead[FREE];
	cache->slabHead[FREE] = slab;
}


Slab* findSlab(kmem_cache_t* cache, void* obj)
{
	Slab* slab = cache->slabHead[FULL];
	for (; slab != 0; slab = slab->next)
	{
		if ((slab->freeSpace <= obj) && ((char*)obj < ((char*)slab->freeSpace + cache->numObjects * cache->objectSize)))
		{
			return slab;
		}
	}
	
	slab = cache->slabHead[NOT_EMPTY];
	for (; slab != 0; slab = slab->next)
	{
		if ((slab->freeSpace <= obj) && (obj < ((char*)slab->freeSpace + cache->numObjects * cache->objectSize)))
		{
			return slab;
		}
	}
	return 0;
}

void swapSlab(kmem_cache_t* cache, Slab* slab, int indexFrom, int indexTo)
{
	if (cache->slabHead[indexFrom] == 0) return;
	if (slab == cache->slabHead[indexFrom])
	{
		cache->slabHead[indexFrom] = cache->slabHead[indexFrom]->next;
	}
	else
	{
		Slab* prev = cache->slabHead[indexFrom];
		while (prev->next != slab)
			prev = prev->next;
		prev->next = slab->next;
	}
	slab->next = cache->slabHead[indexTo];
	cache->slabHead[indexTo] = slab;
}