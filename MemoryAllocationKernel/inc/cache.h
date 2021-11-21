#pragma once
#include "buddyAllocator.h"
#include "slab.h"
#include "windows.h"
#define FREE (0)
#define NOT_EMPTY (1)
#define FULL (2)
#define sizeToIndex(x) (x-5)



struct slab {
	int head;
	unsigned free;
	void* freeSpace;
	struct slab* next;
	int* freeBuffer;
};

typedef struct slab Slab;

struct kmem_cache_s {
	const char cacheName[30];
	size_t objectSize;
	void (*constructor)(void*);
	void (*destructor)(void*);
	int errorCode;
	HANDLE mutex;
	unsigned allocated;
	unsigned shrinked;
	Slab* slabHead[3];
	unsigned blocksNeeded;
	unsigned numObjects;
	unsigned offset;
	unsigned nextSlabOffset;
	struct kmem_cache_s* next;
	struct kmem_cache_s* prev;
};


struct kernel_memory {
	void* startMemory;
	int numOfBlocks;
	buddyAllocator* buddy;
	struct kmem_cache_s* cacheListHead;
	struct kmem_cache_s* otherCaches;
	struct kmem_cache_s** bufferCaches;
};

typedef struct kernel_memory Kernel;

Kernel* metadata;

void allocateSlab(kmem_cache_t* cache);

Slab* findSlab(kmem_cache_t* cache, void* obj);

void swapSlab(kmem_cache_t* cache, Slab* slab, int indexFrom, int indexTo);