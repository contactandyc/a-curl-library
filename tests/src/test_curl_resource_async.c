// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"
#include "a-curl-library/impl/curl_event_priv.h"
#include "a-memory-library/aml_alloc.h"

#include <string.h>

MACRO_TEST(resource_register_async_and_release) {
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    MACRO_ASSERT_TRUE(loop != NULL);

    char *p = aml_strdup("HELLO");
    curl_event_res_id id = curl_event_res_register_async(loop, p, _aml_free);

    // Not published until inbox is drained
    const char *pre = curl_event_res_get_str(loop, id);
    MACRO_ASSERT_TRUE(pre == NULL);

    curl_resource_inbox_drain(loop);

    const char *got = curl_event_res_get_str(loop, id);
    MACRO_ASSERT_TRUE(got != NULL && strcmp(got, "HELLO") == 0);

    // Addref + release twice should delete
    curl_event_res_addref(loop, id);
    curl_event_res_release(loop, id);
    curl_event_res_release(loop, id);

    // Node should be gone
    const char *gone = curl_event_res_get_str(loop, id);
    MACRO_ASSERT_TRUE(gone == NULL);

    curl_event_loop_destroy(loop);
}

MACRO_TEST(resource_block_then_async_publish_requeues) {
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    MACRO_ASSERT_TRUE(loop != NULL);

    // Register async but not drained yet
    char *payload = aml_strdup("OK");
    curl_event_res_id rid = curl_event_res_register_async(loop, payload, _aml_free);

    // Request that depends on rid
    curl_event_request_t *r = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(r != NULL);
    curl_event_request_depend(r, rid);
    curl_event_loop_request_t *wrap = curl_wrap_from_public(r);

    // Should block (not published yet)
    bool blocked = curl_resource_check_and_block_list(loop, wrap, (struct curl_res_dep_s*)r->dep_head);
    MACRO_ASSERT_TRUE(blocked);

    // Drain inbox -> publish -> requeue into loop->pending_requests
    curl_resource_inbox_drain(loop);
    MACRO_ASSERT_TRUE(loop->pending_requests == wrap);

    curl_event_request_destroy_unsubmitted(r);
    curl_event_loop_destroy(loop);
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, resource_register_async_and_release);
    MACRO_ADD(tests, resource_block_then_async_publish_requeues);
    macro_run_all("a-curl-library/curl_resource_async", tests, test_count);
    return 0;
}
