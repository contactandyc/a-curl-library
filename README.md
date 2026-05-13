# a-curl-library

`a-curl-library` is an advanced, asynchronous HTTP client framework built on top of `libcurl`'s multi interface. It provides a non-blocking, event-driven execution loop designed for high-concurrency scraping, complex network pipelines, and acting as the foundation for higher-level API SDKs (like the `a-curl-gcloud-plugin`).

## Architecture & Features (Why use it)

Standard `libcurl` provides raw network transfer capabilities, but handling thousands of concurrent requests, respecting varying API rate limits, and managing inter-request dependencies usually requires writing complex, custom state machines. This library absorbs that state management into a single asynchronous event loop.

* **Asynchronous Event Loop:** Built around `curl_multi_poll`, the core loop manages thousands of simultaneous connections efficiently on a single thread without blocking. It automatically idles the thread when waiting for network I/O or retry timers.
* **Resource Dependency Graph (The Plugin Enabler):** Requests can depend on shared resources (e.g., an OAuth token, a database session). If a request depends on an unresolved resource, it is queued but blocked. When the resource is published, all blocked requests are automatically dequeued and executed. This architecture allows plugins to manage authentication entirely independently from the requests that rely on them.
* **Advanced Rate Limiting:** Implements a thread-safe token bucket algorithm (`rate_manager.c`). You can define logical limits (e.g., max concurrent connections and max requests per second) per API domain. The loop automatically schedules requests to comply with these limits, and reacts to HTTP 429 / 403 responses by pausing the bucket with jittered exponential backoffs.
* **Resilience & Retries:** Features built-in exponential backoff with full jitter for failed requests. Requests can also be configured as "refreshing" (background tasks that run periodically on a set interval).
* **Pluggable Sinks:** Decouples network I/O from memory management. A request can stream data into a dynamically resizing memory buffer (`memory_sink`), directly to disk (`file_sink`), or into custom user-defined parsers.
* **Extensible Design:** The library makes no assumptions about JSON or specific APIs, but provides native `ajson` integration hooks and a `plugin_data` pointer on every request so extensions can safely manage their own memory lifecycles and HTTP header injection.

## Usage (How to use it)

The primary structures are `curl_event_loop_t` and `curl_event_request_t`. Memory for requests is managed via `aml_pool_t` to ensure clean allocations and teardowns.

### 1. The Event Loop

Create the event loop and run it. The loop will block and process requests until all "foreground" (non-refreshing) requests have completed or been cancelled.

```c
#include "a-curl-library/curl_event_loop.h"

int main() {
    // Initialize the loop
    curl_event_loop_t *loop = curl_event_loop_init(NULL, NULL);

    // ... submit requests to the loop ...

    // Blocks until all foreground requests are complete
    curl_event_loop_run(loop);

    // Print metrics and cleanup
    curl_event_metrics_t metrics = curl_event_loop_get_metrics(loop);
    printf("Completed: %llu, Failed: %llu\n", metrics.completed_requests, metrics.failed_requests);

    curl_event_loop_destroy(loop);
    return 0;
}

```

### 2. Building and Submitting a Request

Requests are built using helper functions, attached to a data sink, and then submitted to the loop.

```c
#include "a-curl-library/curl_event_request.h"
#include "a-curl-library/sinks/memory.h"

// Callback fired when the memory sink has fully downloaded the payload
void on_download_complete(char *data, size_t length, bool success, 
                          CURLcode result, long http_code, const char *error_msg, 
                          void *arg, curl_event_request_t *req) {
    if (success) {
        printf("Downloaded %zu bytes!\n", length);
    }
}

void fetch_data(curl_event_loop_t *loop) {
    // 1. Build a basic GET request
    curl_event_request_t *req = curl_event_request_build_get("https://example.com/api/data", NULL, NULL);

    // 2. Attach a memory sink to handle the incoming bytes
    memory_sink(req, on_download_complete, NULL);

    // 3. Optional: Configure timeouts and retries
    curl_event_request_connect_timeout(req, 10); 
    curl_event_request_enable_retries(req, 3, 2.0, 1000, 10000, true); 

    // 4. Submit to the loop with normal priority (0)
    curl_event_request_submit(loop, req, 0);
}

```

### 3. Rate Limiting

To prevent being IP-banned or dropping traffic, map your requests to a specific rate limit bucket.

```c
#include "a-curl-library/rate_manager.h"

// Configure a global limit: Max 5 concurrent connections, 10 requests per second.
rate_manager_set_limit("my_api", 5, 10.0);

curl_event_request_t *req = curl_event_request_build_get("https://api.example.com/items", NULL, NULL);

// Bind the request to the bucket
curl_event_request_rate_limit(req, "my_api", false);
curl_event_request_submit(loop, req, 0);

```

### 4. Resource Dependencies (The Plugin Bridge)

If you need to fetch an Authentication Token before firing your data requests, use the `curl_resource` API. This is how plugins (like the Google Cloud token manager) pause user requests until authentication is negotiated.

```c
// Declare the resource ID
curl_event_res_id auth_token_id = curl_event_res_declare(loop);

// Create the dependent data request
curl_event_request_t *data_req = curl_event_request_build_get("https://api.example.com/protected", NULL, NULL);
curl_event_request_depend(data_req, auth_token_id);
curl_event_request_submit(loop, data_req, 0); 
// ^ Note: data_req is now queued but BLOCKED. It will not execute.

// Later, an authentication request succeeds and publishes the token:
void on_auth_complete(...) {
    const char *token = "Bearer abcdef12345";
    
    // Publishing the resource automatically unblocks and executes `data_req`
    curl_event_res_publish(loop, auth_token_id, strdup(token), free);
}

```

## Internal Module Layout

* **`curl_event_loop.c/h`**: The main scheduler. Manages `curl_multi_init`, request queues (pending, inactive, active, rate-limited, cancelled), and timer calculations based on the earliest required retry.
* **`curl_event_request.c/h`**: The request descriptor builder. Sets up the underlying `CURL *easy_handle`, assigns tracking timestamps, and evaluates the retry/jitter policy logic.
* **`rate_manager.c/h`**: Thread-safe token bucket algorithm. Evaluates `next_retry_at` timestamps based on available capacity and HTTP 429 status codes.
* **`curl_resource.c/h`**: Cross-thread dependency graph. Uses a Lock-free MPSC Treiber stack (`res_inbox_t`) so threads can asynchronously publish resources and wake the main loop, unblocking dependent HTTP requests.
* **`sinks/`**: Implementations of the `curl_sink_interface_t`. Handles the translation of `CURLOPT_WRITEFUNCTION` data chunks into usable formats (`memory.c` into an `aml_buffer`, `file.c` via `fwrite`).
* **`worker_pool.c/h`**: A generic task queue and thread pool utility, useful for offloading heavy JSON parsing or post-download processing to avoid stalling the main `curl_multi` loop thread.
