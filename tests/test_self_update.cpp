/*
 * ps5-native-app-boilerplate - Self-update regression: the check, the job and the helper.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs the app-side kit against the real helper code over a socket pair, with
 * real archives and real folders under a temporary directory. What stands in
 * for the console: the network, the Ed25519 check and the payload loader.
 */
#include "../examples/self-update-helper/files.hpp"
#include "../examples/self-update-helper/updater.hpp"
#include "miniz/miniz.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

// The kit itself, without the console transport, compiled into this test.
#define UPDATE_CHECK_NO_NETWORK
extern "C"
{
#include "../examples/self-update/self_update.c"
#include "../examples/self-update/self_update_sha256.c"
#include "../examples/update-check/update_check.c"
}

namespace
{
int failures = 0;

void check(bool condition, const char *what, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(condition) check((condition), #condition, __LINE__)

const char *const title = "PPSA12345";

std::string digest_of(const std::string &data)
{
    char text[65];
    sha256_text(data.data(), data.size(), text);
    return text;
}

void write_file(const std::string &path, const std::string &body)
{
    FILE *file = std::fopen(path.c_str(), "wb");
    CHECK(file != nullptr);
    if (file != nullptr)
    {
        std::fwrite(body.data(), 1, body.size(), file);
        std::fclose(file);
    }
}

std::string read_file(const std::string &path)
{
    std::string body;
    return self_update::read_small(path, 1 << 20, body) ? body : std::string("<missing>");
}

std::string param_json(const std::string &id, const std::string &version)
{
    return "{\"titleId\":\"" + id + "\",\"contentVersion\":\"" + version + "\"}";
}

// A release archive: the app folder named after the title, as the catalog asks.
std::string make_archive(const std::string &id, const std::string &version,
                         const std::string &program)
{
    mz_zip_archive zip{};
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    const std::string param = param_json(id, version);
    const std::string asset(200000, 'n');
    CHECK(mz_zip_writer_add_mem(&zip, (id + "/eboot.bin").c_str(), program.data(), program.size(),
                                MZ_DEFAULT_COMPRESSION));
    CHECK(mz_zip_writer_add_mem(&zip, (id + "/sce_sys/param.json").c_str(), param.data(),
                                param.size(), MZ_DEFAULT_COMPRESSION));
    CHECK(mz_zip_writer_add_mem(&zip, (id + "/assets/new.txt").c_str(), asset.data(), asset.size(),
                                MZ_DEFAULT_COMPRESSION));
    void *data = nullptr;
    size_t size = 0;
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
    std::string archive(static_cast<const char *>(data), size);
    mz_zip_writer_end(&zip);
    mz_free(data);
    return archive;
}

// One console under a temporary folder: a drive with an installed app, the
// sandbox folder that says it is running, and the console's registered copies.
struct Console
{
    std::string root, drive, target, sandboxes, sandbox, registered;
    std::vector<std::string> notices;
    self_update::Environment environment;

    Console()
    {
        char pattern[] = "/tmp/self-update-test-XXXXXX";
        const char *made = mkdtemp(pattern);
        CHECK(made != nullptr);
        root = made != nullptr ? made : "/tmp/self-update-test";
        drive = root + "/data";
        target = drive + "/homebrew/" + title;
        sandboxes = root + "/sandbox";
        sandbox = sandboxes + "/" + title + "_000";
        registered = root + "/user";
        for (const auto &folder : {drive, drive + "/homebrew", target, target + "/sce_sys",
                                   target + "/assets", sandboxes, sandbox, registered,
                                   registered + "/appmeta", registered + "/appmeta/" + title})
            CHECK(mkdir(folder.c_str(), 0755) == 0);
        write_file(target + "/eboot.bin", "old program");
        write_file(target + "/sce_sys/param.json", param_json(title, "01.000.000"));
        write_file(target + "/assets/old.txt", "left over from the old version");
        write_file(target + "/user-note.txt", "keep me");
        write_file(registered + "/appmeta/" + title + "/param.json", "old registered copy");
        environment.roots = {drive + "/homebrew", root + "/absent"};
        environment.drives = {drive};
        environment.sandboxes = sandboxes;
        environment.registered = registered;
        environment.exit_wait_ms = 1500;
        environment.settle_ms = 10;
        environment.notify = [this](const std::string &message) { notices.push_back(message); };
    }
    ~Console()
    {
        (void)chmod(target.c_str(), 0755);
        CHECK(self_update::remove_tree(root));
    }
    void close_app() const
    {
        CHECK(rmdir(sandbox.c_str()) == 0);
    }
    bool untouched() const
    {
        return read_file(target + "/eboot.bin") == "old program" &&
               read_file(target + "/assets/old.txt") == "left over from the old version" &&
               read_file(target + "/user-note.txt") == "keep me" &&
               self_update::kind(drive + "/self-update") == self_update::Kind::absent;
    }
};

// What stands in for the network, the signature check and the payload loader.
struct World
{
    Console *console = nullptr;
    std::string manifest, signature = std::string(64, 'S'), app_file, archive;
    int app_status = 200;
    uint64_t stored_sequence = 0;
    bool has_sequence = false, loader = true;
    size_t fail_download_after = 0; // bytes; 0: never
    pthread_t helper{};
    bool helper_started = false;
    int helper_result = -1;
    int helper_socket = -1;
};
World *world = nullptr;

int fake_fetch(void *, const char *url, char *body, size_t capacity, size_t *length, int *status)
{
    const std::string address = url;
    const std::string *source = nullptr;
    *status = 200;
    if (address == SELF_UPDATE_API "manifest.json")
        source = &world->manifest;
    else if (address == SELF_UPDATE_API "manifest.sig")
        source = &world->signature;
    else if (address == std::string(SELF_UPDATE_API "apps/") + title + ".json")
    {
        source = &world->app_file;
        *status = world->app_status;
    }
    else
        return -1;
    if (source->size() > capacity)
        return -1;
    std::memcpy(body, source->data(), source->size());
    *length = source->size();
    return 0;
}

int fake_download(void *, const char *url, uint64_t limit, self_update_sink sink, void *user)
{
    if (!self_update_url_allowed(url, 0) || world->archive.size() > limit)
        return -1;
    for (size_t done = 0; done < world->archive.size();)
    {
        const size_t piece = std::min<size_t>(97, world->archive.size() - done);
        if (world->fail_download_after != 0 && done >= world->fail_download_after)
            return -1;
        if (sink(user, world->archive.data() + done, piece) != 1)
            return -1;
        done += piece;
    }
    return 0;
}

// Stands in for Ed25519: the catalog's first key and a signature of 64 'S'.
int fake_verify(void *, const unsigned char key[32], const unsigned char signature[64],
                const void *, size_t)
{
    return key[0] == 0x87 &&
           std::string(reinterpret_cast<const char *>(signature), 64) == std::string(64, 'S');
}

void *helper_main(void *argument)
{
    const int socket = static_cast<int>(reinterpret_cast<intptr_t>(argument));
    const self_update::Io io{[socket](void *data, std::size_t size)
                             { return static_cast<long>(read(socket, data, size)); },
                             [socket](const void *data, std::size_t size)
                             { return static_cast<long>(send(socket, data, size, MSG_NOSIGNAL)); }};
    world->helper_result = self_update::run(world->console->environment, io);
    // The real helper is a process that ends here; ending lets its listener stop reading.
    shutdown(socket, SHUT_RDWR);
    return nullptr;
}

long socket_send(void *user, const void *data, size_t size)
{
    return send(static_cast<int>(reinterpret_cast<intptr_t>(user)), data, size, MSG_NOSIGNAL);
}
long socket_receive(void *user, void *data, size_t size)
{
    return read(static_cast<int>(reinterpret_cast<intptr_t>(user)), data, size);
}
void socket_close(void *user)
{
    shutdown(static_cast<int>(reinterpret_cast<intptr_t>(user)), SHUT_RDWR);
}

int fake_open_helper(void *, self_update_channel *channel)
{
    int pair[2];
    if (!world->loader || socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        return 0;
    world->helper_socket = pair[1];
    world->helper_started =
        pthread_create(&world->helper, nullptr, helper_main,
                       reinterpret_cast<void *>(static_cast<intptr_t>(pair[1]))) == 0;
    channel->send = socket_send;
    channel->receive = socket_receive;
    channel->close = socket_close;
    channel->user = reinterpret_cast<void *>(static_cast<intptr_t>(pair[0]));
    return world->helper_started ? 1 : 0;
}

int fake_load_sequence(void *, uint64_t *sequence)
{
    *sequence = world->stored_sequence;
    return world->has_sequence ? 1 : 0;
}
int fake_save_sequence(void *, uint64_t sequence)
{
    world->stored_sequence = sequence;
    world->has_sequence = true;
    return 1;
}
uint64_t fake_now(void *)
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000u +
           static_cast<uint64_t>(now.tv_nsec) / 1000000u;
}

const self_update_platform platform = {
    fake_fetch,         fake_download,      fake_verify, fake_open_helper,
    fake_load_sequence, fake_save_sequence, fake_now,    nullptr};

// A catalog that lists the app at `version` with `archive`.
void publish(World &w, const std::string &version, const std::string &archive, uint64_t sequence)
{
    w.archive = archive;
    w.app_file = std::string("{\"schema\":3,\"titleid\":\"") + title +
                 "\",\"name\":\"Test App\",\"version\":\"1.1.0\",\"artifact_url\":"
                 "\"https://github.com/example/app/releases/download/1.1.0/PPSA12345.zip\","
                 "\"sha256\":\"" +
                 digest_of(archive) + "\",\"status\":\"available\",\"content_version\":\"" +
                 version + "\",\"format\":\"zip\",\"size\":" + std::to_string(archive.size()) +
                 ",\"page\":\"https://prosperostore.net/app/PPSA12345/\"}";
    w.manifest = std::string("{\"commit\":\"abc\",\"files\":{\"apps/") + title + ".json\":\"" +
                 digest_of(w.app_file) + "\",\"index.json\":\"" + std::string(64, '0') +
                 "\"},\"schema\":3,\"sequence\":" + std::to_string(sequence) + "}";
}

self_update_phase wait_while_busy(self_update_job &job, self_update_status &status)
{
    for (int i = 0; i < 2000; ++i)
    {
        self_update_poll(&job, &status);
        if (status.phase != SELF_UPDATE_STARTING && status.phase != SELF_UPDATE_DOWNLOADING &&
            status.phase != SELF_UPDATE_UNPACKING)
            break;
        usleep(5000);
    }
    return status.phase;
}

void join_helper(World &w)
{
    if (w.helper_started)
    {
        pthread_join(w.helper, nullptr);
        w.helper_started = false;
        // Give the helper's listening thread the moment it needs to see the end.
        usleep(20000);
        close(w.helper_socket);
    }
}

void test_pieces()
{
    // SHA-256 of "abc" (FIPS 180-4) and of a million 'a'.
    CHECK(digest_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(digest_of(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    CHECK(digest_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    CHECK(self_update_url_allowed("https://github.com/o/r/releases/download/v1/a.zip", 0));
    CHECK(!self_update_url_allowed("https://release-assets.githubusercontent.com/x/y?z=1", 0));
    CHECK(self_update_url_allowed("https://release-assets.githubusercontent.com/x/y?z=1", 1));
    CHECK(!self_update_url_allowed("http://github.com/o/r/releases/download/v1/a.zip", 0));
    CHECK(!self_update_url_allowed("https://github.com.evil.example/o/r/a.zip", 1));
    CHECK(!self_update_url_allowed("https://github.com@evil.example/o/r/a.zip", 1));
    CHECK(!self_update_url_allowed("https://github.com/o/r/a b.zip", 0));
    CHECK(!self_update_url_allowed(nullptr, 0));
    // GitHub's redirect carries a long signed query; the catalog's own address never does.
    const std::string query(1500, 'q');
    CHECK(self_update_url_allowed(
        ("https://release-assets.githubusercontent.com/a?jwt=" + query).c_str(), 1));
    CHECK(!self_update_url_allowed(("https://github.com/o/r/a.zip?x=" + query).c_str(), 0));
    CHECK(!self_update_url_allowed(
        ("https://release-assets.githubusercontent.com/a?jwt=" + std::string(5000, 'q')).c_str(),
        1));

    char text[32];
    self_update_time_left(0, 100u << 20, 5.0 * (1u << 20), text, sizeof(text));
    CHECK(std::string(text) == "about 20 s left");
    self_update_time_left(99u << 20, 100u << 20, 5.0 * (1u << 20), text, sizeof(text));
    CHECK(std::string(text) == "a few seconds left");
    self_update_time_left(0, 3000ull << 20, 5.0 * (1u << 20), text, sizeof(text));
    CHECK(std::string(text) == "about 10 min left");
    self_update_time_left(0, 100, 10.0, text, sizeof(text));
    CHECK(text[0] == '\0'); // too slow to say
    self_update_time_left(100, 100, 1e6, text, sizeof(text));
    CHECK(text[0] == '\0');

    // Members are found only where a member can start, never inside a string value.
    const std::string json = "{\"note\":\"x \\\"size\\\":7 y\", \"size\" : 42,\"sequence\":9}";
    uint64_t number = 0;
    CHECK(member_number(json.data(), json.size(), "size", &number) && number == 42);
    CHECK(member_number(json.data(), json.size(), "sequence", &number) && number == 9);
    CHECK(!member_number(json.data(), json.size(), "missing", &number));
    const std::string real = "{\"size\":1.5}";
    CHECK(!member_number(real.data(), real.size(), "size", &number));
}

void test_check()
{
    Console console;
    World w;
    w.console = &console;
    world = &w;
    const std::string archive = make_archive(title, "01.000.010", "new program");
    publish(w, "01.000.010", archive, 72);
    self_update_offer offer;

    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
    CHECK(std::string(offer.title) == title && std::string(offer.name) == "Test App");
    CHECK(std::string(offer.available) == "01.000.010" && std::string(offer.version) == "1.1.0");
    CHECK(offer.size == archive.size() && std::string(offer.sha256) == digest_of(archive));
    CHECK(w.has_sequence && w.stored_sequence == 72);
    CHECK(self_update_check(&platform, title, "01.000.010", &offer) == SELF_UPDATE_UP_TO_DATE);
    CHECK(self_update_check(&platform, "bad", "01.000.000", &offer) == SELF_UPDATE_UNKNOWN);

    // A catalog older than one already accepted is refused; the same one is fine.
    publish(w, "01.000.010", archive, 71);
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNTRUSTED);
    publish(w, "01.000.010", archive, 72);
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);

    // A signature that doesn't verify, and one of the wrong length.
    w.signature = std::string(64, 'X');
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNTRUSTED);
    w.signature = std::string(63, 'S');
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNTRUSTED);
    w.signature = std::string(64, 'S');

    // The app's file changed after the manifest was signed.
    const std::string honest = w.app_file;
    w.app_file.replace(w.app_file.find("1.1.0"), 5, "6.6.6");
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNTRUSTED);
    w.app_file = honest;

    // Not in the manifest, and not on the server.
    const std::string manifest = w.manifest;
    w.manifest.replace(w.manifest.find("apps/PPSA12345"), 14, "apps/PPSA54321");
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNKNOWN);
    w.manifest = manifest;
    w.app_status = 404;
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_UNKNOWN);
    w.app_status = 200;

    // Newer, but an image or a download from somewhere else: not installable.
    for (const auto &change : {std::pair<std::string, std::string>{"\"zip\"", "\"ffpkg\""},
                               {"https://github.com/", "https://example.org/"}})
    {
        w.app_file = honest;
        w.app_file.replace(w.app_file.find(change.first), change.first.size(), change.second);
        w.manifest = manifest;
        w.manifest.replace(w.manifest.find(digest_of(honest)), 64, digest_of(w.app_file));
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) ==
              SELF_UPDATE_NOT_INSTALLABLE);
    }
    world = nullptr;
}

void test_update_applies()
{
    Console console;
    World w;
    w.console = &console;
    world = &w;
    publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
    self_update_offer offer;
    CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);

    static self_update_job job;
    self_update_status status;
    CHECK(self_update_start(&job, &platform, &offer) == 1);
    CHECK(wait_while_busy(job, status) == SELF_UPDATE_READY);
    // Staged beside the app, which still runs untouched.
    CHECK(read_file(console.drive + "/self-update/" + title + "/staged/eboot.bin") ==
          "new program");
    CHECK(read_file(console.target + "/eboot.bin") == "old program");

    CHECK(self_update_apply(&job) == 1);
    self_update_poll(&job, &status);
    CHECK(status.phase == SELF_UPDATE_APPLYING);
    usleep(400000); // The helper waits: the app is still running.
    CHECK(read_file(console.target + "/eboot.bin") == "old program");
    console.close_app();
    join_helper(w);
    self_update_finish(&job);

    CHECK(w.helper_result == 0);
    CHECK(read_file(console.target + "/eboot.bin") == "new program");
    CHECK(read_file(console.target + "/sce_sys/param.json") == param_json(title, "01.000.010"));
    // The console starts only files everyone may read and run.
    for (const char *path : {"/eboot.bin", "/sce_sys", "/sce_sys/param.json", "/assets/new.txt"})
    {
        struct stat info
        {
        };
        CHECK(stat((console.target + path).c_str(), &info) == 0 && (info.st_mode & 0777) == 0777);
    }
    CHECK(read_file(console.target + "/assets/new.txt").size() == 200000);
    CHECK(self_update::kind(console.target + "/assets/old.txt") == self_update::Kind::absent);
    CHECK(read_file(console.target + "/user-note.txt") == "keep me");
    CHECK(self_update::kind(console.drive + "/self-update") == self_update::Kind::absent);
    CHECK(read_file(console.registered + "/appmeta/" + title + "/param.json") ==
          param_json(title, "01.000.010"));
    CHECK(console.notices.size() == 1 &&
          console.notices[0] == "Test App was updated to 1.1.0. Open it again.");
    world = nullptr;
}

// Runs a job that is expected to stop before anything is replaced.
std::string failing_job(World &w, const self_update_offer &offer, self_update_phase expected)
{
    static self_update_job job;
    self_update_status status;
    CHECK(self_update_start(&job, &platform, &offer) == 1);
    CHECK(wait_while_busy(job, status) == expected);
    self_update_cancel(&job);
    join_helper(w);
    self_update_finish(&job);
    return status.error;
}

void test_refusals()
{
    self_update_offer offer;
    {
        // The download isn't the file the catalog lists.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        w.archive[w.archive.size() / 2] ^= 1;
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) ==
              "The download doesn't match the catalog's listing");
        CHECK(console.untouched() && console.notices.empty());
    }
    {
        // The connection drops part-way.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        w.fail_download_after = 1;
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) == "The download failed");
        CHECK(console.untouched());
    }
    {
        // The archive holds another version than the catalog says.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.009", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) ==
              "The download isn't the version the catalog lists");
        CHECK(console.untouched());
    }
    {
        // The archive is another app.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive("PPSA54321", "01.000.010", "another app"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        CHECK(!failing_job(w, offer, SELF_UPDATE_FAILED).empty());
        CHECK(console.untouched());
    }
    {
        // The running app isn't the version on disk.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.005", &offer) == SELF_UPDATE_AVAILABLE);
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) ==
              "The installed files aren't the version that is running");
        CHECK(console.untouched());
    }
    {
        // Two copies of the app are installed.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        const std::string second = console.drive + "/homebrew/copy";
        CHECK(mkdir(second.c_str(), 0755) == 0 && mkdir((second + "/sce_sys").c_str(), 0755) == 0);
        write_file(second + "/sce_sys/param.json", param_json(title, "01.000.000"));
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) ==
              "More than one copy of the app is installed");
        CHECK(console.untouched());
    }
    {
        // No payload loader is listening.
        Console console;
        World w;
        w.console = &console;
        w.loader = false;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        CHECK(failing_job(w, offer, SELF_UPDATE_FAILED) ==
              "The update helper couldn't be started. Is the payload loader running?");
        CHECK(console.untouched());
    }
    world = nullptr;
}

void test_cancel_and_stuck_app()
{
    self_update_offer offer;
    {
        // Cancelled once staged: everything the helper wrote is removed.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        static self_update_job job;
        self_update_status status;
        CHECK(self_update_start(&job, &platform, &offer) == 1);
        CHECK(wait_while_busy(job, status) == SELF_UPDATE_READY);
        self_update_cancel(&job);
        self_update_poll(&job, &status);
        CHECK(status.phase == SELF_UPDATE_CANCELLED);
        CHECK(self_update_apply(&job) == 0);
        join_helper(w);
        self_update_finish(&job);
        CHECK(w.helper_result == 1 && console.untouched() && console.notices.empty());
    }
    {
        // The go-ahead was given but the app never closes: nothing is replaced.
        Console console;
        World w;
        w.console = &console;
        world = &w;
        publish(w, "01.000.010", make_archive(title, "01.000.010", "new program"), 5);
        CHECK(self_update_check(&platform, title, "01.000.000", &offer) == SELF_UPDATE_AVAILABLE);
        static self_update_job job;
        self_update_status status;
        CHECK(self_update_start(&job, &platform, &offer) == 1);
        CHECK(wait_while_busy(job, status) == SELF_UPDATE_READY);
        CHECK(self_update_apply(&job) == 1);
        join_helper(w);
        self_update_finish(&job);
        CHECK(w.helper_result == 1 && console.untouched());
        CHECK(console.notices.size() == 1 &&
              console.notices[0] ==
                  "Test App wasn't updated: it didn't close. Nothing was changed.");
    }
    world = nullptr;
}

void test_swap_rolls_back()
{
    Console console;
    const std::string staged = console.drive + "/staged", backup = console.drive + "/backup";
    CHECK(mkdir(staged.c_str(), 0755) == 0);
    write_file(staged + "/eboot.bin", "new program");
    // The app's folder can't be changed: nothing moves, and no backup is left behind.
    CHECK(chmod(console.target.c_str(), 0555) == 0);
    if (geteuid() != 0)
    {
        CHECK(!self_update::swap_entries(console.target, staged, backup));
        CHECK(read_file(console.target + "/eboot.bin") == "old program");
        CHECK(read_file(staged + "/eboot.bin") == "new program");
        CHECK(self_update::kind(backup) == self_update::Kind::absent);
    }
    CHECK(chmod(console.target.c_str(), 0755) == 0);
    CHECK(self_update::swap_entries(console.target, staged, backup));
    CHECK(read_file(console.target + "/eboot.bin") == "new program");
    CHECK(read_file(backup + "/eboot.bin") == "old program");
    CHECK(read_file(console.target + "/assets/old.txt") == "left over from the old version");
    CHECK(read_file(console.target + "/user-note.txt") == "keep me");

    CHECK(self_update::title_running(console.sandboxes, title) == 1);
    console.close_app();
    CHECK(self_update::title_running(console.sandboxes, title) == 0);
    CHECK(self_update::title_running(console.root + "/nowhere", title) == -1);
    CHECK(self_update::title_running(console.sandboxes, "bad") == -1);
    CHECK(mkdir(console.sandbox.c_str(), 0755) == 0); // for the fixture's own cleanup
    CHECK(rmdir(console.sandbox.c_str()) == 0);
}
} // namespace

int main()
{
    test_pieces();
    test_check();
    test_update_applies();
    test_refusals();
    test_cancel_and_stuck_app();
    test_swap_rolls_back();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d self-update check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
