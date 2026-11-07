// ProsperoStore - Redirect, cancellation and conditional-response trust boundaries.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.hpp"
#include <cassert>
#include <vector>

using namespace store::net;
std::vector<Response> responses;
std::vector<std::string> calls;
namespace store::net
{
Response request_once(const std::string &url, std::uint64_t, const Sink &sink, Control &,
                      const std::string &)
{
    calls.push_back(url);
    assert(calls.size() <= responses.size());
    const auto response = responses[calls.size() - 1];
    if (response.status == 200)
        assert(sink("body"));
    return response;
}
} // namespace store::net
Response reply(int status, const std::string &location = {})
{
    Response response;
    response.status = status;
    response.location = location;
    return response;
}
int main()
{
    Response headers;
    assert(read_headers("HTTP/1.1 302 Found\r\nLocation: /next\r\nETag: \"abc\"\r\n\r\n", headers));
    assert(headers.location == "/next" && headers.etag == "\"abc\"");
    assert(!read_headers("Location: /a\r\nlocation: /b\r\n", headers));
    assert(!read_headers("ETag: a\r\nETAG: b\r\n", headers));
    assert(!read_headers("Location: /a\rb\n", headers));
    assert(!read_headers(std::string(65537, 'x'), headers));
    Control control;
    std::string body = "old";
    const std::string url = "https://github.com/owner/repo/releases/download/v1/app.zip";
    responses = {reply(302, "https://evil.example/payload")};
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok());
    assert(calls.size() == 1 && body == "old");
    calls.clear();
    responses = {reply(302, "http://github.com/insecure")};
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok());
    assert(calls.size() == 1);
    calls.clear();
    responses = {reply(302, "https://release-assets.githubusercontent.com/asset"), reply(200)};
    assert(fetch(url, Purpose::artifact, 100, body, control).ok() && body == "body");
    assert(calls.size() == 2);
    calls.clear();
    responses.assign(6, reply(302, "/loop"));
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && calls.size() == 6);
    calls.clear();
    responses = {reply(304)};
    body = "preserved";
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && body == "preserved");
    calls.clear();
    assert(fetch(url, Purpose::artifact, 100, body, control, "\"tag\"").ok() &&
           body == "preserved");
    calls.clear();
    assert(!fetch(url, Purpose::artifact, 100, body, control, "bad\r\nheader").ok() &&
           calls.empty());
    responses = {reply(302, "/api/v1/index.json"), reply(200)};
    assert(fetch("https://dev.example:8443/feed/", Purpose::catalog, 100, body, control).ok());
    assert(calls.back() == "https://dev.example:8443/api/v1/index.json");
    calls.clear();
    responses = {reply(302, "http://dev.example/api/v1/index.json")};
    assert(!fetch("https://dev.example/feed/", Purpose::catalog, 100, body, control).ok());
    assert(calls.size() == 1);
    calls.clear();
    // Location is a URI reference, resolved against the request that returned it.
    const std::string catalog = "https://dev.example:8443/api/v1/manifest.json?source=/old";
    const std::vector<std::pair<std::string, std::string>> relative = {
        {"index.json", "/api/v1/index.json"},
        {"./index.json", "/api/v1/index.json"},
        {"../index.json", "/api/index.json"},
        {"../../../../index.json", "/index.json"},
        {"folder/../index.json", "/api/v1/index.json"},
        {"folder/..", "/api/v1/"},
        {".", "/api/v1/"},
        {"../..", "/"},
        {"?sequence=2", "/api/v1/manifest.json?sequence=2"},
        {"index.json?path=../keep/./", "/api/v1/index.json?path=../keep/./"},
        {"%2e%2e/index.json", "/api/v1/%2e%2e/index.json"},
        {"/next/./a/../index.json", "/next/index.json"},
    };
    for (const auto &[location, path] : relative)
    {
        responses = {reply(302, location), reply(200)};
        assert(fetch(catalog, Purpose::catalog, 100, body, control).ok());
        assert(calls.size() == 2 && calls.back() == "https://dev.example:8443" + path);
        calls.clear();
    }
    responses = {reply(302, "../next/manifest.json"), reply(307, "?revision=2"), reply(200)};
    assert(fetch(catalog, Purpose::catalog, 100, body, control).ok());
    assert(calls.size() == 3 &&
           calls.back() == "https://dev.example:8443/api/next/manifest.json?revision=2");
    calls.clear();
    // Resolution must not turn an invalid scheme, fragment or missing Location into a path.
    for (const auto *location : {"http:other.json", "file:///tmp/catalog", "#fragment", "",
                                 "bad path/../index.json", "bad\\path/../index.json"})
    {
        responses = {reply(302, location)};
        assert(!fetch(catalog, Purpose::catalog, 100, body, control).ok() && calls.size() == 1);
        calls.clear();
    }
    // An artifact on the custom catalog's host, redirected within it; nowhere else.
    const std::string own = "https://store.example/api/v1/";
    const Sink collect = [&](std::string_view chunk)
    {
        body.append(chunk);
        return true;
    };
    body.clear();
    responses = {reply(302, "/files/app.zip"), reply(200)};
    assert(get("https://store.example/app.zip", Purpose::artifact, 100, collect, control, {}, own)
               .ok() &&
           body == "body" && calls.back() == "https://store.example/files/app.zip");
    calls.clear();
    responses = {reply(302, "https://evil.example/payload")};
    assert(!get("https://store.example/app.zip", Purpose::artifact, 100, collect, control, {}, own)
                .ok() &&
           calls.size() == 1);
    calls.clear();
    assert(!get("https://store.example/app.zip", Purpose::artifact, 100, collect, control).ok() &&
           calls.empty());
    body.clear();
    responses = {reply(302, "../files/app.zip"), reply(200)};
    assert(get("https://store.example/releases/app.zip", Purpose::artifact, 100, collect, control,
               {}, own)
               .ok() &&
           body == "body" && calls.back() == "https://store.example/files/app.zip");
    calls.clear();
    for (const auto *location : {"//evil.example/app.zip", "//store.example:8443/app.zip"})
    {
        responses = {reply(302, location)};
        assert(
            !get("https://store.example/app.zip", Purpose::artifact, 100, collect, control, {}, own)
                 .ok() &&
            calls.size() == 1);
        calls.clear();
    }
    // Without a custom catalog (the official address), GitHub's redirect is still followed and
    // nothing else is.
    const std::string official = "https://prosperostore.net/api/v1/";
    body.clear();
    responses = {reply(302, "https://release-assets.githubusercontent.com/asset"), reply(200)};
    assert(get(url, Purpose::artifact, 100, collect, control, {}, official).ok() &&
           body == "body" && calls.size() == 2);
    calls.clear();
    responses = {reply(302, "https://prosperostore.net/payload")};
    assert(!get(url, Purpose::artifact, 100, collect, control, {}, official).ok() &&
           calls.size() == 1);
    calls.clear();
    control.cancelled.store(true);
    assert(!fetch(url, Purpose::artifact, 100, body, control).ok() && calls.empty());
}
