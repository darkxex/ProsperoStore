// ProsperoStore - Background catalog work and nonblocking frame delivery.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/client.hpp"
#include "core/image.hpp"
#include "install/transaction.hpp"
#include "social/comments.hpp"
#include "system/inventory.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <vector>

namespace store
{
struct Update
{
    enum class Kind
    {
        catalog,
        detail,
        icon,
        qr,
        inventory,
        notice,       // message is its title, detail its body
        store_update, // a newer ProsperoStore is listed: message is its version
        setup,        // what to check in ShadowMountPlus's settings: detail, one per line
        news,         // the catalog's news feed, newest first
        job,          // a finished install, update or uninstall: entry.id, ok, message, detail
        locations,    // where apps can be installed on this console
        likes,        // likes and comment counts of every app (social::Index)
        comments,     // one app's latest comments: entry.id and thread
        error
    } kind = Kind::error;
    std::string detail;
    catalog::Snapshot snapshot;
    std::vector<catalog::News> news;
    social::Index likes;
    social::Thread thread;
    catalog::Entry entry;
    hui::Image image;
    hui::Image ambient;       // icon: the app's picture, made from it (make_ambient)
    std::uint32_t accent = 0; // icon: the colour the screen leans toward, 0xRRGGBB
    system::Inventory installed;
    std::uint64_t generation = 0;
    std::string message;
    bool ok = false;
    bool restart = false; // the store updated itself: restart to finish
    bool close = false;   // the update helper has the go-ahead: the store must close now
    std::vector<std::pair<std::string, std::uint64_t>> locations; // path, free bytes
};
// What the installer is doing, for the frame that draws it.
struct JobView
{
    std::string id; // the title being changed; empty when idle
    install::Phase phase = install::Phase::idle;
    std::uint64_t done = 0, total = 0;
    std::vector<std::string> waiting; // titles queued behind it
};
class Service
{
  public:
    // version is the running store's contentVersion; empty skips its update check.
    explicit Service(std::string root, std::string version = {})
        : root_(std::move(root)), version_(std::move(version))
    {
    }
    ~Service();
    // Set before start() to run the installer: one transaction at a time, on
    // its own worker, after recovering whatever the journal says was interrupted.
    bool installer = false;
    // Set before start(): whether to ask the catalog for a newer store.
    bool check_updates = true;
    // Captured at start; changes in Settings apply on the next launch.
    // The custom catalogs, in the order the settings list them; none for the official
    // catalog alone. With with_official the official catalog is the one shown, signed and
    // checked as ever, and these add their apps to it; without it the first custom catalog
    // takes its place and the rest add to that.
    struct CustomCatalog
    {
        std::string api;   // its API directory (catalog::github_catalog_api)
        std::string label; // its name, "owner/repository", said on its apps' pages
    };
    std::vector<CustomCatalog> catalogs;
    bool verify_signatures = true; // of the custom catalogs; the official one always is
    bool with_official = true;
    // Tests only: replaces the environment built from the console's configuration.
    std::optional<install::Environment> installer_environment;
    bool start();
    void stop();
    bool take(std::vector<Update> &updates);
    bool request_detail(const std::string &id);
    // "Show likes and comments". While it is off nothing is asked of the comments address;
    // switched on, the counts are read then, and an app's comments when its page opens.
    void show_comments(bool on)
    {
        // Each time it goes on the counts are read anew: the screen forgot them when it went off.
        if (!on)
            comments_ = false;
        else if (!comments_.exchange(true))
            likes_stale_ = true;
    }
    bool request_icons(const std::vector<std::string> &ids);
    void report_frames(std::string report);
    // Never block: false means "ask again next frame" (or the queue is full).
    // location is the scanned folder that holds, or will hold, the app's folder.
    bool request_install(const catalog::Entry &entry, const std::string &location,
                         bool replace = false);
    bool request_uninstall(const std::string &id, const std::string &location,
                           const std::string &console_id = {});
    // Takes over an app installed by hand (a receipt is written, nothing else).
    bool request_adopt(const catalog::Entry &entry, const std::string &location);
    // The settings file, written by a worker: never on the frame.
    bool save_settings(std::string text);
    // Where settings are kept; empty means the store folder's settings.txt. Set before
    // start() when settings can be kept although the store has no folder of its own.
    std::string settings_file;
    bool cancel_job(const std::string &id);
    bool job(JobView &view);
    // The titles running now, refreshed every two seconds by the installer's
    // worker. known is false while the console's sandbox folder can't be listed.
    bool running(std::vector<std::string> &ids, bool &known);
    // Development builds: the store's process ends on the spot when a
    // transaction reaches this named step, as it would in a power cut.
    void stop_at(std::string step);

  private:
    std::mutex stop_guard_;
    std::string stop_step_;
    struct Job
    {
        enum class Kind
        {
            install,
            uninstall,
            adopt
        } kind = Kind::install;
        catalog::Entry entry;
        std::string location;
        bool replace = false; // a different app with this title ID is installed there
    };
    static void *entry(void *self);
    static void *icon_entry(void *self);
    static void *install_entry(void *self);
    void load_icons();
    void run();
    void run_installer();
    void publish(Update update);
    void check_store_update();
    bool enqueue(Job job);
    // notes: what the user should check in ShadowMountPlus's settings, in plain words.
    bool load_policy(system::ScanPolicy &policy, std::vector<std::string> *notes = nullptr) const;
    std::string settings_path() const;
    std::string root_, version_;
    net::Control control_;
    net::Control icon_control_;
    net::Control job_control_;
    pthread_t thread_{};
    pthread_t icon_thread_{};
    pthread_t install_thread_{};
    bool started_ = false;
    bool icons_started_ = false;
    bool installer_started_ = false;
    std::mutex mutex_;
    std::vector<Update> updates_;
    std::string detail_;
    std::vector<std::string> icons_;
    std::vector<catalog::Entry> entries_;
    std::map<std::string, std::string> versions_;
    std::deque<Job> jobs_;
    std::string job_id_;
    std::vector<std::string> running_;
    bool running_known_ = false;
    install::Progress progress_;
    std::uint64_t generation_ = 0;
    bool online_ = false;
    std::string frame_report_;
    std::string settings_;
    bool settings_pending_ = false;
    std::atomic<bool> comments_{false}, likes_stale_{false};
};
} // namespace store
