// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef CURL_EVENT_REQUEST_H
#define CURL_EVENT_REQUEST_H

#include <curl/curl.h>
#include <stdint.h>
#include <stdbool.h>

#include "a-curl-library/curl_resource.h"
#include "a-memory-library/aml_pool.h"
#include "a-json-library/ajson.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef A_CURL_EVENT_LOOP_T_DECL
#define A_CURL_EVENT_LOOP_T_DECL
struct curl_event_loop_s;
typedef struct curl_event_loop_s curl_event_loop_t;
#endif

/* --------------------------------------------------------------------- */
/* Callback typedefs                                                     */
typedef int    (*curl_event_on_complete_t)(CURL *easy,
                                           struct curl_event_request_s *req);
typedef int    (*curl_event_on_failure_t)(CURL *easy, CURLcode res, long http,
                                          struct curl_event_request_s *req);
typedef size_t (*curl_event_write_callback_t)(void *ptr, size_t size,
                                              size_t nmemb,
                                              struct curl_event_request_s *req);
/* on_retry: return true to reschedule (req->next_retry_at will be set) */
typedef bool   (*curl_event_on_retry_t)  (struct curl_event_request_s *req);
/* on_prepare: last chance to mutate the request on the loop thread */
typedef bool   (*curl_event_on_prepare_t)(struct curl_event_request_s *req);
/* cleanup for req->sink_data when the request is destroyed */
typedef void   (*curl_event_cleanup_data_t)(void *sink_data);

/* --------------------------------------------------------------------- */
/* Public request descriptor                                             */
typedef struct curl_event_request_s {
    /*— ownership & wiring —*/
    curl_event_loop_t *loop;       /* set by submit()                       */
    aml_pool_t        *pool;       /* request-owned pool (created in _init)  */

    /*— basic HTTP info —*/
    char *url;
    char *method;                  /* default inferred: body? "POST" : "GET" */
    char *post_data;               /* optional body (NUL-terminated)        */
    struct curl_slist *headers;    /* libcurl slist; freed by request destroy */

    /*— dependency / throttling —*/
    struct curl_res_dep_s *dep_head; /* built via curl_event_request_depend() */
    char   *rate_limit;            /* token bucket key (dup into pool)      */
    bool    rate_limit_high_priority;

    /*— timeouts / speed (seconds) —*/
    long connect_timeout;
    long transfer_timeout;
    long low_speed_limit;
    long low_speed_time;

    /*— retry behaviour —*/
    int    max_retries;            /* −1 = unlimited                        */
    double backoff_factor;         /* default 2.0                           */
    uint64_t min_backoff_delay_ms; /* default 0 (no floor)                  */
    uint64_t max_backoff_delay_ms; /* default 0 (no cap)                    */
    bool     full_jitter;          /* default true                          */

    /*— callbacks —*/
    curl_event_on_complete_t    on_complete;   /* required */
    curl_event_on_failure_t     on_failure;    /* optional */
    curl_event_write_callback_t write_cb;      /* required */
    curl_event_on_retry_t       on_retry;      /* optional */
    curl_event_on_prepare_t     on_prepare;    /* optional */

    /*— user payload —*/
    void *sink_data;
    curl_event_cleanup_data_t sink_data_cleanup;

    void *plugin_data;
    curl_event_cleanup_data_t plugin_data_cleanup;


    /*— misc flags / limits —*/
    bool should_refresh;           /* if true, re-enqueue after success     */
    bool sink_initialized;
    long max_download_size;        /* honored via C-Len and during body     */

    ajson_t  *json_root;            /* if set, stringified on submit when post_data==NULL */
    bool      json_set_ct;          /* auto set Content-Type: application/json on commit */

    /*— internal bookkeeping —*/
    int       current_retries;
    uint64_t  next_retry_at;
    uint64_t  start_time;
    uint64_t  request_start_time;

    /*— new ergonomics —*/
    int       priority;            /* default 0 (higher = sooner)           */
    int       http3_override;      /* -1 use loop default; 0 off; 1 on      */
    uint64_t  refresh_interval_ms; /* 0 = disabled; loop resubmits if set   */
    bool      refresh_backoff_on_errors;
} curl_event_request_t;

/* Generic “sink sink” used by default callbacks ------------------------ */
typedef struct curl_sink_interface_s {
    aml_pool_t        *pool;
    curl_event_request_t *request;

    bool   (*init)    (struct curl_sink_interface_s *self, long content_len);
    size_t (*write)   (const void *data, size_t size, size_t nmemb,
                       struct curl_sink_interface_s *self);
    void   (*failure) (CURLcode result, long http_code,
                       struct curl_sink_interface_s *self,
                       struct curl_event_request_s *req);
    void   (*complete)(struct curl_sink_interface_s *self,
                       struct curl_event_request_s *req);
    void   (*destroy) (struct curl_sink_interface_s *self);
} curl_sink_interface_t;

/* --------------------------------------------------------------------- */
/* Construction / submission                                             */
curl_event_request_t *curl_event_request_init(size_t pool_size);
curl_event_request_t *
curl_event_request_submit(curl_event_loop_t *loop,
                          curl_event_request_t *req,
                          int priority);
curl_event_request_t *
curl_event_request_submitp(curl_event_loop_t *loop,
                           curl_event_request_t *req); /* uses req->priority */
void curl_event_request_destroy_unsubmitted(curl_event_request_t *req);

/* applies the basic browser headers that many websites expect */
void curl_event_request_apply_browser_profile(curl_event_request_t *r,
                                              const char *ua_opt,
                                              const char *al_opt);

/* --------------------------------------------------------------------- */
/* Convenience builders (no submit; priority remains 0)                  */
curl_event_request_t *
curl_event_request_build_get(const char *url,
                             curl_event_write_callback_t write_cb,
                             curl_event_on_complete_t on_complete);

curl_event_request_t *
curl_event_request_build_post(const char *url,
                              const char *body,                 /* may be NULL */
                              const char *content_type,         /* e.g. "application/json" */
                              curl_event_write_callback_t write_cb,
                              curl_event_on_complete_t on_complete);

curl_event_request_t *
curl_event_request_build_post_json(const char *url,
                                   const ajson_t *json,
                                   curl_event_write_callback_t write_cb,
                                   curl_event_on_complete_t on_complete);

/**
 * Create (or return existing) JSON root object.
 * If array_root==true, root is an array; else an object.
 * Also sets method to POST if unset. Content-Type is set on commit.
 */
ajson_t *curl_event_request_json_begin(curl_event_request_t *req, bool array_root);

/** Return current JSON root (or NULL if none). */
ajson_t *curl_event_request_json_root(curl_event_request_t *req);

/**
 * Stringify JSON root into req->post_data (pool-owned) and set
 * Content-Type: application/json (once). No-op if already has post_data.
 */
void curl_event_request_json_commit(curl_event_request_t *req);

/** Control whether commit sets the JSON Content-Type header (default: true). */
void curl_event_request_json_autocontenttype(curl_event_request_t *req, bool enable);

/* ajson serializer hook */
typedef char *(*curl_event_ajson_serialize_fn)(aml_pool_t *pool, const ajson_t *json);
void curl_event_set_ajson_serializer(curl_event_ajson_serialize_fn fn);

/* --------------------------------------------------------------------- */
/* Mutators (legacy + new)                                               */
/* Basic fields */
void curl_event_request_url(curl_event_request_t *req, const char *url);
void curl_event_request_urlf(curl_event_request_t *req, const char *fmt, ...);
void curl_event_request_method(curl_event_request_t *req, const char *method);
void curl_event_request_body(curl_event_request_t *req, const char *body);
void curl_event_request_bodyf(curl_event_request_t *req, const char *fmt, ...);

/* JSON helpers */
void curl_event_request_json_body(curl_event_request_t *req, const char *json);
void curl_event_request_json_bodyf(curl_event_request_t *req, const char *fmt, ...);

/* Headers */
void curl_event_request_add_header(curl_event_request_t *req,
                                   const char *name, const char *value);
void curl_event_request_add_headerf(curl_event_request_t *req,
                                    const char *name, const char *fmt, ...);
void curl_event_request_set_header(curl_event_request_t *req,
                                   const char *name, const char *value);
void curl_event_request_set_headerf(curl_event_request_t *req,
                                    const char *name, const char *fmt, ...);

/* Dependencies */
void curl_event_request_depend(curl_event_request_t *req, curl_event_res_id id);
void curl_event_request_depend_many(curl_event_request_t *req,
                                    const curl_event_res_id *ids, int n);

/* Rate limiting */
void curl_event_request_rate_limit(curl_event_request_t *req,
                                   const char *key, bool high_priority);

/* Timeouts / speed (seconds) */
void curl_event_request_connect_timeout(curl_event_request_t *req, long secs);
void curl_event_request_transfer_timeout(curl_event_request_t *req, long secs);
void curl_event_request_low_speed(curl_event_request_t *req,
                                  long bytes_per_sec, long time_secs);

/* Retry policy (simple) */
void curl_event_request_max_retries(curl_event_request_t *req, int max_retries);
void curl_event_request_backoff_factor(curl_event_request_t *req, double factor);

/* Retry policy (enhanced) */
void curl_event_request_enable_retries(curl_event_request_t *req,
                                       int max_retries,
                                       double backoff_factor,
                                       uint64_t min_delay_ms,
                                       uint64_t max_delay_ms,
                                       bool full_jitter);

/* Refresh */
void curl_event_request_enable_refresh(curl_event_request_t *req,
                                       uint64_t interval_ms,
                                       bool backoff_on_errors);

/* Callbacks */
void curl_event_request_on_complete(curl_event_request_t *req,
                                    curl_event_on_complete_t cb);
void curl_event_request_on_failure(curl_event_request_t *req,
                                   curl_event_on_failure_t cb);
void curl_event_request_on_write(curl_event_request_t *req,
                                 curl_event_write_callback_t cb);
void curl_event_request_on_retry(curl_event_request_t *req,
                                 curl_event_on_retry_t cb);
void curl_event_request_on_prepare(curl_event_request_t *req,
                                   curl_event_on_prepare_t cb);

/* sink_data */
void curl_event_request_sink(curl_event_request_t *req,
                               curl_sink_interface_t *sink_iface,
                               curl_event_cleanup_data_t cleanup);

/* plugin data */
void curl_event_request_plugin_data(curl_event_request_t *req,
                                    void *plugin_data,
                                    curl_event_cleanup_data_t cleanup);

/* Misc */
void curl_event_request_should_refresh(curl_event_request_t *req);
void curl_event_request_max_download_size(curl_event_request_t *req, long bytes);

/* New ergonomics */
void curl_event_request_priority(curl_event_request_t *req, int priority);
void curl_event_request_http3(curl_event_request_t *req, bool enable); /* override per-request */
void curl_event_loop_enable_http3(curl_event_loop_t *loop, bool enable);

/* --------------------------------------------------------------------- */
/* Simple timing helpers                                                 */
double curl_event_request_time_spent(const curl_event_request_t *r);
double curl_event_request_time_spent_on_request(const curl_event_request_t *r);

/* --------------------------------------------------------------------- */
/* Runtime helpers                                                       */
long  curl_event_request_content_length(curl_event_request_t *r); /* −1 if unknown */

/* Backward-compatible alias (replaces or adds header) */
static inline void
curl_event_loop_update_header(curl_event_request_t *req,
                              const char *name,
                              const char *value)
{
    curl_event_request_set_header(req, name, value);
}

#ifdef __cplusplus
}
#endif
#endif /* CURL_EVENT_REQUEST_H */
