// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/impl/curl_event_priv.h"
#include "a-curl-library/rate_manager.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg) {
    curl_event_loop_t *loop = (curl_event_loop_t *)aml_calloc(1, sizeof(curl_event_loop_t));
    if (!loop) {
        fprintf(stderr, "[curl_event_loop_init] Memory allocation failed.\n");
        return NULL;
    }
    curl_resource_set_owner_thread(loop);
    loop->on_loop = on_loop;
    loop->on_loop_arg = arg;

    loop->queued_requests = NULL;
    loop->inactive_requests = NULL;
    loop->refresh_requests = NULL;
    loop->resources = NULL;

    loop->multi_handle = curl_multi_init();
    if (!loop->multi_handle) {
        fprintf(stderr, "[curl_event_loop_init] Failed to create multi_handle.\n");
        aml_free(loop);
        return NULL;
    }

    // The user can toggle HTTP/3 support if their libcurl has it
    loop->enable_http3 = true;

    loop->queued_requests = NULL;
    loop->num_queued_requests = 0;
    loop->num_multi_requests = 0;
    loop->inactive_requests = NULL;
    loop->num_inactive_requests = 0;

    loop->refresh_requests = NULL;
    loop->num_refresh_requests = 0;

    loop->cancelled_requests = NULL;
    loop->pending_requests = NULL;
    loop->injected_requests = NULL;
    loop->rate_limited_requests = NULL;

    loop->metrics.total_requests = 0;
    loop->metrics.completed_requests = 0;
    loop->metrics.failed_requests = 0;
    loop->metrics.retried_requests = 0;

    // Default to a high concurrency.
    loop->max_concurrent_requests = 1000;

    loop->keep_running = true;
    pthread_mutex_init(&loop->mutex, NULL);

    return loop;
}

// curl_event_loop_inject lets you post a “synthetic completion” into the loop
void curl_event_loop_inject(curl_event_loop_t *loop, curl_event_request_t *req_pub) {
    if (!loop || !req_pub) return;

    curl_event_loop_request_t *injected_req =
        (curl_event_loop_request_t *)aml_calloc(1, sizeof(curl_event_loop_request_t));
    if (!injected_req) {
        fprintf(stderr, "[curl_event_loop_inject] Memory allocation failed.\n");
        return;
    }

    injected_req->request = *req_pub;
    injected_req->is_injected = true;
    injected_req->request.loop = loop;
    injected_req->request.current_retries = 0;

    injected_req->easy_handle = NULL;
    injected_req->multi_handle = NULL;

    pthread_mutex_lock(&loop->mutex);
    injected_req->next_pending = loop->injected_requests;
    loop->injected_requests = injected_req;
    pthread_mutex_unlock(&loop->mutex);
}

bool curl_event_loop_cancel(curl_event_loop_t *loop, curl_event_request_t *r) {
    if (!loop || !r) return false;

    curl_event_loop_request_t *req = curl_wrap_from_public(r);

    pthread_mutex_lock(&loop->mutex);

    if (req->next_cancelled) {
        pthread_mutex_unlock(&loop->mutex);
        return false; // Already canceled
    }

    req->is_cancelled = true;
    if (!req->is_pending) {
        req->next_cancelled = loop->cancelled_requests;
        loop->cancelled_requests = req;
    }
    pthread_mutex_unlock(&loop->mutex);
    return true;
}

void curl_event_loop_destroy(curl_event_loop_t *loop) {
    if (!loop) return;

    macro_map_t *n = macro_map_first(loop->queued_requests);
    while (n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->queued_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->queued_requests);
    }

    n = macro_map_first(loop->inactive_requests);
    while (n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->inactive_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->inactive_requests);
    }

    n = macro_map_first(loop->refresh_requests);
    while (n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->refresh_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->refresh_requests);
    }

    n = macro_map_first(loop->rate_limited_requests);
    while (n) {
        curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
        macro_map_erase(&loop->rate_limited_requests, n);
        curl_event_request_destroy(req);
        n = macro_map_first(loop->rate_limited_requests);
    }

    pthread_mutex_lock(&loop->mutex);

    curl_event_loop_request_t *req = loop->cancelled_requests;
    while (req) {
        curl_event_loop_request_t *next = req->next_cancelled;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->cancelled_requests = NULL;

    req = loop->pending_requests;
    while (req) {
        curl_event_loop_request_t *next = req->next_pending;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->pending_requests = NULL;

    req = loop->injected_requests;
    while (req) {
        curl_event_loop_request_t *next = req->next_pending;
        curl_event_request_destroy(req);
        req = next;
    }
    loop->injected_requests = NULL;

    pthread_mutex_unlock(&loop->mutex);

    curl_multi_cleanup(loop->multi_handle);
    pthread_mutex_destroy(&loop->mutex);

    curl_resource_destroy_all(loop);
    aml_free(loop);
}

static bool request_is_rate_limited(curl_event_loop_t *loop, macro_map_t **root, curl_event_loop_request_t *req) {
    if (!req->request.rate_limit)
        return false;
    uint64_t next = rate_manager_can_proceed(req->request.rate_limit, req->request.rate_limit_high_priority);
    if (next == 0)
        return false;

    macro_map_erase(root, &req->node);
    req->request.next_retry_at = macro_now() + next;
    curl_event_request_insert(&loop->rate_limited_requests, req);
    return true;
}

static bool request_waiting_on_dependencies(curl_event_loop_t *loop,
                                            curl_event_loop_request_t *req)
{
    if (!req->request.dep_head) return false;
    return curl_resource_check_and_block_list(loop, req, req->request.dep_head);
}

static bool request_ready(curl_event_loop_t *loop, const curl_event_loop_request_t *req) {
    if (loop->num_queued_requests >= loop->max_concurrent_requests) {
        return false;
    }
    if (req->request.rate_limit) {
        if (rate_manager_can_proceed(req->request.rate_limit, req->request.rate_limit_high_priority) > 0) {
            return false;
        }
    }
    if (req->request.dep_head) {
        if (!curl_resource_all_ready_list(loop, req->request.dep_head)) {
            return false;
        }
    }
    return macro_now() >= req->request.next_retry_at;
}

static void enqueue_request(curl_event_loop_t *loop, curl_event_loop_request_t *req) {
    if (req->request.should_refresh) {
        curl_event_request_insert(&loop->refresh_requests, req);
        loop->num_refresh_requests++;
    } else {
        curl_event_request_insert(&loop->inactive_requests, req);
        loop->num_inactive_requests++;
    }
}

void process_cancelled_and_pending_requests(curl_event_loop_t *loop) {
    pthread_mutex_lock(&loop->mutex);
    curl_event_loop_request_t *cancelled = loop->cancelled_requests;
    loop->cancelled_requests = NULL;

    curl_event_loop_request_t *pending = loop->pending_requests;
    loop->pending_requests = NULL;
    pthread_mutex_unlock(&loop->mutex);

    while (cancelled) {
        curl_event_loop_request_t *next = cancelled->next_cancelled;

        if (cancelled->multi_handle) {
            macro_map_erase(&loop->queued_requests, (macro_map_t *)cancelled);
            loop->num_queued_requests--;
        } else {
            if (cancelled->request.should_refresh) {
                macro_map_erase(&loop->refresh_requests, (macro_map_t *)cancelled);
                loop->num_refresh_requests--;
            } else {
                macro_map_erase(&loop->inactive_requests, (macro_map_t *)cancelled);
                loop->num_inactive_requests--;
            }
        }
        curl_event_request_destroy(cancelled);
        cancelled = next;
    }

    while (pending) {
        curl_event_loop_request_t *next = pending->next_pending;
        pending->is_pending = false;
        pending->next_pending = NULL;
        if (pending->is_cancelled) {
            curl_event_request_destroy(pending);
        } else {
            if (!pending->deps_retained && pending->request.dep_head) {
                curl_resource_retain_request_deps(loop, &pending->request);
                pending->deps_retained = true;
            }

            if (!request_waiting_on_dependencies(loop, pending)) {
                if (request_ready(loop, pending)) {
                    curl_event_loop_request_start(pending);
                } else {
                    enqueue_request(loop, pending);
                }
            }
        }
        pending = next;
    }
}

static long calculate_next_timer_expiry(curl_event_loop_t *loop, long max_value) {
    uint64_t current_time, next_time;

    macro_map_t *first_inactive_node = macro_map_first(loop->inactive_requests);
    macro_map_t *first_refresh_node = macro_map_first(loop->refresh_requests);
    if (!first_inactive_node && !first_refresh_node) {
        return max_value;
    } else if (!first_inactive_node) {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_refresh_node;
        next_time = r->request.next_retry_at;
    } else if (!first_refresh_node) {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_inactive_node;
        next_time = r->request.next_retry_at;
    } else {
        curl_event_loop_request_t *r = (curl_event_loop_request_t *)first_inactive_node;
        if (!request_ready(loop, r)) {
            r = (curl_event_loop_request_t *)first_refresh_node;
            next_time = r->request.next_retry_at;
        } else {
            long inactive_time = r->request.next_retry_at;
            r = (curl_event_loop_request_t *)first_refresh_node;
            long refresh_time = r->request.next_retry_at;
            next_time = inactive_time < refresh_time ? inactive_time : refresh_time;
        }
    }
    current_time = macro_now();
    if (next_time < current_time) {
        return 0;
    }
    next_time = next_time - current_time;
    next_time /= 1000000L;
    return next_time > max_value ? max_value : next_time;
}

static void move_inactive_requests_to_queue(curl_event_loop_t *loop) {
    macro_map_t *n;
    macro_map_t *root = loop->rate_limited_requests;
    loop->rate_limited_requests = NULL;
    uint64_t now = macro_now();
    n = macro_map_first(root);
    while (n) {
        if (now < ((curl_event_loop_request_t *)n)->request.next_retry_at)
            break;
        if (!request_is_rate_limited(loop, &root, (curl_event_loop_request_t *)n)) {
            if (!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&root, n);
            curl_event_loop_request_start(req);
        }
        n = macro_map_first(root);
    }
    n = macro_map_first(loop->rate_limited_requests);
    while (n) {
        macro_map_erase(&loop->rate_limited_requests, n);
        curl_event_request_insert(&root, (curl_event_loop_request_t *)n);
        n = macro_map_first(loop->rate_limited_requests);
    }
    loop->rate_limited_requests = root;

    n = macro_map_first(loop->refresh_requests);
    while (n) {
        if (!request_is_rate_limited(loop, &loop->refresh_requests, (curl_event_loop_request_t *)n)) {
            if (!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&loop->refresh_requests, n);
            loop->num_refresh_requests--;
            curl_event_loop_request_start(req);
        } else
            loop->num_refresh_requests--;
        n = macro_map_first(loop->refresh_requests);
    }

    n = macro_map_first(loop->inactive_requests);
    while (n) {
        if (!request_is_rate_limited(loop, &loop->inactive_requests, (curl_event_loop_request_t *)n)) {
            if (!request_ready(loop, (curl_event_loop_request_t *)n)) {
                break;
            }
            curl_event_loop_request_t *req = (curl_event_loop_request_t *)n;
            macro_map_erase(&loop->inactive_requests, n);
            loop->num_inactive_requests--;
            curl_event_loop_request_start(req);
        } else
            loop->num_inactive_requests--;
        n = macro_map_first(loop->inactive_requests);
    }
}

static void process_completed_requests(curl_event_loop_t *loop) {
    int msgs_left = 0;
    CURLMsg *msg = NULL;
    while ((msg = curl_multi_info_read(loop->multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            curl_event_loop_request_t *req = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, (void **)&req);

            long http_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
            CURLcode result = msg->data.result;

            macro_map_erase(&loop->queued_requests, (macro_map_t *)req);
            loop->num_queued_requests--;

            bool success = (result == CURLE_OK && http_code == 200);
            int retry_in;
            if (success) {
                retry_in = req->request.on_complete(easy, &req->request);
            } else {
                retry_in = -1;
                if (req->request.on_failure)
                    retry_in = req->request.on_failure(easy, result, http_code, &req->request);
            }

            if (http_code == 429 && req->request.rate_limit) {
                retry_in = rate_manager_handle_429(req->request.rate_limit);
                req->request.next_retry_at = macro_now_add_seconds(retry_in);
                curl_event_request_insert(&loop->rate_limited_requests, req);
                continue;
            }

            if (req->request.rate_limit) {
                rate_manager_request_done(req->request.rate_limit);
            }

            if (retry_in > 0) {
                req->request.next_retry_at = macro_now_add_seconds(retry_in);
                curl_event_loop_request_cleanup(req);
                enqueue_request(loop, req);
            } else if (retry_in < 0 && req->request.on_retry(&req->request)) {
                curl_event_loop_request_cleanup(req);
                loop->metrics.retried_requests++;
                enqueue_request(loop, req);
            } else {
                if (success)
                    loop->metrics.completed_requests++;
                if (req->request.should_refresh) {
                    req->request.current_retries = 0;
                    curl_event_loop_request_cleanup(req);
                    enqueue_request(loop, req);
                } else {
                    curl_event_request_destroy(req);
                }
            }
        }
    }

    pthread_mutex_lock(&loop->mutex);
    curl_event_loop_request_t *injected = loop->injected_requests;
    loop->injected_requests = NULL;
    pthread_mutex_unlock(&loop->mutex);

    while (injected) {
        curl_event_loop_request_t *next = injected->next_pending;
        if (injected->request.on_complete) {
            injected->request.on_complete(NULL, &injected->request);
        }
        curl_event_request_destroy(injected);
        injected = next;
    }
}

void curl_event_loop_run(curl_event_loop_t *loop) {
    if (!loop) return;

    loop->keep_running = true;

    while (loop->keep_running) {
        curl_resource_set_owner_thread(loop);

        if (loop->on_loop && !loop->on_loop(loop, loop->on_loop_arg)) {
            break;
        }

        process_cancelled_and_pending_requests(loop);
        move_inactive_requests_to_queue(loop);

        int still_running = 0;
        if (loop->num_queued_requests > 0) {
            curl_multi_perform(loop->multi_handle, &still_running);
        }

        process_completed_requests(loop);
        curl_resource_inbox_drain(loop);

        if (still_running == 0 &&
            loop->pending_requests == NULL &&
            macro_map_first(loop->queued_requests) == NULL &&
            macro_map_first(loop->refresh_requests) == NULL &&
            macro_map_first(loop->inactive_requests) == NULL) {
            break;
        }

        // Combine custom timers with libcurl's required timers
        long wait_timeout_ms = calculate_next_timer_expiry(loop, 200);
        long curl_timeout = -1;
        curl_multi_timeout(loop->multi_handle, &curl_timeout);
        if (curl_timeout >= 0 && curl_timeout < wait_timeout_ms) {
            wait_timeout_ms = curl_timeout;
        }

        if (loop->num_multi_requests > 0) {
            int num_fds = 0;
            CURLMcode mc = curl_multi_poll(loop->multi_handle, NULL, 0, wait_timeout_ms, &num_fds);
            if (mc != CURLM_OK) {
                fprintf(stderr, "curl_multi_poll() failed: %s\n", curl_multi_strerror(mc));
            }
        } else if (wait_timeout_ms > 0) {
            usleep(wait_timeout_ms * 1000);
        }
    }
}

void curl_event_loop_stop(curl_event_loop_t *loop) {
    if (!loop) return;
    loop->keep_running = false;
}

curl_event_metrics_t curl_event_loop_get_metrics(const curl_event_loop_t *loop) {
    if (!loop) {
        curl_event_metrics_t empty = {0};
        return empty;
    }
    return loop->metrics;
}

bool curl_event_loop_submit(curl_event_loop_t *loop,
                            curl_event_request_t *req_pub,
                            int priority)
{
    if (!loop || !req_pub) {
        fprintf(stderr, "[curl_event_loop_submit] Invalid arguments.\n");
        return false;
    }

    curl_event_loop_request_t *req = curl_wrap_from_public(req_pub);
    req->request.loop = loop;

    uint64_t now = macro_now();
    req->request.next_retry_at = now - ((int64_t)priority * 1000000000LL);
    req->request.start_time = req->request.next_retry_at;
    req->request.request_start_time = req->request.next_retry_at;

    pthread_mutex_lock(&loop->mutex);
    req->is_pending = true;
    req->next_pending = loop->pending_requests;
    loop->pending_requests = req;
    loop->metrics.total_requests++;
    pthread_mutex_unlock(&loop->mutex);

    return true;
}