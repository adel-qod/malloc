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
/* Debug print macro that works only when the debug is define. 
	To print something that has no arguments DEBUG_PRINT("%s\n","x");
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
/* Min block size in bytes =  footer + header + nextFree pointer */
#define MIN_BLOCK_SIZE ((2 * sizeof(struct blockHeader)) + 8)
/* Clears the alloc bit in the header to indicate that this block is free */
#define SET_FREE(header) ((header->blockSize)=((header->blockSize) & (~0x2)))
/* Sets the alloc bit in the header to indicate this block is allocated */
#define SET_ALLOC(header) ((header->blockSize)=((header->blockSize) | 0x2))
/* Sets up the boundry tag - it's a 64-bits of 1 that marks the start & end of
	the heap*/
#define SET_BOUND_TAG(header) ((header->blockSize) = (~(0)))

/* struct definitions */
struct blockHeader/* Used for lists[5-10] */
{
	uint64_t blockSize;/* the LSB is reserved, the 2nd LSB is used to test
				if the block is allocated or free(0 for free -
					1 for allocated), 
				the 3rd LSB is always set to 0.
				The rest count how many BYTES are in the 
				block. */
};

/* static function prototypes */
static inline int pickAppropriateList(size_t size);
static uint8_t *extractFreeBlock(int listNum, size_t size);
static inline uint8_t *searchList(int listNum, size_t size);
static inline uint8_t *firstFitSearch(int listNum, size_t size);
static inline uint8_t *bestFitSearch(int listNum, size_t size);
static uint8_t *sliceBlock(uint8_t *dataBlock, size_t requestedSize);
static bool canFitInList(int listNum, size_t size);
static inline struct blockHeader *getFooter(uint8_t *block);
static uint8_t *growHeap(size_t size);
static inline int setInitialBoundries(void);
static inline void addFreeBlockToList(int listNum, uint8_t *newBlock);

/* static variables */
static uint8_t *freeLists[FREE_LISTS_COUNT];

/* begin myMalloc */
void *myMalloc(size_t size)
{
	uint8_t *userData; /* The pointer we will return to the user */
	uint8_t *newBlock; /* Used to point to the block added by growHeap */
	uint8_t *slicedBlock; /* Used to point to the left-over of a block */
	struct blockHeader *header, *footer, *sliceHeader;
	int sliceList;/* Which list the slice belongs to */
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
		slicedBlock = sliceBlock(userData, size);
		if(slicedBlock != NULL)
		{
			sliceHeader = (struct blockHeader *) slicedBlock;
			if(canFitInList(listNum, sliceHeader->blockSize))
			{
				addFreeBlockToList(listNum, slicedBlock);
			}
			else
			{
				sliceList = pickAppropriateList(sliceHeader->
								blockSize);
				addFreeBlockToList(sliceList, slicedBlock);
			}
			
		}
		/* Mark the block as allocated */
		header = (struct blockHeader *) userData;
		SET_ALLOC(header);
		footer = getFooter(userData);
		SET_ALLOC(footer);
		userData = userData + 8;/* Now points past the header */
		DEBUG_PRINT("%s\n", "-------------------------------------");
		return userData;	
	}
	/* When we can't extract a free block, it means no list has enough 
	space so we grow the heap and add the new space to the list that the 
	request was originally for */
	if((newBlock = growHeap(size)) == NULL)
	{
		errno = ENOMEM;
		DEBUG_PRINT("%s\n", "-------------------------------------");
		return NULL;
	}
	addFreeBlockToList(listNum, newBlock);
	userData = extractFreeBlock(listNum, size);
	slicedBlock = sliceBlock(userData, size);
	if(slicedBlock != NULL)
	{
		sliceHeader = (struct blockHeader *) slicedBlock;
			if(canFitInList(listNum, sliceHeader->blockSize))
			{
				addFreeBlockToList(listNum, slicedBlock);
			}
			else
			{
				sliceList = pickAppropriateList(sliceHeader->
								blockSize);
				addFreeBlockToList(sliceList, slicedBlock);
			}	}
	userData = userData + 8;/* Now points past the header */
	DEBUG_PRINT("%s\n", "-------------------------------------");
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
static uint8_t *extractFreeBlock(int listNum, size_t size)
{
/* Searches each list(starting from listNum) for a big-enough free block, 
	if it can't find any it returns NULL.
 */
	uint8_t *data;
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
static inline uint8_t *searchList(int listNum, size_t size)
{
/*  This function searches a particular list for a big-enough block.
	Depending on which list we're searching, we go either best-fit or 
	first fit.
*/
	if(listNum <= 4)
	{/* First fit */
		assert(size <= 512);
		return firstFitSearch(listNum, size);
	}
	else
	{/* Best fit */
		assert(listNum > 4);
		return bestFitSearch(listNum, size);
	}
}
/* end searchList */

/* begin firstFitSearch */
static inline uint8_t *firstFitSearch(int listNum, size_t size)
{
	/* We'll work here with word-pointers rather than byte-pointers
		because it makes list manipulation MUCH easier  */
	uint64_t *currentBlock = (uint64_t *)freeLists[listNum];
	struct blockHeader *header;
	if(currentBlock == NULL)
		return NULL;
	DEBUG_PRINT("firstFitSearch: list [%d] is not empy\n", listNum);
	while(currentBlock != NULL)
	{
		header = (struct blockHeader *)currentBlock;
		DEBUG_PRINT("firstFitSearch: blockSize = %lu\n", 
					header->blockSize);
		DEBUG_PRINT("firstFitSearch: requested size = %lu\n",
					size);
		if(header->blockSize >= size)
			return (uint8_t *)currentBlock;
		currentBlock++;/* Now it points to the nextFree pointer */
		currentBlock = (uint64_t *)(*currentBlock);
	}
	return NULL;
}
/* end firstFitSearch */

/* begin bestFitSearch */
static inline uint8_t *bestFitSearch(int listNum, size_t size)
{
	/* We'll work here with word-pointers rather than byte-pointers
		because it makes list manipuation MUCH easier */
	uint64_t *currentBlock = (uint64_t *)freeLists[listNum];
	uint64_t *currentMin = NULL;
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
					currentHeader->blockSize);
		DEBUG_PRINT("bestFitSearch: requested size = %lu\n",
					size);
		if(currentHeader->blockSize >= size && 
			currentHeader->blockSize < minHeader->blockSize )
		{
				currentMin = currentBlock;
				minHeader = (struct blockHeader *)currentMin;
		}
		currentBlock++;/* Now it points to the nextFree pointer */
		currentBlock = (uint64_t *)(*currentBlock);
	}
	/* The current min, if there was no min enough space in the list, 
		points to the firstBlock so we gotta check if it satisfies
		the reqeusted size */
	if(minHeader->blockSize >= size)
		return (uint8_t *)currentMin;
	else 
		return NULL;
}
/* end bestFitSearch */

/* begin sliceBlock */
static uint8_t *sliceBlock(uint8_t *block, size_t requestedSize)
{
	/* Tries to slice a block in two(and sets up appropriate header and 
		footer) if that can be done:
		it returns the 2nd half of the slice and modifies the first
		one (given in the *block)
		returns NULL on failure */
	struct blockHeader *originalHeader, *originalFooter, *sliceHeader,
							*sliceFooter;
	uint8_t *slice;
	originalHeader = (struct blockHeader *)block;
	/* If we can't slice */
	if(MIN_BLOCK_SIZE > originalHeader->blockSize - requestedSize)
		return NULL;
	DEBUG_PRINT("sliceBlock: size of block to be sliced: %lu\n",
					originalHeader->blockSize);
	slice = block + requestedSize;
	sliceHeader = (struct blockHeader *)slice;
	/* Points to the begining of the last word in the original block */
	sliceFooter = (struct blockHeader *) 
				(block + originalHeader->blockSize -8);
	sliceHeader->blockSize = originalHeader->blockSize - requestedSize;
	sliceFooter->blockSize = sliceHeader->blockSize;	
	DEBUG_PRINT("sliceBlock: slice size: %lu\n",
					sliceHeader->blockSize);
	/* Change the original block size and footer */
	originalFooter = sliceHeader - 1;
	originalFooter->blockSize = originalHeader->blockSize - requestedSize;
	originalHeader->blockSize = originalFooter->blockSize;
	
	return slice;
}
/* end sliceBlock */

/* begin catFitInList */
static bool canFitInList(int listNum, size_t size)
{
	/* A block fits in a list if its size is equal or larger than the 
		minimum size allowed in this list  */
	switch(listNum)
	{
		case 0:
			if(size >= 24)
				return true;
			break;
		case 1:
			if(size > 32)
				return true;
			break;
		case 2:
			if(size > 64)
				return true;
			break;
		case 3:
			if(size > 128)
				return true;
			break;
		case 4:
			if(size > 256)
				return true;
			break;
		case 5:
			if(size > 512)
				return true;
			break;
		case 6:
			if(size > 1024)
				return true;
			break;
		case 7:
			if(size > 2048)
				return true;
			break;
		case 8:
			if(size > 4096)
				return true;
			break;
		case 9:
			if(size > 8192)
				return true;
			break;
		case 10:
			if(size > 16384)
				return true;
			break;

	}	
	return true;
}
/* end canFitInList */

/* begin getFooter */
static inline struct blockHeader *getFooter(uint8_t *block)
{
	/* Returns a pointer to the footer of a given block */
	struct blockHeader *header, *footer;
	header = (struct blockHeader *) block;
	block = block + header->blockSize - 8;
	footer = (struct blockHeader *)block;
	return footer;
}
/* end getFooter */

/* begin growHeap */
static uint8_t *growHeap(size_t size)
{
/*
	This function grows the heap by a specific size(found in the doc.txt)
	and returns a pointer to the begining of the new block
*/
	uint8_t *oldBrk, *newBrk; /* Used to set up the headers etc */
	struct blockHeader *header, *footer; 
	static bool firstCall = true;
	bool sbrkWorked = false;/* Used to check if sbrk failed or worked */
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
		subtracting 8 from it */
	oldBrk = oldBrk - 8;
	/* -8 to avoid counting the new boundry tag */
	blockSize = (newBrk - oldBrk - 8);
	DEBUG_PRINT("growHeap: blockSize = %lu\n", blockSize);
	/* set up the header */
	header = (struct blockHeader*) oldBrk;
	header->blockSize = blockSize;
	/* set up the footer */
	footer = (struct blockHeader*) newBrk;/* errCheck = current brk */	
	footer -= 8;/* Now footer points to the brk boundry tag */
	SET_BOUND_TAG(footer);/* Set a new boundry tag  */
	footer -= 8;/* Points to the footer of the newely allocated block */	
	footer->blockSize = blockSize;
	return oldBrk;
}
/* end growHeap */

/* begin setInitialBoundries */
/* 
	Sets up the heap-start boundry tag and heap-end boundry tag
	returns 0 on success -1 on failure */
static inline int setInitialBoundries(void)
{
	uint8_t *ptr; /* Used to grow the heap and set up the boundries */
	struct blockHeader *header;/* Sets values in boundry tags */ 
	ptr = sbrk(0);
	if(ptr == (void*) -1)
		return -1;
	/* If the current brk is not aligned to 8-byte */
	if((intptr_t)ptr % 8 != 0)
	{
		/* Add to it whatever needed to make it reach the next 
			8-byte boundry */
		ptr += 8 - ((intptr_t)ptr % 8);
		if(brk(ptr) < 0)
			return -1;
	}
	/* grow the heap to add the boundry tags */;
	if((ptr = sbrk(sizeof(struct blockHeader)*2)) == (void *) -1)
		return -1;
	/* Retrieve and set the boundry tags */
	header = (struct blockHeader *) ptr;
	header->blockSize = ~0;/* all 1s marks a boundry */
	header++;/* It now points to the end boundry tag */
	header->blockSize = ~0;/* all 1s marks a boundry */		
	return 0;
}
/* end setInitialBoundries */

/* begin addFreeBlockToList */
static inline void addFreeBlockToList(int listNum, uint8_t *newBlock)
{
	/* Always adds to the begining of the list */

	/* List manipulation is always done in words, not bytes
		to avoid several complexities */
	uint64_t *nextFree = (uint64_t *)(newBlock + 8);
	DEBUG_PRINT("addFreeBlockToList: a new block of size %lu was added to"
		" list[%d]\n", ((struct blockHeader*)newBlock)->blockSize,
								listNum);
	*nextFree = (intptr_t) freeLists[listNum];
	freeLists[listNum] = newBlock;		
}
/* end addFreeBlockToList */
