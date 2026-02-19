// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"

// Public headers
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/curl_resource.h"

// Internal header (installed) – we use this to access the wrapper helpers
#include "a-curl-library/impl/curl_event_priv.h"

#include <string.h>

MACRO_TEST(resource_block_and_publish_requeues) {
    // Start loop (sets owner thread etc.)
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);
    MACRO_ASSERT_TRUE(loop != NULL);

    // Declare a resource and make a request that depends on it.
    curl_event_res_id rid = curl_event_res_declare(loop);
    MACRO_ASSERT_TRUE(rid != 0);

    curl_event_request_t *r = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(r != NULL);

    // Add dependency
    curl_event_request_depend(r, rid);

    // Convert public req to loop wrapper for the block check
    curl_event_loop_request_t *wrap = curl_wrap_from_public(r);

    // Not published yet -> should block
    bool blocked = curl_resource_check_and_block_list(loop, wrap, (struct curl_res_dep_s*)r->dep_head);
    MACRO_ASSERT_TRUE(blocked);

    // Nothing pending yet; blocked list is on the resource.
    MACRO_ASSERT_TRUE(wrap->next_pending == NULL);

    // Publish payload to wake dependents
    const char *payload = "OK";
    curl_event_res_publish(loop, rid, (void*)payload, NULL);

    // Publishing moves the blocked request into pending list
    MACRO_ASSERT_TRUE(loop->pending_requests == wrap);

    // Peek to ensure payload is there
    const char *peek = (const char*)curl_event_res_peek(loop, rid);
    MACRO_ASSERT_TRUE(peek != NULL && strcmp(peek, "OK") == 0);

    // Clean up the (unsubmitted) request
    curl_event_request_destroy_unsubmitted(r);
    curl_event_loop_destroy(loop);
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, resource_block_and_publish_requeues);
    macro_run_all("a-curl-library/curl_resource", tests, test_count);
    return 0;
}
