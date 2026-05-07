// SPDX-FileCopyrightText: 2019–2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai
// SPDX-License-Identifier: Apache-2.0
//
// Maintainer: Andy Curtis <contactandyc@gmail.com>

#ifndef RATE_MANAGER_H
#define RATE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Initializes a rate manager that keeps track of request limits for different keys.
 */
void rate_manager_init(void);

/**
 * Sets the rate limit for a given key (like a URL or API key).
 * - `max_concurrent` is the maximum number of requests allowed to run at the same time.
 * - `max_rps` is the maximum number of tokens generated per second.
 */
void rate_manager_set_limit(const char *key, int max_concurrent, double max_rps);

/**
 * Checks if a request **could** proceed under the rate limit.
 * Returns 0 if the request can proceed, otherwise it returns the number of **milliseconds** to wait.
 */
uint64_t rate_manager_can_proceed(const char *key, bool high_priority, double weight);

/**
 * Starts a request, deducting the specified `weight` from the token bucket.
 * Returns 0 if the request is allowed, otherwise it returns the number of **milliseconds** to wait.
 */
uint64_t rate_manager_start_request(const char *key, bool high_priority, double weight);

/**
 * Marks a request as complete, freeing up space in the concurrent limit.
 */
void rate_manager_request_done(const char *key);

/**
 * Handles a `429 Too Many Requests` or `403 Rate Limit` response by locking the global bucket.
 * Returns how many **seconds** the bucket is paused for.
 */
int rate_manager_handle_429(const char *key);

/**
 * Frees all memory associated with the rate manager.
 */
void rate_manager_destroy(void);

#endif  // RATE_MANAGER_H
