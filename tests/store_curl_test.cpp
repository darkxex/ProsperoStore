// ProsperoStore - Real TLS refusal and response-boundary checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/curl_request.hpp"
#include <cassert>
#include <string>
int main(int argc, char **argv)
{
    assert(argc == 3 && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    const std::string local = std::string("https://localhost:") + argv[1];
    store::net::Control control;
    std::string body;
    const auto sink = [&](std::string_view bytes)
    {
        body += bytes;
        return true;
    };
    auto result = store::net::curl_request(local + "/ok", 4, sink, control, {}, argv[2]);
    assert(result.ok() && body == "okay" && result.bytes == 4);
    body.clear();
    result = store::net::curl_request(local + "/reuse", 4, sink, control, {}, argv[2]);
    assert(result.ok() && body == "okay" && !result.etag.empty());
    const auto connection = result.etag;
    body.clear();
    result = store::net::curl_request(local + "/reuse", 4, sink, control, {}, argv[2]);
    assert(result.ok() && body == "okay" && result.etag == connection);
    body.clear();
    result = store::net::curl_request(local + "/ok", 3, sink, control, {}, argv[2]);
    assert(!result.ok() && body.empty());
    result = store::net::curl_request(local + "/ok", 4, sink, control, {});
    assert(!result.ok() && body.empty()); // self-signed, absent from system trust
    result = store::net::curl_request(std::string("https://127.0.0.1:") + argv[1] + "/ok", 4, sink,
                                      control, {}, argv[2]);
    assert(!result.ok() && body.empty()); // trusted certificate, wrong host
    result = store::net::curl_request(local + "/redirect", 4, sink, control, {}, argv[2]);
    assert(result.status == 302 && result.error.empty() &&
           result.location == "https://example.com/forbidden" && body.empty());
    // Redirect pages have their own bound, independent of a small final file (a signature).
    result = store::net::curl_request(local + "/redirect-body", 64, sink, control, {}, argv[2]);
    assert(result.status == 302 && result.error.empty() && result.location == "/ok" &&
           result.bytes == 0 && body.empty());
    result =
        store::net::curl_request(local + "/redirect-body-large", 64, sink, control, {}, argv[2]);
    assert(result.status == 302 && !result.error.empty() && result.bytes == 0 && body.empty());
    // The redirect allowance never raises the next response's actual file limit.
    result = store::net::curl_request(local + "/ok", 3, sink, control, {}, argv[2]);
    assert(!result.ok() && body.empty());
    result = store::net::curl_request(local + "/headers", 4, sink, control, {}, argv[2]);
    assert(!result.ok() && body.empty());
    result = store::net::curl_request(local + "/error", 3, sink, control, {}, argv[2]);
    assert(!result.error.empty() && body.empty()); // Discarded bodies are bounded too.
    result = store::net::curl_request(local + "/interim", 4, sink, control, {}, argv[2]);
    assert(result.ok() && result.etag == "\"current\"" && body == "okay");
    body.clear();
    control.cancel();
    result = store::net::curl_request(local + "/ok", 4, sink, control, {}, argv[2]);
    assert(!result.ok() && result.error == "Cancelled" && body.empty());
    control.connection.reset();
    curl_global_cleanup();
}
