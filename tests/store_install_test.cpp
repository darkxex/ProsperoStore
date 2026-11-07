// ProsperoStore - Hostile archives, refusals, and a power cut at every transaction step.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/archive.hpp"
#include "install/files.hpp"
#include "install/transaction.hpp"
#include "install/worker.hpp"
#include "system/inventory.hpp"
#include "system/running.hpp"
#include "third_party/miniz/miniz.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace store;
namespace
{
const std::string kId = "PPSA99500";
using Files = std::vector<std::pair<std::string, std::string>>;

std::string metadata(const std::string &id, const std::string &version)
{
    return "{\"titleId\":\"" + id + "\",\"contentVersion\":\"" + version +
           "\",\"localizedParameters\":{\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":"
           "\"Example App\"}}}";
}
Files app(const std::string &version, const std::string &id = kId)
{
    // Half incompressible, half not: both the stored and the deflated paths run.
    std::string program(200000, 'e');
    std::uint32_t state = 12345;
    for (std::size_t i = 0; i < program.size() / 2; ++i)
        program[i] = static_cast<char>((state = state * 1664525u + 1013904223u) >> 24);
    return {{id + "/", ""},
            {id + "/eboot.bin", program + version},
            {id + "/sce_sys/", ""},
            {id + "/sce_sys/param.json", metadata(id, version)},
            {id + "/assets/deep/readme.txt", "hello " + version}};
}
std::string zip(const Files &files)
{
    mz_zip_archive archive{};
    assert(mz_zip_writer_init_heap(&archive, 0, 0));
    for (const auto &[name, data] : files)
        assert(mz_zip_writer_add_mem(&archive, name.c_str(), data.data(), data.size(), 6));
    void *bytes = nullptr;
    std::size_t size = 0;
    assert(mz_zip_writer_finalize_heap_archive(&archive, &bytes, &size));
    std::string result(static_cast<const char *>(bytes), size);
    mz_free(bytes);
    mz_zip_writer_end(&archive);
    return result;
}
std::uint32_t u32(const std::string &bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}
std::uint16_t u16(const std::string &bytes, std::size_t offset)
{
    std::uint16_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 2);
    return value;
}
// Offset of the index-th header in the archive's directory.
std::size_t central(const std::string &bytes, unsigned index)
{
    std::size_t offset = u32(bytes, bytes.size() - 22 + 16);
    for (unsigned i = 0; i < index; ++i)
        offset += 46 + u16(bytes, offset + 28) + u16(bytes, offset + 30) + u16(bytes, offset + 32);
    assert(bytes.compare(offset, 4, "PK\x01\x02") == 0);
    return offset;
}
void put(const fs::path &path, const std::string &bytes)
{
    std::ofstream(path, std::ios::binary) << bytes;
}
std::string get(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}
// Every path below root with its content: equal exactly when two trees are.
std::string tree(const fs::path &root)
{
    std::map<std::string, std::string> entries;
    if (fs::exists(root))
        for (const auto &entry : fs::recursive_directory_iterator(root))
            entries[entry.path().lexically_relative(root).string()] =
                entry.is_regular_file() ? get(entry.path()) : "<dir>";
    std::string result;
    for (const auto &[name, data] : entries)
        result += name + "\n" + data + "\n";
    return result;
}
std::string expected_tree(const Files &files)
{
    std::map<std::string, std::string> entries;
    for (auto [name, data] : files)
    {
        const bool directory = name.ends_with('/');
        if (directory)
            name.pop_back();
        if (name.size() <= kId.size())
            continue;
        name = name.substr(kId.size() + 1);
        for (auto slash = name.find('/'); slash != name.npos; slash = name.find('/', slash + 1))
            entries[name.substr(0, slash)] = "<dir>";
        entries[name] = directory ? "<dir>" : data;
    }
    std::string result;
    for (const auto &[name, data] : entries)
        result += name + "\n" + data + "\n";
    return result;
}

void check_archives(const fs::path &root)
{
    const auto file = (root / "a.zip").string();
    const auto refused = [&](const std::string &bytes)
    {
        put(file, bytes);
        install::ArchiveInfo info;
        std::string error;
        const bool ok = install::inspect_archive(file, kId, info, error);
        assert(ok || !error.empty());
        return !ok;
    };
    const auto good = app("01.000.001");
    put(file, zip(good));
    install::ArchiveInfo info;
    std::string error;
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    assert(info.unpacked == good[1].second.size() + good[3].second.size() + good[4].second.size());
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> written{0};
    const auto out = (root / "out").string();
    assert(install::extract_archive(file, kId, out, cancelled, written, error));
    assert(written == info.unpacked && tree(out) == expected_tree(good));
    // Every file and folder is open to all, as the console needs to start the app.
    for (const auto &item : fs::recursive_directory_iterator(out))
        assert((fs::status(item.path()).permissions() & fs::perms::all) == fs::perms::all);
    assert((fs::status(out).permissions() & fs::perms::all) == fs::perms::all);
    // The destination must be new: nothing is ever unpacked over existing files.
    assert(!install::extract_archive(file, kId, out, cancelled, written, error));
    assert(install::remove_tree(out) && !fs::exists(out));

    assert(refused("not a zip"));
    assert(refused(zip(good).substr(0, zip(good).size() / 2)));
    // What lies outside the app's folder is ignored, whatever it is called.
    auto files = good;
    files.push_back({"README.md", "beside the folder"});
    files.push_back({"other/../escape.txt", "never unpacked"});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    assert(install::extract_archive(file, kId, out, cancelled, written, error));
    assert(tree(out) == expected_tree(good) && install::remove_tree(out));
    // The app at the top of the archive, and inside a release folder.
    for (const std::string &wrap :
         {std::string(), "Release-1.0/" + kId + "/", std::string("Release-1.0/")})
    {
        files.clear();
        for (auto [name, data] : good)
            if (name.size() > kId.size() + 1)
                files.push_back({wrap + name.substr(kId.size() + 1), data});
        files.push_back({wrap.empty() ? "notes.txt" : "notes.txt", "loose"});
        put(file, zip(files));
        written = 0;
        assert(install::inspect_archive(file, kId, info, error));
        assert(install::extract_archive(file, kId, out, cancelled, written, error));
        if (wrap.empty()) // Everything at the top belongs to the app, the note too.
            assert(get(fs::path(out) / "notes.txt") == "loose" && info.files == 4);
        else
            assert(tree(out) == expected_tree(good) && info.files == 3);
        assert(install::remove_tree(out));
    }
    // Of two apps side by side the one named after the title is taken; two
    // that are both strangers can't be told apart; a nested copy is not the app.
    files = good;
    for (const auto &[name, data] : app("01.000.001", "PPSA99501"))
        files.push_back({name, data});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 3);
    files = app("01.000.001", "PPSA99501");
    for (const auto &[name, data] : app("01.000.001", "PPSA99502"))
        files.push_back({name, data});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/data/sample/sce_sys/param.json", "{}"});
    put(file, zip(files));
    assert(install::inspect_archive(file, kId, info, error) && info.files == 4);
    files = good;
    files.erase(files.begin() + 1);
    assert(refused(zip(files))); // No eboot.bin.
    files = good;
    files.push_back({kId + "/../escape.txt", "parent path"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/a//b.txt", "empty component"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/./b.txt", "dot component"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/eboot.bin", "duplicate"});
    assert(refused(zip(files)));
    files = good;
    files.push_back({kId + "/" + std::string(install::kArchivePath, 'n'), "long name"});
    assert(refused(zip(files)));
    files = good;
    files.erase(files.begin() + 3);
    assert(refused(zip(files))); // No sce_sys/param.json.

    // Directory entries patched the way a hostile archive would be written.
    auto bytes = zip(good);
    auto header = central(bytes, 1);
    bytes[header + 46] = '/'; // An absolute path.
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 46 + 4] = '\\';
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 46 + 4] = '\x01';
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 5] = 3; // Written on Unix, as a symbolic link.
    const std::uint32_t link = 0120777u << 16;
    std::memcpy(bytes.data() + header + 38, &link, 4);
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 8] |= 1; // Encrypted.
    assert(refused(bytes));
    bytes = zip(good);
    header = central(bytes, 1);
    bytes[header + 10] = 12; // A compression method the reader doesn't support.
    assert(refused(bytes));

    // A directory that understates a file's size passes inspection by design:
    // the size is enforced while unpacking, and nothing more is ever written.
    bytes = zip(good);
    header = central(bytes, 1);
    const std::uint32_t lie = 1000;
    std::memcpy(bytes.data() + header + 24, &lie, 4);
    put(file, bytes);
    written = 0;
    if (install::inspect_archive(file, kId, info, error))
    {
        assert(!install::extract_archive(file, kId, out, cancelled, written, error));
        assert(!fs::exists(out + "/eboot.bin") || fs::file_size(out + "/eboot.bin") <= lie);
        assert(install::remove_tree(out));
    }
    // A cancelled unpack stops and reports it.
    put(file, zip(good));
    cancelled = true;
    assert(!install::extract_archive(file, kId, out, cancelled, written, error));
    assert(error == "Cancelled" && install::remove_tree(out));

    // remove_tree never follows a link out of the tree it was given.
    fs::create_directories(root / "keep");
    put(root / "keep/file.txt", "kept");
    fs::create_directories(root / "junk/sub");
    fs::create_directory_symlink(root / "keep", root / "junk/sub/link");
    assert(install::remove_tree((root / "junk").string()));
    assert(!fs::exists(root / "junk") && get(root / "keep/file.txt") == "kept");
    fs::remove(file);
}

struct Fixture
{
    fs::path root, location, state, work;
    install::Environment environment;
    std::string artifact;
    unsigned requests = 0, failures = 0;
    int running = 0;
    explicit Fixture(const fs::path &base, const std::string &name)
        : root(base / name), location(root / "apps"), state(root / "state"), work(root / "work")
    {
        fs::remove_all(root);
        fs::create_directories(location);
        fs::create_directories(state);
        environment.root = state.string();
        environment.self = "PPSA99000";
        environment.work = work.string();
        environment.registered = (root / "user").string();
        environment.data = (root / "data").string();
        fs::create_directories(environment.data);
        environment.policy.roots = {location.string()};
        environment.now = [] { return std::string("2026-10-02T00:00:00Z"); };
        environment.wait = [](unsigned, const std::atomic<bool> &) {};
        environment.running = [this](const std::string &) { return running; };
        environment.fetch = [this](const std::string &, std::uint64_t limit, const net::Sink &sink,
                                   net::Control &control)
        {
            ++requests;
            net::Response response;
            if (failures)
            {
                --failures;
                sink(std::string_view(artifact).substr(0, artifact.size() / 2));
                response.error = "The network request failed (curl 56)";
                return response;
            }
            for (std::size_t offset = 0; offset < artifact.size(); offset += 65536)
            {
                const auto chunk = std::string_view(artifact).substr(offset, 65536);
                if (control.cancelled.load() || offset + chunk.size() > limit || !sink(chunk))
                {
                    response.error = control.cancelled.load() ? "Cancelled" : "Refused";
                    return response;
                }
            }
            response.status = 200;
            return response;
        };
    }
    install::Request request(const std::string &version)
    {
        artifact = zip(app(version));
        install::Request result;
        result.entry.id = kId;
        result.entry.status = "available";
        result.entry.format = "zip";
        result.entry.version = "v" + version;
        result.entry.content_version = version;
        result.entry.artifact =
            "https://github.com/example/app/releases/download/v1/" + kId + ".zip";
        result.entry.digest = catalog::sha256(artifact);
        result.entry.size = artifact.size();
        result.location = location.string();
        return result;
    }
    install::Result apply(const install::Request &request)
    {
        net::Control control;
        install::Progress progress;
        return install::apply(environment, request, control, progress);
    }
    install::Result uninstall()
    {
        install::Progress progress;
        return install::uninstall(environment, kId, location.string(), progress);
    }
    fs::path target() const
    {
        return location / kId;
    }
    // "", or the version the inventory reports as managed by the store.
    std::string managed() const
    {
        std::atomic<bool> cancelled{false};
        const auto inventory =
            system::scan_installed(environment.policy, (state / "receipts").string(), cancelled);
        for (const auto &app : inventory.apps)
            if (app.id == kId && app.managed)
                return app.version;
        return {};
    }
    // No journal and nothing left in the work folders.
    bool settled() const
    {
        for (const auto *folder : {"staging", "backup", "trash"})
            if (fs::exists(work / folder) && !fs::is_empty(work / folder))
                return false;
        return !fs::exists(state / "journal.json") && !fs::exists(state / "journal.json.tmp");
    }
};

void check_transactions(const fs::path &base)
{
    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    Fixture f(base, "basic");
    auto request = f.request("01.000.001");
    auto result = f.apply(request);
    assert(result.ok && result.operation == "install" && result.version == "01.000.001");
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());
    assert(get(f.state / "receipts" / (kId + ".json")).find("\"releaseTag\":\"v01.000.001\"") !=
           std::string::npos);

    // The same version again is not an update, and nothing is downloaded for it.
    f.requests = 0;
    result = f.apply(request);
    assert(!result.ok && f.requests == 0 && tree(f.target()) == v1 && f.settled());

    // Updates need a definite "not running".
    request = f.request("01.000.002");
    f.running = 1;
    result = f.apply(request);
    assert(!result.ok && result.error == "Close the app first" && f.requests == 0);
    f.running = -1;
    assert(!f.apply(request).ok && f.requests == 0);
    f.environment.running = nullptr;
    assert(!f.apply(request).ok && f.requests == 0 && tree(f.target()) == v1);
    f.environment.running = [&f](const std::string &) { return f.running; };
    // Running is asked again after the download, right before the swap.
    unsigned asked = 0;
    f.environment.running = [&asked](const std::string &) { return asked++ == 0 ? 0 : 1; };
    result = f.apply(request);
    assert(!result.ok && result.error == "Close the app first" && asked == 2);
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());
    f.environment.running = [&f](const std::string &) { return f.running; };
    f.running = 0;
    // The console's own copies of the app's sce_sys, as made at registration.
    fs::create_directories(f.root / "user/appmeta" / kId);
    fs::create_directories(f.root / "user/app" / kId / "sce_sys");
    put(f.root / "user/appmeta" / kId / "param.json", "old");
    put(f.root / "user/app" / kId / "sce_sys/param.json", "old");
    put(f.root / "user/app" / kId / "icon0.png", "old icon");
    result = f.apply(request);
    assert(result.ok && result.operation == "update" && result.version == "01.000.002");
    assert(get(f.root / "user/appmeta" / kId / "param.json") == metadata(kId, "01.000.002"));
    assert(get(f.root / "user/app" / kId / "sce_sys/param.json") == metadata(kId, "01.000.002"));
    assert(get(f.root / "user/app" / kId / "icon0.png") == "old icon"); // The app ships none.
    assert(tree(f.target()) == v2 && f.managed() == "01.000.002" && f.settled());
    // The previous version is kept whole, with what its owner had put inside it.
    assert(result.kept_at == (f.work / "previous" / kId).string());
    assert(tree(f.work / "previous" / kId) == v1);
    // The next update of the app replaces that copy with the version it replaces.
    fs::create_directories(f.target() / "assets/games");
    put(f.target() / "assets/games/mine.bin", "my own file");
    result = f.apply(f.request("01.000.003"));
    assert(result.ok && result.operation == "update" && result.version == "01.000.003");
    assert(!fs::exists(f.target() / "assets/games/mine.bin"));
    assert(get(f.work / "previous" / kId / "assets/games/mine.bin") == "my own file");
    assert(get(f.work / "previous" / kId / "sce_sys/param.json") == metadata(kId, "01.000.002"));
    assert(f.settled());
    // Back to the version the checks below start from.
    assert(f.uninstall().ok);
    assert(f.apply(f.request("01.000.001")).ok && f.apply(f.request("01.000.002")).ok);

    // A listing that claims a newer version than the file holds is not an update.
    request = f.request("01.000.002");
    request.entry.content_version = "01.000.003";
    result = f.apply(request);
    assert(!result.ok && tree(f.target()) == v2 && f.managed() == "01.000.002" && f.settled());

    // Uninstall: refused while running or unknown, then complete.
    f.running = 1;
    assert(!f.uninstall().ok && tree(f.target()) == v2);
    f.running = 0;
    result = f.uninstall();
    assert(result.ok && !fs::exists(f.target()) && f.managed().empty() && f.settled());
    assert(!fs::exists(f.state / "receipts" / (kId + ".json")));
    assert(!f.uninstall().ok);

    // Refusals before anything is requested.
    request = f.request("01.000.001");
    const auto refused = [&](install::Request changed)
    {
        f.requests = 0;
        const auto outcome = f.apply(changed);
        return !outcome.ok && !outcome.error.empty() && f.requests == 0 &&
               !fs::exists(f.target()) && f.settled();
    };
    auto changed = request;
    changed.entry.format = "ffpfsc";
    assert(refused(changed));
    changed = request;
    changed.entry.status = "coming_soon";
    assert(refused(changed));
    changed = request;
    changed.entry.artifact = "https://example.com/" + kId + ".zip";
    assert(refused(changed));
    changed = request;
    changed.entry.artifact = "https://release-assets.githubusercontent.com/x.zip";
    assert(refused(changed)); // Only reachable by redirect from github.com.
    // The custom catalog the user set may serve its files from its own host.
    f.environment.catalog = "https://Example.com/store/api/v1/";
    changed = request;
    changed.entry.artifact = "https://example.com/files/" + kId + ".zip";
    f.requests = 0;
    assert(f.apply(changed).ok && f.requests == 1);
    assert(f.uninstall().ok && !fs::exists(f.target()) && f.settled());
    changed.entry.artifact = "https://cdn.example.com/" + kId + ".zip";
    assert(refused(changed));
    f.environment.catalog = catalog::kDefaultApi;
    changed.entry.artifact = "https://prosperostore.net/" + kId + ".zip";
    assert(refused(changed)); // The official catalog lists files on GitHub only.
    changed.entry.artifact = "https://example.com/files/" + kId + ".zip";
    assert(refused(changed)); // No custom catalog in use: its host is no longer allowed.
    f.requests = 0;
    assert(f.apply(request).ok && f.requests == 1); // GitHub still is.
    assert(f.uninstall().ok && !fs::exists(f.target()) && f.settled());
    f.environment.catalog.clear();
    changed = request;
    changed.entry.digest = "abc";
    assert(refused(changed));
    changed = request;
    changed.entry.size = catalog::kArtifactLimit + 1;
    assert(refused(changed));
    changed = request;
    changed.minimum_version = "01.000.002";
    assert(refused(changed));
    changed = request;
    changed.entry.id = "PPSA99000";
    assert(refused(changed));
    changed = request;
    changed.location = f.root.string();
    assert(refused(changed)); // Not a scanned location.
    changed = request;
    changed.location = f.location.string() + "/../apps";
    assert(refused(changed));
    f.environment.policy.roots.push_back(f.work.string());
    f.environment.policy.depth = 2;
    assert(refused(request)); // The work folder would be scanned.
    f.environment.policy.roots.pop_back();
    f.environment.policy.depth = 1;
    f.environment.space = [](const std::string &, std::uint64_t &bytes)
    {
        bytes = 1000;
        return true;
    };
    result = f.apply(request);
    assert(!result.ok && result.error.starts_with("Not enough space") && f.requests == 0);
    assert(!fs::exists(f.target()) && f.settled());
    // Room for the download, not for what it unpacks to.
    f.environment.space = [&](const std::string &, std::uint64_t &bytes)
    {
        bytes = f.artifact.size() + (17u << 20);
        return true;
    };
    result = f.apply(request);
    assert(!result.ok && result.error.starts_with("Not enough space") && f.requests == 1);
    assert(!fs::exists(f.target()) && f.settled());
    f.environment.space = nullptr;

    // A file that isn't the listed one is never unpacked, and isn't retried.
    changed = request;
    changed.entry.digest = std::string(64, 'a');
    f.requests = 0;
    result = f.apply(changed);
    assert(!result.ok && result.error == "The file doesn't match the listing" && f.requests == 1);
    assert(!fs::exists(f.target()) && f.settled());
    changed = request;
    changed.entry.size += 1;
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    changed = request;
    changed.entry.size -= 1; // The transport's limit is the listed size.
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    // A listed, correctly hashed archive that holds another title's app.
    changed = request;
    f.artifact = zip(app("01.000.001", "PPSA99501"));
    changed.entry.digest = catalog::sha256(f.artifact);
    changed.entry.size = f.artifact.size();
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());
    // The folder is right but its param.json names another title.
    auto files = app("01.000.001");
    files[3].second = metadata("PPSA99501", "01.000.001");
    f.artifact = zip(files);
    changed.entry.digest = catalog::sha256(f.artifact);
    changed.entry.size = f.artifact.size();
    assert(!f.apply(changed).ok && !fs::exists(f.target()) && f.settled());

    // A broken connection is retried from the start, and the result still verifies.
    request = f.request("01.000.001");
    f.requests = 0;
    f.failures = 2;
    result = f.apply(request);
    assert(result.ok && f.requests == 3 && tree(f.target()) == v1 && f.settled());
    assert(f.uninstall().ok);
    f.requests = 0;
    f.failures = 3;
    result = f.apply(request);
    assert(!result.ok && f.requests == 3 && !fs::exists(f.target()) && f.settled());
    f.failures = 0;

    // Cancel during the download.
    {
        net::Control control;
        install::Progress progress;
        const auto fetch = f.environment.fetch;
        f.environment.fetch = [&](const std::string &url, std::uint64_t limit,
                                  const net::Sink &sink, net::Control &inner)
        {
            return fetch(
                url, limit,
                [&](std::string_view chunk)
                {
                    inner.cancelled = true;
                    return sink(chunk);
                },
                inner);
        };
        result = install::apply(f.environment, request, control, progress);
        assert(!result.ok && result.error == "Cancelled" && !fs::exists(f.target()));
        assert(f.settled());
        f.environment.fetch = fetch;
    }

    // An app the store didn't install is never touched.
    fs::create_directories(f.target() / "sce_sys");
    put(f.target() / "sce_sys/param.json", metadata(kId, "01.000.000"));
    const auto foreign = tree(f.target());
    f.requests = 0;
    result = f.apply(f.request("01.000.002"));
    assert(!result.ok && f.requests == 0 && tree(f.target()) == foreign && f.settled());
    assert(!f.uninstall().ok && tree(f.target()) == foreign);
    // Until its owner hands it over: then it is the store's to update and remove.
    assert(!install::adopt(f.environment, "PPSA99501", f.location.string()).ok);
    assert(!install::adopt(f.environment, kId, f.root.string()).ok);
    auto adopted = install::adopt(f.environment, kId, f.location.string());
    assert(adopted.ok && adopted.version == "01.000.000" && f.managed() == "01.000.000");
    assert(tree(f.target()) == foreign); // Nothing but the receipt was written.
    result = f.apply(f.request("01.000.002"));
    assert(result.ok && result.operation == "update" && tree(f.target()) == v2);
    assert(f.uninstall().ok && !fs::exists(f.target()) && f.settled());
    fs::create_directories(f.target() / "sce_sys");
    put(f.target() / "sce_sys/param.json", metadata(kId, "01.000.000"));
    // Nor is one whose receipt names another version or location.
    put(f.state / "receipts" / (kId + ".json"),
        catalog::format_receipt(
            {kId, f.location.string(), "01.000.009", "v", std::string(64, 'a'), "now"}));
    assert(!f.apply(f.request("01.000.010")).ok && !f.uninstall().ok);
    assert(tree(f.target()) == foreign);
    fs::remove_all(f.target());
    put(f.target(), "a file where the app folder would go");
    assert(!f.apply(f.request("01.000.001")).ok && fs::is_regular_file(f.target()));
    fs::remove(f.target());

    // A journal that can't be read turns installing off instead of guessing.
    put(f.state / "journal.json", "{\"schema\":1,\"operation\":\"install\"}");
    assert(!install::recover(f.environment).ok);
    assert(!f.apply(f.request("01.000.001")).ok && !fs::exists(f.target()));
    put(f.state / "journal.json",
        catalog::format_journal({"install", "swap", kId, f.location.string(), "", "", ""}));
    assert(!install::recover(f.environment).ok);
    fs::remove(f.state / "journal.json");
    assert(install::recover(f.environment).ok);
}

// A quick update must leave new app folders accessible just like a clean install.
void check_quick_update(const fs::path &base)
{
    Fixture f(base, "quick-update");
    assert(f.apply(f.request("01.000.001")).ok);
    put(f.target() / "assets/personal.txt", "keep my settings");
    const auto personal_permissions = fs::status(f.target() / "assets/personal.txt").permissions();
    auto request = f.request("01.000.002");
    auto files = app("01.000.002");
    files.push_back({kId + "/assets/new/nested/file.txt", "new content"});
    files.push_back({kId + "/assets/empty/nested/", ""});
    f.artifact = zip(files);
    request.entry.digest = catalog::sha256(f.artifact);
    request.entry.size = f.artifact.size();
    request.overwrite = true;
    const auto old_mask = umask(0077);
    const auto result = f.apply(request);
    umask(old_mask);
    assert(result.ok && result.operation == "update" && result.version == "01.000.002");
    assert(f.managed() == "01.000.002" && f.settled());
    assert(get(f.target() / "sce_sys/param.json") == metadata(kId, "01.000.002"));
    assert(get(f.target() / "assets/new/nested/file.txt") == "new content");
    assert(fs::is_empty(f.target() / "assets/empty/nested"));
    assert(get(f.target() / "assets/personal.txt") == "keep my settings");
    assert(fs::status(f.target() / "assets/personal.txt").permissions() == personal_permissions);
    for (const auto &item : fs::recursive_directory_iterator(f.target()))
        if (item.path() != f.target() / "assets/personal.txt")
            assert((fs::status(item.path()).permissions() & fs::perms::all) == fs::perms::all);
    assert(result.kept_at.empty() && !fs::exists(f.work / "previous" / kId));
}

// Legacy Eden data must remain discoverable across Store's whole-folder update.
void check_eden_migration(const fs::path &base)
{
    const std::string id = "PPSA99008";
    const auto request = [&](Fixture &f, const std::string &version)
    {
        auto r = f.request(version);
        f.artifact = zip(app(version, id));
        r.entry.id = id;
        r.entry.digest = catalog::sha256(f.artifact);
        r.entry.size = f.artifact.size();
        return r;
    };
    const auto seed = [&](Fixture &f)
    {
        assert(f.apply(request(f, "01.000.080")).ok);
        const auto assets = f.location / id / "assets";
        for (const auto *folder :
             {"keys", "firmware", "roms", "updates", "mods", "ryujinx", "save-import"})
        {
            fs::create_directories(assets / folder);
            put(assets / folder / "personal.bin", std::string(folder) + " data");
        }
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        fs::create_directories(data / "config");
        fs::create_directories(data / "user");
        put(data / "user/save.bin", "keep my save");
        put(data / "config/prosperoeden.json",
            "{\"game_files\":\"" + data.string() + "\",\"audio\":{\"volume\":42}}");
        // Eden may have already created empty default folders before updating.
        fs::create_directories(data / "roms");
    };
    const auto check_data = [&](const Fixture &f)
    {
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        for (const auto *folder :
             {"keys", "firmware", "roms", "updates", "mods", "ryujinx", "save-import"})
            assert(get(data / folder / "personal.bin") == std::string(folder) + " data");
        assert(get(data / "user/save.bin") == "keep my save");
        assert(get(data / "config/prosperoeden.json").find("42") != std::string::npos);
    };
    {
        Fixture f(base, "eden-migrate");
        seed(f);
        const auto config =
            get(fs::path(f.environment.data) / "prosperoeden/config/prosperoeden.json");
        const auto result = f.apply(request(f, "01.000.100"));
        assert(result.ok && f.settled());
        check_data(f);
        assert(get(fs::path(f.environment.data) / "prosperoeden/config/prosperoeden.json") ==
               config);
        assert(!fs::exists(f.work / "previous" / id / "assets/roms"));
        // Future updates do not touch migrated data or save files.
        assert(f.apply(request(f, "01.000.110")).ok);
        check_data(f);
    }
    for (const std::string issue : {"conflict", "custom", "bad-config", "link", "older-backup"})
    {
        Fixture f(base, "eden-refuse-" + issue);
        seed(f);
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        if (issue == "conflict")
            put(data / "roms/existing.nsp", "do not replace");
        else if (issue == "custom")
            put(data / "config/prosperoeden.json", "{\"game_files\":\"/mnt/usb0/my-games\"}");
        else if (issue == "bad-config")
            put(data / "config/prosperoeden.json", "broken JSON");
        else if (issue == "link")
        {
            fs::create_directories(f.root / "outside");
            fs::create_directory_symlink(f.root / "outside", data / "keys");
        }
        else
        {
            fs::create_directories(f.work / "previous" / id / "assets/roms");
            put(f.work / "previous" / id / "assets/roms/only-copy.nsp", "older user data");
        }
        const auto old_app = tree(f.location / id), old_data = tree(data);
        const auto result = f.apply(request(f, "01.000.100"));
        assert(!result.ok && !result.error.empty() && f.settled());
        assert(tree(f.location / id) == old_app && tree(data) == old_data);
        if (issue == "older-backup")
            assert(get(f.work / "previous" / id / "assets/roms/only-copy.nsp") ==
                   "older user data");
    }
    // Game files an earlier store's update left in its previous folder are moved out by
    // the same rules, alone or beside the ones still in the app's folder.
    for (const bool both : {false, true})
    {
        Fixture f(base, both ? "eden-stranded-both" : "eden-stranded");
        if (both)
            seed(f);
        else
            assert(f.apply(request(f, "01.000.080")).ok);
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        if (both)
        {
            // The app's folder keeps everything but its games; those are the stranded ones.
            fs::remove_all(f.location / id / "assets/roms");
        }
        fs::create_directories(f.work / "previous" / id / "assets/roms");
        put(f.work / "previous" / id / "assets/roms/only-copy.nsp", "older user data");
        const auto result = f.apply(request(f, "01.000.100"));
        assert(result.ok && f.settled());
        assert(get(data / "roms/only-copy.nsp") == "older user data");
        assert(!fs::exists(f.work / "previous" / id / "assets/roms"));
        if (both)
        {
            assert(get(data / "keys/personal.bin") == "keys data");
            assert(get(data / "user/save.bin") == "keep my save");
        }
        // The next update finds nothing left to move.
        assert(f.apply(request(f, "01.000.110")).ok);
        assert(get(data / "roms/only-copy.nsp") == "older user data");
    }
    // Stopped between two of the stranded folders: nothing is lost, and the update that
    // is asked for again finishes the move.
    {
        Fixture f(base, "eden-stranded-cut");
        assert(f.apply(request(f, "01.000.080")).ok);
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        for (const auto *folder : {"keys", "roms"})
        {
            fs::create_directories(f.work / "previous" / id / "assets" / folder);
            put(f.work / "previous" / id / "assets" / folder / "personal.bin", "kept");
        }
        f.environment.interrupt = [](const char *step)
        { return std::string(step) == "eden-moved-keys"; };
        const auto interrupted = f.apply(request(f, "01.000.100"));
        assert(interrupted.interrupted && !interrupted.ok);
        assert(get(data / "keys/personal.bin") == "kept");
        assert(get(f.work / "previous" / id / "assets/roms/personal.bin") == "kept");
        f.environment.interrupt = nullptr;
        assert(install::recover(f.environment).ok);
        assert(f.apply(request(f, "01.000.100")).ok && f.settled());
        assert(get(data / "keys/personal.bin") == "kept" &&
               get(data / "roms/personal.bin") == "kept");
    }
    // A different filesystem must not leave an activated app with unmigrated data.
    if (fs::is_directory("/dev/shm"))
    {
        Fixture f(base, "eden-cross-drive");
        const auto other = fs::path("/dev/shm") / ("store-eden-" + std::to_string(getpid()));
        fs::create_directories(other);
        f.environment.data = other.string();
        seed(f);
        const auto old_app = tree(f.location / id);
        const auto result = f.apply(request(f, "01.000.100"));
        assert(!result.ok && result.error.find("different drives") != std::string::npos);
        assert(tree(f.location / id) == old_app && f.settled());
        fs::remove_all(other);
    }
    // A power cut between any two renames resumes under the existing swap journal.
    for (const std::string cut : {"placed", "eden-moved-keys", "eden-moved-firmware",
                                  "eden-moved-roms", "eden-moved-updates", "eden-moved-mods",
                                  "eden-moved-ryujinx", "eden-moved-save-import", "recorded"})
    {
        Fixture f(base, "eden-cut-" + cut);
        seed(f);
        f.environment.interrupt = [&](const char *step) { return cut == step; };
        const auto interrupted = f.apply(request(f, "01.000.100"));
        assert(interrupted.interrupted && !interrupted.ok);
        f.environment.interrupt = nullptr;
        const auto recovered = install::recover(f.environment);
        assert(recovered.ok && f.settled());
        check_data(f);
        assert(install::recover(f.environment).ok);
        check_data(f);
    }
    // A conflict appearing after activation stops recovery with the backup intact.
    {
        Fixture f(base, "eden-recovery-conflict");
        seed(f);
        f.environment.interrupt = [](const char *step) { return std::string(step) == "placed"; };
        assert(f.apply(request(f, "01.000.100")).interrupted);
        f.environment.interrupt = nullptr;
        const auto data = fs::path(f.environment.data) / "prosperoeden";
        put(data / "roms/new.nsp", "new data");
        assert(!install::recover(f.environment).ok);
        assert(get(data / "roms/new.nsp") == "new data");
        assert(get(f.work / "backup" / id / "assets/roms/personal.bin") == "roms data");
        assert(fs::exists(f.state / "journal.json"));
        fs::rename(data / "roms/new.nsp", f.root / "new.nsp");
        assert(install::recover(f.environment).ok);
        check_data(f);
    }
}

// Stops dead at every step of every operation, as a power cut would, and
// checks the folder is whole before and after recovery.
void check_interruptions(const fs::path &base)
{
    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    for (const std::string operation : {"install", "update", "uninstall"})
    {
        std::vector<std::string> steps;
        for (std::size_t stop = 0; stop == 0 || stop <= steps.size(); ++stop)
        {
            Fixture f(base, "cut-" + operation + "-" + std::to_string(stop));
            if (operation != "install")
                assert(f.apply(f.request("01.000.001")).ok);
            const auto before = operation == "install" ? std::string{} : v1;
            const auto after = operation == "install"  ? v1
                               : operation == "update" ? v2
                                                       : std::string{};
            const auto run = [&]
            {
                return operation == "uninstall"
                           ? f.uninstall()
                           : f.apply(
                                 f.request(operation == "install" ? "01.000.001" : "01.000.002"));
            };
            std::vector<std::string> seen;
            f.environment.interrupt = [&](const char *step)
            {
                seen.emplace_back(step);
                return stop != 0 && seen.size() == stop;
            };
            const auto result = run();
            if (stop == 0)
            {
                assert(result.ok && tree(f.target()) == after && f.settled());
                steps = seen;
                assert(steps.size() >= 4);
                continue;
            }
            assert(result.interrupted && !result.ok);
            auto now = tree(f.target());
            // Whole or absent at the instant of the cut, never a mix.
            assert(now == before || now == after || now.empty());
            f.environment.interrupt = nullptr;
            const auto recovered = install::recover(f.environment);
            assert(recovered.ok && recovered.operation == operation);
            now = tree(f.target());
            assert((now == before || now == after) && f.settled());
            // What is there is managed with its true version; what isn't has no receipt.
            const auto version = f.managed();
            assert(version == (now == v1 ? "01.000.001" : now == v2 ? "01.000.002" : ""));
            assert(now.empty() == !fs::exists(f.state / "receipts" / (kId + ".json")));
            assert(install::recover(f.environment).ok); // Recovery is safe to repeat.
            // The interrupted operation can simply be asked for again.
            if (now != after)
                assert(run().ok);
            assert(tree(f.target()) == after && f.settled());
            fs::remove_all(f.root);
        }
    }
}

// A running title has a sandbox folder named after it; nothing else counts.
void check_running(const fs::path &root)
{
    const auto sandboxes = (root / "sandbox").string();
    assert(system::title_running(kId, sandboxes) == -1); // Can't be told: treated as running.
    fs::create_directories(root / "sandbox/NPXS40000_000");
    fs::create_directories(root / "sandbox/PPSA99501_000");
    assert(system::title_running(kId, sandboxes) == 0);
    assert(system::title_running("PPSA99501", sandboxes) == 1);
    fs::create_directories(root / "sandbox" / (kId + "_001"));
    assert(system::title_running(kId, sandboxes) == 1);
    assert(system::title_running("../escape", sandboxes) == -1);
    std::vector<std::string> ids;
    assert(system::running_titles(ids, sandboxes) && ids.size() == 2);
    fs::remove_all(root / "sandbox");
}

// The store updating itself: swapped while it runs, finished at the next start.
void check_self_update(const fs::path &base)
{
    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    Fixture f(base, "self");
    f.environment.self = kId;
    f.environment.running = [](const std::string &) { return 1; }; // It is running: itself.
    auto request = f.request("01.000.002");
    assert(!f.apply(request).ok && f.requests == 0); // Never through the ordinary path.
    request.self_update = true;
    assert(!f.apply(request).ok && f.requests == 0); // Not installed here.
    // As put there by hand: the folder, no receipt.
    for (const auto &[name, data] : app("01.000.001"))
        if (!name.ends_with('/'))
        {
            const fs::path file = f.location / name;
            fs::create_directories(file.parent_path());
            put(file, data);
        }
    auto result = f.apply(request);
    assert(result.ok && result.restart && result.operation == "update");
    assert(tree(f.target()) == v2 && tree(f.work / "backup" / kId) == v1);
    assert(fs::exists(f.state / "journal.json"));
    // Until the restart nothing else is changed.
    f.environment.self = "PPSA99000";
    assert(!f.apply(f.request("01.000.001")).ok);
    f.environment.self = kId;
    // The console may start the old store once more: it leaves everything alone.
    f.environment.self_version = "01.000.001";
    const auto early = install::recover(f.environment);
    assert(early.ok && early.restart && tree(f.work / "backup" / kId) == v1);
    assert(fs::exists(f.state / "journal.json") && tree(f.target()) == v2);
    // The next start finishes: receipt written, the old folder gone.
    f.environment.self_version = "01.000.002";
    const auto recovered = install::recover(f.environment);
    assert(recovered.ok && recovered.operation == "update" && f.settled());
    assert(tree(f.target()) == v2 && f.managed() == "01.000.002");
    // The same version again is not an update.
    request = f.request("01.000.002");
    request.self_update = true;
    assert(!f.apply(request).ok && tree(f.target()) == v2 && f.settled());
    fs::remove_all(f.root);
}

void check_records()
{
    catalog::Receipt receipt{"PPSA99500",   "/data/home\"brew",   "01.000.001",
                             "v1 \"beta\"", std::string(64, 'b'), "2026-10-02T00:00:00Z"},
        parsed;
    std::string error;
    assert(catalog::parse_receipt(catalog::format_receipt(receipt), parsed, error));
    assert(parsed.location == receipt.location && parsed.release_tag == receipt.release_tag);
    catalog::Journal journal{"update",     "swap", "PPSA99500",         "/data/homebrew",
                             "01.000.002", "v2",   std::string(64, 'c')},
        read;
    assert(catalog::parse_journal(catalog::format_journal(journal), read, error));
    assert(read.state == "swap" && read.content_version == "01.000.002");
    journal.state = "remove"; // Not a state of an update.
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    journal = {"install", "activate", "PPSA99500", "/data/homebrew", "", "", ""};
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    journal = {"uninstall", "remove", "../escape", "/data/homebrew", "", "", ""};
    assert(!catalog::parse_journal(catalog::format_journal(journal), read, error));
    assert(!catalog::parse_journal(std::string(20000, ' '), read, error));
}

// The file worker, started as the console's loader starts it: a new process
// whose standard input and output are the connection.
struct WorkerHost
{
    std::string program;
    unsigned started = 0;
    bool available = true;
    // What the child runs instead of the worker: empty for the worker itself.
    // Unless every is set, downloads still go to the real worker.
    std::string script;
    bool every = false;
    bool connect(install::Channel &channel, bool download = false)
    {
        if (!available)
            return false;
        int pair[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        const pid_t child = fork();
        assert(child >= 0);
        if (child == 0)
        {
            dup2(pair[1], STDIN_FILENO);
            dup2(pair[1], STDOUT_FILENO);
            close(pair[0]);
            close(pair[1]);
            if (script.empty() || (download && !every))
                execl(program.c_str(), program.c_str(), static_cast<char *>(nullptr));
            else
                execl("/bin/sh", "sh", "-c", script.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        close(pair[1]);
        ++started;
        const int socket = pair[0];
        channel.send = [socket](const void *data, std::size_t size)
        { return static_cast<long>(send(socket, data, size, MSG_NOSIGNAL)); };
        channel.receive = [socket](void *data, std::size_t size)
        { return static_cast<long>(recv(socket, data, size, 0)); };
        channel.close = [socket, child]
        {
            close(socket);
            int status = 0;
            waitpid(child, &status, 0);
        };
        return true;
    }
    void attach(install::Environment &environment)
    {
        const install::Connect connect = [this](install::Channel &channel)
        { return this->connect(channel); };
        environment.unpack = [connect](const std::string &archive, const std::string &title,
                                       const std::string &destination,
                                       const std::atomic<bool> &cancelled,
                                       std::atomic<std::uint64_t> &written, std::string &error,
                                       install::ExtractTimes &times)
        {
            return install::worker_extract(connect, archive, title, destination, cancelled, written,
                                           error, times);
        };
        environment.remove = [connect](const std::string &path)
        { return install::worker_remove(connect, path); };
        const install::Connect saving = [this](install::Channel &channel)
        { return this->connect(channel, true); };
        environment.save = [saving](const std::string &path, install::Writer &writer)
        { return install::worker_store(saving, path, writer); };
    }
};

void check_worker(const fs::path &base, const std::string &program)
{
    using install::worker_path;
    assert(worker_path("/data/prosperostore/staging/PPSA99500"));
    assert(worker_path("/mnt/usb0/prosperostore/trash/PPSA99500"));
    assert(!worker_path("/data/prosperostore"));          // The work folder itself.
    assert(!worker_path("/data/homebrew/PPSA99500"));     // An installed app.
    assert(!worker_path("data/prosperostore/staging/x")); // Not absolute.
    assert(!worker_path("/data/prosperostore/../etc"));
    assert(!worker_path("/data/prosperostore/./x"));
    assert(!worker_path("/data/prosperostore//x"));
    assert(!worker_path("/data/prosperostore/x/"));
    assert(!worker_path("/data/prosperostore/a\nb"));
    assert(!worker_path("/data/myprosperostore/x"));
    assert(!worker_path(""));

    const auto v1 = expected_tree(app("01.000.001")), v2 = expected_tree(app("01.000.002"));
    Fixture f(base, "worker");
    f.work = f.root / "prosperostore";
    f.environment.work = f.work.string();
    WorkerHost host;
    host.program = program;
    host.attach(f.environment);

    // Install, update and uninstall, with the worker unpacking and removing.
    auto request = f.request("01.000.001");
    auto result = f.apply(request);
    assert(result.ok && result.operation == "install" && result.version == "01.000.001");
    // One worker saved the download, another unpacked it.
    assert(result.note.find("worker=1") != std::string::npos && host.started == 2);
    assert(result.note.find("files=3 ") != std::string::npos);
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());

    request = f.request("01.000.002");
    host.started = 0;
    result = f.apply(request);
    assert(result.ok && result.operation == "update" && result.version == "01.000.002");
    // Download and unpack: a worker each. The old version is kept, not removed.
    assert(result.note.find("worker=1") != std::string::npos && host.started == 2);
    assert(tree(f.target()) == v2 && f.managed() == "01.000.002" && f.settled());
    assert(tree(f.work / "previous" / kId) == v1);

    host.started = 0;
    result = f.uninstall();
    assert(result.ok && host.started == 1 && !fs::exists(f.target()) && f.settled());
    assert(!fs::exists(f.state / "receipts" / (kId + ".json")));

    // No worker can be started: the store does all of it by itself.
    host.available = false;
    request = f.request("01.000.001");
    result = f.apply(request);
    assert(result.ok && result.note.find("worker=0") != std::string::npos);
    assert(tree(f.target()) == v1 && f.settled());
    request = f.request("01.000.002");
    result = f.apply(request);
    assert(result.ok && tree(f.target()) == v2 && f.settled());
    assert(f.uninstall().ok && !fs::exists(f.target()) && f.settled());
    host.available = true;

    // Something that isn't the worker answers: nothing was touched, the store does it.
    host.script = "echo nonsense";
    request = f.request("01.000.001");
    result = f.apply(request);
    assert(result.ok && result.note.find("worker=0") != std::string::npos &&
           tree(f.target()) == v1);

    // The worker dies after it accepted the job. An unpack fails cleanly and
    // leaves the installed version alone; a removal is finished by the store.
    host.script = "echo ready; echo 'p 10'";
    request = f.request("01.000.002");
    result = f.apply(request);
    assert(!result.ok && !result.error.empty() && tree(f.target()) == v1 && f.settled());
    assert(f.managed() == "01.000.001");
    result = f.uninstall();
    assert(result.ok && !fs::exists(f.target()) && f.settled());

    // The worker says why it failed.
    host.script = "echo ready; echo 'fail The archive is damaged'";
    request = f.request("01.000.001");
    result = f.apply(request);
    assert(!result.ok && result.error == "The archive is damaged" && !fs::exists(f.target()));
    assert(f.settled());
    host.script.clear();

    // Downloads: something else answers and the store saves the file itself;
    // a worker that dies after accepting loses the download, and nothing else.
    host.every = true;
    host.script = "echo nonsense";
    result = f.apply(request);
    assert(result.ok && result.note.find("worker=0") != std::string::npos &&
           tree(f.target()) == v1);
    host.script = "echo ready";
    request = f.request("01.000.002");
    f.requests = 0;
    result = f.apply(request);
    assert(!result.ok && result.error == "The download could not be saved" && f.requests == 1);
    assert(tree(f.target()) == v1 && f.managed() == "01.000.001" && f.settled());
    host.script.clear();
    host.every = false;
    assert(f.uninstall().ok && f.settled());
    {
        // What the worker saves is what was sent, in pieces of any size; a
        // file that exists is never replaced, and a cut connection leaves none.
        const install::Connect connect = [&host](install::Channel &channel)
        { return host.connect(channel); };
        fs::create_directories(f.work / "staging");
        const auto path = (f.work / "staging/download.bin").string();
        std::string body(3 * install::kWorkerPiece + 12345, 'd');
        for (std::size_t i = 0; i < body.size(); i += 4099)
            body[i] = static_cast<char>(i);
        install::Writer writer;
        assert(install::worker_store(connect, path, writer));
        assert(writer.write(std::string_view(body).substr(0, 7)));
        assert(writer.write(std::string_view(body).substr(7)));
        assert(writer.finish());
        writer = {};
        assert(get(path) == body);
        assert(install::worker_store(connect, path, writer));
        assert(!writer.finish());
        writer = {};
        assert(get(path) == body);
        fs::remove(path);
        assert(install::worker_store(connect, path, writer));
        assert(writer.write("partial"));
        writer = {}; // Dropped: the connection closes before the end mark.
        assert(!fs::exists(path));
        assert(!install::worker_store(connect, (f.root / "elsewhere.bin").string(), writer));
    }

    // The worker itself refuses paths outside the store's work folders, whoever asks.
    const auto ask = [&](const std::string &request_text)
    {
        install::Channel channel;
        assert(host.connect(channel));
        assert(channel.send(request_text.data(), request_text.size()) ==
               static_cast<long>(request_text.size()));
        std::string reply;
        char buffer[256];
        for (long count; (count = channel.receive(buffer, sizeof(buffer))) > 0;)
            reply.append(buffer, static_cast<std::size_t>(count));
        channel.close();
        return reply;
    };
    const fs::path outside = f.root / "outside";
    fs::create_directories(outside / "kept");
    assert(ask("PSW1\nremove\n" + outside.string() + "\n") == "fail The request was refused\n");
    assert(ask("PSW1\nremove\n" + (f.work / "..").string() + "\n") ==
           "fail The request was refused\n");
    assert(ask("PSW1\nextract\n" + (f.work / "a.zip").string() + "\n" + kId + "\n" +
               (outside / "x").string() + "\n") == "fail The request was refused\n");
    assert(ask("PSW1\nformat\n" + (f.work / "x").string() + "\n") ==
           "fail The request was refused\n");
    assert(ask("PSW1\nstore\n" + (outside / "x.bin").string() + "\n") ==
           "fail The request was refused\n");
    assert(ask("HELLO\n").empty());
    assert(fs::exists(outside / "kept"));

    // A cancel reaches the worker: the unpack stops, or had already finished.
    {
        Files big = app("01.000.003");
        std::string noise(1 << 20, 'n');
        std::uint32_t state = 99;
        for (auto &c : noise)
            c = static_cast<char>((state = state * 1664525u + 1013904223u) >> 24);
        for (int i = 0; i < 48; ++i)
            big.push_back({kId + "/data/" + std::to_string(i) + ".bin", noise});
        fs::create_directories(f.work / "staging");
        put(f.work / "staging/big.zip", zip(big));
        std::atomic<bool> cancelled{true};
        std::atomic<std::uint64_t> written{0};
        std::string error;
        install::ExtractTimes times;
        const install::Connect connect = [&host](install::Channel &channel)
        { return host.connect(channel); };
        const int outcome = install::worker_extract(connect, (f.work / "staging/big.zip").string(),
                                                    kId, (f.work / "staging/big").string(),
                                                    cancelled, written, error, times);
        assert(outcome == 1 || (outcome == 0 && error == "Cancelled"));
        assert(install::worker_remove(connect, (f.work / "staging/big").string()) == 1);
        assert(!fs::exists(f.work / "staging/big"));
        // Removing what isn't there is done, as it is for the store itself.
        assert(install::worker_remove(connect, (f.work / "staging/big").string()) == 1);
        // Unregistering: a host worker has no home screen and answers with its code;
        // a malformed title ID never reaches a worker.
        int code = 0;
        assert(install::worker_unregister(connect, "PPSA99109", code) == 0 && code == -3);
        const unsigned before = host.started;
        assert(install::worker_unregister(connect, "ppsa99109", code) == -1);
        assert(install::worker_unregister(connect, "PPSA9910", code) == -1);
        assert(host.started == before);
        fs::remove(f.work / "staging/big.zip");
        // Paths outside the work folders never reach a worker.
        host.started = 0;
        assert(install::worker_remove(connect, outside.string()) == -1 && host.started == 0);
    }
}
} // namespace

int main(int argc, char **argv)
{
    char temporary[] = "/tmp/prospero-install-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path root(temporary);
    check_records();
    check_archives(root);
    check_running(root);
    check_transactions(root);
    check_quick_update(root);
    check_eden_migration(root);
    check_self_update(root);
    check_interruptions(root);
    if (argc > 1)
        check_worker(root, argv[1]);
    fs::remove_all(root);
    std::puts("Install, update, uninstall and recovery checks passed");
}
