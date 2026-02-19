// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _worker_pool_H
#define _worker_pool_H

struct worker_pool_s;
typedef struct worker_pool_s worker_pool_t;

worker_pool_t *worker_pool_init(int num_threads);
void worker_pool_push(worker_pool_t *pool, void (*func)(void *), void *arg);
void worker_pool_destroy(worker_pool_t *pool);

#endif
