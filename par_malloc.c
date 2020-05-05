#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <math.h>

#include "xmalloc.h"
#include "node.h"

size_t PAGE_SIZE = 4096;
static size_t total_alloc = 0;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static node** bins = 0;
static __thread node* tbin = 0;
size_t vals[17] = {16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3192, 4096};

static 
size_t
div_up(size_t xx, size_t yy) {
	size_t zz = xx / yy;
	if (zz * yy == xx) {
		return zz;
	}
	else {
		return zz + 1;
	}
}

// turn the size of bytes to the index for vals
size_t
sizeToIndex(size_t size) {
	size_t halfway = 0;
	size_t power = 4;
	size_t index = 0;
	while (1) {
		if (halfway == 1) {
			if (size == (pow(2, power) + pow(2, power + 1)) / 2) {
				return index;
			}
			else {
				halfway = 0;
				power += 1;
				index += 1;
			}}
		else {
			if (size == pow(2, power)) {
				return index;
			}
			else {
				halfway = 1;
				index += 1;
			}
		}
	}
	return -1;
}

node*
init_bins(int ii){
    char* to_add = mmap(0, 5 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    bins[ii] = (node*) to_add;
    // the size allocation of that bin is placed at the beginning of that bin
    long total_size = 5 * PAGE_SIZE;
    long remain = total_size % vals[ii];
    total_size -= remain;
    node* curr  = (node*) to_add;
    curr->size = 0;

    for(int jj = 0; jj < ((total_size / vals[ii]) - 1); jj++){
        to_add += vals[ii];
        node* new_node = (node*) to_add;
        new_node->size = vals[ii];
        curr->next = new_node;
        curr = new_node;
    }
    curr->next = 0;
    return bins[ii];
}

void
fill_bins(int ii){
    char* to_add = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    long total_size = PAGE_SIZE;
    long remain = total_size % vals[ii];
    total_size -= remain;
    node* curr = bins[ii];
    for(int jj = 0; jj < ((total_size / vals[ii]) - 1); jj++){
        to_add += vals[ii];
        node* new_node = (node*) to_add;
        new_node->size = vals[ii];
        curr->next = new_node;
        curr = new_node;
    }
    curr->next = 0;
}

// makes all the bins and adds the size to the beginning of each map
void
make_bins() {
    bins = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	for (int ii = 0; ii < 17; ii++) {
		// create maps
        bins[ii] = init_bins(ii);
	}
}


// round up when mallocing bytes fix for divides
size_t
roundUpMalloc(size_t bytes) {
    if(bytes <= 16){
        return 16;
    }
	for (int ii = 1; ii< 17; ii++) {
		if (bytes > vals[ii - 1] && bytes <= vals[ii]) {
			return vals[ii];
		}
	}
    return -1;
}


void*
xmalloc(size_t bytes)
{
    pthread_mutex_lock(&mut);
    if(bins == 0){
        make_bins();
    }
    pthread_mutex_unlock(&mut);
    if((bytes + sizeof(size_t)) > PAGE_SIZE){
        size_t size = bytes + sizeof(size_t);
        char* ret = mmap(0, PAGE_SIZE * div_up(size, PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        memcpy(ret, &size, sizeof(size_t));
        ret += sizeof(size_t);
        return (void*) ret;
    }
    size_t size = roundUpMalloc(bytes + sizeof(size_t));
    assert(size != -1);
    int ii = sizeToIndex(size);
    //if you have something to malloc on local cache
    if(tbin[0].next){
        if(tbin[ii].next){
            node* curr = tbin[ii].next;
            tbin[ii].next = curr->next;
            tbin[ii].size -= size;
            char* ret = (char*) curr;
            memset(ret, 0, size);
            memcpy(ret, &size, sizeof(size_t));
            ret += sizeof(size_t);
            return (void*) ret;
        }

    }
    pthread_mutex_lock(&mut);
    node* curr = bins[ii];
    if(!curr->next){
        fill_bins(ii);
    }
    node* nodn = curr->next;
    curr->next = nodn->next;
    char* ret = (char*) nodn;
    memset(ret, 0, size);
    memcpy(ret, &size, sizeof(size_t));
    ret += sizeof(size_t);
    pthread_mutex_unlock(&mut);
    return (void*) ret;
}

// place the freed memory in the correct 
void 
placeLocal(void* ptr) {
	// get the size
	size_t size;
	char* chptr = (char*) ptr;
	chptr -= sizeof(size_t);
	memcpy(&size, chptr, sizeof(size_t));

	size_t index = sizeToIndex(size);
	assert(index != -1);

    node* curr;
    if(!tbin[index].next){
        curr = 0;
    }
    else{
        curr = tbin[index].next;
    }
	tbin[index].size += size;
	node* newNode = (node*) chptr;
    tbin[index].next = newNode;
	newNode->size = size;
	newNode->next = curr;	
}

void
xfree(void* ptr)
{
	// if greater than one page just munmap it
	char* chptr = (char*) ptr;
	chptr -= sizeof(size_t);
	size_t size;
	memcpy(&size, chptr, sizeof(size_t));
	if (size > PAGE_SIZE) {
		munmap(ptr, PAGE_SIZE * div_up(size, PAGE_SIZE));
        return;
	}
	
	// create the tbins with the size of each bin if there are no bins
	if (!tbin){
        tbin = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		for (int ii = 0; ii < 17; ii++) {
            struct node new_node = {1, 0};
            tbin[ii] = new_node;
		}
	}
	placeLocal(ptr);
}

void*
xrealloc(void* ptr, size_t size)
{
    size_t ptr_size;
    memcpy(&ptr_size, ((char*) ptr) - sizeof(size_t), sizeof(size_t));
    ptr_size -= sizeof(size_t);
    if(size == 0){
        xfree(ptr);
        return 0;
    }
    else if(!ptr){
        return xmalloc(size);
    }
    else if(size <= ptr_size){
        return ptr;
    }
    char* new_data = ((char*) xmalloc(size));                                                                            
    memcpy(new_data, ((char*) ptr), ptr_size);
    xfree(ptr);
    return (void*) new_data;
}

