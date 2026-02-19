// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/rate_manager.h"

#include <unistd.h>   // usleep

static void sleep_ns(uint64_t ns) {
    // best effort sleep (convert to microseconds; round up a bit)
    uint64_t us = ns / 1000u;
    if (us == 0) us = 1;
    usleep((useconds_t)us);
}

MACRO_TEST(rate_manager_basic_bucket) {
    rate_manager_init();

    // 1 rps to get a predictable wait, concurrency isn't enforced in this impl
    rate_manager_set_limit("key1", /*max_concurrent*/1, /*max_rps*/1.0);

    // First start should proceed immediately.
    uint64_t w1 = rate_manager_start_request("key1", false);
    MACRO_ASSERT_EQ_INT((int)(w1 == 0), 1);

    // Immediately starting again should be throttled (need ~1s for new token).
    uint64_t w2 = rate_manager_start_request("key1", false);
    MACRO_ASSERT_TRUE(w2 > 0);

    // Mark one request done so concurrent counter goes down (even if not used here).
    rate_manager_request_done("key1");

    // Wait until can proceed returns 0, then start.
    for (;;) {
        uint64_t wait_ns = rate_manager_can_proceed("key1", false);
        if (wait_ns == 0) break;
        sleep_ns(wait_ns);
    }

    uint64_t w3 = rate_manager_start_request("key1", false);
    MACRO_ASSERT_EQ_INT((int)(w3 == 0), 1);
    rate_manager_request_done("key1");

    rate_manager_destroy();
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, rate_manager_basic_bucket);
    macro_run_all("a-curl-library/rate_manager", tests, test_count);
    return 0;
}
