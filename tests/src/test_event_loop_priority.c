// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "the-macro-library/macro_time.h"

MACRO_TEST(submit_priority_affects_next_retry_at) {
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    MACRO_ASSERT_TRUE(loop != NULL);

    curl_event_request_t *low = curl_event_request_init(0);
    curl_event_request_url(low, "file:///dev/null");

    curl_event_request_t *high = curl_event_request_init(0);
    curl_event_request_url(high, "file:///dev/null");

    // Submit with different priorities
    curl_event_loop_submit(loop, low, 0);
    curl_event_loop_submit(loop, high, 5); // higher = sooner

    // Higher priority should have an earlier (smaller) next_retry_at
    MACRO_ASSERT_TRUE(high->next_retry_at < low->next_retry_at);

    // Metrics incremented
    curl_event_metrics_t m = curl_event_loop_get_metrics(loop);
    MACRO_ASSERT_EQ_INT((int)m.total_requests, 2);

    curl_event_loop_destroy(loop);
}

int main(void) {
    macro_test_case tests[4];
    size_t test_count = 0;
    MACRO_ADD(tests, submit_priority_affects_next_retry_at);
    macro_run_all("a-curl-library/event_loop_priority", tests, test_count);
    return 0;
}
