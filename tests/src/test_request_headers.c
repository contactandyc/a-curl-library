// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/curl_event_request.h"
#include "a-json-library/ajson.h"
#include <string.h>

static int header_count(struct curl_slist *h, const char *needle) {
    int c = 0;
    for (; h; h = h->next) if (h->data && strstr(h->data, needle)) ++c;
    return c;
}

static int has_header(struct curl_slist *h, const char *needle) {
    return header_count(h, needle) > 0;
}

MACRO_TEST(headers_set_replaces_existing) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    curl_event_request_add_header(req, "X-Test", "alpha");
    curl_event_request_add_header(req, "Other", "v");
    MACRO_ASSERT_EQ_INT(header_count(req->headers, "X-Test: alpha"), 1);

    // Replace value
    curl_event_request_set_header(req, "X-Test", "beta");
    MACRO_ASSERT_EQ_INT(header_count(req->headers, "X-Test: alpha"), 0);
    MACRO_ASSERT_EQ_INT(header_count(req->headers, "X-Test: beta"), 1);
    MACRO_ASSERT_EQ_INT(header_count(req->headers, "Other: v"), 1);

    curl_event_request_destroy_unsubmitted(req);
}

MACRO_TEST(browser_profile_sets_defaults) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    curl_event_request_apply_browser_profile(req, NULL, NULL);

    MACRO_ASSERT_TRUE(has_header(req->headers, "User-Agent: "));
    MACRO_ASSERT_TRUE(has_header(req->headers, "Accept: "));
    MACRO_ASSERT_TRUE(has_header(req->headers, "Accept-Language: "));

    curl_event_request_destroy_unsubmitted(req);
}

MACRO_TEST(json_autocontenttype_disable) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    ajson_t *root = curl_event_request_json_begin(req, false);
    MACRO_ASSERT_TRUE(root != NULL);

    // Disable automatic Content-Type setting
    curl_event_request_json_autocontenttype(req, false);
    curl_event_request_json_commit(req);

    MACRO_ASSERT_TRUE(req->post_data != NULL);
    MACRO_ASSERT_TRUE(!has_header(req->headers, "Content-Type: application/json"));
    // Method should be POST when JSON body is begun
    MACRO_ASSERT_TRUE(req->method != NULL && strcmp(req->method, "POST") == 0);

    curl_event_request_destroy_unsubmitted(req);
}

static char *serializer_returns_fixed(aml_pool_t *pool, const ajson_t *json) {
    (void)json;
    // Produce a deterministic payload
    return aml_pool_strdup(pool, "{\"foo\":1}");
}

MACRO_TEST(post_json_uses_serializer_hook) {
    // Install serializer
    curl_event_set_ajson_serializer(serializer_returns_fixed);

    curl_event_request_t *req = curl_event_request_build_post_json("http://example.invalid",
                                                                   NULL, NULL, NULL);
    MACRO_ASSERT_TRUE(req != NULL);
    MACRO_ASSERT_TRUE(req->post_data != NULL && strstr(req->post_data, "\"foo\"") != NULL);
    MACRO_ASSERT_TRUE(has_header(req->headers, "Content-Type: application/json"));
    MACRO_ASSERT_TRUE(req->method != NULL && strcmp(req->method, "POST") == 0);

    // Reset hook
    curl_event_set_ajson_serializer(NULL);
    curl_event_request_destroy_unsubmitted(req);
}

int main(void) {
    macro_test_case tests[16];
    size_t test_count = 0;
    MACRO_ADD(tests, headers_set_replaces_existing);
    MACRO_ADD(tests, browser_profile_sets_defaults);
    MACRO_ADD(tests, json_autocontenttype_disable);
    MACRO_ADD(tests, post_json_uses_serializer_hook);
    macro_run_all("a-curl-library/request_headers", tests, test_count);
    return 0;
}
