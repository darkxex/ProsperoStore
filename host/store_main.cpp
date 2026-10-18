// ProsperoStore - Host renderer for inspecting the same screens as the console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/ambient.hpp"
#include "app/keyboard_map.hpp"
#include "app/store.hpp"
#include "catalog/client.hpp"
#include "catalog/icons.hpp"
#include "core/image.hpp"
#include "core/qr.hpp"
#include "core/save_file.hpp"
#include "gfx/gl_program.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <cassert>
#include <set>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "../third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

static void check_artwork_requests()
{
    store::Screen screen;
    assert(screen.artwork().empty());
    std::vector<store::App> apps;
    for (unsigned i = 0; i < 100; ++i)
    {
        store::App app;
        app.title_id = "PPSA" + std::to_string(99000 + i);
        char name[16];
        std::snprintf(name, sizeof(name), "App %03u", i);
        app.name = name;
        app.kind = "app";
        apps.push_back(app);
    }
    screen.set_catalog(std::move(apps), "test");
    hui::ui::Feedback feedback;
    // Discover: down from "New and updated" to the Apps shelf, then along it.
    hui::InputFrame down, right;
    down.nav = hui::Direction::down;
    right.nav = hui::Direction::right;
    screen.update(down, 1.0f / 60.0f, feedback);
    for (unsigned i = 0; i < 20; ++i)
    {
        const auto wanted = screen.artwork();
        assert(!wanted.empty() && wanted.size() <= 16);
        assert(std::set<std::string>(wanted.begin(), wanted.end()).size() == wanted.size());
        screen.set_icon(wanted.front(), 1);
        screen.set_ambient(wanted.front(), 2);
        assert(screen.artwork() == wanted); // Uploading artwork must never reset focus.
        screen.update(right, 1.0f / 60.0f, feedback);
    }
    const auto focused = screen.artwork().front();
    assert(focused == "PPSA99020");
    hui::InputFrame confirm;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(confirm, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{focused});
    screen.set_catalog({}, "empty");
    assert(screen.artwork().empty());
}

static void check_categories()
{
    store::Screen screen;
    store::App doom, mario, eden, old;
    doom.title_id = "PPSA99001";
    doom.name = "Doom";
    doom.kind = "game";
    doom.category = "pc-ports";
    mario.title_id = "PPSA99002";
    mario.name = "Mario";
    mario.kind = "game";
    mario.category = "console-ports";
    eden.title_id = "PPSA99003";
    eden.name = "Eden";
    eden.kind = "app";
    eden.category = "emulators";
    old.title_id = "PPSA99004";
    old.name = "Old Game";
    old.kind = "game"; // from a catalog without categories
    screen.set_catalog({doom, mario, eden, old}, "test");
    hui::InputFrame next, r2, l2;
    next.pressed = hui::action_bit(hui::Action::page_next);
    r2.pressed = hui::action_bit(hui::Action::jump_next);
    l2.pressed = hui::action_bit(hui::Action::jump_prev);
    hui::ui::Feedback quiet;
    const auto shown = [&]
    {
        auto ids = screen.artwork();
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    using Ids = std::vector<std::string>;
    // Games: all four kinds of game first, then R2 steps through the categories it holds in the
    // catalog's order (console ports before PC ports) and back round to all of them.
    screen.update(next, 1.0f / 60.0f, quiet);
    screen.update(next, 1.0f / 60.0f, quiet);
    assert(shown() == (Ids{doom.title_id, mario.title_id, old.title_id}));
    screen.update(r2, 1.0f / 60.0f, quiet);
    assert(shown() == Ids{mario.title_id});
    screen.update(r2, 1.0f / 60.0f, quiet);
    assert(shown() == Ids{doom.title_id});
    screen.update(r2, 1.0f / 60.0f, quiet);
    assert(shown() == (Ids{doom.title_id, mario.title_id, old.title_id}));
    screen.update(l2, 1.0f / 60.0f, quiet);
    assert(shown() == Ids{doom.title_id});
    // Another section starts with all of its apps again.
    screen.update(next, 1.0f / 60.0f, quiet);
    screen.update(next, 1.0f / 60.0f, quiet);
    hui::InputFrame back_one;
    back_one.pressed = hui::action_bit(hui::Action::page_prev);
    screen.update(back_one, 1.0f / 60.0f, quiet);
    screen.update(back_one, 1.0f / 60.0f, quiet);
    assert(shown() == (Ids{doom.title_id, mario.title_id, old.title_id}));
}

static void check_search_and_sort()
{
    store::Screen screen;
    store::App a, b, c;
    a.title_id = "PPSA99001";
    a.name = "Zulu";
    a.author = "Bear Studio";
    a.released = "2026-01-01";
    a.updated = "2026-10-01";
    b.title_id = "PPSA99002";
    b.name = "alpha";
    b.author = "Another Developer";
    b.released = "2026-09-01";
    b.updated = "2026-09-01";
    c.title_id = "PPSA99003";
    c.name = "Coming Soon";
    a.kind = b.kind = c.kind = "app";
    screen.set_catalog({a, b, c}, "test");
    {
        // Sorting and search work on a section's grid: go to Apps.
        hui::InputFrame next;
        next.pressed = hui::action_bit(hui::Action::page_next);
        hui::ui::Feedback quiet;
        screen.update(next, 1.0f / 60.0f, quiet);
    }
    assert(screen.artwork().front() == b.title_id);
    screen.set_query("STUDIO");
    assert(screen.artwork() == std::vector<std::string>{a.title_id});
    screen.set_query("ALpHa");
    assert(screen.artwork() == std::vector<std::string>{b.title_id});
    screen.set_query("no such app");
    assert(screen.artwork().empty());
    screen.set_query("");
    hui::InputFrame sort;
    sort.pressed = hui::action_bit(hui::Action::r3);
    hui::ui::Feedback feedback;
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id); // Newest release.
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == a.title_id); // Recently updated.
    screen.set_catalog({c, b, a}, "refreshed");
    assert(screen.artwork().front() == a.title_id);
    screen.update(sort, 1.0f / 60.0f, feedback);
    assert(screen.artwork().front() == b.title_id);
    hui::InputFrame search;
    search.pressed = hui::action_bit(hui::Action::north);
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
    screen.pending_search = false;
    hui::InputFrame open;
    open.pressed = hui::action_bit(hui::Action::confirm);
    screen.update(open, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id);
    screen.set_detail_error(b.title_id, "offline");
    screen.pending_detail.clear();
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == b.title_id && !screen.pending_search);
    store::catalog::Entry detail;
    detail.id = b.title_id;
    detail.description = "Verified description";
    screen.set_detail(detail);
    // A developer's page: that developer's apps alone, with the link their details give.
    screen.set_catalog({a, b, c}, "again");
    detail = {};
    detail.id = a.title_id;
    detail.source = "https://github.com/bear-studio/zulu";
    screen.set_detail(detail);
    assert(screen.open_developer("Bear Studio") && screen.developer() == "Bear Studio");
    assert(screen.artwork() == std::vector<std::string>{a.title_id});
    assert(screen.pending_developer_link == "https://github.com/bear-studio");
    assert(!screen.open_developer(""));
    hui::InputFrame leave;
    leave.pressed = hui::action_bit(hui::Action::back);
    screen.update(leave, 1.0f / 60.0f, feedback);
    assert(screen.developer().empty() && screen.artwork().size() == 3);
    {
        bool support = false;
        store::catalog::Entry who;
        assert(store::catalog::developer_link(who, support).empty() && !support);
        who.source = "https://example.com/someone/app";
        assert(store::catalog::developer_link(who, support).empty());
        who.source = "https://codeberg.org/someone/app";
        assert(store::catalog::developer_link(who, support) == "https://codeberg.org/someone");
        who.author_url = "https://someone.example/";
        assert(store::catalog::developer_link(who, support) == "https://someone.example/" &&
               !support);
        who.donate_url = "https://ko-fi.com/someone";
        assert(store::catalog::developer_link(who, support) == "https://ko-fi.com/someone" &&
               support);
        who.donate_url = "http://ko-fi.com/someone"; // not HTTPS: passed over
        assert(store::catalog::developer_link(who, support) == "https://someone.example/");
    }
    screen.set_catalog({}, "removed");
    assert(screen.artwork().empty());
    screen.update(search, 1.0f / 60.0f, feedback);
    assert(screen.pending_search);
}

static void check_wrapping_and_closing()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99001";
    app.name = "One";
    app.kind = "app";
    screen.set_catalog({app}, "test");
    hui::ui::Feedback quiet;
    // L1 on the first section goes round to the last one (Updates: nothing listed), and R1 back.
    hui::InputFrame previous, next;
    previous.pressed = hui::action_bit(hui::Action::page_prev);
    next.pressed = hui::action_bit(hui::Action::page_next);
    screen.update(previous, 1.0f / 60.0f, quiet);
    assert(screen.artwork().empty());
    screen.update(next, 1.0f / 60.0f, quiet);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    // Circle: a tap does not close, a hold does, and a hold that began on a page does not.
    hui::InputFrame press, hold;
    press.pressed = press.held = hui::action_bit(hui::Action::back);
    hold.held = hui::action_bit(hui::Action::back);
    screen.update(press, 1.0f / 60.0f, quiet);
    screen.update({}, 1.0f / 60.0f, quiet);
    assert(!screen.wants_quit());
    assert(screen.open_app(app.title_id));
    screen.pending_detail.clear();
    screen.update(press, 1.0f / 60.0f, quiet); // leaves the page
    for (int frame = 0; frame < 200; ++frame)
        screen.update(hold, 1.0f / 60.0f, quiet);
    assert(!screen.page_open() && !screen.wants_quit());
    screen.update({}, 1.0f / 60.0f, quiet);
    screen.update(press, 1.0f / 60.0f, quiet);
    for (int frame = 0; frame < 60; ++frame)
        screen.update(hold, 1.0f / 60.0f, quiet);
    assert(!screen.wants_quit()); // a second in: not yet
    for (int frame = 0; frame < 60; ++frame)
        screen.update(hold, 1.0f / 60.0f, quiet);
    assert(screen.wants_quit());
}

static void check_news_and_new_apps()
{
    // The feed: items this version can read, twenty at most; not a feed at all is an error.
    std::vector<store::catalog::News> news;
    std::string error;
    assert(store::catalog::parse_news(
        R"({"schema":3,"news":[{"id":"2026-10-11-a","date":"2026-10-11T10:00:00Z","title":"Hello",)"
        R"("body":"First.\n\nSecond."},{"id":"x","title":"no date"},7]})",
        news, error));
    assert(news.size() == 1 && news[0].id == "2026-10-11-a" && news[0].body == "First.\n\nSecond.");
    assert(!store::catalog::parse_news(R"({"schema":3,"apps":[]})", news, error));
    assert(!store::catalog::parse_news(R"({"schema":2,"news":[]})", news, error));
    assert(store::catalog::parse_news(R"({"schema":3,"news":[]})", news, error) && news.empty());
    // An item is announced until its room has been opened; that is remembered in the settings.
    store::Screen screen;
    screen.set_news({{"2026-10-11-a", "2026-10-11T10:00:00Z", "Hello", "First."}});
    assert(screen.settings().news_seen.empty() && !screen.settings_changed);
    screen.open_panel(3);
    assert(screen.settings().news_seen == "2026-10-11-a" && screen.settings_changed);
    const auto kept = store::parse_settings(store::format_settings(screen.settings()));
    assert(kept.news_seen == "2026-10-11-a");
    // Discover: apps the catalog listed in the last thirty days have their own shelf, ahead of
    // the others it changed last. Without those dates there is the one shelf, as before.
    store::App fresh, old, undated;
    fresh.title_id = "PPSA99001";
    fresh.name = "Fresh";
    fresh.added = "2026-10-05T00:00:00Z";
    fresh.updated = "2026-10-06T00:00:00Z";
    old.title_id = "PPSA99002";
    old.name = "Old";
    old.added = "2026-07-01T00:00:00Z";
    old.updated = "2026-10-10T00:00:00Z";
    undated.title_id = "PPSA99003";
    undated.name = "Undated";
    undated.updated = "2026-09-01T00:00:00Z";
    fresh.kind = old.kind = undated.kind = "app";
    screen.set_catalog({old, undated, fresh}, "dated");
    auto shown = screen.artwork();
    assert(shown.size() >= 3 && shown[0] == fresh.title_id && shown[1] == old.title_id &&
           shown[2] == undated.title_id);
    fresh.added.clear();
    old.added.clear();
    store::Screen plain; // a new screen: the focus is on the first tile again
    plain.set_catalog({old, undated, fresh}, "undated");
    shown = plain.artwork();
    assert(shown.size() >= 3 && shown[0] == old.title_id && shown[1] == fresh.title_id &&
           shown[2] == undated.title_id);
}

static void check_installed_sections()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99010";
    app.name = "Example";
    app.available_version = "02.000.000";
    screen.set_catalog({app}, "verified", true);
    store::system::Inventory inventory;
    store::system::InstalledApp installed;
    installed.id = app.title_id;
    installed.name = app.name;
    installed.version = "01.000.000";
    installed.path = "/data/homebrew/" + app.title_id;
    installed.managed = true;
    inventory.apps.push_back(installed);
    screen.set_inventory(inventory);
    hui::InputFrame section;
    section.pressed = hui::action_bit(hui::Action::page_next);
    hui::ui::Feedback feedback;
    for (int index = 0; index < 5; ++index)
        screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    screen.update(section, 1.0f / 60.0f, feedback);
    assert(screen.artwork() == std::vector<std::string>{app.title_id});
    // Update all: Square asks for the details, then orders the update.
    screen.set_installer(true, true, "", "/data/homebrew");
    screen.set_running({}, true);
    hui::InputFrame square;
    square.pressed = hui::action_bit(hui::Action::west);
    screen.update(square, 1.0f / 60.0f, feedback);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_detail == app.title_id);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    store::catalog::Entry listed;
    listed.id = app.title_id;
    listed.format = "zip";
    listed.digest = std::string(64, 'a');
    screen.set_detail(listed);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::install);
    assert(screen.pending_order.entry.digest == listed.digest);
    assert(screen.pending_order.location == "/data/homebrew");
    screen.pending_order = {};
    screen.pending_detail.clear();
    // A running app is passed over.
    screen.set_running({app.title_id}, true);
    screen.update(square, 1.0f / 60.0f, feedback);
    screen.update({}, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    screen.set_running({}, true);
    inventory.apps.front().managed = false;
    screen.set_inventory(inventory);
    assert(screen.artwork().empty());
}

static void check_keyboard_keys()
{
    using namespace hui::pad_bits;
    const auto press = [](std::initializer_list<std::uint16_t> keys, std::uint32_t modifiers = 0)
    {
        const std::vector<std::uint16_t> held(keys);
        return store::keyboard_buttons(held, modifiers);
    };
    assert(press({store::hid::kUp}) == kUp && press({store::hid::kLeft}) == kLeft);
    assert(press({store::hid::kEnter}) == kCross && press({store::hid::kSpace}) == kCross);
    assert(press({store::hid::kEscape}) == kCircle && press({store::hid::kBackspace}) == kCircle);
    assert(press({store::hid::kTab}) == kR1 && press({store::hid::kTab}, 0x02) == kL1);
    assert(press({store::hid::kPageDown}) == kR1 && press({store::hid::kPageUp}) == kL1);
    assert(press({store::hid::kSlash}) == kTriangle && press({store::hid::kDelete}) == kSquare);
    assert(press({store::hid::kF10}) == kOptions && press({store::hid::kF5}) == kR3);
    assert(press({store::hid::kF6}) == kTouchpad && press({store::hid::kF7}) == kR2);
    assert(press({store::hid::kDown, store::hid::kEnter}) == (kDown | kCross));
    assert(press({0x04 /* A */}) == 0 && press({}) == 0);
}

static void check_hold_to_uninstall()
{
    store::Screen screen;
    store::App app;
    app.title_id = "PPSA99010";
    app.name = "Example";
    app.kind = "app";
    app.available_version = "01.000.000";
    screen.set_catalog({app}, "verified", true);
    store::system::Inventory inventory;
    store::system::InstalledApp installed;
    installed.id = app.title_id;
    installed.name = app.name;
    installed.version = "01.000.000";
    installed.path = "/data/homebrew/" + app.title_id;
    installed.managed = true;
    inventory.apps.push_back(installed);
    screen.set_inventory(inventory);
    screen.set_installer(true, true, "", "/data/homebrew");
    screen.set_running({}, true);
    assert(screen.open_app(app.title_id));
    store::catalog::Entry listed;
    listed.id = app.title_id;
    listed.version = "01.000.000";
    listed.content_version = "01.000.000";
    listed.format = "zip";
    listed.digest = std::string(64, 'a');
    screen.set_detail(listed);
    hui::ui::Feedback feedback;
    for (int frame = 0; frame < 30; ++frame)
        screen.update({}, 1.0f / 60.0f, feedback);
    // A tap does nothing but say how.
    hui::InputFrame tap;
    tap.pressed = hui::action_bit(hui::Action::confirm);
    tap.held = tap.pressed;
    screen.update(tap, 1.0f / 60.0f, feedback);
    hui::InputFrame up;
    up.released = hui::action_bit(hui::Action::confirm);
    screen.update(up, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    // Letting go halfway does nothing either.
    hui::InputFrame hold;
    hold.held = hui::action_bit(hui::Action::confirm);
    screen.update(tap, 1.0f / 60.0f, feedback);
    for (int frame = 0; frame < 30; ++frame)
        screen.update(hold, 1.0f / 60.0f, feedback);
    screen.update(up, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::none);
    for (int frame = 0; frame < 120; ++frame)
        screen.update({}, 1.0f / 60.0f, feedback);
    // A full hold orders the uninstall.
    screen.update(tap, 1.0f / 60.0f, feedback);
    for (int frame = 0; frame < 90 && screen.pending_order.kind == store::Order::Kind::none;
         ++frame)
        screen.update(hold, 1.0f / 60.0f, feedback);
    assert(screen.pending_order.kind == store::Order::Kind::uninstall);

    store::Settings settings;
    settings.reduce_motion = true;
    assert(store::parse_settings(store::format_settings(settings)).reduce_motion);
    assert(!store::parse_settings("location=/data/homebrew\n").reduce_motion);
}

static void check_catalog_settings()
{
    using namespace store;
    assert(parse_settings("").verify_signatures);
    Settings settings;
    settings.catalog_url = "https://dev.example/api/v1/";
    settings.verify_signatures = false;
    const auto saved = parse_settings(format_settings(settings));
    assert(saved.catalog_url == settings.catalog_url && !saved.verify_signatures);
    assert(parse_settings("catalog_url=http://bad/\nverify_signatures=oops\n").verify_signatures);
    assert(parse_settings("catalog_url=http://bad/\n").catalog_url == catalog::kDefaultApi);
    Screen screen;
    screen.open_panel(1);
    hui::ui::Feedback feedback;
    hui::InputFrame down, up, confirm, back, right;
    down.nav = hui::Direction::down;
    up.nav = hui::Direction::up;
    right.nav = hui::Direction::right;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    back.pressed = hui::action_bit(hui::Action::back);
    for (int i = 0; i < 8; ++i)
        screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback); // Development options: Official catalog.
    // The official catalog can't go while no custom one is in use.
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_official && !screen.settings().use_custom);
    // Custom catalog on: with no address yet, the store asks for one.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_custom && screen.pending_catalog_url);
    assert(screen.settings().use_official); // not active yet: the official one stays
    screen.pending_catalog_url = false;
    screen.set_catalog_url("https://dev.example/api/v1");
    assert(screen.settings().catalog_url == settings.catalog_url && screen.settings_changed);
    screen.set_catalog_url("http://bad/");
    assert(screen.settings().catalog_url == settings.catalog_url);
    // As typed on the console: spaces around it, or no scheme.
    screen.set_catalog_url("  dev.example/api/v1 \n");
    assert(screen.settings().catalog_url == settings.catalog_url);
    // The official address is not a custom catalog, and an empty answer changes nothing.
    screen.set_catalog_url(catalog::kDefaultApi);
    assert(screen.settings().catalog_url == settings.catalog_url);
    screen.set_catalog_url("https://");
    assert(screen.settings().catalog_url == settings.catalog_url && screen.settings().use_custom);
    assert(screen.settings().custom_active() && screen.settings().use_official); // both
    // Now the official catalog can be switched off (custom only), and on again.
    screen.update(up, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(!screen.settings().use_official);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().use_official);
    // The address row asks again.
    screen.update(down, 0.016f, feedback);
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.pending_catalog_url);
    screen.pending_catalog_url = false;
    // Signature checks: asking is not consent.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().verify_signatures);
    screen.update(back, 0.016f, feedback);
    assert(screen.settings().verify_signatures);
    screen.update(confirm, 0.016f, feedback);
    screen.update(right, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(!screen.settings().verify_signatures);
    // Restore: the official catalog alone, checked.
    screen.update(down, 0.016f, feedback);
    screen.update(confirm, 0.016f, feedback);
    assert(screen.settings().verify_signatures &&
           screen.settings().catalog_url == catalog::kDefaultApi);
    assert(screen.settings().use_official && !screen.settings().use_custom);
    // The two switches survive the settings file, and a file can't switch both off.
    Settings both;
    both.catalog_url = "https://dev.example/api/v1/";
    both.use_custom = true;
    both.use_official = false;
    const auto kept = parse_settings(format_settings(both));
    assert(kept.use_custom && !kept.use_official && kept.custom_active());
    assert(parse_settings("use_official=0\nuse_custom=0\n").use_official);
    assert(parse_settings("use_official=0\nuse_custom=1\n").use_official); // no address: not active
    // A switch saved on with no address reads as off.
    assert(!parse_settings("use_official=1\nuse_custom=1\n").use_custom);
}

// Likes and comments: nothing of them while the setting is off, and with it on the counts, the
// "Most liked" order and shelf, the comments of the open page and the code for its discussion.
static void check_likes_and_comments()
{
    using namespace store;
    const std::string base = "https://github.com/darkxex/ps5-homebrew-catalog/discussions/";
    // On unless the settings file says otherwise, and switching it off survives that file.
    assert(parse_settings("").show_comments && !parse_settings("show_comments=yes\n").show_comments);
    Settings chosen;
    chosen.show_comments = false;
    assert(!parse_settings(format_settings(chosen)).show_comments);
    assert(parse_settings(format_settings(Settings{})).show_comments);

    App a, b, c, d;
    a.title_id = "PPSA99001";
    a.name = "Zulu";
    b.title_id = "PPSA99002";
    b.name = "alpha";
    c.title_id = "PPSA99003";
    c.name = "Mike";
    d.title_id = "PPSA99004";
    d.name = "bravo";
    a.kind = b.kind = c.kind = d.kind = "app";
    a.updated = b.updated = c.updated = d.updated = "2026-10-01T00:00:00Z";
    const social::Index likes = {{a.title_id, {7, 2, base + "1"}},
                                 {b.title_id, {3, 0, base + "2"}},
                                 {c.title_id, {7, 1, "https://evil.example/discussions/3"}}};
    hui::ui::Feedback quiet;
    hui::InputFrame next, sort, down, confirm, back, touch;
    next.pressed = hui::action_bit(hui::Action::page_next);
    sort.pressed = hui::action_bit(hui::Action::r3);
    down.nav = hui::Direction::down;
    confirm.pressed = hui::action_bit(hui::Action::confirm);
    back.pressed = hui::action_bit(hui::Action::back);
    touch.pressed = hui::action_bit(hui::Action::touch);
    const auto order = [&](Screen &screen)
    {
        // The Apps grid from its first tile to its last.
        std::vector<std::string> ids;
        for (int k = 0; k < 4; ++k)
        {
            ids.push_back(screen.artwork().front());
            hui::InputFrame right;
            right.nav = hui::Direction::right;
            screen.update(right, 1.0f / 60.0f, quiet);
        }
        hui::InputFrame left;
        left.nav = hui::Direction::left;
        for (int k = 0; k < 4; ++k)
            screen.update(left, 1.0f / 60.0f, quiet);
        return ids;
    };
    using Ids = std::vector<std::string>;
    {
        // Off: the counts are dropped on arrival, there are three orders and no code.
        Screen screen;
        screen.set_settings(chosen);
        screen.set_catalog({a, b, c, d}, "test");
        screen.set_likes(likes);
        social::Thread thread;
        thread.counts = likes.at(a.title_id);
        screen.set_comments(a.title_id, thread);
        screen.update(next, 1.0f / 60.0f, quiet);
        const Ids by_name = {b.title_id, d.title_id, c.title_id, a.title_id};
        assert(order(screen) == by_name);
        for (int k = 0; k < 3; ++k)
            screen.update(sort, 1.0f / 60.0f, quiet);
        assert(order(screen) == by_name); // round the three orders, back to names
        assert(screen.open_app(a.title_id));
        screen.update(touch, 1.0f / 60.0f, quiet);
        assert(!screen.discussion_open() && screen.pending_discussion_link.empty());
    }
    Screen screen;
    screen.set_catalog({a, b, c, d}, "test");
    // Discover: the "Most liked" shelf comes with the likes, under the one that was there, and
    // the focus stays on the tile it was on.
    const auto focused = screen.artwork().front();
    screen.set_likes(likes);
    assert(screen.artwork().front() == focused);
    screen.update(down, 1.0f / 60.0f, quiet);
    assert(screen.artwork().front() == c.title_id); // 7 likes, "Mike" before "Zulu"
    hui::InputFrame right;
    right.nav = hui::Direction::right;
    screen.update(right, 1.0f / 60.0f, quiet);
    assert(screen.artwork().front() == a.title_id);
    screen.update(right, 1.0f / 60.0f, quiet);
    assert(screen.artwork().front() == b.title_id); // 3 likes
    screen.update(right, 1.0f / 60.0f, quiet);
    assert(screen.artwork().front() == b.title_id); // the shelf ends: nobody liked "bravo"
    // The grids gain a fourth order: most liked first, ties and the unliked by name.
    screen.update(next, 1.0f / 60.0f, quiet);
    for (int k = 0; k < 3; ++k)
        screen.update(sort, 1.0f / 60.0f, quiet);
    assert(order(screen) == (Ids{c.title_id, a.title_id, b.title_id, d.title_id}));
    screen.update(sort, 1.0f / 60.0f, quiet);
    assert(order(screen) == (Ids{b.title_id, d.title_id, c.title_id, a.title_id}));
    // A page: Touchpad asks for the code of its discussion, and any button closes it again
    // without leaving the page.
    assert(screen.open_app(a.title_id));
    screen.pending_detail.clear();
    screen.update(touch, 1.0f / 60.0f, quiet);
    assert(screen.discussion_open() && screen.pending_discussion_link == base + "1");
    screen.pending_discussion_link.clear();
    screen.set_discussion_qr(base + "1", 1, 180);
    screen.update(back, 1.0f / 60.0f, quiet);
    assert(!screen.discussion_open() && screen.page_open());
    screen.update(touch, 1.0f / 60.0f, quiet);
    assert(screen.discussion_open() && screen.pending_discussion_link.empty()); // made once
    screen.update(touch, 1.0f / 60.0f, quiet);
    assert(!screen.discussion_open());
    // The app's own file is the newer word on its address, and one that is not GitHub's is
    // never offered. Until that file is here R2 has nowhere to jump to.
    hui::InputFrame jump, jump_back;
    jump.pressed = hui::action_bit(hui::Action::jump_next);
    jump_back.pressed = hui::action_bit(hui::Action::jump_prev);
    store::catalog::Entry listed;
    listed.id = a.title_id;
    listed.description = "Words.";
    listed.release_notes = "Notes.";
    screen.set_detail(listed);
    screen.update(jump, 1.0f / 60.0f, quiet);
    assert(!screen.at_comments());
    social::Thread thread;
    thread.counts = {9, 1, base + "11"};
    thread.latest = {{"someone", "2026-10-12", "Plain words."}};
    screen.set_comments(a.title_id, thread);
    // R2 goes to the comments and R2 again back; L2 goes back too, and never forward.
    screen.update(jump, 1.0f / 60.0f, quiet);
    assert(screen.at_comments() && screen.page_open());
    screen.update(jump, 1.0f / 60.0f, quiet);
    assert(!screen.at_comments());
    screen.update(jump_back, 1.0f / 60.0f, quiet);
    assert(!screen.at_comments());
    screen.update(jump, 1.0f / 60.0f, quiet);
    screen.update(jump_back, 1.0f / 60.0f, quiet);
    assert(!screen.at_comments() && screen.page_open());
    screen.update(touch, 1.0f / 60.0f, quiet);
    assert(screen.pending_discussion_link == base + "11");
    screen.pending_discussion_link.clear();
    screen.update(back, 1.0f / 60.0f, quiet);
    screen.update(back, 1.0f / 60.0f, quiet);
    assert(!screen.page_open());
    assert(screen.open_app(c.title_id));
    screen.pending_detail.clear();
    screen.update(touch, 1.0f / 60.0f, quiet);
    assert(!screen.discussion_open() && screen.pending_discussion_link.empty());
    screen.update(back, 1.0f / 60.0f, quiet);
    // Settings: the sixth row switches it. Off forgets everything at once.
    screen.open_panel(1);
    for (int k = 0; k < 5; ++k)
        screen.update(down, 0.016f, quiet);
    screen.settings_changed = false;
    screen.update(confirm, 0.016f, quiet);
    assert(!screen.settings().show_comments && screen.settings_changed);
    screen.update(back, 0.016f, quiet);
    screen.update(next, 1.0f / 60.0f, quiet);
    for (int k = 0; k < 3; ++k)
        screen.update(sort, 1.0f / 60.0f, quiet);
    assert(order(screen) == (Ids{b.title_id, d.title_id, c.title_id, a.title_id}));
    screen.open_panel(1);
    for (int k = 0; k < 5; ++k)
        screen.update(down, 0.016f, quiet);
    screen.update(confirm, 0.016f, quiet);
    assert(screen.settings().show_comments);
}

int main(int argc, char **argv)
{
    check_artwork_requests();
    check_likes_and_comments();
    check_categories();
    check_search_and_sort();
    check_news_and_new_apps();
    check_wrapping_and_closing();
    check_installed_sections();
    check_hold_to_uninstall();
    check_keyboard_keys();
    check_catalog_settings();
    if (argc < 3 || argc > 5)
        return 2;
    const auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                    : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API))
        return 3;
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        return 4;
    {
        hui::gfx::set_glsl_prefix("#version 450 core\n");
        hui::gfx::Renderer renderer;
        store::Fonts fonts;
        hui::gfx::Canvas target;
        if (!renderer.init() || !fonts.load(renderer, argv[1]) || !target.create(1920, 1080, 1))
            return 5;
        store::Screen screen;
        // Settings previews do not need a live catalog or cached downloads.
        if (argc == 4 &&
            (std::string(argv[3]) == "settings" || std::string(argv[3]) == "catalog-settings"))
        {
            screen.open_panel(1);
            hui::ui::Feedback quiet;
            hui::InputFrame down;
            down.nav = hui::Direction::down;
            if (std::string(argv[3]) == "catalog-settings")
            {
                for (int i = 0; i < 8; ++i)
                    screen.update(down, 0.016f, quiet);
                hui::InputFrame confirm;
                confirm.pressed = hui::action_bit(hui::Action::confirm);
                screen.update(confirm, 0.016f, quiet);
            }
        }
        std::vector<GLuint> textures;
        {
            std::string encoded;
            hui::Image image;
            if (hui::save::read_file(std::string(argv[1]) + "/images/coming-soon.png", &encoded) &&
                hui::decode_png(encoded, image))
            {
                textures.push_back(
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data()));
                screen.set_coming_soon_art(textures.back());
            }
        }
        std::vector<store::catalog::Entry> snapshot_entries; // the catalog shown, if any
        if (argc >= 4 && std::string(argv[3]) != "settings" &&
            std::string(argv[3]) != "catalog-settings")
        {
            store::catalog::Client catalog(argv[3]);
            store::catalog::Snapshot snapshot;
            std::string error;
            if (!catalog.cached(snapshot, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 8;
            }
            snapshot_entries = snapshot.entries;
            std::vector<store::App> apps;
            for (const auto &entry : snapshot.entries)
            {
                apps.push_back({entry.id, entry.name, entry.author, entry.description, entry.kind,
                                entry.version, entry.status == "coming_soon" ? "Coming soon" : "",
                                0, entry.released, entry.updated});
                apps.back().available_version = entry.content_version;
                apps.back().size = entry.size;
                apps.back().added = entry.added;
                apps.back().category = entry.category;
                apps.back().extra = entry.extra;
                // A preview of the shelves with a catalog that doesn't date its apps yet.
                if (argc == 5 && std::string(argv[4]) == "whats-new" && entry.added.empty())
                    apps.back().added = entry.released;
            }
            screen.set_catalog(std::move(apps), "Verified catalog");
            store::catalog::Icons icons(std::string(argv[3]) + "/icons");
            store::net::Control control;
            for (const auto &entry : snapshot.entries)
            {
                hui::Image image;
                if (!icons.cached(entry, image))
                {
                    std::string encoded;
                    const auto response = store::net::fetch(
                        entry.icon, store::net::Purpose::catalog, 2u << 20, encoded, control);
                    if (!response.ok() || !icons.store(entry, encoded, image))
                        continue;
                }
                const auto texture =
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data());
                textures.push_back(texture);
                screen.set_icon(entry.id, texture);
                // Its own picture, made from the icon, and the colour the screen leans toward.
                std::uint32_t vivid = 0;
                const auto ambient = store::make_ambient(image, vivid);
                if (!ambient.rgba.empty())
                {
                    const auto field = renderer.batch().create_texture(
                        ambient.width, ambient.height, ambient.rgba.data());
                    textures.push_back(field);
                    screen.set_ambient(entry.id, field);
                }
                screen.set_accent(
                    entry.id, hui::gfx::Color::rgb(vivid ? vivid : store::average_colour(image)));
            }
            // The verified details the stage would ask for, from the cache.
            for (const auto &entry : snapshot.entries)
            {
                store::catalog::Entry detail;
                std::string why;
                if (catalog.detail(snapshot, entry.id, detail, control, why))
                    screen.set_detail(detail);
            }
        }
        hui::ui::Feedback feedback;
        if (argc == 5)
        {
            const std::string mode = argv[4];
            hui::InputFrame down, right, nav_up;
            down.nav = hui::Direction::down;
            right.nav = hui::Direction::right;
            nav_up.nav = hui::Direction::up;
            const auto settle = [&](int frames)
            {
                for (int frame = 0; frame < frames; ++frame)
                    screen.update({}, 1.0f / 60.0f, feedback);
            };
            if (mode == "stage")
            {
                // The featured app on the stage, the focus on its button.
                screen.update(nav_up, 1.0f / 60.0f, feedback);
                settle(30);
            }
            else if (mode == "shelf" || mode == "shelf-2" || mode == "shelf-3")
            {
                const int downs = mode == "shelf" ? 0 : mode == "shelf-2" ? 1 : 2;
                for (int i = 0; i < downs; ++i)
                {
                    screen.update(down, 1.0f / 60.0f, feedback);
                    settle(20);
                }
                for (int i = 0; i < 2; ++i)
                {
                    screen.update(right, 1.0f / 60.0f, feedback);
                    settle(20);
                }
            }
            else if (mode == "games" || mode == "apps" || mode == "games-category")
            {
                hui::InputFrame section;
                section.pressed = hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "apps" ? 1 : 2); ++index)
                    screen.update(section, 1.0f / 60.0f, feedback);
                if (mode == "games-category")
                {
                    hui::InputFrame r2;
                    r2.pressed = hui::action_bit(hui::Action::jump_next);
                    screen.update(r2, 1.0f / 60.0f, feedback);
                }
                settle(30);
                screen.update(right, 1.0f / 60.0f, feedback);
                settle(20);
            }
            else if (mode == "page-light" || mode == "page-installing" ||
                     mode == "page-unpacking" || mode == "page-installed" || mode == "page-eden")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_running({}, true);
                screen.set_locations({{"/data/homebrew", 548ull << 30}});
                const std::string id = mode == "page-eden"        ? "PPSA99008"
                                       : mode == "page-installed" ? "PPSA99006"
                                       : mode == "page-light"     ? "PPSA99002"
                                                                  : "PPSA99169";
                if (mode == "page-installed")
                {
                    store::system::Inventory inventory;
                    inventory.apps = {{id, "ProsperoPuzzles", "01.000.010", "/data/homebrew/" + id,
                                       "", false, true, false}};
                    screen.set_inventory(std::move(inventory));
                }
                assert(screen.open_app(id));
                screen.pending_detail.clear();
                hui::Image qr;
                assert(hui::encode_qr("https://prosperostore.net/app/" + id + "/", qr));
                const auto texture =
                    renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                textures.push_back(texture);
                screen.set_qr(id, texture, qr.width);
                if (mode == "page-installing" || mode == "page-unpacking")
                {
                    const std::uint64_t total =
                        mode == "page-installing" ? 197867360ull : 412000000ull;
                    const int phase = mode == "page-installing" ? 1 : 3;
                    screen.set_activity({id, phase, total / 5, total, {"PPSA99420"}});
                    settle(60);
                    screen.set_activity({id, phase, total * 43 / 100, total, {"PPSA99420"}});
                }
            }
            else if (mode.starts_with("likes-") || mode.starts_with("comments-"))
            {
                // Likes and comments from files in the folder STORE_COMMENTS names (the
                // tests' fixtures have the shape): the setting on, the counts in, and then
                // "likes-home" the shelf, "likes-grid" the Apps grid in that order,
                // "likes-settings" the switch, "comments-<TITLEID>" that app's page and
                // "comments-code-<TITLEID>" the code for its discussion.
                const char *folder = std::getenv("STORE_COMMENTS");
                if (!folder)
                    return 9;
                auto chosen = screen.settings();
                chosen.show_comments = true;
                screen.set_settings(chosen);
                std::string body;
                store::social::Index likes;
                if (!hui::save::read_file(std::string(folder) + "/v1/index.json", &body) ||
                    !store::social::parse_index(body, likes))
                    return 9;
                screen.set_likes(likes);
                if (mode == "likes-home")
                {
                    // Down to the shelf whose first two tiles are the two most liked apps,
                    // and along it to the second.
                    std::vector<store::social::Ranked> ranked;
                    for (const auto &[id, counts] : likes)
                        for (const auto &entry : snapshot_entries)
                            if (entry.id == id && entry.status != "coming_soon")
                                ranked.push_back({id, entry.name, counts.likes});
                    for (auto &app : ranked)
                        for (auto &c : app.name)
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    const auto most = store::social::most_liked(ranked);
                    assert(most.size() >= 2);
                    for (int i = 0; i < 6; ++i)
                    {
                        screen.update(down, 1.0f / 60.0f, feedback);
                        settle(20);
                        if (screen.artwork().front() != most[0])
                            continue;
                        screen.update(right, 1.0f / 60.0f, feedback);
                        if (screen.artwork().front() == most[1])
                            break;
                        hui::InputFrame left;
                        left.nav = hui::Direction::left;
                        screen.update(left, 1.0f / 60.0f, feedback);
                    }
                }
                else if (mode == "likes-grid")
                {
                    hui::InputFrame section, sort;
                    section.pressed = hui::action_bit(hui::Action::page_next);
                    sort.pressed = hui::action_bit(hui::Action::r3);
                    screen.update(section, 1.0f / 60.0f, feedback);
                    for (int i = 0; i < 3; ++i)
                        screen.update(sort, 1.0f / 60.0f, feedback);
                }
                else if (mode == "likes-settings")
                {
                    screen.open_panel(1);
                    for (int i = 0; i < 5; ++i)
                        screen.update(down, 1.0f / 60.0f, feedback);
                }
                else
                {
                    // "comments-jump-<TITLEID>": the page after R2 (pressed further down).
                    const bool code = mode.starts_with("comments-code-");
                    const bool jumped = mode.starts_with("comments-jump-");
                    const std::string id = mode.substr(code || jumped ? 14 : 9);
                    screen.set_installer(true, true, "", "/data/homebrew");
                    screen.set_running({}, true);
                    screen.set_locations({{"/data/homebrew", 548ull << 30}});
                    if (!screen.open_app(id))
                        return 9;
                    screen.pending_detail.clear();
                    hui::Image qr;
                    assert(hui::encode_qr("https://prosperostore.net/app/" + id + "/", qr));
                    textures.push_back(
                        renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data()));
                    screen.set_qr(id, textures.back(), qr.width);
                    store::social::Thread thread;
                    if (hui::save::read_file(std::string(folder) + "/v1/apps/" + id + ".json",
                                             &body) &&
                        store::social::parse_thread(body, id, thread))
                        screen.set_comments(id, thread);
                    settle(60);
                    if (code)
                    {
                        hui::InputFrame touch;
                        touch.pressed = hui::action_bit(hui::Action::touch);
                        screen.update(touch, 1.0f / 60.0f, feedback);
                        const std::string link = screen.pending_discussion_link;
                        assert(store::social::discussion_url(link) && hui::encode_qr(link, qr));
                        screen.pending_discussion_link.clear();
                        textures.push_back(
                            renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data()));
                        screen.set_discussion_qr(link, textures.back(), qr.width);
                        std::printf("Discussion code: %s\n", link.c_str());
                    }
                }
            }
            else if (mode == "reel-install")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_running({}, true);
                screen.set_locations({{"/data/homebrew", 548ull << 30}});
                assert(screen.open_app("PPSA99169"));
                screen.pending_detail.clear();
                hui::Image qr;
                assert(hui::encode_qr("https://prosperostore.net/app/PPSA99169/", qr));
                const auto texture =
                    renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                textures.push_back(texture);
                screen.set_qr("PPSA99169", texture, qr.width);
            }
            else if (mode == "reel-browse")
            {
            }
            else if (mode == "reel-intro")
                screen.play_intro();
            else if (mode == "busy-home")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_activity(
                    {"PPSA99169", 1, 197867360ull / 5, 197867360ull, {"PPSA99420"}});
                settle(60);
                screen.set_activity(
                    {"PPSA99169", 1, 197867360ull * 43 / 100, 197867360ull, {"PPSA99420"}});
                for (int i = 0; i < 1; ++i)
                {
                    screen.update(right, 1.0f / 60.0f, feedback);
                    settle(20);
                }
            }
            else if (mode == "search")
                screen.set_query("radio");
            else if (mode == "closing")
            {
                // Circle is held after the screen has settled: see below.
            }
            else if (mode == "news")
            {
                screen.set_news(
                    {{"2026-10-11-store-060", "2026-10-11T09:00:00Z", "ProsperoStore 1.000.060",
                      "Developer pages, a News room and a shelf for apps new to the catalog.\n\n"
                      "Open an app and press R3 to see everything else its developer made."},
                     {"2026-10-08-mirror", "2026-10-08T18:00:00Z", "The catalog has a mirror",
                      "When prosperostore.net can't be reached, the store now reads the same signed "
                      "catalog from its mirror."}});
                screen.open_panel(3);
                settle(60);
            }
            else if (mode == "whats-new")
            {
                screen.update(down, 1.0f / 60.0f, feedback);
                settle(40);
            }
            else if (mode == "developer")
            {
                // The page of the developer of the first app whose details are here.
                screen.open_developer("BlackBearReloaded");
                const std::string link = screen.pending_developer_link.empty()
                                             ? "https://github.com/blackbearreloaded"
                                             : screen.pending_developer_link;
                hui::Image qr;
                assert(hui::encode_qr(link, qr));
                const auto texture =
                    renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                textures.push_back(texture);
                screen.pending_developer_link = link;
                screen.set_developer_qr(link, texture);
                settle(40);
            }
            else if (mode == "busy")
            {
                // An install in progress, seen from the grid and the top bar.
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_activity({"PPSA99007", 1, 30u << 20, 58u << 20, {"PPSA99420"}});
            }
            else if (mode == "queue" || mode == "downloads" || mode == "settings" ||
                     mode == "about")
            {
                screen.set_installer(true, true, "", "/data/homebrew");
                screen.set_self("PPSA99000", "01.000.000");
                screen.set_locations(
                    {{"/data/homebrew", 548ull << 30}, {"/mnt/ext1/homebrew", 912ull << 30}});
                if (mode == "queue" || mode == "downloads")
                {
                    screen.set_activity(
                        {"PPSA99007", 3, 61u << 20, 94u << 20, {"PPSA99420", "PPSA99169"}});
                    screen.finish_job(
                        true, false, "ProsperoRadio installed",
                        "ShadowMountPlus will add it to your home screen in a moment.");
                    screen.finish_job(false, false, "PS5SX2: not changed", "Close the app first");
                }
                screen.open_panel(mode == "queue" || mode == "downloads" ? 0
                                  : mode == "settings"                   ? 1
                                                                         : 2);
                if (mode == "downloads")
                    settle(700);
                // Settings: down to the location, right to the next one, saved once.
                if (mode == "settings")
                {
                    hui::InputFrame right;
                    right.nav = hui::Direction::right;
                    screen.update(right, 1.0f / 60.0f, feedback);
                    assert(screen.settings().location == "/mnt/ext1/homebrew");
                    assert(screen.settings_changed);
                    assert(
                        store::parse_settings(store::format_settings(screen.settings())).location ==
                        "/mnt/ext1/homebrew");
                    screen.update(right, 1.0f / 60.0f, feedback);
                    assert(screen.settings().location == "/data/homebrew");
                    assert(!store::parse_settings("location=../x\nsounds=0\n").sounds);
                    assert(store::parse_settings("location=../x\n").location == "/data/homebrew");
                }
                if (mode == "queue")
                {
                    // Cross on the first row cancels the running job.
                    hui::InputFrame cross;
                    cross.pressed = hui::action_bit(hui::Action::confirm);
                    screen.update(cross, 1.0f / 60.0f, feedback);
                    assert(screen.pending_order.kind == store::Order::Kind::cancel);
                    assert(screen.pending_order.id == "PPSA99007");
                    screen.pending_order = {};
                }
            }
            else if (mode == "notice")
                screen.notify("Update available: 01.000.010",
                              "A newer ProsperoStore is listed on prosperostore.net.");
            else if (mode == "scrolled" || mode == "coming-soon")
            {
                hui::InputFrame move;
                move.nav = hui::Direction::down;
                move.pressed = mode == "scrolled" ? 0 : hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "scrolled" ? 2 : 4); ++index)
                    screen.update(move.pressed ? hui::InputFrame{.pressed = move.pressed} : move,
                                  1.0f / 60.0f, feedback);
            }
            else if (mode == "empty")
                screen.set_query("no matching application");
            else if (mode == "updated")
            {
                hui::InputFrame sort;
                sort.pressed = hui::action_bit(hui::Action::r3);
                screen.update(sort, 1.0f / 60.0f, feedback);
                screen.update(sort, 1.0f / 60.0f, feedback);
            }
            else if (mode == "installed" || mode == "updates" || mode == "local-detail")
            {
                store::system::Inventory inventory;
                inventory.apps = {
                    {"PPSA99001", "ProsperoRadio", "01.000.000", "/data/homebrew/PPSA99001", "",
                     false, true, false},
                    {"PPSA99002", "ProsperoLight", "01.000.000", "/data/homebrew/PPSA99002",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"PPSA99980", "Example local app", "01.000.001", "/mnt/ext1/homebrew/PPSA99980",
                     "Installed outside ProsperoStore. Not managed by this app.", false, false,
                     false},
                    {"", "Example.ffpkg", "", "/mnt/ext1/homebrew/Example.ffpkg",
                     "Image installed outside ProsperoStore. Not managed by this app.", true, false,
                     false}};
                screen.set_inventory(std::move(inventory));
                screen.set_status("Installed library preview");
                hui::InputFrame section;
                section.pressed = hui::action_bit(hui::Action::page_next);
                for (int index = 0; index < (mode == "updates" ? 6 : 5); ++index)
                    screen.update(section, 1.0f / 60.0f, feedback);
                if (mode == "local-detail")
                {
                    screen.set_query("Example local");
                    hui::InputFrame open;
                    open.pressed = hui::action_bit(hui::Action::confirm);
                    screen.update(open, 1.0f / 60.0f, feedback);
                    assert(screen.pending_detail.empty());
                }
            }
            else if (mode == "detail" || mode == "detail-error" || mode == "detail-end" ||
                     mode == "detail-armed" || mode == "detail-progress" || mode == "confirm" ||
                     mode == "adopt")
            {
                if (mode == "detail-armed" || mode == "detail-progress" || mode == "confirm" ||
                    mode == "adopt")
                    screen.set_installer(true, true, "", "/data/homebrew");
                if (mode == "adopt")
                {
                    // Installed by hand, listed in the catalog: it can be handed over.
                    store::system::Inventory inventory;
                    inventory.apps = {{"PPSA99001", "ProsperoRadio", "01.000.005",
                                       "/data/homebrew/PPSA99001",
                                       "Installed outside ProsperoStore. Not managed by this app.",
                                       false, false, false}};
                    screen.set_inventory(std::move(inventory));
                }
                if (mode == "confirm")
                {
                    store::system::Inventory inventory;
                    inventory.apps = {{"PPSA99001", "ProsperoRadio", "01.000.000",
                                       "/data/homebrew/PPSA99001", "", false, true, false}};
                    screen.set_inventory(std::move(inventory));
                }
                screen.set_query("radio");
                hui::InputFrame open;
                open.pressed = hui::action_bit(hui::Action::confirm);
                screen.update(open, 1.0f / 60.0f, feedback);
                assert(!screen.pending_detail.empty());
                if (mode == "detail-error")
                    screen.set_detail_error(screen.pending_detail, "The network is unavailable.");
                else
                {
                    store::catalog::Client client(argv[3]);
                    store::catalog::Snapshot snapshot;
                    store::catalog::Entry entry;
                    store::net::Control control;
                    std::string error;
                    assert(client.cached(snapshot, error));
                    assert(client.detail(snapshot, screen.pending_detail, entry, control, error));
                    screen.set_detail(entry);
                    hui::Image qr;
                    assert(hui::encode_qr("https://prosperostore.net/app/" + entry.id + "/", qr));
                    const auto texture =
                        renderer.batch().create_texture(qr.width, qr.height, qr.rgba.data());
                    assert(texture);
                    textures.push_back(texture);
                    screen.set_qr(entry.id, texture, qr.width);
                    if (mode == "detail-progress")
                    {
                        // Two samples a second apart give the page a speed, and so a time left.
                        screen.set_activity({entry.id, 1, entry.size / 5, entry.size, {}});
                        for (int frame = 0; frame < 60; ++frame)
                            screen.update({}, 1.0f / 60.0f, feedback);
                        screen.set_activity({entry.id, 1, entry.size * 2 / 5, entry.size, {}});
                    }
                    if (mode == "detail-armed")
                    {
                        // Cross asks for the install of exactly this verified record.
                        screen.update(open, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::install);
                        assert(screen.pending_order.entry.digest == entry.digest);
                        assert(screen.pending_order.location == "/data/homebrew");
                        screen.pending_order = {};
                    }
                    if (mode == "adopt")
                    {
                        // Cross asks first; the answer orders the hand-over, nothing else.
                        screen.update(open, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::none);
                    }
                    if (mode == "confirm")
                    {
                        // Square asks first; nothing is ordered until the answer.
                        hui::InputFrame square;
                        square.pressed = hui::action_bit(hui::Action::west);
                        screen.update(square, 1.0f / 60.0f, feedback);
                        assert(screen.pending_order.kind == store::Order::Kind::none);
                    }
                }
            }
            else
                return 9;
        }
        for (int frame = 0; frame < 120; ++frame)
            screen.update({}, 1.0f / 60.0f, feedback);
        if (const char *reel = std::getenv("STORE_REEL"); reel && argc == 5)
        {
            // A clip: presses at given frames, every second frame written as a
            // 960x540 JPEG; the installer's progress is simulated for "reel-install".
            const std::string scene = argv[4];
            std::vector<std::pair<int, std::string>> steps;
            int frames = 0;
            if (scene == "reel-browse")
            {
                steps = {{20, "up"},     {150, "right"},   {230, "down"}, {300, "right"},
                         {360, "right"}, {420, "right"},   {490, "down"}, {550, "right"},
                         {610, "right"}, {680, "confirm"}, {860, "back"}, {960, "r1"},
                         {1030, "r1"},   {1100, "right"}};
                frames = 1190;
            }
            else if (scene == "reel-install")
            {
                steps = {{40, "confirm"}};
                frames = 760;
            }
            else if (scene == "reel-intro")
            {
                frames = 300;
                screen.play_intro(); // after the warm-up frames, so the clip starts at zero
            }
            const auto press = [](const std::string &name)
            {
                hui::InputFrame input;
                if (name == "up" || name == "down" || name == "left" || name == "right")
                    input.nav = name == "up"     ? hui::Direction::up
                                : name == "down" ? hui::Direction::down
                                : name == "left" ? hui::Direction::left
                                                 : hui::Direction::right;
                else if (name == "confirm")
                    input.pressed = hui::action_bit(hui::Action::confirm);
                else if (name == "back")
                    input.pressed = hui::action_bit(hui::Action::back);
                else if (name == "r1")
                    input.pressed = hui::action_bit(hui::Action::page_next);
                return input;
            };
            std::vector<unsigned char> pixels(1920 * 1080 * 4), half(960 * 540 * 3);
            stbi_flip_vertically_on_write(1);
            const std::string id = "PPSA99169";
            const std::uint64_t total = 197867360ull, unpacked = 412000000ull;
            for (int frame = 0; frame < frames; ++frame)
            {
                hui::InputFrame input;
                for (const auto &[at, name] : steps)
                    if (at == frame)
                        input = press(name);
                if (scene == "reel-install")
                {
                    // Download 4 s, verify 0.5 s, unpack 2.5 s, finish 0.4 s, then installed.
                    const int start = 60;
                    const int f = frame - start;
                    if (f >= 0 && f < 240)
                        screen.set_activity(
                            {id, 1, total * static_cast<std::uint64_t>(f) / 240, total, {}});
                    else if (f >= 240 && f < 270)
                        screen.set_activity({id, 2, 0, 0, {}});
                    else if (f >= 270 && f < 420)
                        screen.set_activity({id,
                                             3,
                                             unpacked * static_cast<std::uint64_t>(f - 270) / 150,
                                             unpacked,
                                             {}});
                    else if (f >= 420 && f < 444)
                        screen.set_activity({id, 4, 0, 0, {}});
                    else if (f == 444)
                    {
                        screen.set_activity({});
                        store::system::Inventory inventory;
                        inventory.apps = {{id, "RetroArch", "01.000.000", "/data/homebrew/" + id,
                                           "", false, true, false}};
                        screen.set_inventory(std::move(inventory));
                        screen.finish_job(
                            true, false, "RetroArch installed",
                            "ShadowMountPlus adds it to your home screen in a moment.");
                    }
                }
                screen.update(input, 1.0f / 60.0f, feedback);
                if (frame % 2)
                    continue;
                screen.draw(renderer, fonts.refs);
                renderer.present(target.framebuffer(), 1920, 1080);
                glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
                glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                for (int y = 0; y < 540; ++y)
                    for (int x = 0; x < 960; ++x)
                        for (int c = 0; c < 3; ++c)
                        {
                            const auto at = [&](int dx, int dy)
                            { return pixels[((2 * y + dy) * 1920 + (2 * x + dx)) * 4 + c]; };
                            half[(y * 960 + x) * 3 + c] = static_cast<unsigned char>(
                                (at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
                        }
                char name[512];
                std::snprintf(name, sizeof(name), "%s/%05d.jpg", reel, frame / 2);
                if (!stbi_write_jpg(name, 960, 540, 3, half.data(), 90))
                    return 6;
            }
            std::printf("Reel: %d frames in %s\n", frames / 2, reel);
            glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
            eglTerminate(display);
            return 0;
        }
        if (argc == 5 && std::string(argv[4]) == "closing")
        {
            // Circle held for most of a second: the bar is part of the way.
            hui::InputFrame press, hold;
            press.pressed = press.held = hui::action_bit(hui::Action::back);
            hold.held = hui::action_bit(hui::Action::back);
            screen.update(press, 1.0f / 60.0f, feedback);
            for (int frame = 0; frame < 54; ++frame)
                screen.update(hold, 1.0f / 60.0f, feedback);
        }
        screen.draw(renderer, fonts.refs);
        if (const char *steps = std::getenv("STORE_SCROLL"); steps && argc == 5)
        {
            // Down the open page's article by this many presses, once it has been laid out.
            hui::InputFrame scroll;
            scroll.nav = hui::Direction::down;
            for (int i = 0; i < std::atoi(steps); ++i)
            {
                screen.update(scroll, 1.0f / 60.0f, feedback);
                for (int frame = 0; frame < 30; ++frame)
                    screen.update({}, 1.0f / 60.0f, feedback);
            }
            screen.draw(renderer, fonts.refs);
        }
        if (argc == 5 && std::string(argv[4]).starts_with("comments-jump-"))
        {
            // R2, once the article has been laid out: straight to the comments.
            hui::InputFrame jump;
            jump.pressed = hui::action_bit(hui::Action::jump_next);
            screen.update(jump, 1.0f / 60.0f, feedback);
            for (int frame = 0; frame < 60; ++frame)
                screen.update({}, 1.0f / 60.0f, feedback);
            screen.draw(renderer, fonts.refs);
        }
        if (argc == 5 && std::string(argv[4]) == "detail-end")
        {
            hui::InputFrame scroll;
            scroll.nav = hui::Direction::down;
            for (int frame = 0; frame < 120; ++frame)
                screen.update(scroll, 1.0f / 60.0f, feedback);
            screen.draw(renderer, fonts.refs);
        }
        renderer.present(target.framebuffer(), 1920, 1080);
        glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer());
        std::vector<unsigned char> pixels(1920 * 1080 * 4);
        glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        if (!stbi_write_png(argv[2], 1920, 1080, 4, pixels.data(), 1920 * 4))
            return 6;
        const auto error = glGetError();
        std::printf("Snapshot: %s GL=0x%x draws=%zu\n", argv[2], error, renderer.last_draw_calls());
        if (error != GL_NO_ERROR)
            return 7;
        glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
