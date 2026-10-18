// ProsperoStore - The debug build's libcurl probe: one request with everything curl says.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag/trace.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace store::diag
{
namespace
{
constexpr const char *kCa = "/system/common/cert/CA_LIST.cer";
constexpr const char *kUrl = "https://prosperostore.net/api/v1/manifest.json";

std::string error_text()
{
    return std::to_string(errno) + " " + std::strerror(errno);
}

// The pieces libcurl and OpenSSL need, one at a time.
void probe_pieces(const char *when)
{
    struct stat info
    {
    };
    if (stat(kCa, &info) != 0)
        trace("[%s] curl: CA list not found (%s)", when, error_text().c_str());
    else if (const int fd = open(kCa, O_RDONLY); fd < 0)
        trace("[%s] curl: CA list %lld bytes, can't be opened (%s)", when,
              static_cast<long long>(info.st_size), error_text().c_str());
    else
    {
        char head[16];
        const auto got = read(fd, head, sizeof(head));
        close(fd);
        trace("[%s] curl: CA list %lld bytes, read %zd", when, static_cast<long long>(info.st_size),
              got);
    }
    for (const char *device : {"/dev/urandom", "/dev/random"})
    {
        const int fd = open(device, O_RDONLY);
        if (fd < 0)
        {
            trace("[%s] curl: %s can't be opened (%s)", when, device, error_text().c_str());
            continue;
        }
        unsigned char bytes[16];
        const auto got = read(fd, bytes, sizeof(bytes));
        close(fd);
        trace("[%s] curl: %s read %zd bytes", when, device, got);
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    const int resolved = getaddrinfo("prosperostore.net", "443", &hints, &found);
    trace("[%s] curl: getaddrinfo prosperostore.net = %d (%s)", when, resolved,
          resolved == 0 ? "ok" : gai_strerror(resolved));
    if (found)
        freeaddrinfo(found);
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        trace("[%s] curl: socket() failed (%s)", when, error_text().c_str());
    else
    {
        int enabled = 1;
        const int nbio = setsockopt(sock, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled));
        trace("[%s] curl: socket() ok, SO_NBIO %s", when, nbio == 0 ? "ok" : error_text().c_str());
        close(sock);
    }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0)
    {
        trace("[%s] curl: socketpair ok", when);
        close(pair[0]);
        close(pair[1]);
    }
    else
        trace("[%s] curl: socketpair failed (%s)", when, error_text().c_str());
    if (pipe(pair) == 0)
    {
        trace("[%s] curl: pipe ok", when);
        close(pair[0]);
        close(pair[1]);
    }
    else
        trace("[%s] curl: pipe failed (%s)", when, error_text().c_str());
    pthread_t thread;
    const int started =
        pthread_create(&thread, nullptr, +[](void *) -> void * { return nullptr; }, nullptr);
    if (started == 0)
        pthread_join(thread, nullptr);
    trace("[%s] curl: thread start = %d", when, started);
}

struct Capture
{
    const char *when;
    int lines = 0;
};
} // namespace

void curl_probe(const char *when)
{
    const auto *version = curl_version_info(CURLVERSION_NOW);
    trace("[%s] curl: libcurl %s, %s, async DNS %s, threadsafe %s", when, version->version,
          version->ssl_version ? version->ssl_version : "no TLS",
          (version->features & CURL_VERSION_ASYNCHDNS) ? "yes" : "no",
          (version->features & CURL_VERSION_THREADSAFE) ? "yes" : "no");
    probe_pieces(when);
    const auto global = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (global != CURLE_OK)
    {
        trace("[%s] curl: global init %d (%s)", when, static_cast<int>(global),
              curl_easy_strerror(global));
        return;
    }
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        trace("[%s] curl: easy init failed", when);
        curl_global_cleanup();
        return;
    }
    char message[CURL_ERROR_SIZE] = {};
    Capture capture{when};
    // The same options as the store's own requests (net/curl_request.cpp), plus curl's story.
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, message);
    curl_easy_setopt(curl, CURLOPT_CAINFO, kCa);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    const auto socket_options = +[](void *, curl_socket_t socket, curlsocktype) -> int
    {
        int enabled = 1;
        return setsockopt(socket, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) == 0
                   ? CURL_SOCKOPT_OK
                   : CURL_SOCKOPT_ERROR;
    };
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, socket_options);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
    curl_easy_setopt(curl, CURLOPT_URL, kUrl);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &capture);
    curl_easy_setopt(
        curl, CURLOPT_DEBUGFUNCTION,
        +[](CURL *, curl_infotype type, char *data, size_t size, void *opaque) -> int
        {
            auto &c = *static_cast<Capture *>(opaque);
            if (type != CURLINFO_TEXT || c.lines >= 30)
                return 0;
            ++c.lines;
            std::string line(data, size);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            trace("[%s] curl says: %s", c.when, line.c_str());
            return 0;
        });
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    long os_error = 0;
    curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &os_error);
    trace("[%s] curl: result %d (%s), HTTP %ld, os errno %ld, detail: %s", when,
          static_cast<int>(result), curl_easy_strerror(result), status, os_error,
          message[0] ? message : "none");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
}
} // namespace store::diag
