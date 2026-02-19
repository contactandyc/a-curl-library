// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef RATE_MANAGER_H
#define RATE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/* TODO: Consider adding a cost function which is based upon request/response data */

/**
 * Initializes a rate manager that keeps track of request limits for different keys.
 * This function must be called before using any other rate manager functions.
 */
void rate_manager_init(void);

/**
 * Sets the rate limit for a given key (like a URL or API key).
 * If the key doesn’t exist, it will be created.
 *
 * - `max_concurrent` is the maximum number of requests allowed to run at the same time.
 * - `max_rps` is the maximum number of requests per second.  Fractional values are allowed.
 */
void rate_manager_set_limit(const char *key, int max_concurrent, double max_rps);

/**
 * Checks if a request **could** proceed under the rate limit.
 * This does not actually count the request, it just checks.
 *
 * Returns 0 if the request can proceed, otherwise it returns the number of **nanoseconds** to wait.
 */
uint64_t rate_manager_can_proceed(const char *key, bool high_priority);

/**
 * Starts a request, assuming it will proceed.
 * This actually **counts** the request against the rate limit.
 *
 * Returns `true` if the request is allowed, `false` if it should be delayed.
 */
uint64_t rate_manager_start_request(const char *key, bool high_priority);

/**
 * Marks a request as complete, freeing up space in the concurrent limit.
 * This also resets the backoff timer since the request was successful.
 */
void rate_manager_request_done(const char *key);

/**
 * Handles a `429 Too Many Requests` response by increasing the backoff time.
 * This function returns how many **seconds** to wait before retrying.
 * The backoff will increase exponentially but will reset if enough time has passed.
 */
int rate_manager_handle_429(const char *key);

/**
 * Frees all memory associated with the rate manager.
 */
void rate_manager_destroy(void);

#endif  // RATE_MANAGER_H
