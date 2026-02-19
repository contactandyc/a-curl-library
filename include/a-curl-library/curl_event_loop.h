// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef CURL_EVENT_LOOP_H
#define CURL_EVENT_LOOP_H

#include <stdbool.h>
#include <stdint.h>
#include "a-curl-library/curl_event_request.h"  /* brings in callbacks etc. */
#include "a-curl-library/curl_resource.h"

#ifndef A_CURL_EVENT_LOOP_T_DECL
#define A_CURL_EVENT_LOOP_T_DECL
struct curl_event_loop_s;
typedef struct curl_event_loop_s curl_event_loop_t;
#endif

/* Per‑iteration callback: return false to stop the loop */
typedef bool (*curl_event_on_loop_t)(curl_event_loop_t *loop, void *arg);

/* --------------------------------------------------------------------- */
/* Metrics                                                               */
typedef struct {
    uint64_t total_requests;
    uint64_t completed_requests;
    uint64_t failed_requests;
    uint64_t retried_requests;
} curl_event_metrics_t;

/* --------------------------------------------------------------------- */
/* Core loop control API                                                 */
curl_event_loop_t *curl_event_loop_init(curl_event_on_loop_t on_loop, void *arg);
void               curl_event_loop_destroy(curl_event_loop_t *loop);

/* NEW: submit a prebuilt, pooled request (no copying) */
bool  curl_event_loop_submit(curl_event_loop_t *loop,
                             struct curl_event_request_s *req,
                             int priority);

/* Cancel an in-flight or queued request (req is the same pointer you submitted) */
bool  curl_event_loop_cancel(curl_event_loop_t *loop, struct curl_event_request_s *req);

void  curl_event_loop_run(curl_event_loop_t *loop);
void  curl_event_loop_stop(curl_event_loop_t *loop);

curl_event_metrics_t curl_event_loop_get_metrics(const curl_event_loop_t *loop);

#endif /* CURL_EVENT_LOOP_H */
