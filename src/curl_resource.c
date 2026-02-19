// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/curl_resource.h"
#include "a-curl-library/curl_event_request.h"   // curl_res_dep_t
#include "a-curl-library/impl/curl_event_priv.h" // loop + request internals
#include "a-memory-library/aml_alloc.h"
#include "the-macro-library/macro_map.h"

#include <pthread.h>
#include <stdatomic.h>
#include <curl/curl.h>  // CURLE_ABORTED_BY_CALLBACK
#include <stdio.h>

/* ──────────────────────────────────────────────────────────────────────
   Internal node structure
   ────────────────────────────────────────────────────────────────────── */

typedef struct curl_event_res_s {
    macro_map_t       node;         /* ordered by id                        */
    curl_event_res_id id;

    void  *payload;                 /* becomes non‑NULL when ready          */
    void (*cleanup)(void *);

    int    refcnt;                  /* starts at 1                          */
    bool   failed;                  /* publish(NULL) sets this              */
    bool   auto_release_owner;      /* auto-drop owner when refcnt returns to 1 */

    /* list of requests waiting on this resource                           */
    struct curl_event_loop_request_s *blocked_head;
    struct curl_event_loop_request_s *blocked_tail;
} curl_event_res_t;

/* ──────────────────────────────────────────────────────────────────────
   Map helpers
   ────────────────────────────────────────────────────────────────────── */

static inline int compare_res(const curl_event_res_t *a,
                              const curl_event_res_t *b)
{
    return (a->id < b->id) ? -1 : (a->id > b->id);
}
static inline int compare_res_id(const curl_event_res_id *key,
                                 const curl_event_res_t  *b)
{
    return (*key < b->id) ? -1 : (*key > b->id);
}

static inline
macro_map_insert(res_insert, curl_event_res_t, compare_res);
static inline
macro_map_find_kv(res_find, curl_event_res_id, curl_event_res_t, compare_res_id);

/* ──────────────────────────────────────────────────────────────────────
   Loop‑thread assertions & helpers
   ────────────────────────────────────────────────────────────────────── */

#ifndef NDEBUG
static inline void assert_loop_thread(struct curl_event_loop_s *loop) {
    if (!pthread_equal(loop->owner_thread, pthread_self())) {
        fprintf(stderr, "curl_resource: accessed off loop thread\n");
        abort();
    }
}
#else
#define assert_loop_thread(loop) ((void)0)
#endif

/* Requeue into loop->pending (loop thread only) */
static inline void requeue_pending(struct curl_event_loop_s *loop,
                                   struct curl_event_loop_request_s *req)
{
    req->next_pending      = loop->pending_requests;
    loop->pending_requests = req;
}

/* Find or create a placeholder node (loop thread only) */
static curl_event_res_t *res_get_or_create(struct curl_event_loop_s *loop,
                                           curl_event_res_id id)
{
    curl_event_res_t *n = res_find(loop->resources, &id);
    if (n) return n;

    n = (curl_event_res_t *)aml_calloc(1, sizeof(*n));
    n->id     = id;
    n->refcnt = 0;  /* placeholder until declared/published */
    n->auto_release_owner = false;
    res_insert(&loop->resources, n);
    return n;
}

/* Append req to a node’s blocked list (loop thread only) */
static inline void block_request_on_res(struct curl_event_loop_s *loop,
                                        curl_event_res_t  *node,
                                        struct curl_event_loop_request_s *req)
{
    (void)loop; /* not used here, but kept for parity */
    req->next_pending = NULL;
    if (node->blocked_tail) {
        node->blocked_tail->next_pending = req;
        node->blocked_tail = req;
    } else {
        node->blocked_head = node->blocked_tail = req;
    }
}

/* ──────────────────────────────────────────────────────────────────────
   Cross‑thread inbox (MPSC Treiber stack)
   ────────────────────────────────────────────────────────────────────── */

enum { RES_OP_REGISTER = 0, RES_OP_PUBLISH = 1, RES_OP_RELEASE = 2 };

static inline void inbox_push(struct res_inbox_s *q, struct res_op_s *node)
{
    struct res_op_s *old = atomic_load_explicit(&q->head, memory_order_relaxed);
    do {
        node->next = old;
    } while (!atomic_compare_exchange_weak_explicit(
                 &q->head, &old, node,
                 memory_order_release, memory_order_relaxed));
}

/* ──────────────────────────────────────────────────────────────────────
   Public API
   ────────────────────────────────────────────────────────────────────── */

static _Atomic uint64_t g_next_id = 1;

void curl_resource_set_owner_thread(struct curl_event_loop_s *loop)
{
    loop->owner_thread = pthread_self();
    atomic_store_explicit(&loop->res_inbox.head, NULL, memory_order_relaxed);
}

/* Drain the inbox on the loop thread */
void curl_resource_inbox_drain(struct curl_event_loop_s *loop)
{
    assert_loop_thread(loop);

    struct res_op_s *head = atomic_exchange_explicit(&loop->res_inbox.head,
                                                     NULL,
                                                     memory_order_acquire);
    /* reverse to FIFO */
    struct res_op_s *rev = NULL;
    while (head) { struct res_op_s *n=head->next; head->next=rev; rev=head; head=n; }

    for (struct res_op_s *op = rev; op; ) {
        struct res_op_s *next = op->next;
        switch (op->kind) {
        case RES_OP_REGISTER:
            /* declare implied; publish sets payload & wakes deps */
            curl_event_res_publish(loop, (curl_event_res_id)op->id, op->payload, op->cleanup);
            break;
        case RES_OP_PUBLISH:
            curl_event_res_publish(loop, (curl_event_res_id)op->id, op->payload, op->cleanup);
            break;
        case RES_OP_RELEASE:
            curl_event_res_release(loop, (curl_event_res_id)op->id);
            break;
        default: break;
        }
        aml_free(op);
        op = next;
    }
}


void curl_event_res_addref(struct curl_event_loop_s *loop,
                           curl_event_res_id id)
{
    assert_loop_thread(loop);
    curl_event_res_t *n = res_get_or_create(loop, id);
    if (n->refcnt == 0) n->refcnt = 1;
    else n->refcnt++;
}

void curl_resource_retain_request_deps(struct curl_event_loop_s *loop,
                                       struct curl_event_request_s *req)
{
    assert_loop_thread(loop);
    for (curl_res_dep_t *d = req->dep_head; d; d = d->next) {
        curl_event_res_t *n = res_get_or_create(loop, d->id);
        n->refcnt++;
    }
}

void curl_event_res_autorelease_owner(struct curl_event_loop_s *loop,
                                      curl_event_res_id        id,
                                      bool                     enable)
{
    assert_loop_thread(loop);
    curl_event_res_t *n = res_get_or_create(loop, id);
    n->auto_release_owner = enable;
}

/* If auto-release is enabled and only the owner ref remains (refcnt==1),
 * and there are no blocked waiters, drop the owner ref now. */
static inline void maybe_autorelease_owner(struct curl_event_loop_s *loop,
                                           curl_event_res_t *n)
{
    if (n->auto_release_owner &&
        n->refcnt == 1 &&
        n->blocked_head == NULL)
    {
        /* This calls cleanup(payload) (if any) and frees node when refcnt hits 0 */
        curl_event_res_release(loop, n->id);
    }
}

void curl_resource_release_request_deps(struct curl_event_loop_s *loop,
                                        struct curl_event_request_s *req)
{
    assert_loop_thread(loop);
    for (curl_res_dep_t *d = req->dep_head; d; d = d->next) {
        curl_event_res_t *n = res_find(loop->resources, &d->id);
        if (!n) continue; /* already freed or never declared */
        if (n->refcnt > 0)
            n->refcnt--;
        /* If that brought it back to owner-only, and auto-release is enabled,
           drop the owner now (unless there are blocked waiters). */
        maybe_autorelease_owner(loop, n);
    }
}

curl_event_res_id curl_event_res_declare(struct curl_event_loop_s *loop)
{
    assert_loop_thread(loop);

    curl_event_res_t *n = (curl_event_res_t *)aml_calloc(1, sizeof(*n));
    n->id     = atomic_fetch_add(&g_next_id, 1);
    n->refcnt = 1;
    n->auto_release_owner = false;
    res_insert(&loop->resources, n);
    return n->id;
}

void curl_event_res_publish(struct curl_event_loop_s *loop,
                            curl_event_res_id  id,
                            void              *payload,
                            void (*cleanup)(void *))
{
    assert_loop_thread(loop);

    curl_event_res_t *n = res_find(loop->resources, &id);
    if (!n) {
        /* allow publish on unknown id: create then publish */
        n = res_get_or_create(loop, id);
    }

    if (n->refcnt == 0) {
        n->refcnt = 1;
    }

    /* free previous payload on republish */
    if (n->payload && n->cleanup) {
        n->cleanup(n->payload);
    }

    n->payload = payload;
    n->cleanup = cleanup;
    n->failed  = (payload == NULL);

    /* Detach blocked list */
    struct curl_event_loop_request_s *head = n->blocked_head;
    n->blocked_head = n->blocked_tail = NULL;

    /* Requeue or fast‑fail dependents */
    while (head) {
        struct curl_event_loop_request_s *next = head->next_pending;
        head->next_pending = NULL;

        if (n->failed) {
            if (head->request.on_failure) {
                head->request.on_failure(
                    NULL, CURLE_ABORTED_BY_CALLBACK, 0, &head->request);
            }
            curl_event_request_destroy(head);
        } else {
            requeue_pending(loop, head);
        }
        head = next;
    }
}

curl_event_res_id curl_event_res_register(struct curl_event_loop_s *loop,
                                          void              *payload,
                                          void (*cleanup)(void *))
{
    assert_loop_thread(loop);
    curl_event_res_id id = curl_event_res_declare(loop);
    curl_event_res_publish(loop, id, payload, cleanup);
    return id;
}

void curl_event_res_publish_str(struct curl_event_loop_s *loop,
                                curl_event_res_id id,
                                const char *s)
{
    assert_loop_thread(loop);
    char *dup = s ? aml_strdup(s) : NULL;
    curl_event_res_publish(loop, id, dup, dup ? _aml_free : NULL);
}

const char *curl_event_res_get_str(struct curl_event_loop_s *loop,
                                   curl_event_res_id id)
{
    assert_loop_thread(loop);
    curl_event_res_t *n = res_find(loop->resources, &id);
    if (!n || n->failed || !n->payload) return NULL;
    return (const char *)n->payload;
}

void *curl_event_res_peek(struct curl_event_loop_s *loop,
                          curl_event_res_id id)
{
    assert_loop_thread(loop);
    curl_event_res_t *n = res_find(loop->resources, &id);
    if (!n || n->failed) return NULL;
    return n->payload; /* valid only during current loop callback */
}

void curl_event_res_release(struct curl_event_loop_s *loop,
                            curl_event_res_id  id)
{
    assert_loop_thread(loop);
    curl_event_res_t *n = res_find(loop->resources, &id);
    if (!n) return;

    bool do_free = false;

    if (--n->refcnt == 0) {
        macro_map_erase(&loop->resources, &n->node);
        do_free = true;
    }

    if (do_free) {
        if (n->cleanup && n->payload)
            n->cleanup(n->payload);
        aml_free(n);
    }
}


/* ──────────────────────────────────────────────────────────────────────
   Async helpers (safe from any thread)
   ────────────────────────────────────────────────────────────────────── */
void curl_event_loop_wake(curl_event_loop_t *loop) {
    if (!loop) return;
    /* libcurl >= 7.68: wakes a thread blocked in curl_multi_poll/select */
    curl_multi_wakeup(loop->multi_handle);
}

curl_event_res_id
curl_event_res_register_async(struct curl_event_loop_s *loop,
                              void                    *payload,
                              void                   (*cleanup)(void *))
{
    uint64_t id = atomic_fetch_add(&g_next_id, 1);
    struct res_op_s *op = (struct res_op_s *)aml_calloc(1, sizeof(*op));
    op->kind    = RES_OP_REGISTER;
    op->id      = id;
    op->payload = payload;
    op->cleanup = cleanup;
    inbox_push(&loop->res_inbox, op);
    curl_event_loop_wake(loop);
    return (curl_event_res_id)id;
}

void
curl_event_res_publish_async(struct curl_event_loop_s *loop,
                             curl_event_res_id        id,
                             void                    *payload,
                             void                   (*cleanup)(void *))
{
    struct res_op_s *op = (struct res_op_s *)aml_calloc(1, sizeof(*op));
    op->kind    = RES_OP_PUBLISH;
    op->id      = (uint64_t)id;
    op->payload = payload;
    op->cleanup = cleanup;
    inbox_push(&loop->res_inbox, op);
    curl_event_loop_wake(loop);
}

void
curl_event_res_release_async(struct curl_event_loop_s *loop,
                             curl_event_res_id        id)
{
    struct res_op_s *op = (struct res_op_s *)aml_calloc(1, sizeof(*op));
    op->kind    = RES_OP_RELEASE;
    op->id      = (uint64_t)id;
    inbox_push(&loop->res_inbox, op);
    curl_event_loop_wake(loop);
}

/* ──────────────────────────────────────────────────────────────────────
   Loop‑facing helpers (used by scheduler) – loop thread only
   ────────────────────────────────────────────────────────────────────── */

bool curl_resource_block_on_id(struct curl_event_loop_s         *loop,
                               struct curl_event_loop_request_s *req,
                               curl_event_res_id                 id)
{
    assert_loop_thread(loop);
    curl_event_res_t *node = res_get_or_create(loop, id);
    if (node->payload == NULL && !node->failed) {
        block_request_on_res(loop, node, req);
        return true;
    }
    return false;
}

bool curl_resource_check_and_block_list(struct curl_event_loop_s         *loop,
                                        struct curl_event_loop_request_s *req,
                                        struct curl_res_dep_s            *head)
{
    assert_loop_thread(loop);
    for (struct curl_res_dep_s *d = head; d; d = d->next) {
        if (curl_resource_block_on_id(loop, req, d->id)) {
            return true;  /* blocked on first unmet dep */
        }
    }
    return false;         /* all ready */
}

bool curl_resource_all_ready_list(struct curl_event_loop_s    *loop,
                                  const struct curl_res_dep_s *head)
{
    assert_loop_thread(loop);
    for (const struct curl_res_dep_s *d = head; d; d = d->next) {
        curl_event_res_t *node = res_find(loop->resources, &d->id);
        if (!node) return false;
        if (node->payload == NULL && !node->failed) return false;
    }
    return true;
}

void curl_resource_destroy_all(struct curl_event_loop_s *loop)
{
    if (!loop) return;

    /* Walk and drain the entire resource map. We repeatedly fetch first
       because macro_map_erase() invalidates the node we’re iterating. */
    for (macro_map_t *n = macro_map_first(loop->resources);
         n != NULL;
         n = macro_map_first(loop->resources))
    {
        curl_event_res_t *res = (curl_event_res_t *)n;

        /* Detach any blocked list (no need to hold the loop mutex here as
           we’re on the loop thread during teardown and no one else is
           mutating the resources structure anymore). */
        curl_event_loop_request_t *head = res->blocked_head;
        res->blocked_head = res->blocked_tail = NULL;

        /* Fail/cancel all blocked requests cleanly. */
        while (head) {
            curl_event_loop_request_t *next = head->next_pending;
            head->next_pending = NULL;

            if (head->request.on_failure) {
                head->request.on_failure(
                    NULL, CURLE_ABORTED_BY_CALLBACK, 0, &head->request);
            }
            curl_event_request_destroy(head);
            head = next;
        }

        /* Cleanup payload if present */
        if (res->cleanup && res->payload)
            res->cleanup(res->payload);

        /* Unlink and free the resource node */
        macro_map_erase(&loop->resources, n);
        aml_free(res);
    }

    /* After this point there are no resources left. */
    loop->resources = NULL;
}
