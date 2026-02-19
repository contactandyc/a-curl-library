// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "the-macro-library/macro_test.h"
#include "a-curl-library/sinks/memory.h"
#include "a-curl-library/sinks/file.h"
#include "a-curl-library/curl_event_request.h"
#include "a-memory-library/aml_alloc.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
    int called;
    int success;
    CURLcode result;
    long http;
    size_t len;
    char data[128];
} mem_cb_state;

static void mem_cb(char *data, size_t len, bool success,
                   CURLcode result, long http_code, const char *err,
                   void *arg, curl_event_request_t *req) {
    (void)req; (void)err;
    mem_cb_state *s = (mem_cb_state*)arg;
    s->called++;
    s->success = success ? 1 : 0;
    s->result = result;
    s->http = http_code;
    s->len = len;
    size_t n = len < sizeof(s->data)-1 ? len : sizeof(s->data)-1;
    memcpy(s->data, data, n);
    s->data[n] = 0;
}

typedef struct {
    int called;
    int success;
    CURLcode result;
    long http;
    char filename[256];
} file_cb_state;

static void file_cb(const char *filename, bool success,
                    CURLcode result, long http_code, const char *err,
                    void *arg, curl_event_request_t *req) {
    (void)req; (void)err;
    file_cb_state *s = (file_cb_state*)arg;
    s->called++;
    s->success = success ? 1 : 0;
    s->result = result;
    s->http = http_code;
    snprintf(s->filename, sizeof(s->filename), "%s", filename);
}

static void make_tmp_path(char *out, size_t cap) {
#ifndef _WIN32
    char pattern[] = "/tmp/a_curl_file_sink_XXXXXX";
    int fd = mkstemp(pattern);
    if (fd >= 0) close(fd);
    snprintf(out, cap, "%s", pattern);
#else
    snprintf(out, cap, "a_curl_file_sink_test.tmp");
#endif
}

MACRO_TEST(memory_sink_success_and_failure_callbacks) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    mem_cb_state s = {0};
    curl_sink_interface_t *iface = memory_sink(req, mem_cb, &s);
    MACRO_ASSERT_TRUE(iface != NULL);

    // Success path
    MACRO_ASSERT_TRUE(iface->init(iface, 5));
    const char *payload = "hello";
    size_t w = iface->write(payload, 1, strlen(payload), iface);
    MACRO_ASSERT_EQ_INT((int)w, (int)strlen(payload));
    iface->complete(iface, req);
    MACRO_ASSERT_EQ_INT(s.called, 1);
    MACRO_ASSERT_EQ_INT(s.success, 1);
    MACRO_ASSERT_TRUE(strcmp(s.data, "hello") == 0);

    // Failure path (new sink for cleanliness)
    curl_event_request_t *req2 = curl_event_request_init(0);
    mem_cb_state s2 = {0};
    curl_sink_interface_t *iface2 = memory_sink(req2, mem_cb, &s2);
    MACRO_ASSERT_TRUE(iface2->init(iface2, 3));
    const char *errp = "err";
    iface2->write(errp, 1, 3, iface2);
    iface2->failure(CURLE_COULDNT_RESOLVE_HOST, 500, iface2, req2);
    MACRO_ASSERT_EQ_INT(s2.called, 1);
    MACRO_ASSERT_EQ_INT(s2.success, 0);
    MACRO_ASSERT_TRUE(strncmp(s2.data, "err", 3) == 0);

    iface->destroy(iface);
    iface2->destroy(iface2);
    curl_event_request_destroy_unsubmitted(req);
    curl_event_request_destroy_unsubmitted(req2);
}

MACRO_TEST(file_sink_write_complete_and_failure) {
    curl_event_request_t *req = curl_event_request_init(0);
    MACRO_ASSERT_TRUE(req != NULL);

    char path[256];
    make_tmp_path(path, sizeof(path));

    file_cb_state st = {0};
    curl_sink_interface_t *iface = file_sink(req, path, file_cb, &st);
    MACRO_ASSERT_TRUE(iface != NULL);

    MACRO_ASSERT_TRUE(iface->init(iface, 0));
    const char *data = "abc123";
    size_t w = iface->write(data, 1, strlen(data), iface);
    MACRO_ASSERT_EQ_INT((int)w, (int)strlen(data));
    iface->complete(iface, req);
    MACRO_ASSERT_EQ_INT(st.called, 1);
    MACRO_ASSERT_EQ_INT(st.success, 1);
    iface->destroy(iface);

    // Verify file contents
    FILE *f = fopen(path, "rb");
    MACRO_ASSERT_TRUE(f != NULL);
    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    MACRO_ASSERT_EQ_INT((int)n, (int)strlen(data));
    MACRO_ASSERT_TRUE(strncmp(buf, data, strlen(data)) == 0);

    // Failure callback (no write needed)
    file_cb_state st2 = {0};
    curl_event_request_t *req2 = curl_event_request_init(0);
    curl_sink_interface_t *iface2 = file_sink(req2, path, file_cb, &st2);
    MACRO_ASSERT_TRUE(iface2->init(iface2, 0));
    iface2->failure(CURLE_COULDNT_CONNECT, 503, iface2, req2);
    MACRO_ASSERT_EQ_INT(st2.called, 1);
    MACRO_ASSERT_EQ_INT(st2.success, 0);

    // Cleanup
    iface2->destroy(iface2);
    unlink(path);
    curl_event_request_destroy_unsubmitted(req);
    curl_event_request_destroy_unsubmitted(req2);
}

int main(void) {
    macro_test_case tests[16];
    size_t test_count = 0;
    MACRO_ADD(tests, memory_sink_success_and_failure_callbacks);
    MACRO_ADD(tests, file_sink_write_complete_and_failure);
    macro_run_all("a-curl-library/sinks", tests, test_count);
    return 0;
}
