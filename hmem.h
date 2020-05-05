#ifndef HMALLOC_H
#define HMALLOC_H

void* hmalloc(size_t size);
void hfree(void* item);
void* hrealloc(void* ptr, size_t bytes);

#endif
