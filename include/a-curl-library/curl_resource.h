// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef CURL_RESOURCE_H
#define CURL_RESOURCE_H

#include <stdint.h>
#include <stdbool.h>
#include "a-memory-library/aml_alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid circular includes */
struct curl_event_loop_s;
struct curl_event_request_s;
struct curl_event_loop_request_s;
struct curl_res_dep_s;

/* Opaque id for a logical shared resource (auth token, TCP conn, etc.) */
typedef uint64_t curl_event_res_id;

/* ──────────────────────────────────────────────────────────────────────
   Loop‑thread contract

   All functions below are intended to be called on the event‑loop thread
   unless they have the “_async” suffix. The async variants are safe from
   other threads; they post an op into the loop’s inbox and wake the loop.
   ────────────────────────────────────────────────────────────────────── */

/* One‑time setup called by the loop when it starts (records owner thread). */
void curl_resource_set_owner_thread(struct curl_event_loop_s *loop);

/* Drain the cross‑thread inbox. The loop should call this at safe points
   (e.g., each tick) before scheduling/executing requests. */
void curl_resource_inbox_drain(struct curl_event_loop_s *loop);

/* ──────────────────────────────────────────────────────────────────────
   Core resource API (loop thread only)
   ────────────────────────────────────────────────────────────────────── */
/* Manually bump a resource's refcount (loop thread only).
   If the node is a placeholder (refcnt==0), this sets it to 1. */
void curl_event_res_addref(struct curl_event_loop_s *loop,
                           curl_event_res_id id);

/* Retain/release all dependency resources of a request (loop thread only). */
void curl_resource_retain_request_deps(struct curl_event_loop_s *loop,
                                       struct curl_event_request_s *req);
void curl_resource_release_request_deps(struct curl_event_loop_s *loop,
                                        struct curl_event_request_s *req);


/* Two‑phase: declare an empty resource node. Initial refcnt == 1. */
curl_event_res_id
curl_event_res_declare(struct curl_event_loop_s *loop);

/* Publish/republish the payload (or NULL to mark failure) and wake dependents.
   If republishing, the previous payload is cleaned with its cleanup() first. */
void
curl_event_res_publish(struct curl_event_loop_s *loop,
                       curl_event_res_id        id,
                       void                    *payload,
                       void                   (*cleanup)(void *));

/* One‑shot convenience: declare + publish in a single call. */
curl_event_res_id
curl_event_res_register(struct curl_event_loop_s *loop,
                        void                    *payload,
                        void                   (*cleanup)(void *));

/* Convenience: publish a NUL‑terminated string.
   The library dup’s and frees it when replaced/released. */
void curl_event_res_publish_str(struct curl_event_loop_s *loop,
                                curl_event_res_id id,
                                const char *s);

/* Convenience: fetch current payload.  Returns NULL if not ready or failed. */
const char *curl_event_res_get_str(struct curl_event_loop_s *loop,
                                   curl_event_res_id id);

/* Loop‑thread peek of the raw payload pointer (no copying).
   Do NOT retain the pointer outside the current loop callback. */
void *curl_event_res_peek(struct curl_event_loop_s *loop,
                          curl_event_res_id id);

/* Drop one reference; when it reaches zero, cleanup(payload) is called and
   the node is erased. */
void
curl_event_res_release(struct curl_event_loop_s *loop,
                       curl_event_res_id        id);

/* Attach a declared resource as a dependency of a request (O(1)).
   This simply appends to the request’s dependency list; the loop later calls
   internal checks to block/requeue based on readiness. */
void
curl_event_request_depend(struct curl_event_request_s *req,
                          curl_event_res_id           id);

/* ──────────────────────────────────────────────────────────────────────
   Async helpers (safe from any thread)
   These post an op to the loop inbox and wake the loop.
   ────────────────────────────────────────────────────────────────────── */

/* Used to wake up thread in async methods */
void curl_event_loop_wake(struct curl_event_loop_s *loop);

curl_event_res_id
curl_event_res_register_async(struct curl_event_loop_s *loop,
                              void                    *payload,
                              void                   (*cleanup)(void *));
void
curl_event_res_publish_async(struct curl_event_loop_s *loop,
                             curl_event_res_id        id,
                             void                    *payload,
                             void                   (*cleanup)(void *));
void
curl_event_res_release_async(struct curl_event_loop_s *loop,
                             curl_event_res_id        id);

/* ──────────────────────────────────────────────────────────────────────
   Loop‑facing helpers (used by the event loop’s scheduler)
   These remain loop‑thread only.
   ────────────────────────────────────────────────────────────────────── */

/* Block a single dependency id if not ready. Returns true if blocked. */
bool curl_resource_block_on_id(struct curl_event_loop_s           *loop,
                               struct curl_event_loop_request_s   *req,
                               curl_event_res_id                   id);

/* Check a dependency list; block on first unmet dep. Returns true if blocked. */
bool curl_resource_check_and_block_list(struct curl_event_loop_s         *loop,
                                        struct curl_event_loop_request_s *req,
                                        struct curl_res_dep_s            *head);

/* Readiness only: true iff every dep has a published payload or failed flag. */
bool curl_resource_all_ready_list(struct curl_event_loop_s       *loop,
                                  const struct curl_res_dep_s    *head);


/* --------------------------------------------------------------------- */
/* Loop teardown helper (internal)                                       */
/* --------------------------------------------------------------------- */
/**
 * Destroy all resource nodes in loop->resources.
 * - Must be invoked on the loop thread during loop teardown
 *   (typically from curl_event_loop_destroy()).
 * - Cancels any requests blocked on resources (invokes on_failure if set).
 * - Calls cleanup(payload) for published resources.
 */
void curl_resource_destroy_all(struct curl_event_loop_s *loop);


#ifdef __cplusplus
}
#endif

#endif /* CURL_RESOURCE_H */
