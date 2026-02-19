// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/worker_pool.h"
#include "a-memory-library/aml_alloc.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct work_item_s {
    void (*func)(void *arg); // The work function
    void *arg;               // User data
    struct work_item_s *next;
} work_item_t;

typedef struct {
    work_item_t *head;
    work_item_t *tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool stop;
} work_queue_t;

struct worker_pool_s {
    work_queue_t *queue;
    pthread_t *threads;
    int num_threads;
};

/* Pop a work item (blocking) */
static work_item_t *work_queue_pop(work_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    while (!q->stop && !q->head) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }
    if (q->stop && !q->head) {
        // No more work, queue is shutting down
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    work_item_t *item = q->head;
    q->head = item->next;
    if (!q->head) {
        q->tail = NULL;
    }
    pthread_mutex_unlock(&q->mutex);
    return item;
}

static void *worker_thread_main(void *arg) {
    work_queue_t *q = (work_queue_t *)arg;
    for (;;) {
        work_item_t *item = work_queue_pop(q);
        if (!item) {
            // The queue is stopping
            break;
        }
        // Execute the function
        item->func(item->arg);
        aml_free(item);
    }
    return NULL;
}

/* Initialize the queue */
static work_queue_t *work_queue_init() {
    work_queue_t *q = (work_queue_t *)aml_calloc(1, sizeof(work_queue_t));
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->stop = false;
    return q;
}

/* Destroy the queue (make sure all threads are stopped first!) */
static void work_queue_destroy(work_queue_t *q) {
    // Ideally, the caller has already joined worker threads
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    aml_free(q);
}

worker_pool_t *worker_pool_init(int num_threads) {
    worker_pool_t *pool = (worker_pool_t *)aml_calloc(1, sizeof(worker_pool_t));
    pool->num_threads = num_threads;
    pool->threads = (pthread_t *)aml_calloc(num_threads, sizeof(pthread_t));
    pool->queue = work_queue_init();
    // queue must be already initted
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread_main, pool->queue);
    }
    return pool;
}

static void worker_pool_stop(worker_pool_t *pool) {
    // Signal all workers to stop
    pthread_mutex_lock(&pool->queue->mutex);
    pool->queue->stop = true;
    pthread_cond_broadcast(&pool->queue->cond);
    pthread_mutex_unlock(&pool->queue->mutex);

    // Join all threads
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
}

void worker_pool_destroy(worker_pool_t *pool) {
    worker_pool_stop(pool);
    aml_free(pool->threads);
    work_queue_destroy(pool->queue);
    aml_free(pool);
}

/* Enqueue a work item */
void worker_pool_push(worker_pool_t *pool, void (*func)(void *), void *arg) {
    work_queue_t *q = pool->queue;
    work_item_t *item = (work_item_t *)aml_calloc(1, sizeof(work_item_t));
    item->func = func;
    item->arg = arg;
    item->next = NULL;

    pthread_mutex_lock(&q->mutex);
    if (q->tail) {
        q->tail->next = item;
        q->tail = item;
    } else {
        q->head = q->tail = item;
    }
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}
