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

#define DEBUG 0 /* set this to 0 if you want to stop the debugging code */
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
#define MIN_BLOCK_SIZE ((2 * sizeof(struct block_header)) + 8)
/* Clears the alloc bit in the header to indicate that this block is free */
#define SET_FREE(header) ((header->blockSize)=((header->blockSize) & (~0x2)))
/* Sets the alloc bit in the header to indicate this block is allocated */
#define SET_ALLOC(header) ((header->blockSize)=((header->blockSize) | 0x2))
/* Sets up the boundry tag - it's a 64-bits of 1 that marks the start & end of
    the heap*/
#define SET_BOUND_TAG(header) ((header->blockSize) = (~(0)))

/* struct definitions */
struct block_header/* Used for lists[5-10] */
{
    uint64_t blockSize;/* the LSB is reserved, the 2nd LSB is used to test
                if the block is allocated or free(0 for free -
                    1 for allocated), 
                the 3rd LSB is always set to 0.
                The rest count how many BYTES are in the 
                block. */
};

/* static function prototypes */
static inline int pick_list(size_t size);
static uint8_t *extract_free_block(int list_num, size_t size);
static inline uint8_t *search_list(int list_num, size_t size);
static inline uint8_t *first_fit_search(int list_num, size_t size);
static inline uint8_t *best_fit_search(int list_num, size_t size);
static uint8_t *slice_block(uint8_t *dataBlock, size_t requestedSize);
static bool can_fit_in_list(int list_num, size_t size);
static inline struct block_header *get_footer(uint8_t *block);
static uint8_t *grow_heap(size_t size);
static inline int set_initial_boundries(void);
static inline void add_free_block_to_list(int list_num, uint8_t *new_block);

/* static variables */
static uint8_t *free_lists[FREE_LISTS_COUNT];

void *myMalloc(size_t size)
{
    uint8_t *user_data; /* The pointer we will return to the user */
    uint8_t *new_block; /* Used to point to the block added by grow_heap */
    uint8_t *sliced_block; /* Used to point to the left-over of a block */
    struct block_header *header, *footer, *slice_header;
    int slice_list;/* Which list the slice belongs to */
    size += sizeof(struct block_header) * 2; /* The header+footer */
    size = (size+7) & ~7;/* Align the size to 8-byte boundry */
    /* Identify which list to pick from */
    int list_num = pick_list(size);
    DEBUG_PRINT("malloc: List picked: %d\n", list_num);
    /* Try to extract enough space from the lists */
    user_data = extract_free_block(list_num, size);
    /* If one of the lists had enough space */
    if(user_data != NULL)
    {
        /* Slice and return the rest if you can */
        sliced_block = slice_block(user_data, size);
        if(sliced_block != NULL)
        {
            slice_header = (struct block_header *) sliced_block;
            if(can_fit_in_list(list_num, slice_header->blockSize))
            {
                add_free_block_to_list(list_num, sliced_block);
            }
            else
            {
                slice_list = pick_list(slice_header->blockSize);
                add_free_block_to_list(slice_list, sliced_block);
            }
        }
        /* Mark the block as allocated */
        header = (struct block_header *) user_data;
        SET_ALLOC(header);
        footer = get_footer(user_data);
        SET_ALLOC(footer);
        user_data = user_data + 8;/* Now points past the header */
        DEBUG_PRINT("%s\n", "-------------------------------------");
        return user_data;    
    }
    /* When we can't extract a free block, it means no list has enough 
    space so we grow the heap and add the new space to the list that the 
    request was originally for */
    if((new_block = grow_heap(size)) == NULL)
    {
        errno = ENOMEM;
        DEBUG_PRINT("%s\n", "-------------------------------------");
        return NULL;
    }
    add_free_block_to_list(list_num, new_block);
    user_data = extract_free_block(list_num, size);
    sliced_block = slice_block(user_data, size);
    if(sliced_block != NULL)
    {
        slice_header = (struct block_header *) sliced_block;
            if(can_fit_in_list(list_num, slice_header->blockSize))
            {
                /* Check the note above please */
                add_free_block_to_list(list_num, sliced_block);
            }
            else
            {
                slice_list = pick_list(slice_header->blockSize);
                add_free_block_to_list(slice_list, sliced_block);
            }   }
    user_data = user_data + 8;/* Now points past the header */
    DEBUG_PRINT("%s\n", "-------------------------------------");
    return user_data;
}
/* end myMalloc */

/* begin pick_list */
static inline int pick_list(size_t size)
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
/* end pick_list */

/* begin extract_free_block */
static uint8_t *extract_free_block(int list_num, size_t size)
{
/* Searches each list(starting from list_num) for a big-enough free block, 
    if it can't find any it returns NULL.
 */
    uint8_t *data;
    /* Search this list, if you can't find a free-block, search the rest*/
    for(int i = list_num; i < FREE_LISTS_COUNT; i++)
    {
        if((data = search_list(i, size)) != NULL)
            return data;
    }
    DEBUG_PRINT("%s\n", "extract_free_block: returning NULL");
    return NULL;
}
/* end extract_free_block */

/* begin search_list */
static inline uint8_t *search_list(int list_num, size_t size)
{
/*  This function searches a particular list for a big-enough block.
    Depending on which list we're searching, we go either best-fit or 
    first fit.
*/
    if(list_num <= 4)
    {/* First fit */
        assert(size <= 512);
        return first_fit_search(list_num, size);
    }
    else
    {/* Best fit */
        assert(list_num > 4);
        return best_fit_search(list_num, size);
    }
}
/* end search_list */

/* begin first_fit_search */
static inline uint8_t *first_fit_search(int list_num, size_t size)
{
    /* We'll work here with word-pointers rather than byte-pointers
        because it makes list manipulation MUCH easier  */
    uint64_t *currentBlock = (uint64_t *)free_lists[list_num];
    struct block_header *header;
    if(currentBlock == NULL)
        return NULL;
    DEBUG_PRINT("first_fit_search: list [%d] is not empy\n", list_num);
    while(currentBlock != NULL)
    {
        header = (struct block_header *)currentBlock;
        DEBUG_PRINT("first_fit_search: blockSize = %lu\n", 
                    header->blockSize);
        DEBUG_PRINT("first_fit_search: requested size = %lu\n",
                    size);
        if(header->blockSize >= size)
            return (uint8_t *)currentBlock;
        currentBlock++;/* Now it points to the nextFree pointer */
        currentBlock = (uint64_t *)(*currentBlock);
    }
    return NULL;
}
/* end first_fit_search */

/* begin best_fit_search */
static inline uint8_t *best_fit_search(int list_num, size_t size)
{
    /* We'll work here with word-pointers rather than byte-pointers
        because it makes list manipuation MUCH easier */
    uint64_t *currentBlock = (uint64_t *)free_lists[list_num];
    uint64_t *currentMin = NULL;
    struct block_header *currentHeader, *minHeader;
    if(currentBlock == NULL)
        return NULL;
    DEBUG_PRINT("best_fit_search: list [%d] is not empy\n", list_num);
    currentMin = currentBlock;
    minHeader = (struct block_header *) currentBlock;
    /* Find the block with min size that satisfies the request */
    while(currentBlock != NULL)
    {
        currentHeader = (struct block_header *)currentBlock;
        DEBUG_PRINT("best_fit_search: blockSize = %lu\n", 
                    currentHeader->blockSize);
        DEBUG_PRINT("best_fit_search: requested size = %lu\n",
                    size);
        if(currentHeader->blockSize >= size && 
            currentHeader->blockSize < minHeader->blockSize )
        {
                currentMin = currentBlock;
                minHeader = (struct block_header *)currentMin;
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
/* end best_fit_search */

/* begin slice_block */
static uint8_t *slice_block(uint8_t *block, size_t requestedSize)
{
    /* Tries to slice a block in two(and sets up appropriate header and 
        footer) if that can be done:
        it returns the 2nd half of the slice and modifies the first
        one (given in the *block)
        returns NULL on failure */
    struct block_header *originalHeader, *originalFooter, *slice_header,
                            *sliceFooter;
    uint8_t *slice;
    originalHeader = (struct block_header *)block;
    /* If we can't slice */
    if(MIN_BLOCK_SIZE > originalHeader->blockSize - requestedSize)
        return NULL;
    DEBUG_PRINT("slice_block: size of block to be sliced: %lu\n",
                    originalHeader->blockSize);
    slice = block + requestedSize;
    slice_header = (struct block_header *)slice;
    /* Points to the begining of the last word in the original block */
    sliceFooter = (struct block_header *) 
                (block + originalHeader->blockSize -8);
    slice_header->blockSize = originalHeader->blockSize - requestedSize;
    sliceFooter->blockSize = slice_header->blockSize;    
    DEBUG_PRINT("slice_block: slice size: %lu\n",
                    slice_header->blockSize);
    /* Change the original block size and footer */
    originalFooter = slice_header - 1;
    originalFooter->blockSize = originalHeader->blockSize - requestedSize;
    originalHeader->blockSize = originalFooter->blockSize;
    
    return slice;
}
/* end slice_block */

/* begin catFitInList */
static bool can_fit_in_list(int list_num, size_t size)
{
    /* A block fits in a list if its size is equal or larger than the 
        minimum size allowed in this list  */
    switch(list_num)
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
/* end can_fit_in_list */

/* begin get_footer */
static inline struct block_header *get_footer(uint8_t *block)
{
    /* Returns a pointer to the footer of a given block */
    struct block_header *header, *footer;
    header = (struct block_header *) block;
    block = block + header->blockSize - 8;
    footer = (struct block_header *)block;
    return footer;
}
/* end get_footer */

/* begin grow_heap */
static uint8_t *grow_heap(size_t size)
{
/*
    This function grows the heap by a specific size(found in the doc.txt)
    and returns a pointer to the begining of the new block
*/
    uint8_t *oldBrk, *newBrk; /* Used to set up the headers etc */
    struct block_header *header, *footer; 
    static bool firstCall = true;
    bool sbrkWorked = false;/* Used to check if sbrk failed or worked */
    uint64_t blockSize;/* Used to calculate the block size */
    int list_num = pick_list(size);
    /* If this is the first time we grow the heap, we've got to set up
        boundary tag to mark the begining of the heap and its end */
    if(firstCall)
    {
        if(set_initial_boundries() < 0)
            return NULL;
        firstCall = false; 
    }
    
    /* Try to add memory according to the list policy,if all 8 
        different-sized attempts fail nothing can be done
        so return -1 */
    if(list_num <= 4)
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
    else if(list_num <= 9)
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
    DEBUG_PRINT("grow_heap: blockSize = %lu\n", blockSize);
    /* set up the header */
    header = (struct block_header*) oldBrk;
    header->blockSize = blockSize;
    /* set up the footer */
    footer = (struct block_header*) newBrk;/* errCheck = current brk */  
    footer -= 8;/* Now footer points to the brk boundry tag */
    SET_BOUND_TAG(footer);/* Set a new boundry tag  */
    footer -= 8;/* Points to the footer of the newely allocated block */    
    footer->blockSize = blockSize;
    return oldBrk;
}
/* end grow_heap */

/* begin set_initial_boundries */
/* 
    Sets up the heap-start boundry tag and heap-end boundry tag
    returns 0 on success -1 on failure */
static inline int set_initial_boundries(void)
{
    uint8_t *ptr; /* Used to grow the heap and set up the boundries */
    struct block_header *header;/* Sets values in boundry tags */ 
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
    if((ptr = sbrk(sizeof(struct block_header)*2)) == (void *) -1)
        return -1;
    /* Retrieve and set the boundry tags */
    header = (struct block_header *) ptr;
    header->blockSize = ~0;/* all 1s marks a boundry */
    header++;/* It now points to the end boundry tag */
    header->blockSize = ~0;/* all 1s marks a boundry */     
    return 0;
}
/* end set_initial_boundries */

/* begin add_free_block_to_list */
static inline void add_free_block_to_list(int list_num, uint8_t *new_block)
{
    /* Always adds to the begining of the list */

    /* List manipulation is always done in words, not bytes
        to avoid several complexities */
    uint64_t *nextFree = (uint64_t *)(new_block + 8);
    DEBUG_PRINT("add_free_block_to_list: a new block of size %lu was added to"
        " list[%d]\n", ((struct block_header*)new_block)->blockSize,
                                list_num);
    *nextFree = (intptr_t) free_lists[list_num];
    free_lists[list_num] = new_block;      
}
/* end add_free_block_to_list */
