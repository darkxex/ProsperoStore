// ProsperoStore - Shared host and elevated PS5 HTTPS transport.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/curl_request.hpp"
#include <cstdio>
#include <memory>
#include <limits>
#ifdef STORE_NATIVE_CURL
#include <sys/socket.h>
#endif

namespace store::net
{
Response curl_request(const std::string &url, std::uint64_t limit, const Sink &sink,
                      Control &control, const std::string &etag, const char *ca_path)
{
    Response out;
    if (!control.connection)
        control.connection = {curl_easy_init(), +[](void *handle) { curl_easy_cleanup(handle); }};
    auto *curl = static_cast<CURL *>(control.connection.get());
    if (!curl)
    {
        out.error = "The network could not start";
        return out;
    }
    struct ResetOptions
    {
        CURL *handle;
        ~ResetOptions()
        {
            curl_easy_reset(handle);
        }
    } reset{curl}; // Keep connections, but never retain request-local callbacks or headers.
    struct State
    {
        CURL *handle;
        Response &response;
        Control &control;
        const Sink &sink;
        std::uint64_t limit;
        std::string headers;
        std::uint64_t received = 0;
        std::size_t header_bytes = 0;
    } state{curl, out, control, sink, limit, {}};
    const auto write =
        +[](char *data, std::size_t size, std::size_t count, void *opaque) -> std::size_t
    {
        auto &s = *static_cast<State *>(opaque);
        if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
            return 0;
        const auto length = size * count;
        long status = 0;
        if (curl_easy_getinfo(s.handle, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK)
            return 0;
        if (s.control.cancelled.load())
            return 0;
        if (length > s.limit - s.received)
        {
            s.response.error = "The response exceeds its size limit";
            return 0;
        }
        s.received += length;
        if (status != 200)
            return length;
        if (!s.sink({data, length}))
            return 0;
        s.response.bytes += length;
        return length;
    };
    const auto headers =
        +[](char *data, std::size_t size, std::size_t count, void *opaque) -> std::size_t
    {
        auto &s = *static_cast<State *>(opaque);
        if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
            return 0;
        const auto length = size * count;
        if (length > 64 * 1024 - s.header_bytes)
            return 0;
        s.header_bytes += length;
        if (std::string_view(data, length).starts_with("HTTP/"))
            s.headers.clear();
        s.headers.append(data, length);
        return length;
    };
    const auto progress = +[](void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int
    { return static_cast<State *>(opaque)->control.cancelled.load() ? 1 : 0; };
    std::string conditional = "If-None-Match: " + etag;
    curl_slist *list = etag.empty() ? nullptr : curl_slist_append(nullptr, conditional.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> owned_headers(list,
                                                                              curl_slist_free_all);
    if (!etag.empty() && !list)
    {
        out.error = "The request could not be created";
        return out;
    }
    CURLcode configured = CURLE_OK;
    const auto set = [&](CURLoption option, auto value)
    {
        const auto result = curl_easy_setopt(curl, option, value);
        if (result != CURLE_OK)
            configured = result;
    };
    if (ca_path)
        set(CURLOPT_CAINFO, ca_path);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    set(CURLOPT_BUFFERSIZE, 256L * 1024L);
#ifdef STORE_NATIVE_CURL
    const auto socket_options = +[](void *, curl_socket_t socket, curlsocktype) -> int
    {
        int enabled = 1;
        return setsockopt(socket, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) == 0
                   ? CURL_SOCKOPT_OK
                   : CURL_SOCKOPT_ERROR;
    };
    set(CURLOPT_SOCKOPTFUNCTION, socket_options);
    set(CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
#endif
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_USERAGENT, "ProsperoStore/01.000.115");
    set(CURLOPT_ACCEPT_ENCODING, "identity");
    set(CURLOPT_CONNECTTIMEOUT, 15L);
    // A server may take a while to send its first byte, and a slow line stalls for seconds:
    // only twenty seconds of nothing at all ends a request.
    set(CURLOPT_LOW_SPEED_LIMIT, 1L);
    set(CURLOPT_LOW_SPEED_TIME, 20L);
    char message[CURL_ERROR_SIZE] = {};
    set(CURLOPT_ERRORBUFFER, message);
    set(CURLOPT_HTTPHEADER, list);
    set(CURLOPT_WRITEFUNCTION, write);
    set(CURLOPT_WRITEDATA, &state);
    set(CURLOPT_HEADERFUNCTION, headers);
    set(CURLOPT_HEADERDATA, &state);
    set(CURLOPT_XFERINFOFUNCTION, progress);
    set(CURLOPT_XFERINFODATA, &state);
    set(CURLOPT_NOPROGRESS, 0L);
    if (configured != CURLE_OK)
    {
        out.error = "The secure network request could not be configured";
        return out;
    }
    const auto result = curl_easy_perform(curl);
    long status = 0;
    const auto info = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    out.status = static_cast<int>(status);
    if (control.cancelled.load())
        out.error = "Cancelled";
    else if ((result != CURLE_OK || info != CURLE_OK) && out.error.empty())
        out.error =
            "The network request failed (curl " + std::to_string(static_cast<int>(result)) + ")";
    if (result != CURLE_OK)
    {
        // What curl says happened, the address it reached and when each step ended (0: never).
        const auto seconds = [&](CURLINFO what)
        {
            double value = 0.0;
            (void)curl_easy_getinfo(curl, what, &value);
            return static_cast<int>(value * 1000.0);
        };
        char *address = nullptr;
        (void)curl_easy_getinfo(curl, CURLINFO_PRIMARY_IP, &address);
        char text[CURL_ERROR_SIZE + 160];
        std::snprintf(text, sizeof(text),
                      "%s; address %s; ms: name %d, connect %d, tls %d, first byte %d, total %d",
                      message[0] ? message : curl_easy_strerror(result),
                      address && address[0] ? address : "none", seconds(CURLINFO_NAMELOOKUP_TIME),
                      seconds(CURLINFO_CONNECT_TIME), seconds(CURLINFO_APPCONNECT_TIME),
                      seconds(CURLINFO_STARTTRANSFER_TIME), seconds(CURLINFO_TOTAL_TIME));
        out.detail = text;
    }
    else if (!read_headers(state.headers, out))
        out.error = "The response headers are invalid";
    return out;
}
} // namespace store::net
