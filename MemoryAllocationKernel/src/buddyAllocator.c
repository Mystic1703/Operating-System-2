#include "buddyAllocator.h"
#include "slab.h"

#include<stdio.h>


unsigned highestOneBit(unsigned num)
{
	unsigned ret = 0;
	while (num > 0)
	{
		num >>= 1;
		ret++;
	}
	return ret - 1;
}

buddyAllocator* createBuddyAllocator(void* p, unsigned num_blocks)
{
	buddyAllocator* buddy = (buddyAllocator*)p;

	unsigned size = sizeof(buddyAllocator);
	buddy->mutex = CreateMutex(0, FALSE, 0);
	buddy->start_addr = (void*)((char*)p +BLOCK_SIZE);
	buddy->max_buffer_size = highestOneBit(num_blocks);
	buddy->free_blocks = (Node**)((char*)p + sizeof(buddyAllocator));
	for (int i = 0; i <= buddy->max_buffer_size; i++)
	{
		buddy->free_blocks[i] = 0;
	}
	
	put(buddy, buddy->max_buffer_size, ((char*)buddy->start_addr + BLOCK_SIZE));

	char* addr = ((char*)buddy->start_addr + (1 << buddy->max_buffer_size) * BLOCK_SIZE);
	unsigned blocks_remain = num_blocks - (1 << buddy->max_buffer_size) - 1;

	while (blocks_remain > 0)
	{
		unsigned index = 0;
		unsigned temp_blocks_remain = blocks_remain;
		while (temp_blocks_remain > 0)
		{
			index++;
			temp_blocks_remain >>= 1;
		}
		index--;
		put(buddy, index, addr);
		blocks_remain -= (1 << index);
		addr += ((1 << index) * BLOCK_SIZE);
	}

	return buddy;
}

void put(buddyAllocator* buddy, unsigned index, void* _p)
{
	if (buddy == NULL || _p == NULL) return;
	Node* p = (Node*)_p;
	if (buddy->free_blocks[index])
		buddy->free_blocks[index]->prev = p;
	p->next = buddy->free_blocks[index];
	buddy->free_blocks[index] = p;
	p->prev = 0;
}

void* get(buddyAllocator* buddy, unsigned index)
{
	if (buddy == 0) return 0;
	if (index < 0 || index > buddy->max_buffer_size) return 0;
	Node* p = buddy->free_blocks[index];
	if (buddy->free_blocks[index])
	{
		if(buddy->free_blocks[index]->next)
			buddy->free_blocks[index]->next->prev = 0;
		buddy->free_blocks[index] = p->next;
		p->next = 0;
		p->prev = 0;
	}
	return (void*) p;
}

void split(buddyAllocator* buddy,char* addr, int upper, int lower)
{
	while (--upper >= lower)
		put(buddy, upper, addr + (1 << upper) * BLOCK_SIZE);
}

void* allocateBlock(buddyAllocator* buddy, unsigned size)
{
	if (buddy == NULL || size < 0) return 0;
	WaitForSingleObject(buddy->mutex, INFINITE);
	if (size > buddy->max_buffer_size)
	{
		ReleaseMutex(buddy->mutex);
		return 0;
	}

	for (int i = size; i <= buddy->max_buffer_size; i++)
	{
		char* addr = (char*) get(buddy, i);
		if (addr == 0) continue;
		split(buddy, addr, i, size);
		ReleaseMutex(buddy->mutex);
		return (void*) addr;
	}
	ReleaseMutex(buddy->mutex);
	return 0;
}

void freeBlock(buddyAllocator* buddy, void* addr, unsigned size)
{
	if (buddy == NULL || addr == NULL) return;
	WaitForSingleObject(buddy->mutex, INFINITE);
	unsigned flag = 0;
	unsigned index = size;
	for (unsigned i = size; i <= buddy->max_buffer_size; i++)
	{
		for (Node* p = buddy->free_blocks[i]; p != 0; p = p->next)
		{
			if (abs((char*)p - (char*)addr) == ((1 << i) * BLOCK_SIZE))
			{
				if (isBuddy(buddy, p, addr, i) <= 0)
					continue;
				if (p->next)
					p->next->prev = p->prev;

				if (p->prev)
					p->prev->next = p->next;
				else
				{
					buddy->free_blocks[i] = p->next;
					if (p->next)
						p->next->prev = 0;
				}
				if (addr > p)
				{
					addr = (void*)p;
				}
				index = i + 1;
				flag = 1;
				break;
			}
		}
		if (!flag) break;
		flag = 0;
	}
	put(buddy, index, addr);
	ReleaseMutex(buddy->mutex);
}


int isBuddy(buddyAllocator* buddy, void* p1, void* p2, unsigned size)
{
	if (buddy == 0 || p1 == 0 || p2 == 0 || size < 0) return -1;
	if (buddy->max_buffer_size < size) return -2;
	int addr1 = (int)((char*)p1 - (char*)buddy->start_addr);
	int blk_num1 = addr1 / BLOCK_SIZE - 1;
	int addr2 = (int)((char*)p2 - (char*)buddy->start_addr);
	int blk_num2 = addr2 / BLOCK_SIZE - 1;
	if ((blk_num1 ^ (1 << size)) == blk_num2)
		return 1;
	else 
		return 0;
}