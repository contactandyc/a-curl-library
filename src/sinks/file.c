// SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
// SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
// SPDX-License-Identifier: Apache-2.0

#include "a-curl-library/sinks/file.h"
#include "a-memory-library/aml_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct file_sink_s {
    curl_sink_interface_t interface; // Base interface
    FILE *file;                        // File handle
    char *filename;                    // File path
    file_complete_callback_t callback; // Completion callback
    void *callback_arg;                // Argument for the callback
} file_sink_t;

bool file_init(curl_sink_interface_t *interface, long content_length) {
    (void)content_length;
    file_sink_t *file = (file_sink_t *)interface;
    if(file->file) // for retries
        fclose(file->file);
    file->file = fopen(file->filename, "wb");
    if (!file->file) {
        fprintf(stderr, "[file_sink] Failed to open file: %s\n", file->filename);
        return false;
    }
    return true;
}

size_t file_write(const void *data, size_t size, size_t nmemb, curl_sink_interface_t *interface) {
    file_sink_t *file = (file_sink_t *)interface;
    size_t written = fwrite(data, size, nmemb, file->file);
    return written * size;
}

void file_failure(CURLcode result, long http_code, curl_sink_interface_t *interface,
                  curl_event_request_t *req) {
    file_sink_t *file = (file_sink_t *)interface;

    // Log the failure (stderr)
    fprintf(stderr, "[file_sink] Download failed (CURLcode: %d, HTTP code: %ld) for file: %s\n",
            result, http_code, file->filename);

    // Trigger the callback with failure details
    if (file->callback) {
        const char *error_msg = curl_easy_strerror(result);
        file->callback(file->filename, false, result, http_code, error_msg, file->callback_arg, req);
    }
}

void file_complete(curl_sink_interface_t *interface, curl_event_request_t *req) {
    file_sink_t *file = (file_sink_t *)interface;

    // Log the success (stdout)
    // printf("[file_sink] Download complete. File written: %s\n", file->filename);

    // Trigger the callback with success details
    if (file->callback) {
        file->callback(file->filename, true, CURLE_OK, 200, NULL, file->callback_arg, req);
    }
}

void file_destroy(curl_sink_interface_t *interface) {
    file_sink_t *file = (file_sink_t *)interface;
    if (file->file) fclose(file->file);
}

curl_sink_interface_t *file_sink(
    curl_event_request_t *req,
    const char *filename,
    file_complete_callback_t callback,
    void *callback_arg) {

    file_sink_t *file = (file_sink_t *)aml_pool_zalloc(req->pool,
                                                       sizeof(file_sink_t) + strlen(filename) + 1);
    if (!file) return NULL;

    file->filename = (char *)(file + 1);
    strcpy(file->filename, filename);
    file->file = NULL;
    file->callback = callback;
    file->callback_arg = callback_arg;

    // Assign function pointers to the interface
    file->interface.pool = req->pool;
    file->interface.init = file_init;
    file->interface.write = file_write;
    file->interface.failure = file_failure;
    file->interface.complete = file_complete;
    file->interface.destroy = file_destroy;

    curl_event_request_sink(req, (curl_sink_interface_t *)file, NULL);

    return (curl_sink_interface_t *)file;
}
