// ProsperoStore - Isolated PS5 Eden migration and recovery checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/transaction.hpp"
#include "install/files.hpp"
#include "third_party/miniz/miniz.h"
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace store;
#ifndef STORE_EDEN_TEST_ROOT
#define STORE_EDEN_TEST_ROOT "/data/ps5-eden-quick-check-c66e72a4"
#endif
const std::string root = STORE_EDEN_TEST_ROOT;
const std::string id = "PPSA99008";
const char *folders[] = {"keys", "firmware", "roms", "updates", "mods", "ryujinx", "save-import"};
void require(bool ok, const std::string &message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void dirs(const std::string &p)
{
    require(p == root || p.starts_with(root + "/"), "outside test root");
    for (auto slash = p.find('/', root.size()); slash != p.npos; slash = p.find('/', slash + 1))
        require(install::make_directory(p.substr(0, slash)), "mkdir parent " + p);
    require(install::make_directory(p), "mkdir " + p);
}
void put(const std::string &p, const std::string &body)
{
    require(p.starts_with(root + "/"), "write outside test root");
    dirs(p.substr(0, p.rfind('/')));
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0777);
    require(fd >= 0, "open " + p);
    const auto count = write(fd, body.data(), body.size());
    require(close(fd) == 0 && count == static_cast<ssize_t>(body.size()), "write " + p);
}
std::string get(const std::string &p)
{
    std::string body;
    require(install::read_small(p, 1u << 20, body), "read " + p);
    return body;
}
std::string metadata(const std::string &version)
{
    return "{\"titleId\":\"PPSA99008\",\"contentVersion\":\"" + version + "\"}";
}
enum class LegacyRelease
{
    none,
    collision,
    empty_directory
};
std::string archive(const std::string &version, LegacyRelease legacy = LegacyRelease::none)
{
    mz_zip_archive z{};
    require(mz_zip_writer_init_heap(&z, 0, 0), "zip init");
    const auto param = metadata(version);
    const std::string program(4096, 'x');
    require(mz_zip_writer_add_mem(&z, "PPSA99008/eboot.bin", program.data(), program.size(), 6),
            "zip eboot");
    require(
        mz_zip_writer_add_mem(&z, "PPSA99008/sce_sys/param.json", param.data(), param.size(), 6),
        "zip param");
    if (legacy == LegacyRelease::collision)
    {
        const std::string included = "release file must not replace personal data";
        require(mz_zip_writer_add_mem(&z, "PPSA99008/assets/roms/personal.bin", included.data(),
                                      included.size(), 6),
                "zip legacy collision");
    }
    if (legacy == LegacyRelease::empty_directory)
        require(mz_zip_writer_add_mem(&z, "PPSA99008/assets/roms/", "", 0, 0),
                "zip empty legacy directory");
    void *bytes = nullptr;
    size_t size = 0;
    require(mz_zip_writer_finalize_heap_archive(&z, &bytes, &size), "zip finish");
    std::string result(static_cast<char *>(bytes), size);
    mz_free(bytes);
    mz_zip_writer_end(&z);
    return result;
}
struct Fixture
{
    std::string base, apps, app, data, state, work, bytes;
    install::Environment env;
    LegacyRelease release = LegacyRelease::none;
    Fixture(const std::string &name)
        : base(root + "/" + name), apps(base + "/apps"), app(apps + "/" + id),
          data(base + "/data/prosperoeden"), state(base + "/state"), work(base + "/work")
    {
        dirs(apps);
        dirs(state);
        dirs(base + "/data");
        env.root = state;
        env.work = work;
        env.data = base + "/data";
        env.registered.clear();
        env.self = "PPSA99000";
        env.policy.roots = {apps};
        env.running = [](const std::string &) { return 0; };
        env.space = [](const std::string &, uint64_t &available)
        {
            available = 1ull << 40;
            return true;
        };
        env.now = [] { return std::string("2026-10-10T00:00:00Z"); };
        env.wait = [](unsigned, const std::atomic<bool> &) {};
        env.fetch = [this](const std::string &, uint64_t, const net::Sink &sink, net::Control &)
        {
            net::Response response;
            response.status = sink(bytes) ? 200 : 0;
            return response;
        };
        put(app + "/eboot.bin", "not an executable: test fixture only");
        put(app + "/sce_sys/param.json", metadata("01.000.080"));
        require(install::adopt(env, id, apps).ok, "adopt fixture");
        for (const auto *folder : folders)
            put(app + "/assets/" + folder + "/personal.bin", std::string(folder) + " data");
        put(data + "/config/prosperoeden.json",
            "{\"game_files\":\"" + data + "\",\"audio\":{\"volume\":42}}");
        put(data + "/user/save.bin", "preserve my save");
        dirs(data + "/roms");
    }
    install::Result update(const std::string &version = "01.000.100", bool overwrite = false)
    {
        bytes = archive(version, release);
        install::Request r;
        r.location = apps;
        r.overwrite = overwrite;
        r.entry.id = id;
        r.entry.status = "available";
        r.entry.format = "zip";
        r.entry.version = "test";
        r.entry.content_version = version;
        r.entry.artifact = "https://github.com/example/app/releases/download/test/PPSA99008.zip";
        r.entry.size = bytes.size();
        r.entry.digest = catalog::sha256(bytes);
        net::Control control;
        install::Progress progress;
        return install::apply(env, r, control, progress);
    }
    void verify()
    {
        for (const auto *folder : folders)
            require(get(data + "/" + folder + "/personal.bin") == std::string(folder) + " data",
                    "migrated " + std::string(folder));
        require(get(data + "/user/save.bin") == "preserve my save", "save unchanged");
        require(get(data + "/config/prosperoeden.json").find("42") != std::string::npos,
                "settings unchanged");
        require(install::kind(state + "/journal.json") == install::Kind::absent, "journal closed");
    }
};
int main()
{
    if (install::kind(root) != install::Kind::absent)
        return 2; // Never reuse or erase a prior test.
    if (!install::make_directory(root))
        return 3;
    put(root + "/result.txt", "STARTED\n");
    unsigned passed = 0, skipped = 0, failed = 0;
    std::string skip_details, case_details;
    const auto check = [&](const std::string &name, const auto &test)
    {
        try
        {
            test();
            ++passed;
            case_details += "PASS: " + name + "\n";
        }
        catch (const std::exception &e)
        {
            ++failed;
            case_details += "FAIL: " + name + ": " + e.what() + "\n";
        }
    };
    try
    {
        {
            Fixture f("normal");
            const auto config = get(f.data + "/config/prosperoeden.json");
            auto r = f.update();
            require(r.ok, "normal update: " + r.error);
            f.verify();
            require(get(f.data + "/config/prosperoeden.json") == config,
                    "exact settings bytes unchanged");
            require(f.update("01.000.110").ok, "subsequent update");
            f.verify();
            ++passed;
        }
        for (const std::string issue : {"conflict", "custom", "bad-config", "link", "older-backup"})
        {
            Fixture f(issue);
            if (issue == "conflict")
                put(f.data + "/roms/existing.nsp", "existing");
            if (issue == "custom")
                put(f.data + "/config/prosperoeden.json",
                    "{\"game_files\":\"/mnt/usb0/user-games\"}");
            if (issue == "bad-config")
                put(f.data + "/config/prosperoeden.json", "invalid JSON");
            if (issue == "link")
            {
                dirs(f.base + "/outside");
                if (symlink((f.base + "/outside").c_str(), (f.data + "/keys").c_str()) != 0)
                {
                    ++skipped;
                    skip_details += "SKIP: symlink fixture could not be created; errno=" +
                                    std::to_string(errno) + "\n";
                    continue;
                }
            }
            if (issue == "older-backup")
                put(f.work + "/previous/" + id + "/assets/roms/old.bin", "only copy");
            auto r = f.update();
            require(!r.ok && !r.error.empty(), "refusal " + issue);
            require(get(f.app + "/assets/roms/personal.bin") == "roms data",
                    "source preserved " + issue);
            require(get(f.app + "/sce_sys/param.json") == metadata("01.000.080"),
                    "old version preserved " + issue);
            require(install::kind(f.state + "/journal.json") == install::Kind::absent,
                    "no transaction " + issue);
            ++passed;
        }
        for (const std::string cut : {"placed", "eden-moved-keys", "eden-moved-firmware",
                                      "eden-moved-roms", "eden-moved-updates", "eden-moved-mods",
                                      "eden-moved-ryujinx", "eden-moved-save-import", "recorded"})
        {
            Fixture f(cut);
            f.env.interrupt = [&](const char *step) { return cut == step; };
            require(f.update().interrupted, "interruption " + cut);
            f.env.interrupt = nullptr;
            auto r = install::recover(f.env);
            require(r.ok, "recovery " + cut + ": " + r.error);
            f.verify();
            require(install::recover(f.env).ok, "idempotent recovery " + cut);
            f.verify();
            ++passed;
        }
        {
            Fixture f("late-conflict");
            f.env.interrupt = [](const char *step) { return std::string(step) == "placed"; };
            require(f.update().interrupted, "late conflict interruption");
            f.env.interrupt = nullptr;
            put(f.data + "/roms/new.nsp", "new data");
            require(!install::recover(f.env).ok, "late conflict refuses");
            require(get(f.data + "/roms/new.nsp") == "new data", "late destination preserved");
            require(get(f.work + "/backup/" + id + "/assets/roms/personal.bin") == "roms data",
                    "backup preserved");
            require(rename((f.data + "/roms/new.nsp").c_str(), (f.base + "/new.nsp").c_str()) == 0,
                    "resolve test conflict");
            require(install::recover(f.env).ok, "late conflict resumes");
            f.verify();
            ++passed;
        }
        // Quick updates overlay the app in place, but legacy personal folders must still
        // reach Eden's external data directory before the update is recorded as complete.
        check("quick-normal",
              [&]
              {
                  Fixture f("quick-normal");
                  const auto config = get(f.data + "/config/prosperoeden.json");
                  const auto r = f.update("01.000.100", true);
                  require(r.ok, "quick update: " + r.error);
                  f.verify();
                  for (const auto *folder : folders)
                      require(install::kind(f.app + "/assets/" + folder) == install::Kind::absent,
                              "legacy source moved: " + std::string(folder));
                  require(get(f.data + "/config/prosperoeden.json") == config,
                          "exact settings bytes unchanged");
                  require(get(f.app + "/sce_sys/param.json") == metadata("01.000.100"),
                          "quick update version");
                  require(f.update("01.000.110", true).ok, "subsequent quick update");
                  f.verify();
              });
        for (const std::string issue : {"conflict", "custom", "bad-config", "older-backup"})
            check("quick-refuse-" + issue,
                  [&]
                  {
                      Fixture f("quick-refuse-" + issue);
                      if (issue == "conflict")
                          put(f.data + "/roms/existing.nsp", "existing");
                      if (issue == "custom")
                          put(f.data + "/config/prosperoeden.json",
                              "{\"game_files\":\"/mnt/usb0/user-games\"}");
                      if (issue == "bad-config")
                          put(f.data + "/config/prosperoeden.json", "invalid JSON");
                      if (issue == "older-backup")
                          put(f.work + "/previous/" + id + "/assets/roms/old.bin", "only copy");
                      const auto r = f.update("01.000.100", true);
                      require(!r.ok && !r.error.empty(), "quick refusal " + issue);
                      for (const auto *folder : folders)
                          require(get(f.app + "/assets/" + folder + "/personal.bin") ==
                                      std::string(folder) + " data",
                                  "source preserved " + issue);
                      require(get(f.app + "/sce_sys/param.json") == metadata("01.000.080"),
                              "old version preserved " + issue);
                      require(install::kind(f.state + "/journal.json") == install::Kind::absent,
                              "no transaction " + issue);
                  });
        check("quick-archive-collision",
              [&]
              {
                  Fixture f("quick-archive-collision");
                  f.release = LegacyRelease::collision;
                  const auto r = f.update("01.000.100", true);
                  require(!r.ok && !r.error.empty(), "archive with legacy files refused");
                  require(get(f.app + "/assets/roms/personal.bin") == "roms data",
                          "archive did not overwrite personal data");
                  require(get(f.app + "/sce_sys/param.json") == metadata("01.000.080"),
                          "archive refusal keeps old version");
                  require(install::kind(f.data + "/keys") == install::Kind::absent,
                          "archive refusal did not start migration");
                  require(install::kind(f.state + "/journal.json") == install::Kind::absent &&
                              install::kind(f.work + "/staging/" + id) == install::Kind::absent,
                          "archive refusal closes transaction and staging");
              });
        check(
            "quick-archive-collision-after-migration",
            [&]
            {
                Fixture f("quick-archive-collision-after-migration");
                require(f.update("01.000.090").ok, "initial migration");
                f.verify();
                f.release = LegacyRelease::collision;
                const auto r = f.update("01.000.100", true);
                require(!r.ok && !r.error.empty(), "packaged legacy files refused after migration");
                require(get(f.app + "/sce_sys/param.json") == metadata("01.000.090"),
                        "refusal keeps installed version");
                require(install::kind(f.app + "/assets/roms/personal.bin") == install::Kind::absent,
                        "packaged legacy file not installed");
                f.verify();
                require(install::kind(f.work + "/staging/" + id) == install::Kind::absent,
                        "collision staging removed");
            });
        check("quick-empty-legacy-with-external-data",
              [&]
              {
                  Fixture f("quick-empty-legacy-with-external-data");
                  require(
                      rmdir((f.data + "/roms").c_str()) == 0 &&
                          rename((f.app + "/assets/roms").c_str(), (f.data + "/roms").c_str()) == 0,
                      "some legacy data already migrated");
                  put(f.data + "/roms/existing.nsp", "external game must be preserved");
                  f.release = LegacyRelease::empty_directory;
                  const auto r = f.update("01.000.100", true);
                  require(r.ok, "empty source does not conflict with external data: " + r.error);
                  f.verify();
                  require(get(f.data + "/roms/existing.nsp") == "external game must be preserved",
                          "existing external game preserved");
              });
        check("quick-empty-legacy-directory",
              [&]
              {
                  Fixture f("quick-empty-legacy-directory");
                  f.release = LegacyRelease::empty_directory;
                  const auto r = f.update("01.000.100", true);
                  require(r.ok, "empty legacy directory accepted: " + r.error);
                  f.verify();
              });
        check("quick-journaled",
              [&]
              {
                  Fixture f("quick-journaled");
                  f.env.interrupt = [](const char *step)
                  { return std::string(step) == "journaled"; };
                  require(f.update("01.000.100", true).interrupted, "quick journaled interruption");
                  f.env.interrupt = nullptr;
                  require(install::recover(f.env).ok, "quick pre-overlay recovery");
                  require(get(f.app + "/sce_sys/param.json") == metadata("01.000.080"),
                          "pre-overlay recovery keeps old version");
                  for (const auto *folder : folders)
                      require(get(f.app + "/assets/" + folder + "/personal.bin") ==
                                  std::string(folder) + " data",
                              "pre-overlay legacy data preserved");
                  require(install::kind(f.state + "/journal.json") == install::Kind::absent,
                          "pre-overlay journal closed");
                  require(f.update("01.000.100", true).ok, "retry quick update");
                  f.verify();
              });
        for (const std::string cut : {"placed", "eden-moved-keys", "eden-moved-firmware",
                                      "eden-moved-roms", "eden-moved-updates", "eden-moved-mods",
                                      "eden-moved-ryujinx", "eden-moved-save-import", "recorded"})
            check("quick-" + cut,
                  [&]
                  {
                      Fixture f("quick-" + cut);
                      f.env.interrupt = [&](const char *step) { return cut == step; };
                      require(f.update("01.000.100", true).interrupted,
                              "quick interruption " + cut);
                      f.env.interrupt = nullptr;
                      const auto r = install::recover(f.env);
                      require(r.ok, "quick recovery " + cut + ": " + r.error);
                      f.verify();
                      require(get(f.app + "/sce_sys/param.json") == metadata("01.000.100"),
                              "recovery keeps complete new app");
                      require(install::recover(f.env).ok, "idempotent quick recovery " + cut);
                      f.verify();
                  });
        check("quick-late-conflict",
              [&]
              {
                  Fixture f("quick-late-conflict");
                  f.env.interrupt = [](const char *step) { return std::string(step) == "placed"; };
                  require(f.update("01.000.100", true).interrupted,
                          "quick late conflict interruption");
                  f.env.interrupt = nullptr;
                  put(f.data + "/roms/new.nsp", "new data");
                  require(!install::recover(f.env).ok, "quick late conflict refuses");
                  require(get(f.data + "/roms/new.nsp") == "new data",
                          "late destination preserved");
                  require(get(f.app + "/assets/roms/personal.bin") == "roms data",
                          "legacy source preserved");
                  require(install::kind(f.state + "/journal.json") == install::Kind::file,
                          "unfinished migration journal retained");
                  require(rename((f.data + "/roms/new.nsp").c_str(),
                                 (f.base + "/new.nsp").c_str()) == 0,
                          "resolve synthetic late conflict");
                  require(install::recover(f.env).ok, "quick late conflict resumes");
                  f.verify();
                  require(install::recover(f.env).ok, "quick late conflict recovery is idempotent");
              });
        put(root + "/result.txt",
            std::string(failed ? "FAIL: " : "PASS: ") + std::to_string(passed) +
                " console scenarios; " + std::to_string(failed) + " failed; " +
                std::to_string(skipped) + " unsupported setup check(s)\n" + skip_details +
                case_details + "Real Eden files and settings were not accessed.\n");
        return failed ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        put(root + "/result.txt",
            "FAIL after " + std::to_string(passed) + " scenarios: " + e.what() + "\n");
        return 1;
    }
}
