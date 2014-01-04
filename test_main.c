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
#include <sys/time.h>
#include <sys/resource.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "mm.h"
#include "test_functions.h"
int main(int argc, char *argv[])
{
    assert(argc > 1);
    /* Set the memory limits */
    struct rlimit lim;
    lim.rlim_cur = atoi(argv[1]);
    lim.rlim_max = atoi(argv[1]);
    assert(setrlimit(RLIMIT_DATA, &lim) == 0);
    
    assert(malloc(1024 * 1024) != NULL);
    
	uint64_t *ptr = malloc(sizeof(uint64_t)*10);
	if(ptr == NULL) { 
		printf("First malloc returned NULL");
		return 1;
	}
    //ptr = malloc(sizeof(uint64_t)*10);
    if(ptr == NULL) { 
		printf("First malloc returned NULL");
		return 1;
	}
	/*
    for(int i = 0; i < 10; i++)
		ptr[i] = i * 10;
	for(int i = 0; i < 10; i++)
		printf("ptr[%d] = %lu\n", i, ptr[i]);
    
	uint64_t *ptr2 = malloc(sizeof(uint64_t)*10);
	for(int i = 0; i < 10; i++)
		ptr2[i] = i * 100;
	for(int i = 0; i < 10; i++)
		printf("ptr2[%d] = %lu\n", i, ptr2[i]);
//	malloc(sizeof(uint64_t)*10);
//	debug_alignment();
//	ptr = myMalloc(sizeof(char));
//	if(ptr == NULL)
//	{
//		printf("Second malloc returned a NULL\n");
//		return 1;
//	}*/
	return 0;
}
