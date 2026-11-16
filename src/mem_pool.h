#ifndef AHSD_MEM_POOL_H
#define AHSD_MEM_POOL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned char *memory;
    void *free_list;
    size_t block_size;
    size_t capacity;
    size_t in_use;
} MemPool;

bool mem_pool_init(MemPool *pool, size_t block_size, size_t capacity);
void mem_pool_destroy(MemPool *pool);

void *mem_pool_alloc(MemPool *pool);
void mem_pool_free(MemPool *pool, void *ptr);
void mem_pool_reset(MemPool *pool);

size_t mem_pool_available(const MemPool *pool);

#endif
