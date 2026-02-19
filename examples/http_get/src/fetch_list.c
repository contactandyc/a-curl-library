// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/sinks/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char *url;
    int index;
} fetch_ctx_t;

/* Trim helpers */
static char *rtrim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
    return s;
}
static char *ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

/* Called when a download finishes.  'arg' is our per-request fetch_ctx_t*. */
static void on_mem_done(
    char *data, size_t len, bool success,
    CURLcode result, long http, const char *err,
    void *arg, curl_event_request_t *req)
{
    (void)req;
    fetch_ctx_t *ctx = (fetch_ctx_t *)arg;

    if (!success) {
        fprintf(stdout, "[%d] %s -> FAILED (curl=%d http=%ld err=%s)\n",
                ctx->index, ctx->url, result, http, err ? err : "(none)");
    } else {
        fprintf(stdout, "\n===[%d] %s===\n", ctx->index, ctx->url);
        fwrite(data, 1, len, stdout);
        fputc('\n', stdout);
    }

    free(ctx->url);
    free(ctx);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <urls.txt>\n", argv[0]);
        fprintf(stderr, "  (file should contain one URL per line; blank lines and lines starting with '#' are ignored)\n");
        return 1;
    }

    const char *path = argv[1];
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    char *line = NULL;
    size_t cap = 0;
    int idx = 0;

    while (getline(&line, &cap, fp) != -1) {
        char *s = ltrim(rtrim(line));
        if (*s == '\0' || *s == '#') continue;   // skip blank/comment

        // build per-request context
        fetch_ctx_t *ctx = (fetch_ctx_t *)calloc(1, sizeof(*ctx));
        ctx->index = ++idx;
        ctx->url = strdup(s);

        curl_event_request_t *r = curl_event_request_build_get(
            ctx->url,
            NULL,   // use sink instead of manual write callback
            NULL
        );

        memory_sink(r, on_mem_done, ctx);

        // Optional: “browsery” headers (leave commented unless you need them)
        curl_event_request_apply_browser_profile(r, NULL, NULL);

        // Some sensible timeouts
        curl_event_request_connect_timeout(r, 10);
        curl_event_request_transfer_timeout(r, 30);
        curl_event_request_low_speed(r, 1000, 10);

        curl_event_request_submit(loop, r, 0);
    }

    free(line);
    fclose(fp);

    // Process all submitted requests
    curl_event_loop_run(loop);
    curl_event_loop_destroy(loop);
    return 0;
}
