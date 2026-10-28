// ProsperoStore - Redirect policy and bounded response handling.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "net/http.hpp"
#include "catalog/catalog.hpp"
#include "social/comments.hpp"
#include <algorithm>

namespace store::net
{
bool read_headers(std::string_view headers, Response &out)
{
    if (headers.size() > 64 * 1024)
        return false;
    if (headers.ends_with('\0'))
        headers.remove_suffix(1);
    bool location_seen = false, etag_seen = false;
    while (!headers.empty())
    {
        auto end = headers.find('\n');
        auto line = headers.substr(0, end);
        headers = end == std::string_view::npos ? std::string_view{} : headers.substr(end + 1);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        const auto colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        std::string key(line.substr(0, colon));
        for (char &c : key)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
        auto value = line.substr(colon + 1);
        while (value.starts_with(' ') || value.starts_with('\t'))
            value.remove_prefix(1);
        if (std::any_of(value.begin(), value.end(),
                        [](unsigned char c) { return c < 32 || c == 127; }))
            return false;
        if (key == "location")
        {
            if (location_seen || value.size() > 4096)
                return false;
            location_seen = true;
            out.location = value;
        }
        else if (key == "etag")
        {
            if (etag_seen || value.size() > 512)
                return false;
            etag_seen = true;
            out.etag = value;
        }
    }
    return true;
}

Response get(const std::string &url, Purpose purpose, std::uint64_t limit, const Sink &sink,
             Control &control, const std::string &etag, std::string_view custom)
{
    Response response;
    if (limit == 0 || limit > catalog::kArtifactLimit || etag.size() > 512 ||
        std::any_of(etag.begin(), etag.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    {
        response.error = "Invalid download request";
        return response;
    }
    std::string current = url;
    for (int redirects = 0; redirects <= 5; ++redirects)
    {
        if (!(purpose == Purpose::catalog    ? catalog::api_url(current)
              : purpose == Purpose::comments ? social::data_url(current)
              : purpose == Purpose::release
                  ? current == catalog::kSelfReleaseApi
                  : catalog::artifact_url(current, redirects > 0, custom)))
        {
            response.error = "The download address is not allowed";
            return response;
        }
        if (control.cancelled.load())
        {
            response.error = "Cancelled";
            return response;
        }
        response = request_once(current, limit, sink, control, etag);
        if (!response.error.empty())
            return response;
        if (response.status != 301 && response.status != 302 && response.status != 303 &&
            response.status != 307 && response.status != 308)
        {
            if (response.status != 200 && !(response.status == 304 && !etag.empty()))
                response.error = "The server refused the request";
            return response;
        }
        if (response.location.starts_with("//"))
            current = "https:" + response.location;
        else if (response.location.starts_with('/'))
            current = current.substr(0, current.find('/', 8)) + response.location;
        else
            current = response.location;
    }
    response.error = "Too many redirects";
    return response;
}

Response fetch(const std::string &url, Purpose purpose, std::size_t limit, std::string &body,
               Control &control, const std::string &etag)
{
    std::string candidate;
    auto response = get(
        url, purpose, limit,
        [&](std::string_view chunk)
        {
            candidate.append(chunk);
            return true;
        },
        control, etag);
    if (response.ok() && response.status == 200)
        body = std::move(candidate);
    return response;
}
} // namespace store::net
