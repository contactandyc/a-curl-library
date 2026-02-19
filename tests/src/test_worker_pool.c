// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/worker_pool.h"

#include <stdatomic.h>
#include <stddef.h>

static atomic_int g_count = 0;

static void work(void *arg) {
    (void)arg;
    atomic_fetch_add_explicit(&g_count, 1, memory_order_relaxed);
}

MACRO_TEST(worker_pool_executes_all_tasks) {
    g_count = 0;
    const int threads = 4;
    const int tasks   = 1000;

    worker_pool_t *pool = worker_pool_init(threads);
    for (int i = 0; i < tasks; ++i) {
        worker_pool_push(pool, work, NULL);
    }
    worker_pool_destroy(pool); // joins workers after queue drains

    MACRO_ASSERT_EQ_INT(atomic_load(&g_count), tasks);
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, worker_pool_executes_all_tasks);
    macro_run_all("a-curl-library/worker_pool", tests, test_count);
    return 0;
}
