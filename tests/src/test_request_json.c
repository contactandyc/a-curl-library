// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/curl_event_request.h"
#include "a-json-library/ajson.h"

#include <string.h>

static int has_header(struct curl_slist *h, const char *needle) {
    for (; h; h = h->next) {
        if (h->data && strstr(h->data, needle)) return 1;
    }
    return 0;
}

MACRO_TEST(request_json_commit_sets_body_and_header) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    // Begin an object JSON body and set a trivial field
    ajson_t *root = curl_event_request_json_begin(req, /*array_root=*/false);
    MACRO_ASSERT_TRUE(root != NULL);

    // root is object; add a small field: {"x": 42}
    ajsono_append(root, "x", ajson_number(req->pool, 42), false);

    // Commit -> stringify into post_data and set CT header
    curl_event_request_json_commit(req);

    // post_data present and looks JSON-ish
    MACRO_ASSERT_TRUE(req->post_data != NULL);
    MACRO_ASSERT_TRUE(strchr(req->post_data, '{') != NULL);

    // CT header present (unless user disabled)
    MACRO_ASSERT_TRUE(has_header(req->headers, "Content-Type: application/json"));

    // Defaults: method should be POST if it wasn't set earlier
    MACRO_ASSERT_TRUE(req->method != NULL && strcmp(req->method, "POST") == 0);

    curl_event_request_destroy_unsubmitted(req);
}

int main(void) {
    macro_test_case tests[8];
    size_t test_count = 0;
    MACRO_ADD(tests, request_json_commit_sets_body_and_header);
    macro_run_all("a-curl-library/request_json", tests, test_count);
    return 0;
}
