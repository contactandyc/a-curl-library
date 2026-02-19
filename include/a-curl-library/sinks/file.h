// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#ifndef _CURL_FILE_SINK_H
#define _CURL_FILE_SINK_H

#include "a-curl-library/curl_event_loop.h"
#include "a-curl-library/curl_event_request.h"

#include <stdio.h>
#include <stdbool.h>

// Define a callback type for completion
typedef void (*file_complete_callback_t)(
    const char *filename, // The file being downloaded
    bool success,
    CURLcode result,      // CURLcode (0 if success)
    long http_code,       // HTTP status code (0 if success)
    const char *error_msg, // Error message (NULL if success)
    void *arg,             // User-defined argument
    curl_event_request_t *req
);

/**
 * Create a file sink interface for a given file.
 *
 * @param filename The sink file name.
 * @param callback Callback to invoke upon completion (can be NULL).
 * @param callback_arg Argument to pass to the callback function.
 * @return Pointer to a curl_sink_interface_t.
 */
curl_sink_interface_t *file_sink(
    curl_event_request_t *req,
    const char *filename,
    file_complete_callback_t callback,
    void *callback_arg);

#endif
