#ifndef AHSD_TIMER_HEAP_H
#define AHSD_TIMER_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TIMER_INVALID_INDEX SIZE_MAX

typedef struct TimerNode TimerNode;
typedef void (*TimerCallback)(void *user_data);

struct TimerNode {
    uint64_t expires_at_ms;
    size_t index;
    TimerCallback cb;
    void *user_data;
};

typedef struct {
    TimerNode **nodes;
    size_t size;
    size_t capacity;
} TimerHeap;

uint64_t timer_now_ms(void);

bool timer_heap_init(TimerHeap *heap, size_t capacity);
void timer_heap_destroy(TimerHeap *heap);

void timer_node_init(TimerNode *node, TimerCallback cb, void *user_data);
bool timer_node_is_active(const TimerNode *node);

bool timer_heap_upsert(TimerHeap *heap, TimerNode *node, uint64_t timeout_ms);
void timer_heap_remove(TimerHeap *heap, TimerNode *node);

int timer_heap_next_timeout_ms(const TimerHeap *heap);
size_t timer_heap_process_expired(TimerHeap *heap);

#endif
