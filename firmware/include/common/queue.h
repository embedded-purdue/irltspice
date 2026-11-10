#ifndef __COMMON_QUEUE_H__
#define __COMMON_QUEUE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct _QueueNode {
    struct _QueueNode *next;
} QueueNode;

typedef struct _Queue {
    QueueNode *head;
    QueueNode *tail;
} Queue;

#define pop(queue, type) ((type *)((uint8_t *)_pop(queue) - offsetof(type, queue_node)))

bool queue_is_empty(const Queue *queue) { return queue->head == NULL; }

void enqueue(Queue *queue, QueueNode *node, bool priority) {
    if (queue->head == NULL) {
        node->next = NULL;
        queue->head = node;
        queue->tail = node;
        return;
    }

    if (priority) {
        node->next = queue->head;
        queue->head = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
}

QueueNode *_pop(Queue *queue) {
    QueueNode *node = queue->head;
    if (node == NULL) {
        return NULL;
    }

    if (queue->head == queue->tail) {
        queue->tail = NULL;
    }

    queue->head = node->next;
    return node;
}

#define queue_init(queue, free_node_list, node_buffer, node_buffer_size)                           \
    do {                                                                                           \
        free_node_list = queue = (Queue){.head = NULL, .tail = NULL};                              \
        for (size_t i = node_buffer_size; i > 0; i--) {                                            \
            enqueue(&free_node_list, &node_buffer[i - 1].queue_node, true);                        \
        }                                                                                          \
    } while (0);

#endif
