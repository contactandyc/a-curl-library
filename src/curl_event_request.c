// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/impl/curl_event_priv.h"
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/rate_manager.h"

#include "a-memory-library/aml_alloc.h"
#include "a-memory-library/aml_pool.h"
#include "the-macro-library/macro_time.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* ────────────────────────────────────────────────────────────────────
   Small helpers
   ──────────────────────────────────────────────────────────────────── */

static inline curl_event_loop_request_t *
wrap_from_public(curl_event_request_t *r) {
    return (curl_event_loop_request_t *)((char *)r - offsetof(curl_event_loop_request_t, request));
}

/* Lightweight xorshift64* RNG for jitter. Thread-local to prevent data races. */
static _Thread_local uint64_t rng_state = 88172645463393265ull;
static inline uint64_t xrng64(void) {
    uint64_t x = rng_state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    rng_state = x;
    return x * 2685821657736338717ull;
}

/* ajson serializer hook */
static curl_event_ajson_serialize_fn g_ajson_serialize = NULL;
void curl_event_set_ajson_serializer(curl_event_ajson_serialize_fn fn) {
    g_ajson_serialize = fn;
}

/* Robust trim helpers */
static inline const char *ltrim(const char *s) { while (*s && isspace((unsigned char)*s)) ++s; return s; }

/* Compute jittered backoff (ms) with optional floor/cap and full jitter */
static uint64_t compute_backoff_ms(int attempt,
                                   double factor,
                                   uint64_t min_ms,
                                   uint64_t max_ms,
                                   bool full_jitter)
{
    if (attempt < 1) attempt = 1;
    double base = 1.0;
    for (int i = 0; i < attempt; ++i) base *= (factor > 0 ? factor : 2.0);
    double raw_ms = base * 100.0; /* base unit: 100ms steps */
    uint64_t cap_ms = (uint64_t)(raw_ms);

    uint64_t jitter = (uint64_t)(xrng64() % (full_jitter ? (cap_ms + 1) : (cap_ms / 2 + 1)));
    uint64_t delay = full_jitter ? jitter : (cap_ms/2 + jitter);

    if (min_ms && delay < min_ms) delay = min_ms;
    if (max_ms && delay > max_ms) delay = max_ms;
    return delay;
}

/* Enhanced default retry policy */
static bool default_calculate_retry_enhanced(curl_event_request_t *req) {
    if (req->max_retries == -1 || req->current_retries < req->max_retries) {
        req->current_retries++;
        uint64_t delay_ms = compute_backoff_ms(req->current_retries,
                                               req->backoff_factor,
                                               req->min_backoff_delay_ms,
                                               req->max_backoff_delay_ms,
                                               req->full_jitter);
        uint64_t now = macro_now();
        req->next_retry_at = now + (delay_ms * 1000000ull);
        return true;
    }
    return false;
}

static size_t default_on_write(void *data, size_t size, size_t nmemb, curl_event_request_t *req) {
    curl_sink_interface_t *sink = (curl_sink_interface_t *)req->sink_data;
    if(sink) {
        if(!req->sink_initialized && sink->init) {
            sink->init(sink, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (sink && sink->write) {
            return sink->write(data, size, nmemb, sink);
        }
    }
    return size * nmemb; // Default to consuming all data
}

static int default_on_complete(CURL *easy_handle, curl_event_request_t *req) {
    (void)easy_handle; // Unused

    curl_sink_interface_t *sink = (curl_sink_interface_t *)req->sink_data;
    if (sink) {
        if (!req->sink_initialized && sink->init) {
            sink->init(sink, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (sink->complete) {
            sink->complete(sink, req);
        }
    }
    return 0; // Request succeeded
}

static int default_on_failure(CURL *easy_handle, CURLcode result, long http_code, curl_event_request_t *req) {
    (void)easy_handle; // Unused

    curl_sink_interface_t *sink = (curl_sink_interface_t *)req->sink_data;
    if (sink) {
        if (!req->sink_initialized && sink->init) {
            sink->init(sink, curl_event_request_content_length(req));
            req->sink_initialized = true;
        }
        if (sink->failure) {
            sink->failure(result, http_code, sink, req);
        }
    }

    return 0; // Failure is not transient
}

static void default_sink_destroy(void *sink_data) {
    curl_sink_interface_t *sink = (curl_sink_interface_t *)sink_data;
    if (sink && sink->destroy) {
        sink->destroy(sink);
    }
}

void curl_sink_defaults(curl_event_request_t *req) {
    req->write_cb = default_on_write;
    req->on_complete = default_on_complete;
    req->on_failure = default_on_failure;
    req->sink_data_cleanup = default_sink_destroy;
    req->sink_data = NULL;
}

/* ────────────────────────────────────────────────────────────────────
   Public builder / lifecycle
   ──────────────────────────────────────────────────────────────────── */

curl_event_request_t *curl_event_request_init(size_t pool_size) {
    size_t sz = (pool_size == 0) ? 4096 : pool_size;

    aml_pool_t *pool = aml_pool_init(sz);
    if (!pool) return NULL;

    curl_event_loop_request_t *wrap =
        (curl_event_loop_request_t *)aml_pool_calloc(pool, 1, sizeof(*wrap));
    if (!wrap) {
        aml_pool_destroy(pool);
        return NULL;
    }

    curl_event_request_t *req = &wrap->request;
    req->loop                  = NULL;
    req->pool                  = pool;

    req->url                   = NULL;
    req->method                = NULL;
    req->post_data             = NULL;
    req->headers               = NULL;

    req->dep_head              = NULL;
    req->rate_limit            = NULL;
    req->rate_limit_high_priority = false;

    req->connect_timeout       = 0;
    req->transfer_timeout      = 0;
    req->low_speed_limit       = 0;
    req->low_speed_time        = 0;

    req->max_retries           = 0;
    req->backoff_factor        = 2.0;
    req->min_backoff_delay_ms  = 0;
    req->max_backoff_delay_ms  = 0;
    req->full_jitter           = true;

    req->on_complete           = NULL;
    req->on_failure            = NULL;
    req->write_cb              = NULL;
    req->on_retry              = NULL;
    req->on_prepare            = NULL;

    req->sink_data              = NULL;
    req->sink_data_cleanup      = NULL;

    req->plugin_data              = NULL;
    req->plugin_data_cleanup      = NULL;

    req->should_refresh        = false;
    req->sink_initialized    = false;
    req->max_download_size     = 0;

    req->current_retries       = 0;
    req->next_retry_at         = 0;
    req->start_time            = 0;
    req->request_start_time    = 0;

    req->priority              = 0;
    req->http3_override        = -1;
    req->refresh_interval_ms   = 0;
    req->refresh_backoff_on_errors = true;

    wrap->content_length_found = false;
    wrap->content_length       = -1;
    wrap->is_injected          = false;
    wrap->is_cancelled         = false;
    wrap->is_pending           = false;
    wrap->deps_retained        = false;
    wrap->multi_handle         = NULL;
    wrap->easy_handle          = NULL;
    wrap->next_cancelled       = NULL;
    wrap->next_pending         = NULL;
    wrap->bytes_downloaded     = 0;

    req->json_root              = NULL;
    req->json_set_ct            = true;

    curl_sink_defaults(req);

    return req;
}

void curl_event_request_destroy_unsubmitted(curl_event_request_t *req_pub) {
    if (!req_pub) return;
    if (req_pub->headers) {
        curl_slist_free_all(req_pub->headers);
        req_pub->headers = NULL;
    }
    if (req_pub->pool) {
        aml_pool_destroy(req_pub->pool);
        req_pub->pool = NULL;
    }
}

/* ────────────────────────────────────────────────────────────────────
   Convenience builders (no submit)
   ──────────────────────────────────────────────────────────────────── */

curl_event_request_t *
curl_event_request_build_get(const char *url,
                             curl_event_write_callback_t write_cb,
                             curl_event_on_complete_t on_complete)
{
    curl_event_request_t *r = curl_event_request_init(0);
    if (!r) return NULL;
    curl_event_request_url(r, url);
    curl_event_request_method(r, "GET");
    if(write_cb)
        curl_event_request_on_write(r, write_cb);
    if(on_complete)
        curl_event_request_on_complete(r, on_complete);
    return r;
}

curl_event_request_t *
curl_event_request_build_post(const char *url,
                              const char *body,
                              const char *content_type,
                              curl_event_write_callback_t write_cb,
                              curl_event_on_complete_t on_complete)
{
    curl_event_request_t *r = curl_event_request_init(0);
    if (!r) return NULL;
    curl_event_request_url(r, url);
    curl_event_request_method(r, "POST");
    if (body) curl_event_request_body(r, body);
    if (content_type) curl_event_request_set_header(r, "Content-Type", content_type);
    if(write_cb)
        curl_event_request_on_write(r, write_cb);
    if(on_complete)
        curl_event_request_on_complete(r, on_complete);
    return r;
}

curl_event_request_t *
curl_event_request_build_post_json(const char *url,
                                   const ajson_t *json,
                                   curl_event_write_callback_t write_cb,
                                   curl_event_on_complete_t on_complete)
{
    curl_event_request_t *r = curl_event_request_init(0);
    if (!r) return NULL;
    curl_event_request_url(r, url);
    curl_event_request_method(r, "POST");
    curl_event_request_set_header(r, "Content-Type", "application/json");
    if (g_ajson_serialize) {
        char *json_str = g_ajson_serialize(r->pool, json);
        curl_event_request_body(r, json_str ? json_str : "{}");
    } else {
        curl_event_request_body(r, "{}");
    }
    if(write_cb)
        curl_event_request_on_write(r, write_cb);
    if(on_complete)
        curl_event_request_on_complete(r, on_complete);
    return r;
}

/* ────────────────────────────────────────────────────────────────────
   Submission
   ──────────────────────────────────────────────────────────────────── */

curl_event_request_t *
curl_event_request_submitp(curl_event_loop_t *loop, curl_event_request_t *req)
{
    return curl_event_request_submit(loop, req, req->priority);
}

static bool setup_curl_handle(curl_event_loop_request_t *req, curl_event_loop_t *loop);
static size_t write_thunk(void *ptr, size_t size, size_t nmemb, void *sink_data);
static size_t header_callback(char *buffer, size_t size, size_t nitems, void *sink_data);

curl_event_request_t *
curl_event_request_submit(curl_event_loop_t *loop,
                          curl_event_request_t *req_pub,
                          int priority)
{
    if (!loop || !req_pub || !req_pub->url) {
        fprintf(stderr, "[curl_event_request_submit] Invalid arguments.\n");
        return NULL;
    }

    curl_event_loop_request_t *wrap = wrap_from_public(req_pub);
    if (wrap->is_pending || wrap->multi_handle) {
        fprintf(stderr, "[curl_event_request_submit] Request already submitted.\n");
        return NULL;
    }

    if (!req_pub->method) {
        req_pub->method = aml_pool_strdup(req_pub->pool,
                           (req_pub->post_data ? "POST" : "GET"));
    }
    if (!req_pub->on_retry && (req_pub->max_retries != 0)) {
        req_pub->on_retry = default_calculate_retry_enhanced;
    }

    req_pub->loop                = loop;
    req_pub->next_retry_at       = macro_now();
    req_pub->start_time          = req_pub->next_retry_at;
    req_pub->request_start_time  = req_pub->next_retry_at;

    int pri = (priority != 0) ? priority : req_pub->priority;
    if (pri != 0) {
        uint64_t adj = (uint64_t)((int64_t)pri > 0 ? (int64_t)pri : 0) * 1000000000ull;
        if (adj < req_pub->next_retry_at) req_pub->next_retry_at -= adj;
    }

    pthread_mutex_lock(&loop->mutex);
    wrap->is_pending    = true;
    wrap->next_pending  = loop->pending_requests;
    loop->pending_requests = wrap;
    loop->metrics.total_requests++;
    pthread_mutex_unlock(&loop->mutex);

    return req_pub;
}

/* ────────────────────────────────────────────────────────────────────
   Loop-internal helpers required by the scheduler / transport
   ──────────────────────────────────────────────────────────────────── */

void curl_event_loop_request_cleanup(curl_event_loop_request_t *req) {
    if (req->easy_handle) {
        if (req->multi_handle) {
            curl_multi_remove_handle(req->multi_handle, req->easy_handle);
            req->request.loop->num_multi_requests--;
        }
        curl_easy_cleanup(req->easy_handle);
        req->easy_handle  = NULL;
        req->multi_handle = NULL;
    }
    req->content_length_found = false;
    req->content_length       = -1;
    req->bytes_downloaded     = 0;
}

void curl_event_request_destroy(curl_event_loop_request_t *req) {
    if (!req) return;

    curl_event_loop_request_cleanup(req);

    if (req->deps_retained) {
        curl_resource_release_request_deps(req->request.loop, &req->request);
        req->deps_retained = false;
    }

    if (req->request.headers) {
        curl_slist_free_all(req->request.headers);
        req->request.headers = NULL;
    }

    if (req->request.sink_data && req->request.sink_data_cleanup) {
        req->request.sink_data_cleanup(req->request.sink_data);
        req->request.sink_data = NULL;
    }

    if (req->request.plugin_data && req->request.plugin_data_cleanup) {
        req->request.plugin_data_cleanup(req->request.plugin_data);
        req->request.plugin_data = NULL;
    }

    if (req->request.pool) {
        aml_pool_destroy(req->request.pool);
        req->request.pool = NULL;
    }
}

static size_t write_thunk(void *ptr, size_t size, size_t nmemb, void *sink_data) {
    curl_event_request_t *pub = (curl_event_request_t *)sink_data;
    curl_event_loop_request_t *req = wrap_from_public(pub);
    size_t total = size * nmemb;

    if (pub->max_download_size > 0) {
        if ((long)(req->bytes_downloaded + (long)total) > pub->max_download_size) {
            size_t allowed = (size_t)((pub->max_download_size - req->bytes_downloaded) > 0
                                      ? (pub->max_download_size - req->bytes_downloaded) : 0);
            if (allowed && pub->write_cb) {
                size_t w = pub->write_cb(ptr, 1, allowed, pub);
                req->bytes_downloaded += (long)w;
            }
            return 0; /* abort transfer */
        }
    }

    req->bytes_downloaded += (long)total;
    if (!pub->write_cb) return total;
    return pub->write_cb(ptr, size, nmemb, pub);
}

/* Zero-copy header parser */
static size_t header_callback(char *buffer, size_t size, size_t nitems, void *sink_data) {
    size_t total_size = size * nitems;
    curl_event_loop_request_t *req = (curl_event_loop_request_t *)sink_data;

    if (total_size > 15 && strncasecmp(buffer, "Content-Length:", 15) == 0) {
        const char *p = buffer + 15;
        p = ltrim(p);
        long cl = 0;
        for (; *p; ++p) {
            if (!isdigit((unsigned char)*p)) break;
            cl = cl * 10 + (*p - '0');
            if (cl < 0) { cl = -1; break; }
        }
        req->content_length_found = true;
        req->content_length = cl;

        if (cl > req->request.max_download_size && req->request.max_download_size > 0) {
            fprintf(stderr, "[curl_event_loop] Content-Length exceeds max_download_size (%ld > %ld)\n",
                    cl, req->request.max_download_size);
            return 0; /* abort transfer */
        }
    }
    return total_size;
}

static bool setup_curl_handle(curl_event_loop_request_t *req, curl_event_loop_t *loop) {
    req->easy_handle          = NULL;
    req->content_length_found = false;
    req->content_length       = -1;
    req->bytes_downloaded     = 0;

    if (req->request.on_prepare) {
        if (!req->request.on_prepare(&req->request))
            return false;
    }

    req->easy_handle = curl_easy_init();
    if (!req->easy_handle) {
        fprintf(stderr, "[setup_curl_handle] curl_easy_init failed.\n");
        return false;
    }

    curl_easy_setopt(req->easy_handle, CURLOPT_URL, req->request.url);
    curl_easy_setopt(req->easy_handle, CURLOPT_ACCEPT_ENCODING, "");

    if (req->request.max_download_size > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(req->easy_handle, CURLOPT_HEADERDATA, req);
    }

    if (req->request.json_root && !req->request.post_data) {
        curl_event_request_json_commit(&req->request);
    }

    const char *method = req->request.method ? req->request.method
                        : (req->request.post_data ? "POST" : "GET");
    if (strcasecmp(method, "POST") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_POST, 1L);
        if (req->request.post_data) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, req->request.post_data);
        }
    } else if (strcasecmp(method, "PUT") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_UPLOAD, 1L);
        if (req->request.post_data) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, req->request.post_data);
        }
    } else if (strcasecmp(method, "DELETE") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (strcasecmp(method, "PATCH") == 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (req->request.post_data) {
            curl_easy_setopt(req->easy_handle, CURLOPT_POSTFIELDS, req->request.post_data);
        }
    }

    if (req->request.headers) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTPHEADER, req->request.headers);
    }

    if (req->request.connect_timeout > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_CONNECTTIMEOUT, req->request.connect_timeout);
    }
    if (req->request.transfer_timeout > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_TIMEOUT, req->request.transfer_timeout);
    }
    if (req->request.low_speed_limit > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_LOW_SPEED_LIMIT, req->request.low_speed_limit);
    }
    if (req->request.low_speed_time > 0) {
        curl_easy_setopt(req->easy_handle, CURLOPT_LOW_SPEED_TIME, req->request.low_speed_time);
    }

    bool use_http3 = loop->enable_http3;
    if (req->request.http3_override == 0) use_http3 = false;
    if (req->request.http3_override == 1) use_http3 = true;
    if (use_http3) {
        curl_easy_setopt(req->easy_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3);
    }

    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEFUNCTION, write_thunk);
    curl_easy_setopt(req->easy_handle, CURLOPT_WRITEDATA, &req->request);
    curl_easy_setopt(req->easy_handle, CURLOPT_PRIVATE, req);

    return true;
}

bool curl_event_loop_request_start(curl_event_loop_request_t *req) {
    curl_event_loop_t *loop = req->request.loop;

    if (req->request.rate_limit) {
        uint64_t next = rate_manager_start_request(
            req->request.rate_limit, req->request.rate_limit_high_priority);
        if (next) {
            req->request.next_retry_at = next;
            curl_event_request_insert(&loop->rate_limited_requests, req);
            return false;
        }
    }

    if (!setup_curl_handle(req, loop)) {
        curl_event_request_destroy(req);
        return false;
    }

    curl_multi_add_handle(loop->multi_handle, req->easy_handle);
    req->request.request_start_time = macro_now();
    loop->num_multi_requests++;
    req->multi_handle = loop->multi_handle;
    curl_event_request_insert(&loop->queued_requests, req);
    loop->num_queued_requests++;
    return true;
}

void curl_event_request_apply_browser_profile(curl_event_request_t *r,
                                              const char *ua_opt,
                                              const char *al_opt)
{
    const char *ua = ua_opt ? ua_opt :
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36";

    const char *al = al_opt ? al_opt : "en-US,en;q=0.9";

    curl_event_request_set_header(r, "User-Agent", ua);
    curl_event_request_set_header(r, "Accept",
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    curl_event_request_set_header(r, "Accept-Language", al);
}

void curl_event_request_depend(curl_event_request_t *req, curl_event_res_id id) {
    if (!req || id == 0) return;
    curl_res_dep_t *node = (curl_res_dep_t *)aml_pool_calloc(req->pool, 1, sizeof(*node));
    node->id = id;
    node->next = (curl_res_dep_t *)req->dep_head;
    req->dep_head = (struct curl_res_dep_s *)node;
}

void curl_event_request_depend_many(curl_event_request_t *req,
                                    const curl_event_res_id *ids, int n) {
    if (!req || !ids || n <= 0) return;
    for (int i = 0; i < n; ++i) curl_event_request_depend(req, ids[i]);
}

/* ────────────────────────────────────────────────────────────────────
   Public helpers (unchanged signatures)
   ──────────────────────────────────────────────────────────────────── */

double curl_event_request_time_spent(const curl_event_request_t *r) {
    return macro_time_diff(macro_now(), r->start_time);
}

double curl_event_request_time_spent_on_request(const curl_event_request_t *r) {
    return macro_time_diff(macro_now(), r->request_start_time);
}

long curl_event_request_content_length(curl_event_request_t *r) {
    curl_event_loop_request_t *req = wrap_from_public(r);
    if (!req->content_length_found) {
        curl_off_t content_length = 0;
        if (req->easy_handle) {
            curl_easy_getinfo(req->easy_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
            if (content_length >= 0) {
                req->content_length = (long)content_length;
            }
        }
        req->content_length_found = true;
    }
    return req->content_length;
}

/* ────────────────────────────────────────────────────────────────────
   Mutators
   ──────────────────────────────────────────────────────────────────── */

void curl_event_request_url(curl_event_request_t *req, const char *url) {
    req->url = aml_pool_strdup(req->pool, url);
}
void curl_event_request_urlf(curl_event_request_t *req, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    req->url = aml_pool_strdupvf(req->pool, fmt, ap);
    va_end(ap);
}
void curl_event_request_method(curl_event_request_t *req, const char *method) {
    req->method = aml_pool_strdup(req->pool, method);
}
void curl_event_request_body(curl_event_request_t *req, const char *body) {
    req->post_data = aml_pool_strdup(req->pool, body);
}
void curl_event_request_bodyf(curl_event_request_t *req, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    req->post_data = aml_pool_strdupvf(req->pool, fmt, ap);
    va_end(ap);
}
void curl_event_request_json_body(curl_event_request_t *req, const char *json) {
    curl_event_request_set_header(req, "Content-Type", "application/json");
    req->post_data = aml_pool_strdup(req->pool, json);
    if (!req->method) req->method = aml_pool_strdup(req->pool, "POST");
}
void curl_event_request_json_bodyf(curl_event_request_t *req, const char *fmt, ...) {
    curl_event_request_set_header(req, "Content-Type", "application/json");
    va_list ap; va_start(ap, fmt);
    req->post_data = aml_pool_strdupvf(req->pool, fmt, ap);
    va_end(ap);
    if (!req->method) req->method = aml_pool_strdup(req->pool, "POST");
}

static void add_header_line(struct curl_slist **list, const char *name, const char *value) {
    size_t n = strlen(name) + 2 + strlen(value) + 1;
    char *line = (char *)aml_calloc(1, n);
    sprintf(line, "%s: %s", name, value);
    *list = curl_slist_append(*list, line);
    aml_free(line);
}
void curl_event_request_add_header(curl_event_request_t *req,
                                   const char *name, const char *value) {
    add_header_line(&req->headers, name, value);
}
void curl_event_request_add_headerf(curl_event_request_t *req,
                                     const char *name, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *val = aml_pool_strdupvf(req->pool, fmt, ap);
    va_end(ap);
    add_header_line(&req->headers, name, val);
}

void curl_event_request_set_header(curl_event_request_t *req,
                                   const char *name, const char *value)
{
    struct curl_slist *new_list = NULL;
    struct curl_slist *current  = req->headers;
    size_t name_len = strlen(name);
    bool found = false;
    while (current) {
        if (strncmp(current->data, name, name_len) == 0 &&
            current->data[name_len] == ':')
        {
            add_header_line(&new_list, name, value);
            found = true;
        } else {
            new_list = curl_slist_append(new_list, current->data);
        }
        current = current->next;
    }
    if (!found) add_header_line(&new_list, name, value);
    if (req->headers) curl_slist_free_all(req->headers);
    req->headers = new_list;
}
void curl_event_request_set_headerf(curl_event_request_t *req,
                                     const char *name, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *val = aml_pool_strdupvf(req->pool, fmt, ap);
    va_end(ap);
    curl_event_request_set_header(req, name, val);
}

void curl_event_request_rate_limit(curl_event_request_t *req,
                                   const char *key, bool high_priority) {
    req->rate_limit = aml_pool_strdup(req->pool, key);
    req->rate_limit_high_priority = high_priority;
}

void curl_event_request_connect_timeout(curl_event_request_t *req, long secs) {
    req->connect_timeout = secs;
}
void curl_event_request_transfer_timeout(curl_event_request_t *req, long secs) {
    req->transfer_timeout = secs;
}
void curl_event_request_low_speed(curl_event_request_t *req,
                                  long bytes_per_sec, long time_secs) {
    req->low_speed_limit = bytes_per_sec;
    req->low_speed_time  = time_secs;
}

void curl_event_request_max_retries(curl_event_request_t *req, int max_retries) {
    req->max_retries = max_retries;
}
void curl_event_request_backoff_factor(curl_event_request_t *req, double factor) {
    req->backoff_factor = factor;
}

void curl_event_request_enable_retries(curl_event_request_t *req,
                                       int max_retries,
                                       double backoff_factor,
                                       uint64_t min_delay_ms,
                                       uint64_t max_delay_ms,
                                       bool full_jitter)
{
    req->max_retries = max_retries;
    req->backoff_factor = backoff_factor;
    req->min_backoff_delay_ms = min_delay_ms;
    req->max_backoff_delay_ms = max_delay_ms;
    req->full_jitter = full_jitter;
    if (!req->on_retry) req->on_retry = default_calculate_retry_enhanced;
}

void curl_event_request_enable_refresh(curl_event_request_t *req,
                                       uint64_t interval_ms,
                                       bool backoff_on_errors)
{
    req->should_refresh = (interval_ms != 0);
    req->refresh_interval_ms = interval_ms;
    req->refresh_backoff_on_errors = backoff_on_errors;
}

void curl_event_request_on_complete(curl_event_request_t *req,
                                    curl_event_on_complete_t cb) {
    req->on_complete = cb;
}
void curl_event_request_on_failure(curl_event_request_t *req,
                                   curl_event_on_failure_t cb) {
    req->on_failure = cb;
}
void curl_event_request_on_write(curl_event_request_t *req,
                                 curl_event_write_callback_t cb) {
    req->write_cb = cb;
}
void curl_event_request_on_retry(curl_event_request_t *req,
                                 curl_event_on_retry_t cb) {
    req->on_retry = cb;
}
void curl_event_request_on_prepare(curl_event_request_t *req,
                                   curl_event_on_prepare_t cb) {
    req->on_prepare = cb;
}

void curl_event_request_sink(curl_event_request_t *req,
                               curl_sink_interface_t *sink_iface,
                               curl_event_cleanup_data_t cleanup) {
    sink_iface->request = req;
    req->sink_data = sink_iface;
    if(cleanup)
        req->sink_data_cleanup = cleanup;
}

void curl_event_request_plugin_data(curl_event_request_t *req,
                                    void *plugin_data,
                                    curl_event_cleanup_data_t cleanup) {
    req->plugin_data = plugin_data;
    req->plugin_data_cleanup = cleanup;
}

void curl_event_request_should_refresh(curl_event_request_t *req) {
    req->should_refresh = true;
}
void curl_event_request_max_download_size(curl_event_request_t *req, long bytes) {
    req->max_download_size = bytes;
}

void curl_event_request_priority(curl_event_request_t *req, int priority) {
    req->priority = priority;
}
void curl_event_request_http3(curl_event_request_t *req, bool enable) {
    req->http3_override = enable ? 1 : 0;
}
void curl_event_loop_enable_http3(curl_event_loop_t *loop, bool enable) {
    loop->enable_http3 = enable;
}

ajson_t *curl_event_request_json_root(curl_event_request_t *req) {
    return req->json_root;
}

ajson_t *curl_event_request_json_begin(curl_event_request_t *req, bool array_root) {
    if (!req->json_root) {
        req->json_root = array_root ? ajsona(req->pool) : ajsono(req->pool);
        if (!req->method) req->method = aml_pool_strdup(req->pool, "POST");
    }
    return req->json_root;
}

void curl_event_request_json_autocontenttype(curl_event_request_t *req, bool enable) {
    req->json_set_ct = enable;
}

void curl_event_request_json_commit(curl_event_request_t *req) {
    if (!req || !req->json_root || req->post_data) return;
    const char *s = ajson_stringify(req->pool, req->json_root);
    if (s) {
        req->post_data = aml_pool_strdup(req->pool, s);
        if (req->json_set_ct) {
            curl_event_request_set_header(req, "Content-Type", "application/json");
        }
        if (!req->method) req->method = aml_pool_strdup(req->pool, "POST");
    }
}
