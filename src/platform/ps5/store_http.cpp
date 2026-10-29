// ProsperoStore - sceHttp transport based on the boilerplate update-check kit.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/http.hpp"
#include "platform/ps5/system.hpp"
#include "net/curl_request.hpp"
#include <algorithm>
#ifdef STORE_DEBUG_TRACE
#include "diag/trace.hpp"
#include <atomic>
#endif
#include <unistd.h>
#include <vector>

extern "C"
{
    int sceNetPoolCreate(const char *, int, int);
    int sceNetPoolDestroy(int);
    int sceSslInit(std::size_t);
    int sceSslTerm(int);
    int sceHttpInit(int, int, std::size_t);
    int sceHttpTerm(int);
    int sceHttpCreateTemplate(int, const char *, int, int);
    int sceHttpDeleteTemplate(int);
    int sceHttpSetAutoRedirect(int, int);
    int sceHttpSetResolveTimeOut(int, std::uint32_t);
    int sceHttpSetConnectTimeOut(int, std::uint32_t);
    int sceHttpSetSendTimeOut(int, std::uint32_t);
    int sceHttpSetRecvTimeOut(int, std::uint32_t);
    int sceHttpsEnableOption(int, std::uint32_t);
    int sceHttpCreateConnectionWithURL(int, const char *, int);
    int sceHttpDeleteConnection(int);
    int sceHttpCreateRequestWithURL(int, int, const char *, std::uint64_t);
    int sceHttpDeleteRequest(int);
    int sceHttpAddRequestHeader(int, const char *, const char *, std::uint32_t);
    int sceHttpSendRequest(int, const void *, std::size_t);
    int sceHttpGetStatusCode(int, int *);
    int sceHttpGetAllResponseHeaders(int, char **, std::size_t *);
    int sceHttpReadData(int, void *, std::size_t);
    int sceHttpAbortRequest(int);
}

namespace store::net
{
namespace
{
constexpr const char *kCaList = "/system/common/cert/CA_LIST.cer";
bool elevated_transport = false;
bool curl_ready = false;
#ifdef STORE_DEBUG_TRACE
// The debug log's record of requests: every failure (the first two hundred) and the first
// forty that worked, so a trace shows both what the network refuses and what it carries.
std::atomic<int> traced_failures = 0, traced_successes = 0;
void trace_request(const std::string &url, const Response &out, const char *transport,
                   const char *step, unsigned code)
{
    const bool worked = out.error.empty() && (out.status == 200 || out.status == 304);
    if (worked ? traced_successes.fetch_add(1) >= 40 : traced_failures.fetch_add(1) >= 200)
        return;
    store::diag::trace("request %s: %.110s -> HTTP %d, %llu bytes%s%s%s%s (code 0x%08x)", transport,
                       url.c_str(), out.status, static_cast<unsigned long long>(out.bytes),
                       worked ? "" : ", failed at ", worked ? "" : step,
                       out.error.empty() ? "" : ": ", out.error.c_str(), code);
    if (!out.detail.empty())
        store::diag::trace("request detail: %.400s", out.detail.c_str());
}
#endif
} // namespace
int start_transport(bool elevated)
{
    // libcurl needs the whole elevation: in a sandbox it can't start its resolver thread
    // and can't see the console's CA list. A store that only reached /data (mounted by
    // ShadowMountPlus 1.7, Lapy never asked) is still sandboxed, so it keeps sceHttp.
    const bool ca_list = access(kCaList, R_OK) == 0;
    elevated_transport = elevated && ca_list;
    hui::sys::log("[STORE] transport elevated=%d ca_list=%d -> %s", elevated ? 1 : 0,
                  ca_list ? 1 : 0, transport_name());
    if (!elevated_transport)
        return 0;
    const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ready = result == CURLE_OK;
    return curl_ready ? 0 : -static_cast<int>(result);
}
const char *transport_name()
{
    return elevated_transport ? "libcurl" : "sceHttp";
}
void stop_transport()
{
    if (curl_ready)
        curl_global_cleanup();
    curl_ready = false;
}
void Control::cancel()
{
    cancelled.store(true);
    std::lock_guard lock(guard);
    if (request >= 0)
        sceHttpAbortRequest(request);
}

Response request_once(const std::string &url, std::uint64_t limit, const Sink &sink,
                      Control &control, const std::string &etag)
{
    if (elevated_transport)
    {
        if (!curl_ready)
        {
            Response failure;
            failure.error = "The secure network transport is unavailable";
            return failure;
        }
        auto result =
            curl_request(url, limit, sink, control, etag, "/system/common/cert/CA_LIST.cer");
        hui::sys::log("[STORE] curl status=%d bytes=%llu error=%s", result.status,
                      static_cast<unsigned long long>(result.bytes), result.error.c_str());
#ifdef STORE_DEBUG_TRACE
        trace_request(url, result, "libcurl", "the request", 0);
#endif
        return result;
    }
    struct Resources
    {
        int pool = -1, ssl = -1, http = -1, tmpl = -1, connection = -1, request = -1;
        Control &control;
        ~Resources()
        {
            {
                std::lock_guard lock(control.guard);
                control.request = -1;
                if (request >= 0)
                    sceHttpDeleteRequest(request);
            }
            if (connection >= 0)
                sceHttpDeleteConnection(connection);
            if (tmpl >= 0)
                sceHttpDeleteTemplate(tmpl);
            if (http >= 0)
                sceHttpTerm(http);
            if (ssl >= 0)
                sceSslTerm(ssl);
            if (pool >= 0)
                sceNetPoolDestroy(pool);
        }
    } resources{-1, -1, -1, -1, -1, -1, control};
    Response out;
    int result = resources.pool = sceNetPoolCreate("ProsperoStore", 1024 * 1024, 0);
    if (result >= 0)
        result = resources.ssl = sceSslInit(2 * 1024 * 1024);
    if (result >= 0)
        result = resources.http = sceHttpInit(resources.pool, resources.ssl, 4 * 1024 * 1024);
    if (result >= 0)
        result = resources.tmpl =
            sceHttpCreateTemplate(resources.http, "ProsperoStore/01.000.116", 2, 0);
    if (result >= 0)
        result = sceHttpSetAutoRedirect(resources.tmpl, 0);
    if (result >= 0)
        result = sceHttpSetResolveTimeOut(resources.tmpl, 5000000);
    if (result >= 0)
        result = sceHttpSetConnectTimeOut(resources.tmpl, 5000000);
    if (result >= 0)
        result = sceHttpSetSendTimeOut(resources.tmpl, 5000000);
    if (result >= 0)
        result = sceHttpSetRecvTimeOut(resources.tmpl, 5000000);
    if (result >= 0)
        result = sceHttpsEnableOption(resources.tmpl, 0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80);
    if (result >= 0)
        result = resources.connection =
            sceHttpCreateConnectionWithURL(resources.tmpl, url.c_str(), 0);
    if (result >= 0)
        result = resources.request =
            sceHttpCreateRequestWithURL(resources.connection, 0, url.c_str(), 0);
    if (result >= 0 && !etag.empty())
        result = sceHttpAddRequestHeader(resources.request, "If-None-Match", etag.c_str(), 0);
    if (result >= 0)
        result = sceHttpAddRequestHeader(resources.request, "Accept-Encoding", "identity", 0);
    {
        std::lock_guard lock(control.guard);
        control.request = resources.request;
        if (control.cancelled.load())
            result = -1;
    }
    if (result >= 0)
        result = sceHttpSendRequest(resources.request, nullptr, 0);
    if (result >= 0)
        result = sceHttpGetStatusCode(resources.request, &out.status);
    char *headers = nullptr;
    std::size_t size = 0;
    if (result >= 0)
        result = sceHttpGetAllResponseHeaders(resources.request, &headers, &size);
    if (result >= 0 && (!headers || !read_headers(std::string_view(headers, size), out)))
        result = -1;
    std::vector<char> buffer(64 * 1024);
    while (result >= 0 && out.status == 200 && !control.cancelled.load())
    {
        const auto wanted =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), limit - out.bytes + 1));
        const int got = sceHttpReadData(resources.request, buffer.data(), wanted);
        if (got == 0)
            break;
        if (got < 0)
        {
            result = got;
            break;
        }
        if (static_cast<std::uint64_t>(got) > limit - out.bytes)
        {
            out.error = "The response exceeds its size limit";
            break;
        }
        if (!sink(std::string_view(buffer.data(), static_cast<std::size_t>(got))))
        {
            out.error = "The download could not be saved";
            break;
        }
        out.bytes += static_cast<std::uint64_t>(got);
    }
    if (control.cancelled.load())
        out.error = "Cancelled";
    else if (result < 0 && out.error.empty())
        out.error = "The network request failed";
    hui::sys::log("[STORE] http rc=0x%x status=%d bytes=%llu pool=%d ssl=%d http=%d tmpl=%d "
                  "conn=%d req=%d headers=%zu error=%s",
                  static_cast<unsigned>(result), out.status,
                  static_cast<unsigned long long>(out.bytes), resources.pool, resources.ssl,
                  resources.http, resources.tmpl, resources.connection, resources.request, size,
                  out.error.c_str());
#ifdef STORE_DEBUG_TRACE
    // Which step the console's HTTPS library stopped at, from what was created.
    const char *step = resources.pool < 0         ? "the network pool"
                       : resources.ssl < 0        ? "TLS start"
                       : resources.http < 0       ? "HTTP start"
                       : resources.tmpl < 0       ? "the request template"
                       : resources.connection < 0 ? "the connection set-up"
                       : resources.request < 0    ? "the request set-up"
                       : out.status == 0          ? "sending (name lookup, connecting or TLS)"
                                                  : "reading the answer";
    trace_request(url, out, "sceHttp", step, static_cast<unsigned>(result));
#endif
    return out;
}
} // namespace store::net
