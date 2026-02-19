// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _curl_memory_sink_H
#define _curl_memory_sink_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"

#include <stdio.h>
#include <stdbool.h>

// Define a callback type for completion
typedef void (*memory_complete_callback_t)(
    char *data,
    size_t length,
    bool success,
    CURLcode result,      // CURLcode (0 if success)
    long http_code,       // HTTP status code (0 if success)
    const char *error_msg, // Error message (NULL if success)
    void *arg,            // User-defined argument
    curl_event_request_t *req
);

curl_sink_interface_t *memory_sink(
    curl_event_request_t *req,
    memory_complete_callback_t callback,
    void *callback_arg);

#endif
