/* 
	
Copyright (c) 2013 Mhd Adel Al Qodmani

Permission is hereby granted, free of charge, to any person obtaining a copy 
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <unistd.h>  /* Needed for sbrk and brk */
#include <stdint.h> /* Needed for the uint64_t and intptr_t */
#include <stdbool.h> 
#include <stdio.h> /* debug by printing */
#include <assert.h> /* Dragons be flying :P */
#include <errno.h> /* To set errno in case of failure */

#define DEBUG 1 /* set this to 0 if you want to stop the debugging code */
#define NULL ((void*)0) /* Just in case it's not included */
/* Debug print macro that works only when the debug is defined 
	to print something that has no arguments DEBUG_PRINT("%s\n","x");
	We can simply omit the fmt but that would mean fprintf won't be able
	to check the format string and the parameters passed */
static int DEBUG_COUNT;
#define DEBUG_PRINT(fmt, ...) \
	do { \
	if(DEBUG) \
		 fprintf(stderr,"DEBUG %d: "fmt, DEBUG_COUNT++,  __VA_ARGS__);\
	} \
	while(0)
#define FREE_LISTS_COUNT 11 /* How many segragated lists we're maintaining */
/* Sets the free bit in the header to indicate that this block is free */
#define SET_FREE(header) ((header->blockSize)=((header->blockSize) | 0x2))
/* Clears the free bit in the header to indicate this block is allocated */
#define SET_ALLOC(header) ((header->blockSize)=((header->blockSize) & (~0x2)))
/* Sets up the boundry tag - it's a 64-bits of 1 that marks the start & end of
	the heap*/
#define SET_BOUND_TAG(header) ((header->blockSize) = (~(0)))

/* struct definitions */
struct blockHeader/* Used for lists[5-10] */
{
	uint64_t blockSize;/* the LSB is reserved, the 2nd LSB is used to test
				if the block is allocated or free, the 3rd LSB
				is always set to 0.
				The rest count how many words are in the 
				block.
				A word is 8-bytes! */
};

/* static function prototypes */
static inline int pickAppropriateList(size_t size);
static uint64_t *extractFreeBlock(int listNum, size_t size);
static uint64_t *searchList(int listNum, size_t size);
static inline uint64_t *firstFitSearch(int listNum, size_t size);
static inline uint64_t *bestFitSearch(int listNum, size_t size);
static uint64_t *growHeap(size_t size);
static inline int setInitialBoundries(void);
static inline void addFreeBlockToList(int listNum, uint64_t *newBlock);

/* static variables */
static uint64_t *freeLists[FREE_LISTS_COUNT];

/* begin myMalloc */
void *myMalloc(size_t size)
{
	uint64_t *userData; /* The pointer we will return to the user */
	uint64_t *newBlock; /* Used to point to the block added by growHeap */
	size += sizeof(struct blockHeader) * 2; /* The header+footer */
	size = (size+7) & ~7;/* Align the size to 8-byte boundry */
	/* Identify which list to pick from */
	int listNum = pickAppropriateList(size);
	DEBUG_PRINT("malloc: List picked: %d\n", listNum);
	/* Try to extract enough space from the lists */
	userData = extractFreeBlock(listNum, size);
	/* If one of the lists had enough space */
	if(userData != NULL)
	{
		/* Slice and return the rest if you can */
		/* Set up the block header and footer */
		/* Return the block to the user */
		return userData;	
	}
	/* When we can't extract a free block, it means no list has enough 
	space so we grow the heap and add the new space to the list that the 
	request was originally for */
	if((newBlock = growHeap(size)) == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}
	addFreeBlockToList(listNum, newBlock);
	DEBUG_PRINT("malloc: a new block of size %lu was added to list[%d]\n",
			(*newBlock) & (~3), listNum);
	/* We were able to grow the heap */
	userData = extractFreeBlock(listNum, size);
	/* Slice, set up header & footer, return the block to the user */
	userData++;/* Points to the begining of the data */
	return userData;
}
/* end myMalloc */

/* begin pickAppropriateList */
static inline int pickAppropriateList(size_t size)
{
	if(size <= 32) 
	{
		return 0;
	}
	else if(size <= 64) 
	{ 
		return 1;
	}
	else if(size <= 128)
	{
		return 2;
	}
	else if(size <= 256)
	{
		return 3;
	}
	else if(size <= 512)
	{
		return 4;
	}
	else if(size <= 1024)
	{
		return 5;
	}
	else if(size <= 2048)
	{
		return 6;
	}
	else if(size <= 4096)
	{
		return 7;
	}
	else if(size <= 8192)
	{
		return 8;
	}
	else if(size <= 16384)
	{
		return 9;
	}
	else
	{
		return 10;
	}
}
/* end pickAppropriateList */

/* begin extractFreeBlock */
static uint64_t *extractFreeBlock(int listNum, size_t size)
{
/* Searches each list(starting from listNum) for a big-enough free block, 
	if it can't find any it returns NULL.
 */
	void *data = NULL;
	/* Search this list, if you can't find a free-block, search the rest*/
	for(int i = listNum; i < FREE_LISTS_COUNT; i++)
	{
		if((data = searchList(i, size)) != NULL)
			return data;
	}
	DEBUG_PRINT("%s\n", "extractFreeBlock: returning NULL");
	return NULL;
}
/* end extractFreeBlock */

/* begin searchList */
static uint64_t *searchList(int listNum, size_t size)
{
/*  This function searches a particular list for a big-enough block.
	Depending on which list we're searching, we go either best-fit or 
	first fit.
*/
	uint64_t *currentBlock = freeLists[listNum];
	if(listNum <= 4)
	{/* First fit */
		assert(size <= 512);
		return firstFitSearch(listNum, size);
	}
	else
	{/* Best fit */
		return bestFitSearch(listNum, size);
	}
}
/* end searchList */

/* begin firstFitSearch */
static inline uint64_t *firstFitSearch(int listNum, size_t size)
{
	uint64_t *currentBlock = freeLists[listNum];
	struct blockHeader *header;
	if(currentBlock == NULL)
		return NULL;
	DEBUG_PRINT("firstFitSearch: list [%d] is not empy\n", listNum);
	while(currentBlock != NULL)
	{
		header = (struct blockHeader *)currentBlock;
		DEBUG_PRINT("firstFitSearch: blockSize = %lu\n", 
					(header->blockSize & (~3)));
		DEBUG_PRINT("firstFitSearch: requested size = %lu\n",
					size);
		/* Set first 3 bits to 0 before comparing */
		if((header->blockSize & (~3)) >= size)
			return currentBlock;
		currentBlock++;/* Now it points to the nextFree pointer */
		currentBlock = (uint64_t *)(*currentBlock);
	}
	return NULL;
}
/* end firstFitSearch */

/* begin bestFitSearch */
static inline uint64_t *bestFitSearch(int listNum, size_t size)
{
	uint64_t *currentBlock = freeLists[listNum];
	uint64_t *currentMin;
	struct blockHeader *currentHeader, *minHeader;
	if(currentBlock == NULL)
		return NULL;
	DEBUG_PRINT("bestFitSearch: list [%d] is not empy\n", listNum);
	currentMin = currentBlock;
	minHeader = (struct blockHeader *) currentBlock;
	/* Find the block with min size that satisfies the request */
	while(currentBlock != NULL)
	{
		currentHeader = (struct blockHeader *)currentBlock;
		DEBUG_PRINT("bestFitSearch: blockSize = %lu\n", 
					(currentHeader->blockSize & (~3)));
		DEBUG_PRINT("bestFitSearch: requested size = %lu\n",
					size);
		/* Set first 3 bits to 0 before comparing */
		if((currentHeader->blockSize & (~3)) >= size && 
			currentHeader->blockSize == minHeader->blockSize )
		{
				currentMin = currentBlock;
				minHeader = (struct blockHeader *)currentMin;
		}
		currentBlock++;/* Now it points to the nextFree pointer */
		currentBlock = (uint64_t *)(*currentBlock);
	}
	return currentMin;
}
/* end bestFitSearch */

/* begin growHeap */
static uint64_t *growHeap(size_t size)
{
/*
	This function grows the heap by a specific size(found in the doc.txt)
	and returns a pointer to the begining of the new block
*/
	uint64_t *oldBrk, *newBrk; /* Used to set up the headers etc */
	struct blockHeader *header, *footer; 
	static bool firstCall = true;
	bool sbrkWorked = true;/* Used to check if sbrk failed or worked */
	uint64_t blockSize;/* Used to calculate the block size */
	int listNum = pickAppropriateList(size);
	/* If this is the first time we grow the heap, we've got to set up
		boundary tag to mark the begining of the heap and its end */
	if(firstCall)
	{
		if(setInitialBoundries() < 0)
			return NULL;
		firstCall = false; 
	}
	
	/* Try to add memory according to the list policy,if all 8 
		different-sized attempts fail nothing can be done
		so return -1 */
	if(listNum <= 4)
	{
		size_t initAllocSize = 65536;
		for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2)
		{
			/* If sbrk didn't fail */
			if((oldBrk = sbrk(initAllocSize/divisor)) != (void*) -1)
			{
				sbrkWorked = true;
				break;
			}
		}
		if(!sbrkWorked)
		{
			if((oldBrk = sbrk(2*size)) != (void*) -1)
				sbrkWorked = true;
			else if((oldBrk = sbrk(size)) != (void*) -1)
				sbrkWorked = true;
			else
				return NULL;
		}
	}
	else if(listNum <= 9)
	{
		size_t initAllocSize = 1024 * 1024 * 8;
		for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2)
		{
			/* If sbrk didn't fail */
			if((oldBrk = sbrk(initAllocSize/divisor)) != (void*) -1)
			{
				sbrkWorked = true;
				break;
			}
		}
		if(!sbrkWorked)
		{
			if((oldBrk = sbrk(2*size)) != (void*) -1)
				sbrkWorked = true;
			else if((oldBrk = sbrk(size)) != (void*) -1)
				sbrkWorked = true;
			else
				return NULL;
		}	
	}
	else
	{
		size_t initAllocSize = 128 * size;
		for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2)
		{
			/* If sbrk didn't fail */
			if((oldBrk = sbrk(initAllocSize/divisor)) != (void*) -1)
			{
				sbrkWorked = true;
				break;
			}
		}
		if(!sbrkWorked)
		{
			if((oldBrk = sbrk(2*size)) != (void*) -1)
				sbrkWorked = true;
			else if((oldBrk = sbrk(size)) != (void*) -1)
				sbrkWorked = true;
			else
				return NULL;
		}
	}
	/* The code cannot reach this part unless one sbrk worked
		So now we have to set up the header & footer */
	if((newBrk = sbrk(0)) == (void*)-1)
		return NULL;
	/* set oldBrk to point to the begining of the last boundry tag by
		subtracting 1(one 8-byte word) from it */
	oldBrk = oldBrk - 1;
	/* -1 to avoid counting the new boundry tag
		* 8 because the block size is in bytes, not words */
	blockSize = (newBrk - oldBrk - 1) * 8;
	DEBUG_PRINT("growHeap: blockSize = %lu\n", blockSize);
	/* set up the header */
	header = (struct blockHeader*) oldBrk;
	header->blockSize = blockSize;
	SET_FREE(header);
	/* set up the footer */
	footer = (struct blockHeader*) newBrk;/* errCheck = current brk */	
	footer--;/* Now footer points to the brk boundry tag */
	SET_BOUND_TAG(footer);/* Set a new boundry tag  */
	footer--;/* Points to the footer of the newely allocated block */	
	footer->blockSize = blockSize;
	SET_FREE(footer);
	return oldBrk;
}
/* end growHeap */

/* begin setInitialBoundries */
/* 
	Sets up the heap-start boundry tag and heap-end boundry tag
	returns 0 on success -1 on failure */
static inline int setInitialBoundries(void)
{
	unsigned char *ptr; /* Used to grow the heap and set up the boundries */
	struct blockHeader *header;/* Sets values in boundry tags */ 
	ptr = sbrk(0);
	if(ptr == (void*) -1)
		return -1;
	/* If the current brk is not aligned to 8-byte */
	if((intptr_t)ptr % 8 != 0)
	{
		/* Add to it whatever needed to make it reach the next				8-byte boundry */
		ptr += 8 - ((intptr_t)ptr % 8);
		if(brk(ptr) < 0)
			return -1;
	}
	/* grow the heap to add the boundry tags */
	ptr += (sizeof(struct blockHeader) * 2);
	if(brk(ptr) < 0)
		return -1;
	/* Retrieve and set the boundry tags */
	header = (struct blockHeader *)(
			ptr - (sizeof(struct blockHeader) * 2));
	header->blockSize = ~0;/* all 1s marks a boundry */
	header++;
	header->blockSize = ~0;/* all 1s marks a boundry */		
	return 0;
}
/* end setInitialBoundries */

/* begin addFreeBlockToList */
static inline void addFreeBlockToList(int listNum, uint64_t *newBlock)
{
	/* Always adds to the begining of the list */
	uint64_t *nextFree = newBlock + 1;
	*nextFree = (intptr_t) freeLists[listNum];
	freeLists[listNum] = newBlock;		
}
/* end addFreeBlockToList */
