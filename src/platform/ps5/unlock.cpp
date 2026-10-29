// ProsperoStore - Ways out of the sandbox, tried in an order and remembered.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "unlock.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace store::ps5
{
namespace
{
// The marker goes in /download0, which a sandboxed title can write, since it is written before the
// console is unlocked. The route is read before the unlock (a sandboxed title can open files in
// /data) and written only after /data was proved, as the elevated user, so reclaim_store_folder has
// no foreign entry to take back.
constexpr char marker_path[] = "/download0/prosperostore_unlock_attempt";
constexpr char route_dir[] = "/data/prosperostore/cache";
constexpr char route_path[] = "/data/prosperostore/cache/unlock-route";
constexpr std::array<const char *, 2> request_paths{"/download0/etahen_jailbreak",
                                                    "/download0/onionhen_jailbreak"};
constexpr useconds_t poll_us = 16667;

enum class Route
{
    none,
    hen,
    lapy,
};

const char *selected = "none";

// etaHEN's HijackerCommand, as its legacy command server reads it.
struct HijackerCommand
{
    int magic; // 0xDEADBEEF
    int cmd;   // 5: jailbreak
    int pid;
    int ret; // -1337 until answered
    char msg1[0x500];
    char msg2[0x500];
};

// /data lists (that is what a sandbox with ShadowMountPlus refuses) and a file written there reads
// back.
bool data_ready(pid_t pid) noexcept
{
    DIR *folder = opendir("/data");
    if (folder == nullptr)
        return false;
    (void)closedir(folder);
    std::array<char, 80> path{};
    std::snprintf(path.data(), path.size(), "/data/.prosperostore_probe_%ld",
                  static_cast<long>(pid));
    (void)unlink(path.data());
    const int fd = open(path.data(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return false;
    constexpr std::array<char, 4> token{'H', 'E', 'N', '\n'};
    std::array<char, 4> actual{};
    const bool passed =
        write(fd, token.data(), token.size()) == static_cast<ssize_t>(token.size()) &&
        lseek(fd, 0, SEEK_SET) == 0 &&
        read(fd, actual.data(), actual.size()) == static_cast<ssize_t>(actual.size()) &&
        actual == token;
    (void)close(fd);
    (void)unlink(path.data());
    return passed;
}

bool wait_for_data(pid_t pid, unsigned polls) noexcept
{
    for (unsigned poll = 0; poll < polls; ++poll)
    {
        (void)usleep(poll_us);
        if (data_ready(pid))
            return true;
    }
    return false;
}

bool publish(const char *path, pid_t pid) noexcept
{
    std::array<char, 80> staged{};
    std::array<char, 32> body{};
    std::snprintf(staged.data(), staged.size(), "%s.tmp", path);
    const int length =
        std::snprintf(body.data(), body.size(), "{\"PID\":%ld}\n", static_cast<long>(pid));
    (void)unlink(staged.data());
    (void)unlink(path);
    const int fd = open(staged.data(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
        return false;
    (void)fchmod(fd, 0666);
    const bool written = length > 0 &&
                         write(fd, body.data(), static_cast<std::size_t>(length)) == length &&
                         fsync(fd) == 0;
    (void)close(fd);
    if (!written || rename(staged.data(), path) != 0)
    {
        (void)unlink(staged.data());
        return false;
    }
    return true;
}

// Writes the request files; false when none could be written.
bool publish_all(pid_t pid) noexcept
{
    int published = 0;
    for (const char *path : request_paths)
        published += publish(path, pid) ? 1 : 0;
    return published > 0;
}

void withdraw_all() noexcept
{
    for (const char *path : request_paths)
        (void)unlink(path);
}

// etaHEN's command server. True when the request went out whole; whether it worked is for /data to
// say.
bool ask_etahen_server(pid_t pid) noexcept
{
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
    const timeval limit{2, 0};
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
    sockaddr_in address{};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(9028);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool sent = false;
    if (connect(sock, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
    {
        static HijackerCommand command;
        std::memset(&command, 0, sizeof(command));
        command.magic = static_cast<int>(0xDEADBEEF);
        command.cmd = 5;
        command.pid = static_cast<int>(pid);
        command.ret = -1337;
        sent = send(sock, &command, sizeof(command), 0) == static_cast<ssize_t>(sizeof(command));
        // Read the reply through so the server is not left talking to a closed socket.
        std::size_t got = 0;
        while (sent && got < sizeof(command))
        {
            const ssize_t n =
                recv(sock, reinterpret_cast<char *>(&command) + got, sizeof(command) - got, 0);
            if (n <= 0)
                break;
            got += static_cast<std::size_t>(n);
        }
    }
    (void)close(sock);
    return sent;
}

enum class Hen
{
    granted,
    no_answer,
    cannot_request,
    skipped_after_crash,
};

// The marker is there while a request is out. A request that closed the store leaves it behind, and
// the next start skips this way once, so a HEN that kills the store on a request cannot make a
// crash loop.
bool arm_marker() noexcept
{
    const int fd = open(marker_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0)
    {
        (void)fsync(fd);
        (void)close(fd);
    }
    return fd >= 0;
}

// `full` waits ~1.5 s for the files, then asks the command server; otherwise ~0.3 s for the files
// only, when another way is known to work and a loaded HEN answers within a few frames.
Hen ask_hen(pid_t pid, bool full) noexcept
{
    if (access(marker_path, F_OK) == 0)
    {
        if (full)
            (void)unlink(marker_path);
        return Hen::skipped_after_crash;
    }
    (void)arm_marker();
    (void)seteuid(geteuid()); // the store's own credential, before a HEN changes it
    Hen result = Hen::cannot_request;
    if (publish_all(pid))
    {
        result = wait_for_data(pid, full ? 90 : 18) ? Hen::granted : Hen::no_answer;
        withdraw_all();
        if (full && result == Hen::no_answer && ask_etahen_server(pid) && wait_for_data(pid, 60))
            result = Hen::granted;
    }
    (void)unlink(marker_path);
    return result;
}

Route remembered() noexcept
{
    std::array<char, 16> word{};
    const int fd = open(route_path, O_RDONLY);
    if (fd < 0)
        return Route::none;
    (void)read(fd, word.data(), word.size() - 1);
    (void)close(fd);
    if (std::strncmp(word.data(), "lapy", 4) == 0)
        return Route::lapy;
    return std::strncmp(word.data(), "hen", 3) == 0 ? Route::hen : Route::none;
}

void remember(Route route) noexcept
{
    if (route == Route::none)
    {
        (void)unlink(route_path); // refused while still sandboxed; the old route then stays
        return;
    }
    if (remembered() == route)
        return;
    (void)mkdir("/data/prosperostore", 0777);
    (void)mkdir(route_dir, 0777);
    const int fd = open(route_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    const char *word = route == Route::lapy ? "lapy\n" : "hen\n";
    (void)write(fd, word, std::strlen(word));
    (void)close(fd);
}

// The Lapy client, with the route it took noted for the log.
elevation::Status ask_lapy() noexcept
{
    const elevation::Status status = elevation::request(elevation::Capability::filesystem);
    selected = elevation::path();
    if (status == elevation::Status::ok)
        remember(Route::lapy);
    return status;
}
} // namespace

elevation::Status unlock_filesystem() noexcept
{
    selected = "none";
    const pid_t pid = getpid();
    if (data_ready(pid))
    {
        selected = "existing";
        return elevation::Status::ok;
    }
    // The way that worked at the last start goes first, so a console that always answers the same
    // way does not wait for the others to stay silent. If it fails, the usual order runs.
    const bool lapy_first = remembered() == Route::lapy;
    elevation::Status lapy = elevation::Status::unavailable;
    bool lapy_asked = false;
    if (lapy_first)
    {
        if (ask_hen(pid, false) == Hen::granted)
        {
            selected = "hen";
            remember(Route::hen);
            return elevation::Status::ok;
        }
        lapy = ask_lapy();
        lapy_asked = true;
        if (lapy == elevation::Status::ok)
            return lapy;
    }
    if (ask_hen(pid, true) == Hen::granted)
    {
        selected = "hen";
        remember(Route::hen);
        return elevation::Status::ok;
    }
    // Lapy is asked once per process.
    if (!lapy_asked)
    {
        lapy = ask_lapy();
        if (lapy == elevation::Status::ok)
            return lapy;
    }
    remember(Route::none); // nothing answered: forget the old way
    return lapy;
}

const char *unlock_route() noexcept
{
    return selected;
}
} // namespace store::ps5
