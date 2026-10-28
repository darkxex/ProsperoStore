// ProsperoStore - Durable logs and crash recovery lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag/diagnostics.hpp"
#include "core/save_file.hpp"
#include "../../examples/crash-report/crash_report.hpp"
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>

extern "C" int sceSystemServiceLoadExec(const char *, const char **);
namespace store::diag
{
namespace
{
void lifecycle(bool restart)
{
    sceSystemServiceLoadExec(restart ? "/app0/eboot.bin" : "exit", nullptr);
}
// The log is buffered and written out five times a second from here. An
// unbuffered log on /data cost the render thread tens of milliseconds for
// every line the OpenGL runtime prints; at most a fifth of a second of log
// is lost if the app dies.
std::atomic<bool> flushing{false};
std::atomic<bool> holding{false};
pthread_t flusher{};
void *flush_log(void *)
{
    while (flushing.load())
    {
        // While the installer keeps the disk busy a write can take most of a
        // second, and the render thread would wait for it at its next log
        // line: the log stays in memory until the disk is quiet again.
        if (!holding.load())
        {
            std::fflush(stdout);
            std::fflush(stderr);
        }
        usleep(200000);
    }
    return nullptr;
}
void rotate(const std::string &path)
{
    for (int i = 4; i > 0; --i)
        std::rename((path + "." + std::to_string(i)).c_str(),
                    (path + "." + std::to_string(i + 1)).c_str());
    std::rename(path.c_str(), (path + ".1").c_str());
}
} // namespace
bool start(const std::string &root)
{
    if (!hui::save::ensure_directory(root) || !hui::save::ensure_directory(root + "/logs"))
        return false;
    const auto directory = root + "/logs";
    const auto log = directory + "/app.log";
    rotate(log);
    rotate(directory + "/crash-latest.txt");
    // Reopen the runtime's FILE objects as in the qualified Eden lifecycle.
    std::fflush(nullptr);
    const bool redirected =
        std::freopen(log.c_str(), "a", stdout) && std::freopen(log.c_str(), "a", stderr);
    if (redirected)
    {
        std::setvbuf(stdout, nullptr, _IOFBF, 1024 * 1024);
        std::setvbuf(stderr, nullptr, _IOFBF, 16 * 1024);
        flushing = true;
        if (pthread_create(&flusher, nullptr, flush_log, nullptr) != 0)
        {
            flushing = false;
            std::setvbuf(stdout, nullptr, _IONBF, 0);
            std::setvbuf(stderr, nullptr, _IONBF, 0);
        }
    }
    return redirected && crash_report::install(directory.c_str(), "01.000.115", lifecycle);
}
void hold_log(bool hold)
{
    holding = hold;
}
void stop()
{
    if (flushing.exchange(false))
        pthread_join(flusher, nullptr);
    std::fflush(nullptr);
    crash_report::stop();
}
bool recovered()
{
    return crash_report::recovered();
}
} // namespace store::diag
