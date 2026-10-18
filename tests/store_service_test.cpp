// ProsperoStore - Artwork remains available during refresh and bounded during stalls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/service.hpp"
#include "catalog/icons.hpp"
#include "core/save_file.hpp"
#include "third_party/miniz/miniz.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;
namespace
{
std::atomic<bool> refresh_ready{false};
std::atomic<unsigned> downloads{0};
// Every request made of the likes and comments address, in order.
std::mutex comment_guard;
std::vector<std::string> comment_requests;
std::vector<std::string> asked_for_comments()
{
    std::lock_guard lock(comment_guard);
    return comment_requests;
}
const unsigned char png[] = {
    137, 80, 78, 71, 13, 10, 26,  10,  0,   0,   0, 13, 73, 72, 68, 82, 0,  0,  0,   1,   0,  0,  0,
    1,   8,  6,  0,  0,  0,  31,  21,  196, 137, 0, 0,  0,  11, 73, 68, 65, 84, 120, 156, 99, 96, 0,
    2,   0,  0,  5,  0,  1,  165, 246, 69,  64,  0, 0,  0,  0,  73, 69, 78, 68, 174, 66,  96, 130};
std::string encoded(reinterpret_cast<const char *>(png), sizeof(png));
store::catalog::Snapshot fixture()
{
    store::catalog::Snapshot result;
    result.verified = true;
    result.accepted = true;
    result.manifest.sequence = 75;
    for (unsigned i = 0; i < 16; ++i)
    {
        store::catalog::Entry entry;
        entry.id = "PPSA" + std::to_string(99000 + i);
        entry.icon = "https://prosperostore.net/icons/" + entry.id + ".png";
        entry.icon_hash = "one";
        entry.version = "1.0.10";
        result.versions[entry.id] = "01.000.010";
        result.entries.push_back(entry);
    }
    return result;
}
store::Update next(store::Service &service, store::Update::Kind kind)
{
    // The notice arrives with the catalog it follows: keep what a call didn't ask for.
    static std::vector<store::Update> waiting;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        for (auto update = waiting.begin(); update != waiting.end(); ++update)
            if (update->kind == kind)
            {
                auto found = std::move(*update);
                waiting.erase(waiting.begin(), update + 1);
                return found;
            }
        waiting.clear();
        service.take(waiting);
        if (!waiting.empty())
            continue;
        std::this_thread::sleep_for(5ms);
    }
    assert(false && "worker response timed out");
    return {};
}
} // namespace
namespace store::catalog
{
bool Client::cached(Snapshot &out, std::string &)
{
    out = fixture();
    return true;
}
bool Client::refresh(Snapshot &out, net::Control &control, std::string &)
{
    while (!refresh_ready && !control.cancelled)
        std::this_thread::sleep_for(5ms);
    out = fixture();
    out.online = true;
    return !control.cancelled;
}
bool Client::detail(const Snapshot &, const std::string &id, Entry &entry, net::Control &,
                    std::string &error)
{
    if (id == "PPSA99008")
    {
        entry = fixture().entries[8];
        entry.page = "https://prosperostore.net/app/PPSA99008/";
        return true;
    }
    if (id != "PPSA99000")
    {
        error = "offline";
        return false;
    }
    entry = fixture().entries.front();
    entry.large_icon = "https://prosperostore.net/full/PPSA99000.png";
    entry.page = "https://prosperostore.net/app/PPSA99000/";
    return true;
}
bool Client::news(const Snapshot &, std::vector<News> &out, net::Control &, std::string &)
{
    out = {{"2026-10-11-a", "2026-10-11T10:00:00Z", "Hello", "First."}};
    return true;
}
} // namespace store::catalog
namespace store::net
{
void Control::cancel()
{
    cancelled = true;
}
Response fetch(const std::string &url, Purpose purpose, std::size_t, std::string &body, Control &,
               const std::string &)
{
    if (purpose == Purpose::comments || url.starts_with(store::social::kBase))
    {
        // The comments files, from the fixtures; an app without one answers 404.
        assert(purpose == Purpose::comments && store::social::data_url(url));
        {
            std::lock_guard lock(comment_guard);
            comment_requests.push_back(url);
        }
        Response answer;
        std::ifstream file(std::string(STORE_TEST_FIXTURES) + "/" +
                               url.substr(store::social::kBase.size()),
                           std::ios::binary);
        if (!file)
        {
            answer.status = 404;
            answer.error = "The server refused the request";
            return answer;
        }
        std::ostringstream bytes;
        bytes << file.rdbuf();
        body = bytes.str();
        answer.status = 200;
        return answer;
    }
    if (url == "https://prosperostore.net/api/v1/apps/PPSA99000.json")
    {
        // The store's own listing, one release ahead of the running build.
        body = R"({"schema":3,"titleid":"PPSA99000","status":"available",)"
               R"("content_version":"01.000.010","version":"1.0.10",)"
               R"("page":"https://prosperostore.net/app/PPSA99000/"})";
        Response listed;
        listed.status = 200;
        return listed;
    }
    ++downloads;
    body = encoded;
    Response result;
    result.status = 200;
    return result;
}
Response get(const std::string &, Purpose, std::uint64_t, const Sink &, Control &,
             const std::string &)
{
    Response failure;
    failure.error = "The test has no network";
    return failure;
}
} // namespace store::net

// The installer through the service: one worker, a queue, progress the frame
// can read without waiting, and a result for every request.
static void check_installer()
{
    namespace fs = std::filesystem;
    char path[] = "/tmp/prospero-installer-XXXXXX";
    assert(mkdtemp(path));
    const fs::path root(path);
    fs::create_directories(root / "apps");
    const std::string id = "PPSA99500", location = (root / "apps").string();
    // The smallest valid app archive.
    mz_zip_archive archive{};
    assert(mz_zip_writer_init_heap(&archive, 0, 0));
    const std::string program(300000, 'p'),
        param = "{\"titleId\":\"" + id + "\",\"contentVersion\":\"01.000.001\"}";
    assert(mz_zip_writer_add_mem(&archive, (id + "/eboot.bin").c_str(), program.data(),
                                 program.size(), 6));
    assert(mz_zip_writer_add_mem(&archive, (id + "/sce_sys/param.json").c_str(), param.data(),
                                 param.size(), 6));
    void *bytes = nullptr;
    std::size_t size = 0;
    assert(mz_zip_writer_finalize_heap_archive(&archive, &bytes, &size));
    const std::string artifact(static_cast<const char *>(bytes), size);
    mz_free(bytes);
    mz_zip_writer_end(&archive);

    std::atomic<bool> hold{false};
    store::install::Environment environment;
    environment.root = (root / "state").string();
    environment.work = (root / "work").string();
    environment.self = "PPSA99000";
    environment.policy.roots = {location};
    environment.fetch = [&](const std::string &, std::uint64_t, const store::net::Sink &sink,
                            store::net::Control &control)
    {
        store::net::Response response;
        sink(std::string_view(artifact).substr(0, artifact.size() / 2));
        while (hold && !control.cancelled)
            std::this_thread::sleep_for(5ms);
        if (control.cancelled || !sink(std::string_view(artifact).substr(artifact.size() / 2)))
            response.error = "Cancelled";
        else
            response.status = 200;
        return response;
    };
    store::catalog::Entry entry;
    entry.id = id;
    entry.name = "Example";
    entry.status = "available";
    entry.format = "zip";
    entry.version = "v1";
    entry.content_version = "01.000.001";
    entry.artifact = "https://github.com/example/app/releases/download/v1/" + id + ".zip";
    entry.digest = store::catalog::sha256(artifact);
    entry.size = artifact.size();

    store::Service service("");
    assert(!service.request_install(entry, location)); // No installer: nothing is queued.
    service.installer = true;
    service.installer_environment = environment;
    refresh_ready = true;
    assert(service.start());
    const auto ask = [&](const auto &request)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!request() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
    };
    // A held download shows its progress, and a cancel leaves nothing behind.
    hold = true;
    ask([&] { return service.request_install(entry, location); });
    store::JobView view;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline &&
           !(service.job(view) && view.id == id && view.done > 0))
        std::this_thread::sleep_for(1ms);
    assert(view.id == id && view.phase == store::install::Phase::downloading);
    assert(view.done == artifact.size() / 2 && view.total == artifact.size());
    ask([&] { return service.cancel_job(id); });
    auto done = next(service, store::Update::Kind::job);
    assert(!done.ok && done.entry.id == id && done.message == "Example: cancelled");
    assert(!fs::exists(root / "apps" / id) && !fs::exists(root / "state/journal.json"));
    // The next request is not cancelled by the previous one's cancel.
    hold = false;
    ask([&] { return service.request_install(entry, location); });
    done = next(service, store::Update::Kind::job);
    assert(done.ok && done.message == "Example installed");
    assert(fs::file_size(root / "apps" / id / "eboot.bin") == program.size());
    assert(fs::exists(root / "state/receipts" / (id + ".json")));
    // Without a running-app check an uninstall is refused, and says so.
    ask([&] { return service.request_uninstall(id, location); });
    done = next(service, store::Update::Kind::job);
    assert(!done.ok && done.detail.find("running") != std::string::npos);
    assert(fs::exists(root / "apps" / id / "eboot.bin"));
    assert(service.job(view) && view.id.empty() && view.waiting.empty());
    service.stop();
    fs::remove_all(root);
}

int main()
{
    char path[] = "/tmp/prospero-service-XXXXXX";
    assert(mkdtemp(path));
    const std::string root = path;
    assert(std::filesystem::create_directory(root + "/cache"));
    store::catalog::Icons cache(root + "/cache/icons");
    hui::Image image;
    assert(cache.store(fixture().entries.front(), encoded, image));
    assert(cache.cached(fixture().entries.front(), image));
    store::Service service(root, "01.000.000");
    assert(service.start());
    const auto cached = next(service, store::Update::Kind::catalog);
    assert(cached.generation == 1 && !cached.snapshot.online);
    assert(service.request_icons({"PPSA99000"}));
    const auto icon = next(service, store::Update::Kind::icon);
    assert(icon.generation == 1 && icon.image.width == 1 && downloads == 0);
    refresh_ready = true;
    const auto online = next(service, store::Update::Kind::catalog);
    assert(online.generation == 2 && online.snapshot.online);
    // A newer store in the catalog becomes one offer; nothing is downloaded for it.
    const auto notice = next(service, store::Update::Kind::store_update);
    assert(notice.message == "1.0.10");
    // The catalog's news follows the catalog it came with.
    const auto feed = next(service, store::Update::Kind::news);
    assert(feed.news.size() == 1 && feed.news.front().title == "Hello");
    std::vector<std::string> ids;
    for (const auto &entry : fixture().entries)
        ids.push_back(entry.id);
    assert(service.request_icons(ids));
    std::this_thread::sleep_for(1500ms); // Fill the eight-result queue without a render consumer.
    std::set<std::string> received;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (received.size() != ids.size() && std::chrono::steady_clock::now() < deadline)
    {
        std::vector<store::Update> updates;
        service.take(updates);
        assert(updates.size() <= 8);
        for (const auto &update : updates)
            if (update.kind == store::Update::Kind::icon)
            {
                assert(update.generation == 2 && update.image.width == 1);
                received.insert(update.entry.id);
            }
        std::this_thread::sleep_for(5ms);
    }
    assert(received.size() == ids.size() && downloads == 15);
    assert(service.request_detail("PPSA99000"));
    const auto qr = next(service, store::Update::Kind::qr);
    assert(qr.entry.id == "PPSA99000" && qr.image.width >= 116 && downloads == 16);
    assert(service.request_detail("PPSA99000"));
    assert(next(service, store::Update::Kind::detail).image.width == 1 && downloads == 16);
    assert(service.request_detail("PPSA99001"));
    const auto failure = next(service, store::Update::Kind::error);
    assert(failure.entry.id == "PPSA99001" && failure.message == "offline");
    // Likes and comments. The setting is off until now: through the catalog, the icons and
    // three app pages, nothing was asked of their address.
    assert(asked_for_comments().empty());
    service.show_comments(true);
    const auto likes = next(service, store::Update::Kind::likes);
    assert(likes.likes.at("PPSA99008").likes == 41 && likes.likes.at("PPSA99008").comments == 37);
    assert(asked_for_comments() == std::vector<std::string>{store::social::index_url()});
    // An app's page asks for that app's comments, after its verified details.
    assert(service.request_detail("PPSA99008"));
    const auto thread = next(service, store::Update::Kind::comments);
    assert(thread.entry.id == "PPSA99008" && thread.thread.counts.comments == 37);
    assert(thread.thread.latest.size() == store::social::kComments);
    assert(asked_for_comments().back() == store::social::thread_url("PPSA99008"));
    // An app without a file, and one whose details could not be verified: no comments, no error.
    assert(service.request_detail("PPSA99000"));
    assert(next(service, store::Update::Kind::detail).entry.id == "PPSA99000");
    assert(service.request_detail("PPSA99001"));
    assert(next(service, store::Update::Kind::error).entry.id == "PPSA99001");
    std::this_thread::sleep_for(300ms);
    {
        std::vector<store::Update> rest;
        service.take(rest);
        for (const auto &update : rest)
            assert(update.kind != store::Update::Kind::comments &&
                   update.kind != store::Update::Kind::error);
        const auto asked = asked_for_comments();
        assert(asked.size() == 3 && asked.back() == store::social::thread_url("PPSA99000"));
    }
    // Switched off again: the next page asks for nothing.
    service.show_comments(false);
    assert(service.request_detail("PPSA99008"));
    assert(next(service, store::Update::Kind::detail).entry.id == "PPSA99008");
    std::this_thread::sleep_for(300ms);
    assert(asked_for_comments().size() == 3);
    // On again, however quickly: the counts are read anew.
    service.show_comments(true);
    service.show_comments(false);
    service.show_comments(true);
    assert(next(service, store::Update::Kind::likes).likes.contains("PPSA99008"));
    assert(asked_for_comments().size() == 4 &&
           asked_for_comments().back() == store::social::index_url());
    service.show_comments(false);
    assert(service.request_icons(ids));
    std::this_thread::sleep_for(1500ms);
    const auto before = std::chrono::steady_clock::now();
    service.stop();
    assert(std::chrono::steady_clock::now() - before < 1s);
    // Closing during the initial network refresh must still persist next-launch settings.
    refresh_ready = false;
    store::Service closing(root);
    assert(closing.start());
    const std::string settings = "catalog_url=https://dev.example/api/v1/\nverify_signatures=0\n";
    while (!closing.save_settings(settings))
        std::this_thread::sleep_for(1ms);
    closing.stop();
    std::string saved;
    assert(hui::save::read_file(root + "/settings.txt", &saved) && saved == settings);
    // A store without a folder of its own (not elevated) still keeps settings where it was
    // told it can write them.
    store::Service sandboxed("");
    sandboxed.settings_file = root + "/sandboxed-settings.txt";
    assert(sandboxed.start());
    while (!sandboxed.save_settings("debug=1\n"))
        std::this_thread::sleep_for(1ms);
    sandboxed.stop();
    saved.clear();
    assert(hui::save::read_file(root + "/sandboxed-settings.txt", &saved) && saved == "debug=1\n");
    std::filesystem::remove_all(root);
    check_installer();
}
