/* 
Copyright (c) 2013, Mhd Adel G. Al Qodamni
All rights reserved.

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions are met:


Redistributions of source code must retain the above copyright notice, 
this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above copyright notice, 
this list of conditions and the following disclaimer in the documentation 
and/or other materials provided with the distribution.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE.
*/

#include <unistd.h>  /* Needed for sbrk and brk */
#include <stdint.h> /* Needed for the uint64_t and intptr_t */
#include <stdbool.h> 
#include <stdio.h> /* debug by printing */
#include <assert.h> /* Dragons be flying :P */
#include <errno.h> /* To set errno in case of failure */

#define DEBUG 1 /* set this to 0 if you want to stop the debugging code */
/* Debug print macro that works only when the debug is define. 
    To print something that has no arguments DEBUG_PRINT("%s\n","x");
    We can simply omit the fmt but that would mean fprintf won't be able
    to check the format string and the parameters passed */
static int DEBUG_COUNT;
#define DEBUG_PRINT(fmt, ...) \
    do { \
    if(DEBUG) \
         /* Use ANSI escape sequences to colorize output */ \
         fprintf(stderr,"\x1b[31m DEBUG:%d:\x1b[0m" \
                "\x1b[34m%s:\x1b[0m" \
                "\x1b[32m%d:\x1b[0m" \
                fmt, \
                DEBUG_COUNT++, \
                __func__, \
                __LINE__, \
                __VA_ARGS__);\
    } \
    while(0)
#define FREE_LISTS_COUNT 11 /* How many segregated lists we're maintaining */
/* Min block size in bytes =  footer + header + nextFree pointer */
#define MIN_BLOCK_SIZE ((2 * sizeof(struct block_header)) + 8)
/* Clears the alloc bit in the header to indicate that this block is free */
#define SET_FREE(header) ((header->block_size)=((header->block_size) & (~0x2)))
/* Sets the alloc bit in the header to indicate this block is allocated */
#define SET_ALLOC(header) ((header->block_size)=((header->block_size) | 0x2))
/* Sets up the boundary tag - it's a 64-bits of 1 that marks the start & end of
    the heap*/
#define SET_BOUND_TAG(header) ((header->block_size) = (~(0)))

/* struct definitions */
struct block_header/* Used for lists[5-10] */
{
    uint64_t block_size;/* the LSB is reserved, the 2nd LSB is used to test
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
static uint8_t *slice_block(uint8_t *dataBlock, size_t requested_size);
static inline struct block_header *get_footer(uint8_t *block);
static uint8_t *grow_heap(size_t size);
static inline int set_initial_boundries(void);
static inline void add_free_block_to_list(int list_num, uint8_t *new_block);

/* static variables */
static uint8_t *free_lists[FREE_LISTS_COUNT];

/// <summary> 
/// Does what you'd expect the malloc C standard library to do, check 
/// the requirements document for more details.
/// <summary>
/// <param name='size'> How many bytes the user needs to allocate </param>
/// <return> 
/// A properely aligned pointer to a block of memory whose size at
/// is at least equal to the size requested by the caller. 
/// </return>
void *my_malloc(size_t size)
{
    uint8_t *user_data; /* The pointer we will return to the user */
    uint8_t *new_block; /* Used to point to the block added by grow_heap */
    uint8_t *sliced_block; /* Used to point to the left-over of a block */
    struct block_header *header, *footer, *slice_header;
    int slice_list;/* Which list the slice belongs to */
    if(size <= 0)
        return NULL;
    size += sizeof(struct block_header) * 2; /* The header+footer */
    size = (size+7) & ~7;/* Align the size to 8-byte boundary */
    /* Identify which list to pick from */
    int list_num = pick_list(size);
    DEBUG_PRINT("size requested: %zd\n", size);
    DEBUG_PRINT("List picked: %d\n", list_num);
    user_data = extract_free_block(list_num, size);
    /* If no list had enough space */
    if(user_data == NULL) {
        if((new_block = grow_heap(size)) == NULL) {
            DEBUG_PRINT("%s\n", "-------------------------------------");
            errno = ENOMEM;
            return NULL;
        }
        add_free_block_to_list(list_num, new_block);
        user_data = extract_free_block(list_num, size);
    }
    assert(user_data != NULL);
    DEBUG_PRINT("%s\n", "Found a block!");
    /* If we can slice, add the left-over back to our lists */
    sliced_block = slice_block(user_data, size);
    if(sliced_block != NULL) {
        slice_header = (struct block_header *) sliced_block;
        slice_list = pick_list(slice_header->block_size);
        add_free_block_to_list(slice_list, sliced_block);
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

/// <summary> Chooses which list this size belongs to </summary>
/// <param name='size'> The size of block </param>
/// <return> The index of the list the block of size belongs to </return>
static inline int pick_list(size_t size)
{
    if(size <= 32) {
        return 0;
    }
    else if(size <= 64) { 
        return 1;
    }
    else if(size <= 128) {
        return 2;
    }
    else if(size <= 256) {
        return 3;
    }
    else if(size <= 512) {
        return 4;
    }
    else if(size <= 1024) {
        return 5;
    }
    else if(size <= 2048) {
        return 6;
    }
    else if(size <= 4096) {
        return 7;
    }
    else if(size <= 8192) {
        return 8;
    }
    else if(size <= 16384) {
        return 9;
    }
    else {
        return 10;
    }
}

/// <summary> 
/// Searches each list(starting from list_num) for a big-enough free block 
/// </summary>
/// <param name='list_num'> 
/// The index of the list where the search will start from 
/// </param>
/// <param name='size'> The size of the block we need, in bytes </param>
/// <return>
/// A properely aligned pointer to the beginning of the block
/// or NULL in failure
/// </return>
static uint8_t *extract_free_block(int list_num, size_t size)
{
    uint8_t *data;
    /* Search this list, if you can't find a free-block, search all larger 
     * ones */
    for(int i = list_num; i < FREE_LISTS_COUNT; i++) {
        DEBUG_PRINT("Searching list[%d] for block of size %zd\n", i
                                                                , size);
        if((data = search_list(i, size)) != NULL) {
            DEBUG_PRINT("Found block in list[%d]\n", i);
            return data;
        }
    }
    DEBUG_PRINT("%s\n", "returning NULL");
    return NULL;
}

/// <summary>
/// This function searches a particular list for a big-enough block.
/// Depending on which list we're searching, we go either best-fit or 
/// first fit.
/// </summary>
/// <param name='list_num'> 
/// The index of the list that will be searched 
/// </param>
/// <param name='size'> The size of the block we need, in bytes </param>
/// <return>
/// A properely aligned pointer to the beginning of the block
/// or NULL in failure
/// </return>
static inline uint8_t *search_list(int list_num, size_t size)
{
    if(list_num <= 4) {
        assert(size <= 512);
        return first_fit_search(list_num, size);
    }
    else {
        assert(list_num > 4);
        return best_fit_search(list_num, size);
    }
}

/// <summary>
/// Uses the first fit method to search the list specified by the index
/// list_num - best fit: return the first block that's equal or more the 
/// size needed
/// </summary>
/// <param name='list_num'> 
/// The index of the list that will be searched
/// </param>
/// <param name='size'> The size of the block we need, in bytes </param>
/// <return>
/// A properely aligned pointer to the beginning of the block
/// or NULL in failure
/// </return>
static inline uint8_t *first_fit_search(int list_num, size_t size)
{
    /* We'll work here with word-pointers rather than byte-pointers
        because it makes list manipulation MUCH easier  */
    uint64_t *current_block = (uint64_t *)free_lists[list_num];
    struct block_header *header;
    if(current_block == NULL) {
        DEBUG_PRINT("List[%d] is empty\n", list_num);
        return NULL;
    }
    DEBUG_PRINT("list [%d] is not empy\n", list_num);
    while(current_block != NULL) {
        header = (struct block_header *)current_block;
        DEBUG_PRINT("block_size = %lu\n", header->block_size);
        DEBUG_PRINT("requested size = %lu\n", size);
        if(header->block_size >= size)
            return (uint8_t *)current_block;
        current_block++;/* Now it points to the nextFree pointer */
        current_block = (uint64_t *)(*current_block);
    }
    DEBUG_PRINT("List[%d] didn't have any sufficient block\n", list_num);
    return NULL;
}

/// <summary>
/// Uses the best fit method to search the list specified by the index
/// list_num - best fit: return the smallest block that's equal or more the 
/// size needed
/// </summary>
/// <param name='list_num'> 
/// The index of the list that will be searched
/// </param>
/// <param name='size'> The size of the block we need, in bytes </param>
/// <return>
/// A properely aligned pointer to the beginning of the block
/// or NULL in failure
/// </return>
static inline uint8_t *best_fit_search(int list_num, size_t size)
{
    /* We'll work here with word-pointers rather than byte-pointers
        because it makes list manipuation MUCH easier */
    uint64_t *current_block = (uint64_t *)free_lists[list_num];
    uint64_t *current_min = NULL;
    struct block_header *current_header, *min_header;
    if(current_block == NULL) {
        DEBUG_PRINT("List[%d] is empty\n", list_num);
        return NULL;
    }
    DEBUG_PRINT("list [%d] is not empy\n", list_num);
    current_min = current_block;
    min_header = (struct block_header *) current_block;
    /* Find the block with min size that satisfies the request */
    while(current_block != NULL) {
        current_header = (struct block_header *)current_block;
        DEBUG_PRINT("block_size = %lu\n", current_header->block_size);
        DEBUG_PRINT("requested size = %lu\n", size);
        if(current_header->block_size >= size && 
            current_header->block_size < min_header->block_size ) {
                current_min = current_block;
                min_header = (struct block_header *)current_min;
        }
        current_block++;/* Now it points to the nextFree pointer */
        current_block = (uint64_t *)(*current_block);
    }
    /* The current min, if there was no min enough space in the list, 
        points to the firstBlock so we gotta check if it satisfies
        the reqeusted size */
    if(min_header->block_size >= size)
        return (uint8_t *)current_min;
    DEBUG_PRINT("List[%d] didn't have any sufficient block\n", list_num);
    return NULL;
}

/// <summary>
/// Tries to slice a block in two(and sets up appropriate header and footer) 
/// </summary>
/// <param name='block'> The block which we're trying to slice </param>
/// <param name='requested_size'> 
/// The size that needs to be kept in the first part in bytes
/// </param>
/// <return> 
/// The 2nd half of the slice and modifies the first one given in the 
/// *block parameter
/// returns NULL on failur - block cannot be sliced so that the first part
/// is still more or equal requested_size and the second part can fit in 
/// some free list
/// </return>
static uint8_t *slice_block(uint8_t *block, size_t requested_size)
{
    struct block_header *original_hdr, *original_ftr, *slice_header,
                            *slice_ftr;
    uint8_t *slice;
    original_hdr = (struct block_header *)block;
    /* If we can't slice */
    if(MIN_BLOCK_SIZE > original_hdr->block_size - requested_size)
        return NULL;
    DEBUG_PRINT("size of block to be sliced: %lu\n",
                    original_hdr->block_size);
    slice = block + requested_size;
    slice_header = (struct block_header *)slice;
    /* Points to the beginning of the last word in the original block */
    slice_ftr = (struct block_header *) 
                (block + original_hdr->block_size -8);
    slice_header->block_size = original_hdr->block_size - requested_size;
    slice_ftr->block_size = slice_header->block_size;    
    DEBUG_PRINT("slice size: %lu\n", slice_header->block_size);
    /* Change the original block size and footer */
    original_ftr = slice_header - 1;
    original_ftr->block_size = original_hdr->block_size - requested_size;
    original_hdr->block_size = original_ftr->block_size;
    return slice;
}

/// <summary> Returns the footer of a given block </summary>
/// <param name='block'> 
/// Parameter to a block; header must be set correctly in block 
/// </param>
/// <return> a pointer to the footer of the block </return>
static inline struct block_header *get_footer(uint8_t *block)
{
    struct block_header *header, *footer;
    header = (struct block_header *) block;
    block = block + header->block_size - 8;
    footer = (struct block_header *)block;
    return footer;
}

/// <summary> 
/// Grows the heap by a specific amount of bytes according to the size
/// (details found in the doc.txt)
/// <param name='size'> The minimum size to grow the heap by </param>
/// <return> 
/// Pointer to the beginning of the new block or NULL on failure
//  </return>
static uint8_t *grow_heap(size_t size)
{
    uint8_t *old_brk, *new_brk; /* Used to set up the headers etc */
    struct block_header *header, *footer; 
    static bool first_call = true;
    bool sbrk_worked = false;/* Used to check if sbrk failed or worked */
    uint64_t block_size;/* Used to calculate the block size */
    int list_num = pick_list(size);
    /* If this is the first time we grow the heap, we've got to set up
        boundary tag to mark the beginning of the heap and its end */
    if(first_call) {
        if(set_initial_boundries() < 0) {
            DEBUG_PRINT("%s\n", "Failed to set init bounds => "
                    "returning NULL");
            return NULL;
        }
        first_call = false; 
    }
    /* Try to add memory according to the list policy,if all 8 
        different-sized attempts fail nothing can be done
        so return -1 */
    if(list_num <= 4) {
        size_t initial_alloc_size = 65536;
        for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2) {
            /* If sbrk didn't fail */
            if((old_brk = sbrk(initial_alloc_size/divisor)) != (void*) -1) { 
                sbrk_worked = true;
                break;
            }
        }
        if(!sbrk_worked) {
            if((old_brk = sbrk(2*size)) != (void*) -1)
                sbrk_worked = true;
            else if((old_brk = sbrk(size)) != (void*) -1)
                sbrk_worked = true;
            else {
                DEBUG_PRINT("%s\n", "Returning NULL");
                return NULL;
            }
        }
    }
    else if(list_num <= 9) {
        size_t initial_alloc_size = 1024 * 1024 * 8;
        for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2) {
            /* If sbrk didn't fail */
            if((old_brk = sbrk(initial_alloc_size/divisor)) != (void*) -1) {
                sbrk_worked = true;
                break;
            }
        }
        if(!sbrk_worked) {
            if((old_brk = sbrk(2*size)) != (void*) -1)
                sbrk_worked = true;
            else if((old_brk = sbrk(size)) != (void*) -1)
                sbrk_worked = true;
            else {
                DEBUG_PRINT("%s\n", "Returning NULL");   
                return NULL;
            }
        }   
    }
    else {
        size_t initial_alloc_size = 128 * size;
        for(int i = 0, divisor = 1; i < 6; ++i, divisor *= 2) {
            /* If sbrk didn't fail */
            if((old_brk = sbrk(initial_alloc_size/divisor)) != (void*) -1) {
                sbrk_worked = true;
                break;
            }
        }
        if(!sbrk_worked) {
            if((old_brk = sbrk(2*size)) != (void*) -1)
                sbrk_worked = true;
            else if((old_brk = sbrk(size)) != (void*) -1)
                sbrk_worked = true;
            else {
                DEBUG_PRINT("%s\n", "Returning NULL");
                return NULL;
            }
        }
    }
    assert(sbrk_worked == true);
    if((new_brk = sbrk(0)) == (void*)-1) {
        DEBUG_PRINT("%s\n", "Failed to query sbrk! Returning NULL");
        return NULL;
    }
    /* set old_brk to point to the beginning of the last boundary tag by
        subtracting 8 from it */
    old_brk = old_brk - 8;
    /* -8 to avoid counting the new boundary tag */
    block_size = (new_brk - old_brk - 8);
    DEBUG_PRINT("block_size = %lu\n", block_size);
    /* set up the header */
    header = (struct block_header*) old_brk;
    header->block_size = block_size;
    /* set up the footer */
    footer = (struct block_header*) new_brk;/* errCheck = current brk */  
    footer -= 8;/* Now footer points to the brk boundary tag */
    SET_BOUND_TAG(footer);/* Set a new boundary tag  */
    footer -= 8;/* Points to the footer of the newely allocated block */    
    footer->block_size = block_size;
    return old_brk;
}

/// <summary> 
/// Sets up the heap-start boundary tag and heap-end boundary tag
/// </summary>
/// <return> 0 on success -1 on failure </return>
static inline int set_initial_boundries(void)
{
    uint8_t *ptr; /* Used to grow the heap and set up the boundries */
    struct block_header *header;/* Sets values in boundary tags */ 
    ptr = sbrk(0);
    if(ptr == (void*) -1)
        return -1;
    /* If the current brk is not aligned to 8-byte */
    if((intptr_t)ptr % 8 != 0) {
        /* Add to it whatever needed to make it reach the next 
            8-byte boundary */
        ptr += 8 - ((intptr_t)ptr % 8);
        if(brk(ptr) < 0)
            return -1;
    }
    /* grow the heap to add the boundary tags */;
    if((ptr = sbrk(sizeof(struct block_header)*2)) == (void *) -1)
        return -1;
    /* Retrieve and set the boundary tags */
    header = (struct block_header *) ptr;
    header->block_size = ~0;/* all 1s marks a boundary */
    header++;/* It now points to the end boundary tag */
    header->block_size = ~0;/* all 1s marks a boundary */     
    return 0;
}

/* begin add_free_block_to_list */
/// <summary> adds the new block to the head of the list </summary>
/// <param name='list_num'> The list index we'll add to </param>
/// <param name='new_block'> The block to be added </param>
static inline void add_free_block_to_list(int list_num, uint8_t *new_block)
{
    /* List manipulation is always done in words, not bytes
        to avoid several complexities */
    uint64_t *nextFree = (uint64_t *)(new_block + 8);
    DEBUG_PRINT("a new block of size %lu was added to list[%d]\n", 
                ((struct block_header*)new_block)->block_size,
                                list_num);
    *nextFree = (intptr_t) free_lists[list_num];
    free_lists[list_num] = new_block;      
}
