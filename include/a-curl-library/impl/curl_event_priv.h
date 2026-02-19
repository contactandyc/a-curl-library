// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H
#define A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H

/* Public facade (brings in request struct & callbacks) */
#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_resource.h"   /* curl_event_res_id */

/* Third‑party / support */
#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
#include <stddef.h>                   /* offsetof */
#include <curl/curl.h>

#include "the-macro-library/macro_map.h"
#include "the-macro-library/macro_time.h"
#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"

/* ------------------------------------------------------------------ */
/* Forward decl for per‑request wrapper ------------------------------ */
typedef struct curl_event_loop_request_s curl_event_loop_request_t;

typedef struct res_op_s {
    int kind; /* 0=REGISTER, 1=PUBLISH, 2=RELEASE */
    uint64_t id;
    void *payload;
    void (*cleanup)(void *);
    struct res_op_s *next;
} res_op_t;

typedef struct res_inbox_s { _Atomic(res_op_t*) head; } res_inbox_t;

/* ------------------------------------------------------------------ */
/* Per‑request wrapper that lives in the loop’s containers ----------- */
/* Note: This wrapper is typically allocated from req->pool now. */
struct curl_event_loop_request_s {
    macro_map_t node;               /* multimap key = next_retry_at        */
    curl_event_request_t request;   /* public view                         */

    /* libcurl plumbing */
    CURLM *multi_handle;
    CURL  *easy_handle;

    /* book‑keeping / links */
    struct curl_event_loop_request_s *next_cancelled;
    struct curl_event_loop_request_s *next_pending;

    long  content_length;
    bool  content_length_found;
    bool  is_injected;
    bool  is_cancelled;
    bool  is_pending;
    bool  deps_retained;
    long  bytes_downloaded;
};

typedef struct curl_res_dep_s {
  curl_event_res_id id;
  struct curl_res_dep_s *next;
} curl_res_dep_t;

/* Convenience helpers to convert public→internal (container-of) */
static inline curl_event_loop_request_t *
curl_wrap_from_public(struct curl_event_request_s *req)
{
    return (curl_event_loop_request_t *)((char *)req
        - offsetof(curl_event_loop_request_t, request));
}
static inline const curl_event_loop_request_t *
curl_wrap_from_public_const(const struct curl_event_request_s *req)
{
    return (const curl_event_loop_request_t *)((const char *)req
        - offsetof(curl_event_loop_request_t, request));
}

/* multimap helper (order by “retry at” timestamp) */
static inline int curl_event_request_compare(const curl_event_loop_request_t *a,
                                             const curl_event_loop_request_t *b)
{
    if (a->request.next_retry_at < b->request.next_retry_at) return -1;
    return (a->request.next_retry_at > b->request.next_retry_at);
}
static inline macro_multimap_insert(
    curl_event_request_insert,
    curl_event_loop_request_t,
    curl_event_request_compare)

/* ------------------------------------------------------------------ */
/* Full loop object (opaque to users) -------------------------------- */
struct curl_event_loop_s {
    /* user hook */
    curl_event_on_loop_t  on_loop;
    void                 *on_loop_arg;

    /* libcurl handles */
    CURLM  *multi_handle;
    CURLSH *shared_handle;

    /* flags, limits */
    bool    enable_http3;
    size_t  max_concurrent_requests;
    bool    keep_running;

    pthread_t             owner_thread;

    /* request containers */
    macro_map_t *queued_requests;        /* active in multi */
    macro_map_t *inactive_requests;      /* waiting on retry time */
    macro_map_t *refresh_requests;       /* periodic re-run */
    macro_map_t *rate_limited_requests;  /* delayed by token bucket */
    macro_map_t *resources;              /* resource DAG nodes (internal) */
    res_inbox_t res_inbox;

    int  num_queued_requests;
    int  num_multi_requests;
    int  num_inactive_requests;
    int  num_refresh_requests;

    /* statistics */
    curl_event_metrics_t metrics;

    /* cross‑thread lists (protected by mutex) */
    pthread_mutex_t            mutex;
    curl_event_loop_request_t *cancelled_requests;
    curl_event_loop_request_t *pending_requests;
    curl_event_loop_request_t *injected_requests;
};

/* ------------------------------------------------------------------ */
/* Loop‑internal request helpers ------------------------------------ */
void  curl_event_loop_request_cleanup(struct curl_event_loop_request_s *req);
void  curl_event_request_destroy      (struct curl_event_loop_request_s *req);
bool  curl_event_loop_request_start   (struct curl_event_loop_request_s *req);

#endif /* A_CURL_LIBRARY_IMPL_CURL_EVENT_PRIV_H */
