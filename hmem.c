
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "hmem.h"
#include "node.h"

const size_t PAGE_SIZE = 4096;
static node* free_list = 0;
static size_t total_alloc = 0;
pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;

long
free_list_length()
{
    if(!free_list){
        return 0;
    }
  
    long len = 1;
    node* curr = free_list;
    while(curr->next){
        len += 1;
        curr = curr->next;
    }
    return len;
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

//checks to see if there is a block big enough for the size
int
space(size_t size){
    node* curr = free_list;                                                                                   
    while((!curr) == 0){                                                                                              
        if(curr->size >= size){                                                                         
            return 1;
        }                                                                                                     
        else{       
            curr = curr->next;                
        }                                                                           
    }
    return 0;
}

//finds a block big enough to fit size                                                                        
//returns a pointer to the block requested after the size                                                     
char*                                                                                                         
find_block(size_t size){
    char* ret;    
    node* curr = free_list;
    node* prev = 0; 
    while(curr){
        if(curr->size >= size){
            ret = ((char*) curr) + size;
            memcpy(ret, curr, sizeof(node));
            curr = ((node*) ret);
            curr->size -= size;
            ret -= size;
            memcpy(ret, &size, sizeof(size_t));
            ret += sizeof(size_t);
            if(prev){
                prev->next = curr;
            }
            else{
                free_list = curr;
            }
            return ret;
        }
        prev = curr;
        curr = curr->next;   
    }
    return 0;    
} 

void
clear_zero(){
    node* curr = free_list->next;
    node* prev = free_list;
    if(free_list->size <= 0 && free_list->next != 0){
        free_list = free_list->next;
        return;
    }
    while(curr){
        if(curr->size <= 0){
            prev->next = curr->next;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
}

size_t
total_free(){
    size_t total_size = 0;
    node* curr = free_list;
    while(curr){
        total_size += curr->size;
        curr = curr->next;
    }
    return total_size;
}

void*
hmalloc(size_t size)
{
    pthread_mutex_lock(&mut);
    if(!free_list){
        free_list = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        free_list->size = PAGE_SIZE - sizeof(node);
        free_list->next = 0;
        total_alloc += (PAGE_SIZE - sizeof(node));
    }

    char* ret;
    size += sizeof(size_t);
    if(space(size)){
        ret = find_block(size);
        assert(ret != 0);
        clear_zero();
        pthread_mutex_unlock(&mut);
        return ((void*) ret);
    }
    else if(size < PAGE_SIZE){
        total_alloc += PAGE_SIZE;
        ret = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        node* add_node = (node*)(ret + size);
        memcpy(ret, &size, sizeof(size_t));
        ret = ret + sizeof(size_t);
        node* curr = free_list->next;                                                                         
        node* prev = free_list;                                                                               
        int ii = 0;                                                                                           
        while(ii < free_list_length()){
            if(add_node < prev){
                add_node->next = prev;
                add_node->size = PAGE_SIZE - size - sizeof(node); 
                free_list = add_node;
                break;
            }   
            if(curr == 0){                                                                                    
                prev->next = add_node;
                add_node->next = 0;
                add_node->size = PAGE_SIZE - size - sizeof(node);      
                break;                                                                                   
            }                                                                                                 
            else if(add_node > prev && add_node < curr){                                                          
                prev->next = add_node;                                                                          
                add_node->next = curr;                                                                           
                add_node->size = PAGE_SIZE - size - sizeof(node);  
                break;                
            }                                                                                                 
            else{
                prev = curr;
                curr = curr->next;                
                ii++;                                                                                        
            }                                                                                                 
        }
        clear_zero();
        pthread_mutex_unlock(&mut);
        return ((void*) ret);
    }
    else{
        size_t num_page = div_up(size, 4096);
        ret = mmap(0, num_page * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        memcpy(ret, &size, sizeof(size_t));
        ret = ret + sizeof(size_t);    
        pthread_mutex_unlock(&mut);    
        return ((void*) ret); 
    }
}

void
coll(){
    node* prev = free_list;
    node* curr = free_list->next;
    char* to_clear;
    size_t tol_size;
    while(curr){
        tol_size = prev->size + sizeof(node);
        if(((char*) prev) + tol_size == ((char*) curr)){
            prev->next = curr->next;
            prev->size += curr->size + sizeof(node);
            to_clear = ((char*) prev) + sizeof(node);
            memset(to_clear, 0, prev->size);
            curr = prev->next;
        }
        else{
            prev = curr;
            curr = curr->next;
        }
    }
    if(total_alloc == total_free()){
        munmap(free_list, total_alloc + sizeof(node));
        free_list = 0;
    }
}

void
hfree(void* item)
{
    pthread_mutex_lock(&mut);
    size_t isize[1];
    char* to_free = ((char*) item);
    to_free -= sizeof(size_t);
    memcpy(isize, to_free, sizeof(size_t));
    size_t size = *isize;
    if(size > PAGE_SIZE){
        munmap(item, size);
    }
    else{
        memset(to_free, 0, size);
        node* to_add = ((node*) to_free);
        node* curr = free_list->next;
        node* prev = free_list;
        int ii = 0;
        while(ii < free_list_length()){
            if(to_add < prev){
                to_add->next = prev;
                to_add->size = size - sizeof(node);
                free_list = to_add;
                break;
            }
            if(curr == 0){
                prev->next = to_add;
                to_add->size = size - sizeof(node);
                to_add->next = 0;
                break;
            }
            else if(to_add > prev && to_add < curr){
                prev->next = to_add;
                to_add->next = curr;
                to_add->size = size - sizeof(node);
                break;
            }
            else{
                prev = curr;
                curr = curr->next;
                ii++;
            }
        }
    }
    coll();
    pthread_mutex_unlock(&mut);
}

void*
hrealloc(void* ptr, size_t size)
{
    size_t ptr_size;
    memcpy(&ptr_size, ((char*) ptr) - sizeof(size_t), sizeof(size_t));
    if(size == 0){
        hfree(ptr);
        return 0;
    }
    else if(!ptr){
        return hmalloc(size);
    }
    else if(size < ptr_size){
        return ptr;
    }
    char* new_data = ((char*) hmalloc(size));                                                                            
    memcpy(new_data, ((char*) ptr), ptr_size);
    hfree(ptr);
    return (void*) new_data;
}
