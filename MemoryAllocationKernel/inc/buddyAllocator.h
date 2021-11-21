#pragma once
#include"windows.h"

struct node {
	struct node* prev;
	struct node* next;
};

typedef struct node Node;


struct buddy_allocator
{
	unsigned max_buffer_size;
	HANDLE mutex;
	void* start_addr;
	Node** free_blocks;
};

typedef struct buddy_allocator buddyAllocator;


buddyAllocator* createBuddyAllocator(void* p, unsigned num_blocks);

void freeBuddyAllocator(buddyAllocator* buddy);

void* allocateBlock(buddyAllocator* buddy, unsigned size);

void freeBlock(buddyAllocator* buddy, void* addr, unsigned size);

void put(buddyAllocator* buddy, unsigned index, void* p);

void* get(buddyAllocator* buddy, unsigned index);

void split(buddyAllocator* buddy, char* addr, int upper, int lower);

int isBuddy(buddyAllocator* buddy, void* p1, void* p2, unsigned size);

unsigned highestOneBit(unsigned num);
