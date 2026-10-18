// ProsperoStore - Catalog cache and HTTPS never run on the render thread.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef STORE_DEBUG_TRACE
#include "diag/trace.hpp"
#endif
#include "app/ambient.hpp"
#include "system/title_registry.hpp"
#include "system/self_update_store.hpp"
#include "app/service.hpp"
#include "core/save_file.hpp"
#include "core/qr.hpp"
#include "platform/ps5/system.hpp"
#include "system/locations.hpp"
#include "system/running.hpp"
#include "system/storage_probe.hpp"
#ifdef STORE_FILE_WORKER
#include "system/worker_launch.hpp"
#endif
#include "install/files.hpp"
#include "catalog/icons.hpp"
#include <algorithm>
#include <cstdio>
#include <set>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace store
{
Service::~Service()
{
    stop();
}
bool Service::start()
{
    if (started_)
        return true;
    started_ = pthread_create(&thread_, nullptr, entry, this) == 0;
    if (started_)
        icons_started_ = pthread_create(&icon_thread_, nullptr, icon_entry, this) == 0;
    if (started_ && !icons_started_)
        stop();
    if (started_ && installer)
    {
        const bool created = pthread_create(&install_thread_, nullptr, install_entry, this) == 0;
        std::lock_guard lock(mutex_);
        installer_started_ = created;
    }
    return started_;
}
void Service::stop()
{
    control_.cancel();
    icon_control_.cancel();
    bool joining = false;
    {
        // Under the lock, so a job starting now can't clear the cancellation.
        std::lock_guard lock(mutex_);
        job_control_.cancel();
        joining = installer_started_;
        installer_started_ = false;
    }
    if (started_)
        pthread_join(thread_, nullptr);
    if (icons_started_)
        pthread_join(icon_thread_, nullptr);
    if (joining)
        pthread_join(install_thread_, nullptr);
    control_.connection.reset();
    icon_control_.connection.reset();
    job_control_.connection.reset();
    icons_started_ = false;
    started_ = false;
    // A user may change the feed and close while the initial refresh is still retrying.
    if (settings_pending_ && !settings_path().empty())
    {
        const auto error = hui::save::write_atomic(settings_path(), settings_);
        if (!error.empty())
            hui::sys::log("[STORE] settings save failed: %s", error.c_str());
        settings_pending_ = false;
    }
}
bool Service::take(std::vector<Update> &updates)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || updates_.empty())
        return false;
    updates.swap(updates_);
    return true;
}
bool Service::request_detail(const std::string &id)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    detail_ = id;
    return true;
}
bool Service::request_icons(const std::vector<std::string> &ids)
{
    if (ids.size() > 16)
        return false;
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    icons_ = ids;
    return true;
}
void Service::publish(Update update)
{
    // Backpressure stays on workers; never drop an image the screen is waiting for.
    while (!control_.cancelled.load())
    {
        {
            std::lock_guard lock(mutex_);
            if (updates_.size() < 8)
            {
                if (update.kind == Update::Kind::catalog)
                {
                    entries_ = update.snapshot.entries;
                    versions_ = update.snapshot.versions;
                    update.generation = ++generation_;
                    online_ = update.snapshot.online;
                }
                updates_.push_back(std::move(update));
                return;
            }
        }
        hui::sys::sleep_us(10000);
    }
}
void Service::report_frames(std::string report)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (lock.owns_lock())
        frame_report_ = std::move(report);
}
void *Service::entry(void *self)
{
    static_cast<Service *>(self)->run();
    return nullptr;
}

// What ShadowMountPlus scans on this console, from its own configuration.
// An existing configuration that can't be read is never replaced by defaults.
bool Service::load_policy(system::ScanPolicy &out, std::vector<std::string> *notes) const
{
    const auto note = [&](std::string text)
    {
        if (notes)
            notes->push_back(std::move(text));
    };
    if (installer_environment)
    {
        out = installer_environment->policy;
        return true;
    }
    if (root_ != "/data/prosperostore")
        return false;
    std::string configuration, manual, policy_error;
    const bool configured =
        hui::save::read_file("/data/shadowmount/config.ini", &configuration, 256 * 1024);
    const bool listed = hui::save::read_file("/data/shadowmount/manual.lst", &manual, 256 * 1024);
    const auto unreadable = [](const char *path, bool read)
    {
        struct stat info
        {
        };
        return !read && (lstat(path, &info) == 0 || errno != ENOENT);
    };
    system::ScanPolicy policy;
    if (unreadable("/data/shadowmount/config.ini", configured) ||
        unreadable("/data/shadowmount/manual.lst", listed))
    {
        hui::sys::log("[STORE] storage policy error=Existing configuration is unreadable; "
                      "refusing default paths");
#ifdef STORE_DEBUG_TRACE
        diag::trace("ShadowMountPlus settings: config.ini or manual.lst exists but can't be read");
#endif
        note("config.ini or manual.lst in /data/shadowmount exists but can't be read, so "
             "installing is off. Check the file's permissions.");
        return false;
    }
    if (!system::scan_policy(configuration, manual, policy, policy_error))
    {
        hui::sys::log("[STORE] storage policy error=%s", policy_error.c_str());
#ifdef STORE_DEBUG_TRACE
        diag::trace("ShadowMountPlus settings refused: %s", policy_error.c_str());
#endif
        note("ShadowMountPlus's settings can't be used, so installing is off: " + policy_error +
             ".");
        return false;
    }
    hui::sys::log("[STORE] storage config=%d manual=%d roots=%zu entries=%zu depth=%u", configured,
                  listed, policy.roots.size(), policy.manual.size(), policy.depth);
#ifdef STORE_DEBUG_TRACE
    {
        // The scan paths outside removable drives, and the manual list: what a report needs.
        std::string roots, manual_entries;
        for (const auto &root : policy.roots)
            if (!root.starts_with("/mnt/") && roots.size() < 300)
                roots += (roots.empty() ? "" : ", ") + root;
        for (const auto &entry : policy.manual)
            if (manual_entries.size() < 300)
                manual_entries += (manual_entries.empty() ? "" : ", ") + entry;
        diag::trace("ShadowMountPlus settings: config.ini %s, manual.lst %s, scan depth %u, "
                    "%zu scan paths (not on drives: %s), %zu manual entries%s%s",
                    configured ? "found" : "none", listed ? "found" : "none", policy.depth,
                    policy.roots.size(), roots.empty() ? "none" : roots.c_str(),
                    policy.manual.size(), manual_entries.empty() ? "" : ": ",
                    manual_entries.c_str());
        for (const auto &line : policy.ignored)
            diag::trace("ShadowMountPlus settings: left out, not a path the store can read: %s",
                        line.c_str());
    }
#endif
    // What the store did about lines it can't use, and what the user may want to fix.
    for (const auto &line : policy.ignored)
        note("Left out, not a full path: " + line + ". Write it like /data/homebrew.");
    if (policy.fell_back)
        note("None of your scan paths could be read, so ShadowMountPlus's default folders "
             "are used (/data/homebrew and the homebrew folder on each drive).");
    for (const auto &entry : policy.manual)
    {
        // A manual entry is one game folder or one image. A folder of apps there does
        // nothing in ShadowMountPlus: it belongs in config.ini as scanpath=.
        struct stat info
        {
        };
        if (stat(entry.c_str(), &info) == 0 && S_ISDIR(info.st_mode) &&
            stat((entry + "/sce_sys/param.json").c_str(), &info) != 0)
            note("manual.lst lists " + entry +
                 ", which is a folder and not one game. For a "
                 "folder of apps use scanpath= in config.ini.");
    }
    out = std::move(policy);
    return true;
}

bool Service::request_install(const catalog::Entry &entry, const std::string &location)
{
    return enqueue({Job::Kind::install, entry, location});
}

bool Service::request_adopt(const catalog::Entry &entry, const std::string &location)
{
    return enqueue({Job::Kind::adopt, entry, location});
}

bool Service::enqueue(Job job)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !installer_started_ || jobs_.size() >= 16)
        return false;
    const auto same = [&](const Job &other) { return other.entry.id == job.entry.id; };
    if (job_id_ != job.entry.id && std::none_of(jobs_.begin(), jobs_.end(), same))
        jobs_.push_back(std::move(job));
    return true;
}

std::string Service::settings_path() const
{
    if (!settings_file.empty())
        return settings_file;
    return root_.empty() ? std::string() : root_ + "/settings.txt";
}

bool Service::save_settings(std::string text)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    settings_ = std::move(text);
    settings_pending_ = true;
    return true;
}

bool Service::request_uninstall(const std::string &id, const std::string &location)
{
    catalog::Entry entry;
    entry.id = id;
    std::string name;
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        for (const auto &listed : entries_)
            if (listed.id == id)
                entry.name = listed.name;
    }
    return enqueue({Job::Kind::uninstall, std::move(entry), location});
}

bool Service::cancel_job(const std::string &id)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    std::erase_if(jobs_, [&](const Job &job) { return job.entry.id == id; });
    if (job_id_ == id)
        job_control_.cancelled = true; // The transfer and the unpacking both watch it.
    return true;
}

void Service::stop_at(std::string step)
{
    std::lock_guard lock(stop_guard_);
    stop_step_ = std::move(step);
}

bool Service::job(JobView &view)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    view.id = job_id_;
    view.phase = static_cast<install::Phase>(progress_.phase.load());
    view.done = progress_.done.load();
    view.total = progress_.total.load();
    view.waiting.clear();
    for (const auto &job : jobs_)
        view.waiting.push_back(job.entry.id);
    return true;
}

bool Service::running(std::vector<std::string> &ids, bool &known)
{
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    ids = running_;
    known = running_known_;
    return true;
}

void *Service::install_entry(void *self)
{
    static_cast<Service *>(self)->run_installer();
    return nullptr;
}

void Service::run_installer()
{
    install::Environment environment;
    if (installer_environment)
        environment = *installer_environment;
    else
    {
        environment.root = root_;
        environment.self = "PPSA99000";
        environment.self_version = version_;
        environment.fetch = [](const std::string &url, std::uint64_t limit, const net::Sink &sink,
                               net::Control &control)
        {
#ifdef STORE_DEVELOPMENT
            // A scripted self-update reads its archive from the console, since
            // no newer store is published while it is being tested.
            if (url.ends_with("/dev/PPSA99000.zip"))
            {
                // In pieces, as a download arrives: never the whole archive in memory.
                net::Response response;
                const int file = open("/data/prosperostore/dev/self.zip", O_RDONLY);
                std::vector<char> piece(256 * 1024);
                std::uint64_t total = 0;
                bool ok = file >= 0;
                while (ok)
                {
                    const auto count = read(file, piece.data(), piece.size());
                    if (count <= 0)
                    {
                        ok = count == 0;
                        break;
                    }
                    total += static_cast<std::uint64_t>(count);
                    ok = total <= limit && !control.cancelled.load() &&
                         sink(std::string_view(piece.data(), static_cast<std::size_t>(count)));
                }
                if (file >= 0)
                    close(file);
                if (ok)
                    response.status = 200;
                else
                    response.error = "The development archive could not be read";
                return response;
            }
#endif
            return net::get(url, net::Purpose::artifact, limit, sink, control);
        };
        environment.running = [](const std::string &id) { return system::title_running(id); };
#ifdef STORE_FILE_WORKER
        environment.unpack = [](const std::string &archive, const std::string &title,
                                const std::string &destination, const std::atomic<bool> &cancelled,
                                std::atomic<std::uint64_t> &written, std::string &error,
                                install::ExtractTimes &times)
        {
            return install::worker_extract(system::launch_worker, archive, title, destination,
                                           cancelled, written, error, times);
        };
        environment.remove = [](const std::string &path)
        { return install::worker_remove(system::launch_worker, path); };
        environment.save = [](const std::string &path, install::Writer &writer)
        { return install::worker_store(system::launch_worker, path, writer); };
#endif
#ifdef STORE_DEVELOPMENT
        environment.interrupt = [this](const char *step)
        {
            std::lock_guard lock(stop_guard_);
            if (stop_step_ != step)
                return false;
            hui::sys::log("[STORE] stopping dead at step %s", step);
            std::fflush(stdout);
            _exit(0);
        };
#endif
    }
    std::string broken;
    if (environment.root.empty() || !hui::save::ensure_directory(environment.root) ||
        (!installer_environment && !load_policy(environment.policy)))
        broken = "Installing is unavailable: ShadowMountPlus's settings couldn't be read. "
                 "About says what to check.";
    const auto rescan = [&]
    {
        Update installed;
        installed.kind = Update::Kind::inventory;
        installed.installed = system::scan_installed(
            environment.policy, environment.root + "/receipts", control_.cancelled);
        publish(std::move(installed));
    };
    if (broken.empty())
    {
        // Where apps can go: the folders ShadowMountPlus scans that exist on a
        // drive the store can work on, with the room each has.
        Update places;
        places.kind = Update::Kind::locations;
        for (const auto &path : environment.policy.roots)
        {
            std::uint64_t available = 0;
            if (path.starts_with("/mnt/shadowmnt") ||
                (!installer_environment && system::drive_root(path).empty()) ||
                install::kind(path) != install::Kind::directory ||
                !system::available_space(path, available) || available < (256ull << 20))
                continue;
            places.locations.emplace_back(path, available);
        }
        publish(std::move(places));
        // Before anything else: finish or undo what was interrupted last time.
        const auto recovered = install::recover(environment);
        hui::sys::log("[STORE] recovery ok=%d operation=%s error=%s", recovered.ok,
                      recovered.operation.c_str(), recovered.error.c_str());
        if (!recovered.ok)
            broken = recovered.error;
        else if (recovered.restart)
        {
            // The console started the old store once more after its update.
            broken = "ProsperoStore is finishing its own update. Close it and open it again.";
            Update notice;
            notice.kind = Update::Kind::job;
            notice.entry.id = environment.self;
            notice.ok = true;
            notice.restart = true;
            notice.message = "ProsperoStore was updated";
            notice.detail = "The console is still running the previous version. Close "
                            "ProsperoStore, restart the console and open it again.";
            publish(std::move(notice));
        }
        else if (!recovered.operation.empty())
        {
            Update notice;
            notice.kind = Update::Kind::notice;
            notice.message = "ProsperoStore recovered";
            notice.detail = "An interrupted " + recovered.operation + " was cleaned up.";
            publish(std::move(notice));
            rescan();
        }
    }
    unsigned idle = 0;
    const auto entries_snapshot = [&]
    {
        std::vector<std::string> ids;
        std::lock_guard lock(mutex_);
        for (const auto &entry : entries_)
            ids.push_back(entry.id);
        return ids;
    };
    while (!control_.cancelled.load())
    {
        Job job;
        std::string minimum;
        {
            std::lock_guard lock(mutex_);
            if (!jobs_.empty())
            {
                job = std::move(jobs_.front());
                jobs_.pop_front();
                job_id_ = job.entry.id;
                const auto listed = versions_.find(job_id_);
                minimum = listed == versions_.end() ? std::string{} : listed->second;
                // A cancel belongs to the job it was asked for, never to the next one.
                job_control_.cancelled = control_.cancelled.load();
                progress_.phase = static_cast<int>(install::Phase::idle);
                progress_.done = 0;
                progress_.total = 0;
            }
        }
        if (job.entry.id.empty())
        {
            // Idle: keep the list of running titles fresh for the screens.
            if (++idle % 40 == 1)
            {
                std::vector<std::string> ids;
                const bool known = installer_environment ? true : system::running_titles(ids);
                if (installer_environment && installer_environment->running)
                    for (const auto &entry : entries_snapshot())
                        if (installer_environment->running(entry) == 1)
                            ids.push_back(entry);
                std::lock_guard lock(mutex_);
#ifdef STORE_DEVELOPMENT
                if (ids != running_ || known != running_known_)
                {
                    std::string list;
                    for (const auto &id : ids)
                        list += (list.empty() ? "" : ",") + id;
                    hui::sys::log("[STORE] running known=%d titles=%s", known, list.c_str());
                }
#endif
                running_ = std::move(ids);
                running_known_ = known;
            }
            hui::sys::sleep_us(50000);
            continue;
        }
        install::Result result;
        if (!broken.empty())
            result.error = broken;
        else if (job.kind == Job::Kind::uninstall)
            result = install::uninstall(environment, job.entry.id, job.location, progress_);
        else if (job.kind == Job::Kind::adopt)
            result = install::adopt(environment, job.entry.id, job.location);
#ifdef STORE_FILE_WORKER
        else if (job.entry.id == environment.self)
        {
            // The store itself: the kit's helper swaps the files of its folder
            // once it has closed, so the next start runs the new version.
            result.operation = "self-update";
            result.ok = system::update_self(job.entry, environment.self_version, progress_,
                                            job_control_, result.error);
            result.version = job.entry.content_version;
        }
#endif
        else
            result = install::apply(
                environment, {job.entry, job.location, minimum, job.entry.id == environment.self},
                job_control_, progress_);
        hui::sys::log("[STORE] job id=%s operation=%s ok=%d version=%s error=%s %s",
                      job.entry.id.c_str(), result.operation.c_str(), result.ok,
                      result.version.c_str(), result.error.c_str(), result.note.c_str());
        // An uninstalled app leaves the home screen too. Its own files under
        // /data are not the console's to remove and stay where they are.
        int unregistered = 1;
        if (result.ok && result.operation == "uninstall")
        {
            unregistered = system::unregister_title(job.entry.id);
            hui::sys::log("[STORE] unregister id=%s rc=0x%08x", job.entry.id.c_str(),
                          static_cast<unsigned>(unregistered));
        }
        Update done;
        done.kind = Update::Kind::job;
        done.entry.id = job.entry.id;
        done.ok = result.ok;
        const auto &name = job.entry.name.empty() ? job.entry.id : job.entry.name;
        if (result.ok && result.operation == "self-update")
        {
            done.close = true;
            done.message = "Updating ProsperoStore";
            done.detail = "It closes now. A notification says when the new version is in "
                          "place; then open it again.";
        }
        else if (result.ok)
        {
            done.restart = result.restart;
            done.message = name + (result.operation == "uninstall" ? " uninstalled"
                                   : result.operation == "adopt"   ? " is now managed"
                                   : result.operation == "update"  ? " updated"
                                                                   : " installed");
            done.detail =
                result.restart ? "Close ProsperoStore and open it again to finish. If the "
                                 "old version opens, restart the console first."
                : result.operation == "uninstall"
                    ? (unregistered == 0 ? "It is off the home screen. Its saved data stays."
                                         : "Its saved data stays. Its home-screen tile goes "
                                           "after a restart.")
                : result.operation == "adopt" ? "ProsperoStore will offer its updates from now on."
                : result.operation == "update"
                    ? "The new version is in place. Give the console a few minutes "
                      "before starting it."
                    : "ShadowMountPlus will add it to your home screen in a moment.";
            if (result.operation == "update" && !result.kept_at.empty())
                done.detail = "Previous folder kept in " + result.kept_at + ".";
        }
        else
        {
            done.message = name + (result.error == "Cancelled" ? ": cancelled" : ": not changed");
            done.detail = result.error == "Cancelled" ? "Nothing was installed." : result.error;
        }
        if (broken.empty())
            rescan();
        {
            std::lock_guard lock(mutex_);
            job_id_.clear();
            progress_.phase = static_cast<int>(install::Phase::idle);
        }
        publish(std::move(done));
    }
}
void Service::run()
{
#ifdef STORE_SANDBOX_CONTROL
    std::string probe_body;
    const auto probe =
        net::fetch("https://prosperostore.net/api/v1/manifest.json", net::Purpose::catalog,
                   catalog::kVersionsLimit, probe_body, control_);
    hui::sys::log("[STORE] sandbox TLS control status=%d bytes=%zu error=%s", probe.status,
                  probe_body.size(), probe.error.c_str());
#endif
    if (!root_.empty() && !hui::save::ensure_directory(root_))
    {
        Update failure;
        failure.message = "Storage is unavailable. The catalog could not be loaded.";
        publish(std::move(failure));
        return;
    }
    // "Official plus custom": the official catalog is the one everything below is about,
    // signed and checked as ever; the custom one is loaded beside it and only adds apps.
    const bool additive = with_official && catalog_url != catalog::kDefaultApi;
    const std::string cache_folder = root_.empty() ? "" : root_ + "/cache";
    catalog::Client client(cache_folder, additive ? std::string(catalog::kDefaultApi) : catalog_url,
                           additive ? true : verify_signatures);
    catalog::Client extra_client(cache_folder, catalog_url, verify_signatures);
    catalog::Snapshot extra;
    std::string extra_error;
    std::vector<std::string> clashes;
    const auto shown = [&](const catalog::Snapshot &official)
    {
        clashes.clear();
        return additive ? catalog::with_extra(official, extra, &clashes) : official;
    };
    system::ScanPolicy scan_policy;
    std::vector<std::string> setup_notes;
    const bool can_scan = load_policy(scan_policy, &setup_notes);
    if (!setup_notes.empty())
    {
        Update setup;
        setup.kind = Update::Kind::setup;
        for (const auto &line : setup_notes)
            setup.detail += line + "\n";
        publish(std::move(setup));
    }
#ifdef STORE_DEVELOPMENT
    for (const auto &path : scan_policy.roots)
    {
        if (!can_scan || control_.cancelled.load())
            break;
        const auto drive = system::drive_root(path);
        if (drive.empty())
            continue;
        const auto probe = system::probe_storage(path);
        const bool work_safe =
            system::work_path_unscanned(scan_policy, drive + "/prosperostore/staging/PPSA99000");
        hui::sys::log(
            "[STORE] storage path=%s fs=%s available=%llu rename=%d work-safe=%d error=%s",
            path.c_str(), probe.filesystem.c_str(),
            static_cast<unsigned long long>(probe.available), probe.renamed, work_safe,
            probe.error.c_str());
    }
#endif
    catalog::Snapshot snapshot;
    std::string error;
    const bool had_cache = client.cached(snapshot, error);
    if (additive)
        (void)extra_client.cached(extra, extra_error);
#ifdef STORE_DEBUG_TRACE
    diag::trace("catalog cache in %s: %s%s%s", root_.empty() ? "(no store folder)" : root_.c_str(),
                had_cache ? "found" : "none", error.empty() ? "" : ", ", error.c_str());
#endif
    if (had_cache)
    {
        Update cached;
        cached.kind = Update::Kind::catalog;
        cached.snapshot = shown(snapshot);
        cached.message = snapshot.verified ? "Offline catalog • Checking for updates"
                                           : "Offline catalog • Signatures not checked";
        publish(std::move(cached));
    }
    bool refreshed = false;
    for (unsigned attempt = 0; attempt < 4 && !control_.cancelled.load(); ++attempt)
    {
        if (client.refresh(snapshot, control_, error))
        {
            refreshed = true;
            break;
        }
        hui::sys::log("[STORE] catalog attempt=%u error=%s", attempt + 1, error.c_str());
#ifdef STORE_DEBUG_TRACE
        diag::trace("catalog attempt %u of 4: %s", attempt + 1, error.c_str());
#endif
        if (attempt < 3)
            for (unsigned tick = 0; tick < (10U << attempt) && !control_.cancelled.load(); ++tick)
                hui::sys::sleep_us(100000);
    }
    // The custom catalog beside it: two tries, and the official one is shown either way.
    bool extra_refreshed = false;
    for (unsigned attempt = 0; additive && attempt < 2 && !control_.cancelled.load(); ++attempt)
    {
        if ((extra_refreshed = extra_client.refresh(extra, control_, extra_error)))
            break;
        hui::sys::log("[STORE] custom catalog attempt=%u error=%s", attempt + 1,
                      extra_error.c_str());
#ifdef STORE_DEBUG_TRACE
        diag::trace("custom catalog attempt %u of 2: %s", attempt + 1, extra_error.c_str());
#endif
    }
    if (!control_.cancelled.load())
    {
        Update result;
        result.kind = snapshot.accepted ? Update::Kind::catalog : Update::Kind::error;
        result.snapshot = shown(snapshot);
        result.message = refreshed ? (snapshot.verified ? "Catalog verified • Up to date"
                                                        : "Signatures not checked • Up to date")
                                   : "Offline • " + error;
        if (!refreshed && snapshot.accepted && !snapshot.verified)
            result.message = "Signatures not checked • " + result.message;
        hui::sys::log("[STORE] catalog verified=%d online=%d sequence=%llu apps=%zu",
                      snapshot.verified, refreshed,
                      static_cast<unsigned long long>(snapshot.manifest.sequence),
                      snapshot.entries.size());
        if (additive && snapshot.accepted)
            result.message += extra.accepted
                                  ? (extra.verified ? " • Custom catalog added"
                                                    : " • Custom catalog added, not checked")
                                  : " • Custom catalog unavailable";
        publish(std::move(result));
        if (additive && !control_.cancelled.load() && (!extra.accepted || !clashes.empty()))
        {
            // Said once: why the custom catalog, or some of it, isn't on the shelves.
            Update notice;
            notice.kind = Update::Kind::notice;
            if (!extra.accepted)
            {
                notice.message = "Custom catalog not loaded";
                notice.detail = extra_error.empty() ? "It could not be read." : extra_error;
            }
            else
            {
                notice.message =
                    std::to_string(clashes.size()) +
                    (clashes.size() == 1 ? " custom app hidden" : " custom apps hidden");
                notice.detail = "prosperostore.net lists the same title ID, and its app is the one "
                                "shown: " +
                                clashes.front() + (clashes.size() > 1 ? " and others." : ".");
            }
            publish(std::move(notice));
        }
    }
    // Likes and comment counts, read with the catalog and only while the setting is on. They
    // are decoration from an address of their own: any failure shows nothing, and is tried
    // again when a page next asks for comments. Without a network they are not asked for.
    bool likes_wanted = true, likes_known = false;
    const auto read_likes = [&]
    {
        if (likes_stale_.exchange(false))
        {
            likes_wanted = true;
            likes_known = false;
        }
        if (!comments_.load() || !refreshed || !likes_wanted || likes_known ||
            control_.cancelled.load())
            return;
        likes_wanted = false;
        Update counts;
        counts.kind = Update::Kind::likes;
        std::string body;
        const auto response = net::fetch(social::index_url(), net::Purpose::comments,
                                         social::kIndexLimit, body, control_);
        likes_known = response.ok() && social::parse_index(body, counts.likes);
        hui::sys::log("[STORE] likes known=%d apps=%zu", likes_known, counts.likes.size());
        if (likes_known)
            publish(std::move(counts));
    };
    read_likes();
    if (refreshed && check_updates && !control_.cancelled.load())
        check_store_update();
    if (refreshed && snapshot.accepted && !control_.cancelled.load())
    {
        // The catalog's news; a catalog without any says nothing.
        Update feed;
        feed.kind = Update::Kind::news;
        std::string failure;
        if (client.news(snapshot, feed.news, control_, failure) && !feed.news.empty())
            publish(std::move(feed));
        else if (!failure.empty())
            hui::sys::log("[STORE] news: %s", failure.c_str());
    }
    if (!control_.cancelled.load())
    {
        Update installed;
        installed.kind = Update::Kind::inventory;
        if (can_scan)
            installed.installed =
                system::scan_installed(scan_policy, root_ + "/receipts", control_.cancelled);
        else
        {
            installed.installed.complete = false;
            installed.installed.errors.push_back("Installed apps could not be inspected with the "
                                                 "current permissions or scan configuration.");
        }
        hui::sys::log("[STORE] inventory complete=%d apps=%zu errors=%zu",
                      installed.installed.complete, installed.installed.apps.size(),
                      installed.installed.errors.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(3, installed.installed.errors.size());
             ++i)
            hui::sys::log("[STORE] inventory error=%s", installed.installed.errors[i].c_str());
        publish(std::move(installed));
    }
    while (!control_.cancelled.load())
    {
        std::string id;
        std::string frame_report;
        {
            std::lock_guard lock(mutex_);
            id.swap(detail_);
            frame_report.swap(frame_report_);
        }
        if (!frame_report.empty())
            hui::sys::log("[STORE] %s", frame_report.c_str());
        read_likes();
        std::string settings;
        bool save = false;
        {
            std::lock_guard lock(mutex_);
            if (settings_pending_)
            {
                settings.swap(settings_);
                settings_pending_ = false;
                save = true;
            }
        }
        if (save)
        {
            const auto error =
                settings_path().empty()
                    ? "This console gave the store nowhere to keep them. They last until you "
                      "close the store."
                    : hui::save::write_atomic(settings_path(), settings);
            if (!error.empty())
            {
                Update notice;
                notice.kind = Update::Kind::notice;
                notice.message = "Settings could not be saved";
                notice.detail = error;
                publish(std::move(notice));
            }
        }
        if (!id.empty())
        {
            Update result;
            // An app the official catalog doesn't list is the custom catalog's to describe.
            const bool from_extra =
                additive && std::none_of(snapshot.entries.begin(), snapshot.entries.end(),
                                         [&](const auto &entry) { return entry.id == id; });
            if (from_extra ? extra_client.detail(extra, id, result.entry, control_, result.message)
                           : client.detail(snapshot, id, result.entry, control_, result.message))
            {
                result.entry.extra = from_extra;
                result.kind = Update::Kind::detail;
                if (!result.entry.large_icon.empty())
                {
                    auto artwork_entry = result.entry;
                    artwork_entry.icon = artwork_entry.large_icon;
                    catalog::Icons artwork(root_.empty() ? "" : root_ + "/cache/icons");
                    if (!artwork.cached(artwork_entry, result.image) && snapshot.online)
                    {
                        std::string encoded;
                        const auto response = net::fetch(artwork_entry.icon, net::Purpose::catalog,
                                                         2u << 20, encoded, control_);
                        if (response.ok())
                            artwork.store(artwork_entry, encoded, result.image);
                    }
                }
            }
            else
                result.entry.id = id;
            const bool verified_detail = result.kind == Update::Kind::detail;
            const auto page = result.entry.page;
            publish(std::move(result));
            if (verified_detail)
            {
                Update qr;
                qr.kind = Update::Kind::qr;
                qr.entry.id = id;
                if (catalog::api_url(page) && hui::encode_qr(page, qr.image))
                    publish(std::move(qr));
            }
            if (verified_detail && comments_.load() && refreshed && !from_extra &&
                !control_.cancelled.load())
            {
                // The app's latest comments, after everything the catalog itself says about it.
                likes_wanted = true;
                Update latest;
                latest.kind = Update::Kind::comments;
                latest.entry.id = id;
                std::string body;
                const auto response = net::fetch(social::thread_url(id), net::Purpose::comments,
                                                 social::kThreadLimit, body, control_);
                if (response.ok() && social::parse_thread(body, id, latest.thread))
                    publish(std::move(latest));
            }
        }
        hui::sys::sleep_us(100000);
    }
}

// Once per launch, after the catalog: is a newer ProsperoStore listed? Any
// failure means "unknown", and unknown shows nothing.
void Service::check_store_update()
{
    Update notice;
    notice.kind = Update::Kind::store_update;
    {
        std::lock_guard lock(mutex_);
        const auto version = versions_.find("PPSA99000");
        if (version == versions_.end() || !catalog::update_available(version_, version->second))
            return;
        const auto app = std::find_if(entries_.begin(), entries_.end(),
                                      [](const auto &entry) { return entry.id == "PPSA99000"; });
        if (app == entries_.end())
            return;
        notice.message = app->version;
    }
    publish(std::move(notice));
}

void *Service::icon_entry(void *self)
{
    static_cast<Service *>(self)->load_icons();
    return nullptr;
}

void Service::load_icons()
{
    catalog::Icons artwork(root_.empty() ? "" : root_ + "/cache/icons");
    std::set<std::string> failed_icons;
    std::uint64_t failure_generation = 0;
    // A picture just delivered is asked for again until the frame loop has
    // uploaded it: it is not read and decoded a second time for that.
    std::map<std::string, std::int64_t> delivered;
    while (!icon_control_.cancelled.load())
    {
        catalog::Entry entry;
        std::uint64_t generation = 0;
        bool online = false;
        {
            std::lock_guard lock(mutex_);
            generation = generation_;
            if (!icons_.empty())
            {
                const auto found =
                    std::find_if(entries_.begin(), entries_.end(),
                                 [&](const auto &item) { return item.id == icons_.front(); });
                if (found != entries_.end())
                    entry = *found;
                icons_.erase(icons_.begin());
                online = online_;
            }
        }
        if (generation != failure_generation)
        {
            failed_icons.clear();
            failure_generation = generation;
        }
        const auto key = catalog::Icons::key(entry);
        const auto moment = hui::sys::monotonic_us();
        const auto stamp = key + "#" + std::to_string(generation);
        if (const auto last = delivered.find(stamp);
            last != delivered.end() && moment - last->second < 1500000)
            continue;
        if (delivered.size() > 4096)
            delivered.clear();
        if (!key.empty() && !failed_icons.contains(key))
        {
            Update result;
            result.kind = Update::Kind::icon;
            result.entry.id = entry.id;
            result.generation = generation;
#ifdef STORE_DEVELOPMENT
            const auto started = hui::sys::monotonic_us();
#endif
            bool loaded = artwork.cached(entry, result.image);
#ifdef STORE_DEVELOPMENT
            const bool cached = loaded;
#endif
            if (!loaded && online)
            {
                std::string encoded;
                const auto response =
                    net::fetch(entry.icon, net::Purpose::catalog, 2u << 20, encoded, icon_control_);
                loaded = response.ok() && artwork.store(entry, encoded, result.image);
            }
#ifdef STORE_DEVELOPMENT
            hui::sys::log(
                "[STORE] icon id=%s cached=%d loaded=%d elapsed_ms=%llu", entry.id.c_str(), cached,
                loaded,
                static_cast<unsigned long long>((hui::sys::monotonic_us() - started) / 1000));
#endif
            if (loaded)
            {
                // The app's picture and colour come from its icon, here on the worker.
                std::uint32_t vivid = 0;
                result.ambient = make_ambient(result.image, vivid);
                result.accent = vivid ? vivid : average_colour(result.image);
                delivered[stamp] = moment;
                publish(std::move(result));
            }
            else if (online)
                failed_icons.insert(key);
        }
        if (entry.id.empty())
            hui::sys::sleep_us(10000);
    }
}
} // namespace store
