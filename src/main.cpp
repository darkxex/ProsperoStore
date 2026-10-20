// ProsperoStore - Native display, input, audio and clean application lifecycle.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "app/service.hpp"
#include "audio/cues.hpp"
#include "audio/music.hpp"
#include "core/frame_stats.hpp"
#include "core/i18n.hpp"
#include "core/image.hpp"
#include "core/qr.hpp"
#include "core/version.hpp"
#include "core/save_file.hpp"
#include "diag/diagnostics.hpp"
#ifdef STORE_DEBUG_TRACE
#include "diag/trace.hpp"
#endif
#ifdef STORE_DEVELOPMENT
#include "diag/fsbench.hpp"
#include "system/worker_launch.hpp"
#endif
#include "system/title_registry.hpp"
#include "system/app_folder.hpp"
#include "system/store_folder.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/ime.hpp"
#include "platform/ps5/keyboard.hpp"
#include "platform/ps5/ime_abi.hpp"
#include "platform/ps5/system.hpp"
#include "platform/ps5/unlock.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/stat.h>
#include <cstdlib>
#include <map>
#include <mutex>
#include <GL/glcorearb.h>

extern "C" int sceUserServiceGetInitialUser(int *user);

namespace
{
std::atomic<bool> quit{false};
std::string request_path;
std::string handled_path;
std::string run_token;
// Requests of a scripted run, handed from the reader thread to the frame loop.
std::mutex remote_guard;
std::vector<std::pair<std::string, std::string>> remote_requests;

void *development_requests(void *)
{
#ifdef STORE_DEVELOPMENT
    while (!quit.load())
    {
        std::string request;
        if (hui::save::read_file(request_path, &request, 256))
        {
            char verb[16]{};
            char argument[64]{};
            char token[64]{};
            std::string handled;
            hui::save::read_file(handled_path, &handled, 64);
            if (std::sscanf(request.c_str(), "%15s %63s %63s", verb, argument, token) == 3 &&
                handled != token && hui::save::write_atomic(handled_path, token).empty())
            {
                if (std::strcmp(verb, "quit") == 0)
                {
                    hui::sys::log("[STORE] remote clean exit token=%s", token);
                    quit.store(true);
                }
                else
                {
                    hui::sys::log("[STORE] remote request %s %s token=%s", verb, argument, token);
                    std::lock_guard lock(remote_guard);
                    remote_requests.emplace_back(verb, argument);
                }
            }
        }
        hui::sys::sleep_us(250000);
    }
#endif
    return nullptr;
}
} // namespace

int main()
{
    using namespace hui;
    sys::log("[STORE] PPSA99000 startup");
    // Start-up steps, timed from here: where the wait before the first frame goes.
    const std::int64_t started_us = sys::monotonic_us();
    const auto since_start = [started_us]
    { return static_cast<long long>((sys::monotonic_us() - started_us) / 1000); };
    // The system keyboard's library is refused once the store has left its
    // sandbox (the loader answers 0x63), so it is loaded while still inside.
    const int keyboard_dialog = sceCommonDialogInitialize();
    const int keyboard_module = sceSysmoduleLoadModule(0x0096);
    // USB keyboards for moving around: the same rule, so their library loads here too.
    store::ps5::Keyboards keyboards;
    const int keyboards_ready = keyboards.prepare();
    sys::log("[STORE] usb keyboard library rc=0x%08x", static_cast<unsigned>(keyboards_ready));
    // The console's app-install service: the home screen's list of titles.
    const int registry = store::system::prepare_title_registry();
    sys::log("[STORE] title registry rc=0x%08x", static_cast<unsigned>(registry));
    sys::log("[STORE] keyboard preload dialog=0x%08x module=0x%08x",
             static_cast<unsigned>(keyboard_dialog), static_cast<unsigned>(keyboard_module));
#ifdef STORE_DEBUG_TRACE
    // The "Debug log" setting, read as early as the settings file can be: before elevation
    // that is only where ShadowMountPlus already mounted /data into the sandbox.
    const auto debug_wanted = []
    {
        std::string saved;
        return hui::save::read_file("/data/prosperostore/settings.txt", &saved, 4096) &&
               store::parse_settings(saved).debug_log;
    };
    store::diag::set_enabled(debug_wanted());
    store::diag::trace("ProsperoStore started");
    store::diag::trace_firmware();
    store::diag::trace("keyboard library 0x%08x, usb keyboards 0x%08x, title registry 0x%08x",
                       static_cast<unsigned>(keyboard_module),
                       static_cast<unsigned>(keyboards_ready), static_cast<unsigned>(registry));
    if (store::diag::enabled())
    {
        store::diag::trace_console("start");
        store::diag::curl_probe("sandboxed");
    }
#endif
#ifdef STORE_SANDBOX_CONTROL
    const auto elevation_status = elevation::Status::unavailable;
#else
    const auto elevation_status = store::ps5::unlock_filesystem();
#endif
    constexpr const char *storage_root = "/data/prosperostore";
    const bool elevated = elevation_status == elevation::Status::ok;
    // Wherever the store is installed (system/app_folder.hpp), now that the root may have changed.
    const std::string app_root = store::system::app_folder();
    sys::log("[STORE] app folder=%s", app_root.c_str());
    request_path = std::string(storage_root) + "/dev/request.txt";
    handled_path = std::string(storage_root) + "/handled.txt";
    // A folder left by a sandboxed run is taken back before anything is written to it.
    const std::string reclaimed =
        elevated ? store::system::reclaim_store_folder(storage_root) : std::string("not elevated");
    sys::log("[STORE] store folder: %s", reclaimed.c_str());
#ifdef STORE_DEBUG_TRACE
    store::diag::trace("store folder: %s", reclaimed.c_str());
#endif
    if (elevated && !store::diag::start(storage_root))
        sys::log("[STORE] persistent diagnostics unavailable");
#ifdef STORE_DEVELOPMENT
    if (elevated)
        hui::save::read_file(std::string(storage_root) + "/dev/run.txt", &run_token, 64);
    sys::log("[STORE] run start token=%s", run_token.c_str());
#endif
    sys::log("[STORE] elevation status=%u path=%s at_ms=%lld",
             static_cast<unsigned>(elevation_status), store::ps5::unlock_route(), since_start());
    const int transport = store::net::start_transport(elevation_status == elevation::Status::ok);
    sys::log("[STORE] transport startup rc=0x%08x", static_cast<unsigned>(transport));
#ifdef STORE_DEBUG_TRACE
    // Status: 0 ok, 1 invalid request, 3 unsupported, 5 unavailable, 6 prepare failed,
    // 7 apply failed, 9 transport error (no answer from Lapy), 11 timeout.
    store::diag::trace("elevation (Lapy): status %u via %s after %lld ms",
                       static_cast<unsigned>(elevation_status), store::ps5::unlock_route(),
                       since_start());
    store::diag::trace("elevation stopped at: %s, value %d (0x%08x)", elevation::step(),
                       elevation::step_code(), static_cast<unsigned>(elevation::step_code()));
    store::diag::set_enabled(debug_wanted());
    const bool debug_at_start = store::diag::enabled();
    if (debug_at_start)
    {
        store::diag::trace_console("after elevation");
        if (elevated)
            store::diag::curl_probe("after elevation");
    }
    store::diag::trace("app folder: %s; network: %s (0x%08x)", app_root.c_str(),
                       store::net::transport_name(), static_cast<unsigned>(transport));
#endif
    ps5::Display display;
    if (!display.open(3840, 2160))
    {
        sys::log("[STORE] display failed");
        sys::quit();
    }
    gfx::Renderer renderer;
    store::Fonts fonts;
    const bool rendered = renderer.init();
    const bool loaded_fonts = rendered && fonts.load(renderer, app_root + "/assets");
    if (!rendered || !loaded_fonts)
    {
        sys::log("[STORE] initialization failed renderer=%d fonts=%d root=%s", rendered,
                 loaded_fonts, app_root.c_str());
        sys::quit();
    }
    ps5::Pad pad;
    if (!pad.open())
        sys::log("[STORE] controller unavailable");
    int keyboard_user = -1;
    if (sceUserServiceGetInitialUser(&keyboard_user) < 0)
        keyboard_user = -1;
    PadSample last_pad{}; // the controller's latest state, before keys are added to it
    int keyboards_seen = 0;
    audio::Mixer mixer;
    audio::SoundBank sounds;
    const auto bank = sounds.load(app_root + "/assets/audio/sfx");
    sys::log("[STORE] sound files=%d rejected=%d at_ms=%lld", bank.files, bank.rejected,
             since_start());
    // The store's song, looping quietly under everything (as in ProsperoPuzzles).
    // The stream attaches before the audio thread starts; the bus stays silent
    // until the intro hands over, then fades in.
    audio::MusicPlayer music;
    const int songs = music.init(mixer, app_root + "/assets/audio/music",
                                 static_cast<std::uint64_t>(sys::monotonic_us()));
    mixer.set_bus_gain(audio::Bus::music, 0.0f);
    sys::log("[STORE] music songs=%d", songs);
    // About 12 dB under the interface sounds: the song is mastered near -15 dBFS,
    // the cues sit near -30 dBFS, and the player already lowers its stream 8 dB.
    constexpr float kMusicLevel = 0.11f;
    float music_level = 0.0f; // 0..1 of kMusicLevel, eased in and out
    ps5::AudioOut audio;
    audio.start(mixer);
    pthread_t request_thread{};
    const bool requests_started =
        elevated && pthread_create(&request_thread, nullptr, development_requests, nullptr) == 0;
    store::Screen screen;
    screen.play_intro();
    // The picture for coming-soon apps that have no artwork of their own.
    std::uint32_t coming_soon_texture = 0;
    {
        std::string encoded;
        Image image;
        if (save::read_file(app_root + "/assets/images/coming-soon.png", &encoded, 2u << 20) &&
            decode_png(encoded, image))
            coming_soon_texture =
                renderer.batch().create_texture(image.width, image.height, image.rgba.data());
        screen.set_coming_soon_art(coming_soon_texture);
    }
    const std::string own_version = read_content_version(app_root + "/sce_sys/param.json");
    store::Service service(elevated ? storage_root : "", own_version);
    screen.set_self("PPSA99000", own_version);
    {
        // What the player chose last time; the defaults when there is no file. A store left in
        // its sandbox keeps them too where /data is shown to it (ShadowMountPlus does that):
        // Debug log has to survive a restart on exactly those consoles.
        const std::string settings_file = std::string(storage_root) + "/settings.txt";
        if (!elevated)
        {
            (void)mkdir(storage_root, 0777);
            const std::string probe = std::string(storage_root) + "/.settings-probe";
            const int descriptor = open(probe.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (descriptor >= 0)
            {
                close(descriptor);
                unlink(probe.c_str());
                service.settings_file = settings_file;
            }
            sys::log("[STORE] settings without elevation: %s",
                     descriptor >= 0 ? "kept in the store folder" : "nowhere to keep them");
#ifdef STORE_DEBUG_TRACE
            store::diag::trace("settings: %s", descriptor >= 0
                                                   ? "kept in /data/prosperostore (no elevation)"
                                                   : "can't be kept (no store folder)");
#endif
        }
        std::string saved;
        save::read_file(settings_file, &saved, 4096);
        // Japanese is offered when its font files are with the app.
        struct stat japanese_font
        {
        };
        store::i18n::set_japanese_available(
            ::stat((app_root + "/assets/fonts/noto-sans-jp-regular.huifont").c_str(),
                   &japanese_font) == 0);
        screen.set_console_language(sys::language_id());
        screen.set_settings(store::parse_settings(saved));
        service.check_updates = screen.settings().check_updates;
        service.show_comments(screen.settings().show_comments);
        // The custom catalogs only when they are switched on; the official one is always signed.
        const auto &chosen = screen.settings();
        const bool custom = chosen.custom_active();
        service.catalogs.clear();
        if (custom)
            for (const auto &name : chosen.catalogs)
                service.catalogs.push_back({store::catalog::github_catalog_api(name), name});
        service.verify_signatures = custom ? chosen.verify_signatures : true;
        service.with_official = !custom || chosen.use_official;
    }
#ifdef STORE_INSTALLER
    service.installer = elevated;
    const char *installer_reason = "This console didn't grant permission to write, so nothing "
                                   "can be installed.";
#else
    const char *installer_reason = "Installing is not switched on in this build.";
#endif
    screen.set_installer(service.installer, false, installer_reason, "/data/homebrew");
#ifdef STORE_DEBUG_TRACE
    store::diag::trace("installer: %s", service.installer ? "on" : installer_reason);
#endif
    if (!service.start())
        screen.set_status("The catalog service could not start");
    if (elevation_status != elevation::Status::ok)
        screen.set_catalog({}, "Read only: install permission unavailable");
    InputTracker input;
    FrameStats stats;
    std::int64_t previous = sys::monotonic_us();
    sys::log("[STORE] interactive width=%d height=%d at_ms=%lld", display.width(), display.height(),
             since_start());
    bool first_swap = true;
    // Pictures are uploaded once and kept for the session. Creating and
    // deleting textures while the focus moves stalled frames for over a
    // second on the console, so nothing is deleted on the way.
    struct Art
    {
        std::uint32_t icon = 0, large = 0, code = 0, ambient = 0;
        std::uint32_t accent = 0; // 0xRRGGBB, from the icon
        int code_width = 0;
        std::string hash;
        std::uint64_t seen = 0; // the last frame it was on screen
    };
    std::map<std::string, Art> textures;
    std::map<std::string, std::string> hashes; // the current catalog's icon hashes
    const auto release = [](Art &art)
    {
        for (auto *texture : {&art.icon, &art.large, &art.code, &art.ambient})
            if (*texture)
                glDeleteTextures(1, texture);
        art = {};
    };
    // A catalog of a thousand apps must not hold a thousand pictures: each
    // kind has a budget, and past it the picture that has been off screen
    // longest gives its texture to the new one. Giving a texture new pixels
    // costs about a millisecond on the console; nothing is created or deleted.
    std::size_t icon_budget = 160; // 256 x 256: about 42 MB
    constexpr std::size_t kLargeBudget = 12, kCodeBudget = 12;
    std::uint64_t frame_number = 0;
    const auto place = [&](std::uint32_t Art::*slot, std::size_t budget, const std::string &id,
                           const Image &image) -> std::uint32_t
    {
        std::size_t used = 0;
        Art *victim = nullptr;
        const std::string *victim_id = nullptr;
        for (auto &[other, art] : textures)
            if (art.*slot)
            {
                ++used;
                if (other != id && art.seen + 120 < frame_number &&
                    (!victim || art.seen < victim->seen))
                {
                    victim = &art;
                    victim_id = &other;
                }
            }
        if (used < budget || !victim)
            return renderer.batch().create_texture(image.width, image.height, image.rgba.data());
        const std::uint32_t texture = victim->*slot;
        victim->*slot = 0;
        if (slot == &Art::icon)
            screen.set_icon(*victim_id, 0);
        else if (slot == &Art::large)
            screen.set_art(*victim_id, 0);
        else if (slot == &Art::ambient)
            screen.set_ambient(*victim_id, 0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.rgba.data());
        return texture;
    };
    std::vector<std::string> requested_icons;
    std::uint64_t catalog_generation = 0;
    std::vector<store::Update> updates;
    [[maybe_unused]] const char *note = "", *previous_note = "";
    [[maybe_unused]] std::int64_t tour_until = 0, tour_next = 0;
    [[maybe_unused]] unsigned tour_step = 0;
    [[maybe_unused]] std::string press_keys; // development: buttons still to press
    [[maybe_unused]] std::int64_t press_next = 0;
    [[maybe_unused]] bool shot_wanted = false;       // development: save the next frame
    [[maybe_unused]] std::string shot_name = "shot"; // ... as /data/prosperostore/dev/<name>.tga
    [[maybe_unused]] int bench_step = -1;
    ps5::Ime keyboard;
    bool keyboard_active = false;
    bool keyboard_catalog = false;
    std::int64_t close_at = 0; // set when the store updates itself: close at this time
    while (!quit.load() && !screen.wants_quit() &&
           (close_at == 0 || sys::monotonic_us() < close_at))
    {
        const auto now = sys::monotonic_us();
        const double ms = static_cast<double>(now - previous) / 1000.0;
        previous = now;
        const float dt = std::clamp(static_cast<float>(ms / 1000.0), 0.001f, 0.1f);
        std::array<PadSample, 64> samples{};
        auto count = pad.read(samples);
        // Keys act as the controller buttons they stand for (app/keyboard_map.hpp), so
        // repeats and holds behave the same. Without a new controller sample this
        // frame, its last state carries the keys.
        keyboards.open(keyboard_user, now);
        if (keyboards.opened() != keyboards_seen)
        {
            keyboards_seen = keyboards.opened();
            sys::log("[STORE] usb keyboards opened=%d", keyboards_seen);
        }
        if (count > 0)
            last_pad = samples[count - 1];
        bool keys_connected = false;
        const std::uint32_t keys = keyboards.buttons(keys_connected);
        if (keys_connected)
        {
            if (count == 0)
            {
                samples[0] = last_pad;
                samples[0].timestamp_us = static_cast<std::uint64_t>(now);
                count = 1;
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                samples[i].buttons |= keys;
                samples[i].connected = true;
            }
        }
        ui::Feedback feedback;
        if (updates.empty())
            service.take(updates);
        // At most one texture upload per frame, including after a render stall.
        if (!updates.empty())
        {
            auto update = std::move(updates.front());
            updates.erase(updates.begin());
            note = update.kind == store::Update::Kind::catalog     ? "catalog"
                   : update.kind == store::Update::Kind::icon      ? "icon"
                   : update.kind == store::Update::Kind::detail    ? "detail"
                   : update.kind == store::Update::Kind::qr        ? "code"
                   : update.kind == store::Update::Kind::inventory ? "inventory"
                                                                   : "update";
            if (update.kind == store::Update::Kind::catalog)
            {
                if (!catalog_generation)
                    sys::log("[STORE] catalog arrived at_ms=%lld", since_start());
                requested_icons.clear();
                catalog_generation = update.generation;
                hashes.clear();
                for (const auto &entry : update.snapshot.entries)
                    if (!entry.icon.empty())
                        hashes[entry.id] = entry.icon_hash;
                // Only a picture that changed, or whose app left the catalog, goes.
                for (auto it = textures.begin(); it != textures.end();)
                {
                    const auto listed = hashes.find(it->first);
                    if (listed == hashes.end() || listed->second != it->second.hash)
                    {
                        release(it->second);
                        it = textures.erase(it);
                    }
                    else
                        ++it;
                }
                std::vector<store::App> apps;
                for (const auto &entry : update.snapshot.entries)
                {
                    apps.push_back({entry.id, entry.name, entry.author, entry.description,
                                    entry.kind, entry.version,
                                    entry.status == "coming_soon" ? "Coming soon" : "", 0,
                                    entry.released, entry.updated});
                    apps.back().available_version = entry.content_version;
                    apps.back().size = entry.size;
                    apps.back().added = entry.added;
                    apps.back().category = entry.category;
                    apps.back().extra = entry.extra;
                    apps.back().console_id = entry.console_id();
                    apps.back().conflicts = entry.conflicts;
                }
#ifdef STORE_DEBUG_TRACE
                store::diag::trace("catalog: %zu apps, %s, %s", apps.size(),
                                   update.snapshot.online ? "online" : "offline",
                                   update.message.c_str());
#endif
                screen.set_catalog(std::move(apps),
                                   elevated ? update.message
                                            : "Read only: install permission unavailable • " +
                                                  update.message,
                                   update.snapshot.online);
                for (const auto &[id, art] : textures)
                {
                    screen.set_icon(id, art.icon);
                    screen.set_art(id, art.large);
                    screen.set_ambient(id, art.ambient);
                    if (art.accent)
                        screen.set_accent(id, hui::gfx::Color::rgb(art.accent));
                }
            }
            else if (update.kind == store::Update::Kind::inventory)
                screen.set_inventory(std::move(update.installed));
            else if (update.kind == store::Update::Kind::notice)
                screen.notify(std::move(update.message), std::move(update.detail));
            else if (update.kind == store::Update::Kind::store_update)
                screen.offer_store_update(std::move(update.message));
            else if (update.kind == store::Update::Kind::news)
                screen.set_news(std::move(update.news));
            else if (update.kind == store::Update::Kind::setup)
            {
                std::vector<std::string> notes;
                std::size_t at = 0;
                while (at < update.detail.size())
                {
                    const auto end = std::min(update.detail.find('\n', at), update.detail.size());
                    if (end > at)
                        notes.push_back(update.detail.substr(at, end - at));
                    at = end + 1;
                }
#ifdef STORE_DEBUG_TRACE
                for (const auto &line : notes)
                    store::diag::trace("ShadowMountPlus settings to check: %s", line.c_str());
#endif
                screen.set_setup_notes(std::move(notes));
            }
            else if (update.kind == store::Update::Kind::job)
            {
                // The update helper waits for the store to close: say so, then close.
                if (update.close)
                {
                    close_at = sys::monotonic_us() + 3500000;
                    sys::log("[STORE] self-update applied: closing");
                }
#ifdef STORE_DEBUG_TRACE
                store::diag::trace("job %s: %s - %s", update.entry.id.c_str(),
                                   update.message.c_str(), update.detail.c_str());
#endif
                screen.finish_job(update.ok, update.restart, std::move(update.message),
                                  std::move(update.detail));
            }
            else if (update.kind == store::Update::Kind::locations)
                screen.set_locations(std::move(update.locations));
            else if (update.kind == store::Update::Kind::likes)
                screen.set_likes(std::move(update.likes));
            else if (update.kind == store::Update::Kind::comments)
                screen.set_comments(update.entry.id, std::move(update.thread));
            else if (update.kind == store::Update::Kind::icon)
            {
                const auto listed = hashes.find(update.entry.id);
                if (update.generation == catalog_generation && listed != hashes.end() &&
                    !textures[update.entry.id].icon)
                {
                    auto &art = textures[update.entry.id];
                    art.hash = listed->second;
                    art.seen = frame_number;
                    art.icon = place(&Art::icon, icon_budget, update.entry.id, update.image);
                    screen.set_icon(update.entry.id, art.icon);
                    // The field made from the icon (20 KB) lives and goes with it.
                    if (!update.ambient.rgba.empty() && !art.ambient)
                        art.ambient =
                            place(&Art::ambient, icon_budget, update.entry.id, update.ambient);
                    screen.set_ambient(update.entry.id, art.ambient);
                    art.accent = update.accent;
                    if (art.accent)
                        screen.set_accent(update.entry.id, hui::gfx::Color::rgb(art.accent));
                }
            }
            else if (update.kind == store::Update::Kind::detail)
            {
                screen.set_detail(update.entry);
                const auto listed = hashes.find(update.entry.id);
                if (!update.image.rgba.empty() && listed != hashes.end() &&
                    !textures[update.entry.id].large)
                {
                    auto &art = textures[update.entry.id];
                    art.hash = listed->second;
                    art.seen = frame_number;
                    art.large = place(&Art::large, kLargeBudget, update.entry.id, update.image);
                    screen.set_art(update.entry.id, art.large);
                }
            }
            else if (update.kind == store::Update::Kind::qr)
            {
                // An app's code never changes: made the first time its page opens.
                auto &art = textures[update.entry.id];
                if (!art.code)
                {
                    art.seen = frame_number;
                    art.code = place(&Art::code, kCodeBudget, update.entry.id, update.image);
                    art.code_width = update.image.width;
                    if (const auto listed = hashes.find(update.entry.id); listed != hashes.end())
                        art.hash = listed->second;
                }
                screen.set_qr(update.entry.id, art.code, art.code_width);
            }
            else if (!update.entry.id.empty())
                screen.set_detail_error(update.entry.id, update.message);
            else
            {
#ifdef STORE_DEBUG_TRACE
                store::diag::trace("catalog failed: %s", update.message.c_str());
#endif
                screen.catalog_failed(update.message);
            }
        }
        if (!screen.pending_detail.empty() && service.request_detail(screen.pending_detail))
            screen.pending_detail.clear();
        if (!screen.pending_developer_link.empty())
        {
            // One code at a time: the last developer's page that was opened.
            static std::uint32_t developer_code = 0;
            const std::string link = std::move(screen.pending_developer_link);
            screen.pending_developer_link.clear();
            Image code;
            if (hui::encode_qr(link, code))
            {
                if (developer_code)
                    glDeleteTextures(1, &developer_code);
                developer_code =
                    renderer.batch().create_texture(code.width, code.height, code.rgba.data());
                screen.set_developer_qr(link, developer_code);
            }
        }
        if (!screen.pending_discussion_link.empty())
        {
            // "Like or comment": one code at a time, for the discussion that was asked for.
            static std::uint32_t discussion_code = 0;
            const std::string link = std::move(screen.pending_discussion_link);
            screen.pending_discussion_link.clear();
            Image code;
            if (store::social::discussion_url(link) && hui::encode_qr(link, code))
            {
                if (discussion_code)
                    glDeleteTextures(1, &discussion_code);
                discussion_code =
                    renderer.batch().create_texture(code.width, code.height, code.rgba.data());
                screen.set_discussion_qr(link, discussion_code, code.width);
            }
        }
        // The page's request goes to the installer; one refused by a busy lock is asked again.
        if (auto &order = screen.pending_order; order.kind != store::Order::Kind::none)
        {
            const bool accepted =
                order.kind == store::Order::Kind::install
                    ? service.request_install(order.entry, order.location, order.replace)
                : order.kind == store::Order::Kind::adopt
                    ? service.request_adopt(order.entry, order.location)
                : order.kind == store::Order::Kind::uninstall
                    ? service.request_uninstall(order.id, order.location, order.entry.console_id())
                    : service.cancel_job(order.id);
            if (accepted || !service.installer)
                order = {};
        }
        {
            std::vector<std::string> running;
            bool known = false;
            if (service.running(running, known))
                screen.set_running(std::move(running), known);
        }
        if (screen.settings_changed &&
            service.save_settings(store::format_settings(screen.settings())))
            screen.settings_changed = false;
        service.show_comments(screen.settings().show_comments);
        if (store::JobView view; service.job(view))
            store::diag::hold_log(!view.id.empty());
        if (store::JobView view; service.job(view))
        {
#ifdef STORE_DEVELOPMENT
            // Once a second: what the page shows for the running job.
            static std::int64_t progress_logged = 0;
            const bool report = !view.id.empty() && now - progress_logged >= 1000000;
            const std::string id = view.id;
            const auto phase = static_cast<int>(view.phase);
            const auto done = view.done, total = view.total;
#endif
            screen.set_activity({view.id, static_cast<int>(view.phase), view.done, view.total,
                                 std::move(view.waiting)});
#ifdef STORE_DEVELOPMENT
            if (report)
            {
                progress_logged = now;
                sys::log("[STORE] progress id=%s phase=%d done=%llu total=%llu left=%s", id.c_str(),
                         phase, static_cast<unsigned long long>(done),
                         static_cast<unsigned long long>(total), screen.remote_time_left().c_str());
            }
#endif
        }
        auto frame = input.update(std::span(samples.data(), count), now);
#ifdef STORE_DEVELOPMENT
        // A scripted run: requests become what a player would do, and a tour
        // moves the focus the way a hand on the D-pad does.
        {
            std::pair<std::string, std::string> request;
            {
                std::unique_lock lock(remote_guard, std::try_to_lock);
                if (lock.owns_lock() && !remote_requests.empty())
                {
                    request = std::move(remote_requests.front());
                    remote_requests.erase(remote_requests.begin());
                }
            }
            const auto &[verb, argument] = request;
            if (verb == "open")
                sys::log("[STORE] remote open %s found=%d", argument.c_str(),
                         screen.open_app(argument));
            else if (verb == "install")
                screen.remote_install(argument);
            else if (verb == "uninstall")
                sys::log("[STORE] remote uninstall %s accepted=%d", argument.c_str(),
                         screen.remote_uninstall(argument));
            else if (verb == "adopt")
                sys::log("[STORE] remote adopt %s accepted=%d", argument.c_str(),
                         screen.remote_adopt(argument));
            else if (verb == "updateall")
                sys::log("[STORE] remote update all apps=%d",
                         static_cast<int>(screen.remote_update_all()));
            else if (verb == "dieat")
            {
                service.stop_at(argument);
                sys::log("[STORE] remote die at step %s", argument.c_str());
            }
            else if (verb == "panel")
                screen.open_panel(std::atoi(argument.c_str()));
            else if (verb == "search")
                screen.pending_search = true;
            else if (verb == "selfupdate")
            {
                // The archive and its SHA-256 were put on the console by the test:
                // the store updates itself from them as it would from a release.
                store::catalog::Entry entry;
                std::string digest;
                save::read_file(std::string(storage_root) + "/dev/self.sha256", &digest, 80);
                entry.id = "PPSA99000";
                entry.name = "ProsperoStore";
                entry.status = "available";
                entry.format = "zip";
                entry.version = argument;
                entry.content_version = argument;
                entry.digest = digest.substr(0, 64);
                struct stat archive
                {
                };
                if (stat((std::string(storage_root) + "/dev/self.zip").c_str(), &archive) == 0)
                    entry.size = static_cast<std::uint64_t>(archive.st_size);
                entry.artifact = "https://github.com/darkxex/ProsperoStore/releases/"
                                 "download/dev/PPSA99000.zip";
                // Or a real release on GitHub: its address and size in dev/self.url and
                // dev/self.size, downloaded through the store's transport like any release.
                std::string url, size;
                if (save::read_file(std::string(storage_root) + "/dev/self.url", &url, 1024) &&
                    url.starts_with("https://github.com/"))
                {
                    while (!url.empty() && (url.back() == '\n' || url.back() == '\r'))
                        url.pop_back();
                    entry.artifact = url;
                    if (save::read_file(std::string(storage_root) + "/dev/self.size", &size, 32))
                        entry.size = std::strtoull(size.c_str(), nullptr, 10);
                }
                sys::log("[STORE] remote selfupdate accepted=%d",
                         service.request_install(entry, "/data/homebrew"));
            }
            else if (verb == "order")
                sys::log("[STORE] remote order sent=%d", screen.remote_order());
            else if (verb == "cancel")
            {
                screen.pending_order = {};
                screen.pending_order.kind = store::Order::Kind::cancel;
                screen.pending_order.id = argument;
            }
            else if (verb == "shot")
            {
                // An optional name of letters and dashes, so several screens can be kept.
                shot_name = "shot";
                if (!argument.empty() && argument.size() <= 32 &&
                    argument.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") == std::string::npos)
                    shot_name = argument;
                shot_wanted = true;
            }
            else if (verb == "tour")
            {
                tour_until = now + std::atoll(argument.c_str()) * 1000000;
                tour_step = 0;
                tour_next = now;
            }
            else if (verb == "fsbench")
            {
                // The folder to measure, e.g. /data/prosperostore/staging.
                static std::string bench_root;
                bench_root = argument;
                pthread_t thread;
                if (pthread_create(&thread, nullptr, store::diag::fsbench, &bench_root) == 0)
                    pthread_detach(thread);
            }
            else if (verb == "clean")
            {
                // Removes a leftover inside one of the store's work folders, through the worker.
                static std::string clean_path;
                clean_path = argument;
                pthread_t thread;
                if (pthread_create(
                        &thread, nullptr,
                        +[](void *opaque) -> void *
                        {
                            const auto &path = *static_cast<std::string *>(opaque);
                            const int result =
                                store::install::worker_remove(store::system::launch_worker, path);
                            sys::log("[STORE] clean %s result=%d", path.c_str(), result);
                            return nullptr;
                        },
                        &clean_path) == 0)
                    pthread_detach(thread);
            }
            else if (verb == "press")
            {
                // Buttons one after another: r l u d (D-pad), X (Cross), B (Circle), O (Options).
                press_keys = argument;
                press_next = now;
            }
            else if (verb == "texbench")
                bench_step = 0;
            else if (verb == "stress")
                screen.stress(static_cast<std::size_t>(std::atoll(argument.c_str())));
            else if (verb == "pool")
                icon_budget = static_cast<std::size_t>(std::max(4LL, std::atoll(argument.c_str())));
        }
        if (!press_keys.empty() && now >= press_next)
        {
            const char step = press_keys.front();
            press_keys.erase(press_keys.begin());
            frame = {};
            if (step == 'X')
                frame.pressed = action_bit(Action::confirm);
            else if (step == 'B')
                frame.pressed = action_bit(Action::back);
            else if (step == 'O')
                frame.pressed = action_bit(Action::menu);
            else
                frame.nav = step == 'r'   ? Direction::right
                            : step == 'l' ? Direction::left
                            : step == 'd' ? Direction::down
                                          : Direction::up;
            press_next = now + 400000;
            note = "press";
        }
        if (now < tour_until && now >= tour_next)
        {
            // Across a row, down, back across, down; a page opened and closed
            // now and then; back to the top when the grid ends.
            static constexpr char kPath[] = "rrrrdlllldrrrrXBdllllduuuuu";
            // A tour only looks: Cross on an open page would install the app.
            char step = kPath[tour_step++ % (sizeof(kPath) - 1)];
            if (screen.page_open())
                step = 'B';
            frame = {};
            if (step == 'X')
                frame.pressed = action_bit(Action::confirm);
            else if (step == 'B')
                frame.pressed = action_bit(Action::back);
            else
                frame.nav = step == 'r'   ? Direction::right
                            : step == 'l' ? Direction::left
                            : step == 'd' ? Direction::down
                                          : Direction::up;
            tour_next = now + (step == 'X' ? 1500000 : 170000);
            note = "tour";
        }
        if (bench_step >= 0)
        {
            // What does a texture cost on the frame? Eight of each, one per frame.
            static std::vector<std::uint8_t> pixels(256 * 256 * 4, 0x80);
            static std::uint32_t made[8]{};
            const auto started = sys::monotonic_us();
            const int index = bench_step % 8;
            const char *what = "create";
            if (bench_step < 8)
                made[index] = renderer.batch().create_texture(256, 256, pixels.data());
            else if (bench_step < 16)
            {
                what = "update";
                glBindTexture(GL_TEXTURE_2D, made[index]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE,
                                pixels.data());
            }
            else
            {
                what = "delete";
                glDeleteTextures(1, &made[index]);
            }
            sys::log("[STORE] texbench %s %d call_us=%lld", what, index,
                     static_cast<long long>(sys::monotonic_us() - started));
            note = what;
            bench_step = bench_step == 23 ? -1 : bench_step + 1;
        }
#endif
        const bool keyboard_owns_input =
            keyboard_active || screen.pending_search || screen.pending_catalog_add;
        if (!keyboard_active && ((screen.pending_search && !frame.is_held(Action::north)) ||
                                 (screen.pending_catalog_add && !frame.is_held(Action::confirm))))
        {
            keyboard_catalog = screen.pending_catalog_add;
            screen.pending_search = false;
            screen.pending_catalog_add = false;
            keyboard_active =
                keyboard_catalog
                    ? keyboard.open(store::i18n::trs("Add a GitHub catalog"),
                                    store::i18n::trs("owner/repository"), std::string(), 160,
                                    ps5::Ime::Enter::go)
                    : keyboard.open(store::i18n::trs("Search ProsperoStore"),
                                    store::i18n::trs("App name or developer"), screen.query());
            sys::log("[STORE] keyboard open=%d", keyboard_active ? 1 : 0);
            if (!keyboard_active)
            {
                // Which of the keyboard's steps refused, for a report from the console.
                std::int32_t user = -1;
                const int common = sceCommonDialogInitialize();
                const int module = sceSysmoduleLoadModule(0x0096);
                const int foreground = sceUserServiceGetForegroundUser(&user);
                sys::log("[STORE] keyboard refused: dialog=0x%08x module=0x%08x user=0x%08x id=%d",
                         static_cast<unsigned>(common), static_cast<unsigned>(module),
                         static_cast<unsigned>(foreground), user);
                screen.notify(store::i18n::trs("Keyboard unavailable"),
                              store::i18n::trs("The system keyboard could not open. Please try "
                                               "again."));
                feedback.play(audio::Cue::error);
            }
        }
        if (keyboard_active)
        {
            const auto state = keyboard.poll();
            if (state == ps5::Ime::State::accepted)
            {
                if (keyboard_catalog)
                {
                    screen.add_catalog(keyboard.text());
                    sys::log("[STORE] custom catalogs=%zu active=%d",
                             screen.settings().catalogs.size(),
                             screen.settings().custom_active() ? 1 : 0);
                }
                else
                    screen.set_query(keyboard.text());
            }
            else if (keyboard_catalog && state != ps5::Ime::State::open)
                screen.catalog_add_cancelled(); // closed without a name
            if (state == ps5::Ime::State::failed)
                screen.notify(store::i18n::trs("Keyboard closed"),
                              store::i18n::trs("The system keyboard closed unexpectedly. Please "
                                               "try again."));
            if (state != ps5::Ime::State::open)
                sys::log("[STORE] keyboard closed state=%d", static_cast<int>(state));
            keyboard_active = state == ps5::Ime::State::open;
        }
#ifdef STORE_DEBUG_TRACE
        // About shows the trace as it grows.
        // The Debug log setting takes effect at once; About shows the trace while it is on.
        {
            static std::size_t shown = 0;
            const bool wanted = screen.settings().debug_log;
            if (wanted != store::diag::enabled())
            {
                // The file records both ends: the line is written while it is still on.
                if (wanted)
                    store::diag::set_enabled(true);
                store::diag::trace("debug log switched %s in Settings", wanted ? "on" : "off");
                // Switched on while running: the console checks a start with it on would
                // have made, so the report is complete without reopening the store.
                static bool checked = false;
                if (wanted && !checked && !debug_at_start)
                    store::diag::trace_console("switched on");
                checked = checked || wanted;
                if (!wanted)
                    store::diag::set_enabled(false);
                if (!wanted)
                {
                    screen.set_debug({}, {});
                    shown = 0;
                }
            }
            if (wanted && store::diag::trace_lines().size() != shown)
            {
                auto lines = store::diag::trace_lines();
                shown = lines.size();
                screen.set_debug(std::move(lines), store::diag::trace_file());
            }
        }
#endif
        screen.update(keyboard_owns_input ? InputFrame{} : frame, dt, feedback);
        // What is on screen first; then, when the whole catalog fits the
        // budget, the rest of it, sixteen at a time.
        ++frame_number;
        const auto on_screen = screen.artwork();
        for (const auto &id : on_screen)
            if (const auto found = textures.find(id); found != textures.end())
                found->second.seen = frame_number;
        std::vector<std::string> missing;
        for (const auto &id : hashes.size() <= icon_budget ? screen.artwork_backlog() : on_screen)
        {
            const auto found = textures.find(id);
            if (missing.size() < 16 && hashes.contains(id) &&
                (found == textures.end() || !found->second.icon))
                missing.push_back(id);
        }
        if (missing != requested_icons && service.request_icons(missing))
            requested_icons = std::move(missing);
        if (screen.settings().sounds)
            for (const auto &cue : feedback.cues)
            {
                sounds.play(mixer, audio::SoundSet::glass, cue);
                if (cue.cue == audio::Cue::complete)
                    music.duck();
            }
        // In as the intro hands over, over two seconds; quiet when sounds are off.
        {
            const float target = screen.intro_finishing() && screen.settings().sounds ? 1.0f : 0.0f;
            const float step = std::min(dt, 0.05f) / 2.0f;
            const float before = music_level;
            music_level = target > music_level ? std::min(target, music_level + step)
                                               : std::max(target, music_level - step * 4.0f);
            if (music_level != before)
                mixer.set_bus_gain(audio::Bus::music, kMusicLevel * music_level * music_level);
        }
        music.pump(std::min(dt, 0.05f));
        if (feedback.rumble_strength > 0 && screen.settings().vibration)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);
        // The Japanese faces are loaded the first time the store speaks Japanese; if they cannot
        // be, it goes back to English.
        if (store::i18n::language() == store::i18n::Language::japanese && !fonts.has_japanese() &&
            !fonts.load_japanese(renderer, app_root + "/assets"))
        {
            store::i18n::set_japanese_available(false);
            store::i18n::set_language(store::i18n::Language::english);
        }
        screen.draw(renderer, fonts.active());
        renderer.present(0, display.width(), display.height());
#ifdef STORE_DEVELOPMENT
        if (shot_wanted)
        {
            // What the TV shows, at half size: /data/prosperostore/dev/shot.tga.
            shot_wanted = false;
            const int w = display.width(), h = display.height(), hw = w / 2, hh = h / 2;
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            std::string tga(18, '\0');
            tga[2] = 2; // uncompressed true colour, rows from the bottom like GL
            tga[12] = static_cast<char>(hw & 0xff);
            tga[13] = static_cast<char>(hw >> 8);
            tga[14] = static_cast<char>(hh & 0xff);
            tga[15] = static_cast<char>(hh >> 8);
            tga[16] = 24;
            tga.reserve(tga.size() + static_cast<std::size_t>(hw) * hh * 3);
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < hw; ++x)
                {
                    const std::uint8_t *px =
                        &pixels[(static_cast<std::size_t>(y) * 2 * w + x * 2) * 4];
                    tga.push_back(static_cast<char>(px[2]));
                    tga.push_back(static_cast<char>(px[1]));
                    tga.push_back(static_cast<char>(px[0]));
                }
            const auto error =
                hui::save::write_atomic("/data/prosperostore/dev/" + shot_name + ".tga", tga);
            sys::log("[STORE] shot %dx%d gl=0x%x saved=%d", hw, hh, glGetError(),
                     error.empty() ? 1 : 0);
        }
#endif
        if (!display.swap())
        {
            sys::log("[STORE] presentation failed");
            break;
        }
        if (first_swap)
        {
            // The console's splash picture stays up until there is a frame to show.
            sys::hide_splash_screen();
            sys::log("[STORE] first-swap ok at_ms=%lld", since_start());
            first_swap = false;
        }
        stats.add(ms);
#ifdef STORE_DEVELOPMENT
        // A late frame is named with what the frame before it did.
        if (ms > 25.0 && !first_swap)
            sys::log("[STORE] hitch ms=%.1f after=%s", ms, previous_note);
        previous_note = note;
        note = "";
#endif
        if (stats.count() == 600)
        {
            char report[256]{};
            stats.format(report, sizeof(report));
            service.report_frames(std::string(report) +
                                  " icons=" + std::to_string(textures.size()));
            stats.reset();
        }
    }
    quit.store(true);
    // The song fades out over half a second instead of stopping mid-note.
    for (int step = 0; step < 30 && music_level > 0.0f; ++step)
    {
        music_level = std::max(0.0f, music_level - 1.0f / 30.0f);
        mixer.set_bus_gain(audio::Bus::music, kMusicLevel * music_level * music_level);
        music.pump(1.0f / 60.0f);
        sys::sleep_us(16000);
    }
    keyboard.close();
    service.stop();
    store::net::stop_transport();
    if (requests_started)
        pthread_join(request_thread, nullptr);
    audio.stop();
    keyboards.close();
    pad.close();
    if (coming_soon_texture)
        glDeleteTextures(1, &coming_soon_texture);
    for (auto &[id, art] : textures)
    {
        (void)id;
        release(art);
    }
    renderer.release();
    display.close();
    sys::log("[STORE] teardown complete");
#ifdef STORE_DEVELOPMENT
    sys::log("[STORE] run end token=%s", run_token.c_str());
#endif
    store::diag::stop();
    sys::quit();
}
