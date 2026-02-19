// SPDX-FileCopyrightText: 2025 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/rate_manager.h"
#include "the-macro-library/macro_time.h"
#include "the-macro-library/macro_map.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

typedef struct {
    macro_map_t node;
    char *key;
    int max_concurrent;
    double max_rps;
    int current_requests;
    int high_priority_requests;
    double tokens;
    uint64_t last_refill;
    uint64_t last_success;
    int backoff_seconds;
} rate_limit_t;

static inline
int compare_rate_limit(const rate_limit_t *a, const rate_limit_t *b) {
    return strcmp(a->key, b->key);
}

static inline
int compare_rate_limit_string(const char *a, const rate_limit_t *b) {
    return strcmp(a, b->key);
}

static inline
macro_map_insert(rate_limit_insert, rate_limit_t, compare_rate_limit);

static inline
macro_map_find_kv(rate_limit_find, char, rate_limit_t, compare_rate_limit_string);

typedef struct {
    pthread_mutex_t mutex;
    macro_map_t *limits;
} rate_manager_t;

static rate_manager_t *g_rate_manager = NULL;

void rate_manager_init(void) {
    if(g_rate_manager)
        return;

    g_rate_manager = (rate_manager_t *)aml_calloc(1, sizeof(rate_manager_t));
    pthread_mutex_init(&g_rate_manager->mutex, NULL);
    g_rate_manager->limits = NULL;
}

void rate_manager_set_limit(const char *key, int max_concurrent, double max_rps) {
    if(!g_rate_manager)
        rate_manager_init();

    pthread_mutex_lock(&g_rate_manager->mutex);

    rate_limit_t *limit = rate_limit_find(g_rate_manager->limits, key);
    if (!limit) {
        limit = (rate_limit_t *)aml_calloc(1, sizeof(rate_limit_t));
        limit->key = aml_strdup(key);
        rate_limit_insert(&g_rate_manager->limits, limit);
    }

    limit->max_concurrent = max_concurrent;
    limit->max_rps = max_rps;
    limit->tokens = max_rps;
    limit->last_refill = macro_now();
    limit->last_success = macro_now();
    limit->backoff_seconds = 1;

    pthread_mutex_unlock(&g_rate_manager->mutex);
}

uint64_t rate_manager_can_proceed(const char *key, bool high_priority) {
    if (!g_rate_manager)
        return 0;

    pthread_mutex_lock(&g_rate_manager->mutex);
    rate_limit_t *limit = rate_limit_find(g_rate_manager->limits, key);
    if (!limit) {
        pthread_mutex_unlock(&g_rate_manager->mutex);
        return 0; // No rate limit exists, proceed immediately
    }

    uint64_t now = macro_now();
    double elapsed = macro_time_diff(now, limit->last_refill);

    // Refill shared token bucket
    limit->tokens = fmin(limit->max_rps, limit->tokens + elapsed * limit->max_rps);
    limit->last_refill = now;

    // If a high-priority request is waiting, it gets first access
    if (high_priority) {
        if (limit->tokens >= 1) {
            pthread_mutex_unlock(&g_rate_manager->mutex);
            return 0; // Can proceed immediately
        }
        // Not enough tokens, but should take precedence when available
        limit->high_priority_requests++;
        double wait_time_ns = (1.0 - limit->tokens) / limit->max_rps * 1e9;
        pthread_mutex_unlock(&g_rate_manager->mutex);
        return (uint64_t)wait_time_ns;
    }

    // Normal requests are blocked if there are pending high-priority requests
    if (limit->tokens >= 1 && limit->high_priority_requests == 0) {
        pthread_mutex_unlock(&g_rate_manager->mutex);
        return 0; // Normal request can proceed immediately
    }

    // Normal requests must wait if high-priority requests are pending
    double wait_time_ns = (1.0 - limit->tokens) / limit->max_rps * 1e9;
    pthread_mutex_unlock(&g_rate_manager->mutex);
    return (uint64_t)wait_time_ns;
}

uint64_t rate_manager_start_request(const char *key, bool high_priority) {
    if (!g_rate_manager)
        return 0;

    pthread_mutex_lock(&g_rate_manager->mutex);
    rate_limit_t *limit = rate_limit_find(g_rate_manager->limits, key);
    if (!limit) {
        pthread_mutex_unlock(&g_rate_manager->mutex);
        return 0; // No rate limit exists, proceed immediately
    }

    uint64_t now = macro_now();
    double elapsed = macro_time_diff(now, limit->last_refill);

    // Refill shared token bucket
    limit->tokens = fmin(limit->max_rps, limit->tokens + elapsed * limit->max_rps);
    limit->last_refill = now;

    // Ensure high-priority requests get served first
    if (high_priority || (limit->high_priority_requests == 0 && limit->tokens >= 1)) {
        if (limit->tokens >= 1) {
            limit->tokens -= 1.0;
            limit->current_requests++;
            if (high_priority && limit->high_priority_requests > 0) {
                limit->high_priority_requests--; // Reduce pending high-priority count
            }
            pthread_mutex_unlock(&g_rate_manager->mutex);
            return 0; // Request can proceed immediately
        }
    }

    // Otherwise, wait for next available token
    double wait_time_ns = (1.0 - limit->tokens) / limit->max_rps * 1e9;
    pthread_mutex_unlock(&g_rate_manager->mutex);
    return (uint64_t)wait_time_ns;
}

void rate_manager_request_done(const char *key) {
    if(!g_rate_manager)
        return;

    pthread_mutex_lock(&g_rate_manager->mutex);
    rate_limit_t *limit = rate_limit_find(g_rate_manager->limits, key);
    if (limit) {
        if (limit->current_requests > 0) {
            limit->current_requests--;
        }
        limit->last_success = macro_now();
        limit->backoff_seconds = 1;  // Reset backoff on success
    }
    pthread_mutex_unlock(&g_rate_manager->mutex);
}

int rate_manager_handle_429(const char *key) {
    if(!g_rate_manager)
        return 0;

    pthread_mutex_lock(&g_rate_manager->mutex);
    rate_limit_t *limit = rate_limit_find(g_rate_manager->limits, key);
    if (!limit) {
        pthread_mutex_unlock(&g_rate_manager->mutex);
        return 0;
    }

    // Decrement current request count since the request finished (but was rate limited)
    if (limit->current_requests > 0) {
        limit->current_requests--;
    }

    uint64_t now = macro_now();
    double time_since_last_success = macro_time_diff(now, limit->last_success);

    // Adjust backoff behavior for rate-limited responses
    if (time_since_last_success < 2) {
        limit->backoff_seconds = 1;  // Reset backoff if a recent success was seen
    } else {
        limit->backoff_seconds = fmin(limit->backoff_seconds * 2, 60);
    }

    pthread_mutex_unlock(&g_rate_manager->mutex);
    return limit->backoff_seconds;
}

void rate_manager_destroy(void) {
    if (!g_rate_manager) return;

    pthread_mutex_lock(&g_rate_manager->mutex);

    // Iterate over the rate limits map and free each entry
    macro_map_t *node = macro_map_first(g_rate_manager->limits);
    while (node) {
        rate_limit_t *limit = (rate_limit_t *)node;
        macro_map_erase(&g_rate_manager->limits, node);
        aml_free(limit->key);
        aml_free(limit);
        node = macro_map_first(g_rate_manager->limits);
    }

    pthread_mutex_unlock(&g_rate_manager->mutex);
    pthread_mutex_destroy(&g_rate_manager->mutex);
    aml_free(g_rate_manager);
    g_rate_manager = NULL;
}