// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/rate_manager.h"
#include <unistd.h>
#include <stdint.h>

static void sleep_ns(uint64_t ns) {
    uint64_t us = ns / 1000u;
    if (us == 0) us = 1;
    usleep((useconds_t)us);
}

MACRO_TEST(rate_manager_high_priority_preempts_normal) {
    rate_manager_init();
    rate_manager_set_limit("hp", /*max_concurrent*/1, /*max_rps*/0.5); // tokens start at 0.5

    // HP request waiting: increments internal hp counter, returns wait
    uint64_t whp = rate_manager_can_proceed("hp", true);
    MACRO_ASSERT_TRUE(whp > 0);

    // Normal should now wait while HP is pending
    uint64_t wn = rate_manager_can_proceed("hp", false);
    MACRO_ASSERT_TRUE(wn > 0);

    // Eventually HP should be able to start; spin until token refills
    uint64_t wstart = rate_manager_start_request("hp", true);
    while (wstart > 0) {
        // Wait the suggested amount before retrying
        sleep_ns(wstart);
        wstart = rate_manager_start_request("hp", true);
    }

    rate_manager_request_done("hp");

    // After serving HP, normal can proceed (no pending HP)
    uint64_t wn2 = rate_manager_can_proceed("hp", false);
    MACRO_ASSERT_EQ_INT((int)(wn2 == 0), 1);

    rate_manager_destroy();
}

MACRO_TEST(rate_manager_429_backoff_and_reset) {
    rate_manager_init();
    rate_manager_set_limit("k429", 1, 100.0);

    int b1 = rate_manager_handle_429("k429");
    MACRO_ASSERT_EQ_INT(b1, 1); // recent success → reset backoff=1

    // Wait >2s so backoff grows
    usleep(2200000);
    int b2 = rate_manager_handle_429("k429");
    MACRO_ASSERT_TRUE(b2 >= 2);

    // Any success resets backoff to 1
    rate_manager_request_done("k429");
    int b3 = rate_manager_handle_429("k429");
    MACRO_ASSERT_EQ_INT(b3, 1);

    rate_manager_destroy();
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, rate_manager_high_priority_preempts_normal);
    MACRO_ADD(tests, rate_manager_429_backoff_and_reset);
    macro_run_all("a-curl-library/rate_manager_hp_429", tests, test_count);
    return 0;
}
