// ProsperoStore - Install, update and uninstall as journaled transactions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/catalog.hpp"
#include "install/archive.hpp"
#include "install/worker.hpp"
#include "net/http.hpp"
#include "system/locations.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace store::install
{
enum class Phase : int
{
    idle,
    downloading,
    verifying,
    unpacking,
    activating,
    removing
};
struct Progress
{
    std::atomic<int> phase{static_cast<int>(Phase::idle)};
    std::atomic<std::uint64_t> done{0}, total{0};
};
using Fetch = std::function<net::Response(const std::string &url, std::uint64_t limit,
                                          const net::Sink &sink, net::Control &control)>;
struct Environment
{
    std::string root; // The store's own state: receipts and the journal.
    std::string self; // The store's title: never changed through these paths.
    // The version of the store that is running. After the store swapped its own
    // folder, the console may start the old one once more (it keeps the old
    // folder mounted for a while): that old store must not finish the update.
    std::string self_version;
    system::ScanPolicy policy;
    Fetch fetch;
    // The custom catalog that lists the app, if any (Entry::catalog_api): release files on its
    // host are allowed too.
    std::string catalog;
    // 0: not running, 1: running, anything else: unknown. Unset means unknown,
    // and unknown refuses every update and uninstall.
    std::function<int(const std::string &id)> running;
    // Defaults: the filesystem's free space, the clock, a cancellable sleep.
    std::function<bool(const std::string &directory, std::uint64_t &bytes)> space;
    std::function<std::string()> now;
    std::function<void(unsigned seconds, const std::atomic<bool> &cancelled)> wait;
    // Where the console keeps the copies of an app's sce_sys it made when the
    // app was first registered (<registered>/app/<TITLEID>/sce_sys and
    // <registered>/appmeta/<TITLEID>). They are refreshed after an update, so a
    // new icon, name or background reaches the home screen.
    std::string registered = "/user";
    // Persistent app data, separate from installed folders; redirected by host tests.
    std::string data = "/data";
    // The console limits how fast an app may write to its storage, so the
    // heavy file work is given to another process when these are set. Each
    // returns 1 when done, 0 when it failed and -1 when it could not start
    // (nothing touched): then the store does the work itself.
    std::function<int(const std::string &archive, const std::string &title,
                      const std::string &destination, const std::atomic<bool> &cancelled,
                      std::atomic<std::uint64_t> &written, std::string &error, ExtractTimes &times)>
        unpack;
    std::function<int(const std::string &path)> remove;
    // A download written by another process. False: write it here instead.
    std::function<bool(const std::string &path, Writer &writer)> save;
    // Tests only. Work folder instead of <drive>/prosperostore, and a hook
    // that returns true to stop dead at a named step, as a power cut would.
    std::string work;
    std::function<bool(const char *step)> interrupt;
};
struct Request
{
    catalog::Entry entry;        // The app's verified detail record.
    std::string location;        // A folder ShadowMountPlus scans; used for a new install.
    std::string minimum_version; // From the signed versions.json; empty when unknown.
    // The store updating itself: its own running folder is swapped for the new
    // one and the old one is kept until the next start, which finishes the job.
    bool self_update = false;
    // Another app with this title ID is installed there and this one takes its place: the
    // version need not be newer than the installed one.
    bool replace = false;
    // An update that lays the new version's files over the installed folder instead of
    // replacing it: what the new version doesn't bring stays where it was, and the previous
    // folder is not kept. Ignored for an install and for the store itself.
    bool overwrite = false;
};
struct Result
{
    bool ok = false;
    bool interrupted = false;
    bool restart = false;  // Done as far as a running store can: restart to finish.
    std::string operation; // "install", "update", "uninstall", or what recovery found.
    std::string version;   // The contentVersion now on disk.
    std::string error;
    std::string note; // For the log: where the time went.
    // An update: where the previous version's folder was kept, with whatever the user
    // had put inside it (<drive>/prosperostore/previous/<TITLEID>, until the next update).
    std::string kept_at;
};
// At every instant <location>/<TITLEID> is the complete old version, the
// complete new version, or absent. Installs when the folder is absent, updates
// when a matching receipt manages it, refuses anything else.
Result apply(const Environment &environment, const Request &request, net::Control &control,
             Progress &progress);
Result uninstall(const Environment &environment, const std::string &id, const std::string &location,
                 Progress &progress);
// Takes over an app that was installed by hand: a folder named after the title
// in a scanned location, whose param.json names that title. Only a receipt is
// written; the folder is not touched. From then on the store updates and
// uninstalls it like any app it installed.
Result adopt(const Environment &environment, const std::string &id, const std::string &location);
// Run once at start, before any transaction: finishes or undoes what the
// journal says was in progress.
Result recover(const Environment &environment);
} // namespace store::install
