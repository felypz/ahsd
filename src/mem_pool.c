#include "mem_pool.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

static bool is_valid_ptr(const MemPool *pool, const void *ptr) {
    const unsigned char *p = (const unsigned char *)ptr;
    const unsigned char *start = pool->memory;
    const unsigned char *end = start + (pool->block_size * pool->capacity);

    if (p < start || p >= end) {
        return false;
    }
    return ((size_t)(p - start) % pool->block_size) == 0;
}

bool mem_pool_init(MemPool *pool, size_t block_size, size_t capacity) {
    if (!pool || block_size == 0 || capacity == 0) {
        return false;
    }

    size_t min_size = block_size < sizeof(void *) ? sizeof(void *) : block_size;
    size_t aligned_size = align_up(min_size, alignof(max_align_t));

    if (capacity > SIZE_MAX / aligned_size) {
        return false;
    }

    size_t total_bytes = aligned_size * capacity;
    unsigned char *mem = aligned_alloc(alignof(max_align_t), total_bytes);
    if (!mem) {
        return false;
    }

    pool->memory = mem;
    pool->block_size = aligned_size;
    pool->capacity = capacity;
    pool->in_use = 0;
    pool->free_list = NULL;

    mem_pool_reset(pool);
    return true;
}

void mem_pool_destroy(MemPool *pool) {
    if (!pool) {
        return;
    }
    free(pool->memory);
    memset(pool, 0, sizeof(*pool));
}

void mem_pool_reset(MemPool *pool) {
    if (!pool || !pool->memory) {
        return;
    }

    pool->free_list = NULL;
    pool->in_use = 0;

    for (size_t i = pool->capacity; i > 0; i--) {
        unsigned char *block = pool->memory + ((i - 1) * pool->block_size);
        *(void **)block = pool->free_list;
        pool->free_list = block;
    }
}

void *mem_pool_alloc(MemPool *pool) {
    if (!pool || !pool->free_list) {
        return NULL;
    }

    void *block = pool->free_list;
    pool->free_list = *(void **)block;
    pool->in_use++;

    memset(block, 0, pool->block_size);
    return block;
}

void mem_pool_free(MemPool *pool, void *ptr) {
    if (!pool || !ptr || !is_valid_ptr(pool, ptr)) {
        return;
    }

    *(void **)ptr = pool->free_list;
    pool->free_list = ptr;
    pool->in_use--;
}

size_t mem_pool_available(const MemPool *pool) {
    if (!pool) {
        return 0;
    }
    return pool->capacity - pool->in_use;
}
