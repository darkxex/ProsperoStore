// ProsperoStore - Bounded HTTPS transport shared by host and console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>

namespace store::net
{
// Console startup/shutdown, outside the worker lifetime.
int start_transport(bool elevated);
void stop_transport();
// "libcurl" or "sceHttp": the one start_transport chose.
const char *transport_name();
enum class Purpose
{
    catalog,
    artifact,
    comments, // the likes and comments files: their one address, and nowhere it redirects to
    release   // the store's own latest release (catalog::kSelfReleaseApi): that address only
};
struct Control
{
    std::atomic<bool> cancelled{false};
    std::mutex guard;
    int request = -1;
    // Owned by one worker; release after joining it, before transport shutdown.
    std::unique_ptr<void, void (*)(void *)> connection{nullptr, nullptr};
    void cancel();
};
struct Response
{
    int status = 0;
    std::uint64_t bytes = 0;
    std::string location, etag, error;
    // For the debug log, when a request failed: how far it got and how long each step took.
    std::string detail;
    bool ok() const
    {
        return error.empty() && (status == 200 || status == 304);
    }
};
using Sink = std::function<bool(std::string_view)>;
bool read_headers(std::string_view headers, Response &out);
Response request_once(const std::string &url, std::uint64_t limit, const Sink &sink,
                      Control &control, const std::string &etag);
// custom: the custom catalog's address, whose host may serve artifacts (catalog::artifact_url).
Response get(const std::string &url, Purpose purpose, std::uint64_t limit, const Sink &sink,
             Control &control, const std::string &etag = {}, std::string_view custom = {});
Response fetch(const std::string &url, Purpose purpose, std::size_t limit, std::string &body,
               Control &control, const std::string &etag = {});
} // namespace store::net
