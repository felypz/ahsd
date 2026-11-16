#include "timer_heap.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t timer_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static void heap_swap(TimerHeap *heap, size_t i, size_t j) {
    TimerNode *tmp = heap->nodes[i];
    heap->nodes[i] = heap->nodes[j];
    heap->nodes[j] = tmp;

    heap->nodes[i]->index = i;
    heap->nodes[j]->index = j;
}

static void sift_up(TimerHeap *heap, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap->nodes[idx]->expires_at_ms >= heap->nodes[parent]->expires_at_ms) {
            break;
        }
        heap_swap(heap, idx, parent);
        idx = parent;
    }
}

static void sift_down(TimerHeap *heap, size_t idx) {
    for (;;) {
        size_t smallest = idx;
        size_t left = (2 * idx) + 1;
        size_t right = (2 * idx) + 2;

        if (left < heap->size &&
            heap->nodes[left]->expires_at_ms < heap->nodes[smallest]->expires_at_ms) {
            smallest = left;
        }
        if (right < heap->size &&
            heap->nodes[right]->expires_at_ms < heap->nodes[smallest]->expires_at_ms) {
            smallest = right;
        }
        if (smallest == idx) {
            break;
        }

        heap_swap(heap, idx, smallest);
        idx = smallest;
    }
}

bool timer_heap_init(TimerHeap *heap, size_t capacity) {
    if (!heap || capacity == 0) {
        return false;
    }

    heap->nodes = calloc(capacity, sizeof(TimerNode *));
    if (!heap->nodes) {
        return false;
    }

    heap->size = 0;
    heap->capacity = capacity;
    return true;
}

void timer_heap_destroy(TimerHeap *heap) {
    if (!heap) {
        return;
    }
    free(heap->nodes);
    memset(heap, 0, sizeof(*heap));
}

void timer_node_init(TimerNode *node, TimerCallback cb, void *user_data) {
    if (!node) {
        return;
    }
    node->expires_at_ms = 0;
    node->index = TIMER_INVALID_INDEX;
    node->cb = cb;
    node->user_data = user_data;
}

bool timer_node_is_active(const TimerNode *node) {
    return node && node->index != TIMER_INVALID_INDEX;
}

bool timer_heap_upsert(TimerHeap *heap, TimerNode *node, uint64_t timeout_ms) {
    if (!heap || !node) {
        return false;
    }

    uint64_t new_expiry = timer_now_ms() + timeout_ms;

    if (timer_node_is_active(node)) {
        uint64_t old_expiry = node->expires_at_ms;
        node->expires_at_ms = new_expiry;
        if (new_expiry < old_expiry) {
            sift_up(heap, node->index);
        } else if (new_expiry > old_expiry) {
            sift_down(heap, node->index);
        }
        return true;
    }

    if (heap->size >= heap->capacity) {
        return false;
    }

    size_t idx = heap->size++;
    node->expires_at_ms = new_expiry;
    node->index = idx;
    heap->nodes[idx] = node;
    sift_up(heap, idx);
    return true;
}

void timer_heap_remove(TimerHeap *heap, TimerNode *node) {
    if (!heap || !timer_node_is_active(node)) {
        return;
    }

    size_t idx = node->index;
    size_t last_idx = --heap->size;

    node->index = TIMER_INVALID_INDEX;
    node->expires_at_ms = 0;

    if (idx == last_idx) {
        heap->nodes[last_idx] = NULL;
        return;
    }

    TimerNode *moved = heap->nodes[last_idx];
    heap->nodes[idx] = moved;
    heap->nodes[last_idx] = NULL;
    moved->index = idx;

    sift_up(heap, idx);
    sift_down(heap, moved->index);
}

int timer_heap_next_timeout_ms(const TimerHeap *heap) {
    if (!heap || heap->size == 0) {
        return -1;
    }

    uint64_t now = timer_now_ms();
    uint64_t earliest = heap->nodes[0]->expires_at_ms;
    if (earliest <= now) {
        return 0;
    }

    uint64_t diff = earliest - now;
    return diff > (uint64_t)INT32_MAX ? INT32_MAX : (int)diff;
}

size_t timer_heap_process_expired(TimerHeap *heap) {
    if (!heap || heap->size == 0) {
        return 0;
    }

    size_t fired = 0;
    uint64_t now = timer_now_ms();

    while (heap->size > 0 && heap->nodes[0]->expires_at_ms <= now) {
        TimerNode *expired = heap->nodes[0];
        timer_heap_remove(heap, expired);
        fired++;

        if (expired->cb) {
            expired->cb(expired->user_data);
        }
    }

    return fired;
}
