// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"

static int noop_complete(CURL *easy, struct curl_event_request_s *req) {
    (void)easy; (void)req; return 0;
}
static size_t noop_write(void *p, size_t s, size_t n, struct curl_event_request_s *req) {
    (void)p;
    (void)req; return s*n;
}

MACRO_TEST(event_loop_cancel_pending_dep_no_network) {
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    MACRO_ASSERT_TRUE(loop != NULL);

    curl_event_res_id rid = curl_event_res_declare(loop);

    curl_event_request_t *req = curl_event_request_init(0);
    curl_event_request_url(req, "file:///dev/null");  // never started anyway
    curl_event_request_on_complete(req, noop_complete);
    curl_event_request_on_write(req, noop_write);
    curl_event_request_depend(req, rid);

    // Submit -> goes into pending list, blocked on dep
    curl_event_request_submitp(loop, req);

    // Cancel before it ever starts
    bool ok = curl_event_loop_cancel(loop, req);
    MACRO_ASSERT_TRUE(ok);

    // Run loop: processes cancelled/pending without I/O
    curl_event_loop_run(loop);

    // Metrics: 1 submitted
    curl_event_metrics_t m = curl_event_loop_get_metrics(loop);
    MACRO_ASSERT_EQ_INT((int)m.total_requests, 1);

    curl_event_loop_destroy(loop);
}

int main(void) {
    macro_test_case tests[4];
    size_t test_count = 0;
    MACRO_ADD(tests, event_loop_cancel_pending_dep_no_network);
    macro_run_all("a-curl-library/event_loop_cancel", tests, test_count);
    return 0;
}
