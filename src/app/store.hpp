// ProsperoStore - Controller-driven store screens assembled from Homebrew UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/renderer.hpp"
#include "catalog/catalog.hpp"
#include "social/comments.hpp"
#include "system/inventory.hpp"
#include "ui/components/dialog.hpp"
#include "ui/components/hold_button.hpp"
#include "ui/components/text_view.hpp"
#include "ui/components/toast.hpp"
#include "ui/motion.hpp"
#include "gfx/draw_list.hpp"

#include <map>
#include <string>
#include <vector>
#include <optional>

namespace store
{

struct App
{
    std::string title_id;
    std::string name;
    std::string author;
    std::string description;
    std::string kind;
    std::string version;
    std::string badge;
    std::uint32_t icon = 0;
    std::string released, updated;
    std::optional<catalog::Entry> detail = {};
    std::string detail_error = {};
    std::string catalog_badge = {}, available_version = {};
    std::vector<system::InstalledApp> installed = {};
    bool local_only = false;
    std::uint32_t art = 0; // the full-size picture, once the page has asked for it
    std::string folded_name = {}, folded_author = {}; // for sorting and search
    // The app's key art (its home-screen background, 16:9) and the colour the
    // screen leans toward while it is in focus.
    std::uint32_t background = 0;
    bool ambient = false; // the background was made from the icon (make_ambient)
    hui::gfx::Color accent = hui::gfx::Color::rgb(0x42358f);
    std::uint64_t size = 0;
    std::string added = {};    // when the catalog first listed it
    std::string category = {}; // its shelf: a catalog category key, or empty
    bool extra = false;        // from the custom catalog: it has no likes or comments
};

// What the page asks the installer to do. The frame loop hands it to the
// service and clears it once it has been accepted.
struct Order
{
    enum class Kind
    {
        none,
        install, // or update: the engine decides from what is on disk
        uninstall,
        adopt, // take over an app that was installed by hand
        cancel
    } kind = Kind::none;
    catalog::Entry entry; // install: the app's verified detail record
    std::string id, location;
};
// What the player chose, kept in /data/prosperostore/settings.txt.
struct Settings
{
    std::string location = "/data/homebrew"; // where new apps are installed
    bool check_updates = true;               // ask the catalog for a newer store at start
    bool sounds = true;
    bool vibration = true;
    bool reduce_motion = false; // no drifting, floating or sliding: things fade instead
    // Records what the store does at each step, for a report: shown in About, written to
    // /data/prosperostore/debug-trace.txt (or a USB drive) and the kernel log.
    bool debug_log = false;
    std::string catalog_url = catalog::kDefaultApi;
    // The two sources, each switched on or off by itself in Development options. Both on:
    // the custom catalog's apps are added to prosperostore.net's. Never both off.
    bool use_official = true;
    bool use_custom = false;
    // The custom catalog is in use: switched on, and its address is not the official one.
    bool custom_active() const
    {
        return use_custom && catalog_url != catalog::kDefaultApi;
    }
    bool verify_signatures = true;
    std::string news_seen; // the newest news item that was read
    // Likes and comments from the apps' discussions on GitHub. On until the player says no:
    // while it is off the store reads nothing from that address and shows none of it.
    bool show_comments = true;
};
std::string format_settings(const Settings &settings);
Settings parse_settings(std::string_view text);

// What the installer is doing right now, as the page shows it.
struct Activity
{
    std::string id; // empty when idle
    int phase = 0;  // install::Phase
    std::uint64_t done = 0, total = 0;
    std::vector<std::string> waiting;
};

// The layout follows the UI library's "Storefront" design: a featured banner,
// section chips, a grid of cards and a product page. The colours are those of
// "Farlight" as the Aurora Shelf design shows it.
class Screen
{
  public:
    Screen();
    void set_catalog(std::vector<App> apps, std::string status, bool current = false);
    void set_inventory(system::Inventory inventory);
    void set_detail(const catalog::Entry &entry);
    void set_detail_error(const std::string &id, std::string message);
    void set_icon(const std::string &id, std::uint32_t texture);
    void set_art(const std::string &id, std::uint32_t texture);
    void set_background(const std::string &id, std::uint32_t texture);
    // The field made from the app's icon; key art drawn for the store wins over it.
    void set_ambient(const std::string &id, std::uint32_t texture);
    void set_accent(const std::string &id, hui::gfx::Color accent);
    // Every catalog app with a picture, the ones on screen first: the frame
    // loop loads them all once and keeps them, so moving never waits.
    std::vector<std::string> artwork_backlog() const;
    void set_qr(std::string id, std::uint32_t texture, int width);
    // Likes and comments (social/comments.hpp): the counts of every app, and the latest
    // comments of one. Both are dropped unless "Show likes and comments" is on.
    void set_likes(social::Index likes);
    void set_comments(const std::string &id, social::Thread thread);
    // "Like or comment" on an app's page (Touchpad): a code for its discussion on GitHub.
    // The frame loop turns the pending address into the code.
    std::string pending_discussion_link;
    void set_discussion_qr(std::string link, std::uint32_t texture, int width)
    {
        discussion_qr_link_ = std::move(link);
        discussion_qr_ = texture;
        discussion_qr_width_ = width;
    }
    bool discussion_open() const
    {
        return discussion_;
    }
    // R2 on an app's page took the article to its comments (R2 again, or L2, goes back up).
    bool at_comments() const
    {
        return at_comments_;
    }
    // A developer's page: every app by one developer, opened from an app's page (R3) and
    // left with Circle. The link is the developer's (catalog::developer_link); the frame
    // loop turns the pending one into a code.
    bool open_developer(const std::string &author);
    const std::string &developer() const
    {
        return developer_;
    }
    std::string pending_developer_link;
    void set_developer_qr(std::string link, std::uint32_t texture)
    {
        developer_qr_link_ = std::move(link);
        developer_qr_ = texture;
    }
    // The picture shown for a coming-soon app that has no artwork of its own.
    void set_coming_soon_art(std::uint32_t texture)
    {
        coming_soon_art_ = texture;
    }
    // A floating notice in the top-right corner; it leaves after ten seconds.
    void notify(std::string title, std::string body);
    // A newer ProsperoStore is listed: asked once each time the store opens
    // (Update now / What's new / Skip), with the release notes in a view of their own.
    void offer_store_update(std::string version);
    // Whether this build and this console can install (reason says why not),
    // whether running apps can be told apart (updates and uninstalls need it),
    // and the scanned folder new apps go to.
    void set_installer(bool available, bool guard, std::string reason, std::string location);
    void set_activity(Activity activity);
    void finish_job(bool ok, bool restart, std::string title, std::string body);
    void set_settings(Settings settings)
    {
        active_catalog_url_ =
            settings.custom_active() ? settings.catalog_url : std::string(catalog::kDefaultApi);
        active_with_official_ = settings.custom_active() && settings.use_official;
        settings_ = std::move(settings);
    }
    const Settings &settings() const
    {
        return settings_;
    }
    bool settings_changed = false; // the frame loop saves them and clears this
    bool pending_catalog_url = false;
    void set_catalog_url(std::string_view value);
    // The address keyboard was closed without an answer: a custom catalog that was just
    // switched on, and has no address, goes off again.
    void catalog_url_cancelled();
    // The scanned folders apps can be installed to, with the room in each.
    void set_locations(std::vector<std::pair<std::string, std::uint64_t>> locations);
    // The running store: its title and the version it was built as.
    // The debug build's trace, shown first in About (diag/trace.hpp).
    void set_debug(std::vector<std::string> lines, std::string file)
    {
        debug_lines_ = std::move(lines);
        debug_file_ = std::move(file);
        write_about();
    }
    // The catalog's news, newest first: its own room, and a notice for an item not read yet.
    void set_news(std::vector<catalog::News> news);
    // What to check in ShadowMountPlus's settings: a notice now, the whole list in About.
    void set_setup_notes(std::vector<std::string> notes);
    void set_self(std::string id, std::string version)
    {
        self_id_ = std::move(id);
        self_version_ = std::move(version);
    }
    // Queue (0), Settings (1), About (2) or News (3), over whatever is on screen.
    void open_panel(int tab);
    // The titles running now; known is false when that can't be told, and
    // then nothing installed is changed.
    void set_running(std::vector<std::string> ids, bool known)
    {
        running_ = std::move(ids);
        guard_ = known;
    }
    // Scripted runs: what a player would do with the controller.
    bool open_app(const std::string &id);
    bool page_open() const
    {
        return details_;
    }
    void remote_install(const std::string &id);
    bool remote_uninstall(const std::string &id);
    bool remote_adopt(const std::string &id);
    // Sends the open page's install or update to the installer even when the
    // page would not offer it, to see the installer's own refusal.
    bool remote_order();
    // Every app the Updates shelf lists, as Square does there. Returns how many.
    std::size_t remote_update_all();
    // The time-left text of the running job, as the page shows it.
    std::string remote_time_left() const
    {
        return time_left();
    }
    // Fills the catalog with copies up to count apps, to measure a large one.
    void stress(std::size_t count);
    Order pending_order;
    std::vector<std::string> artwork() const;
    void set_query(std::string query);
    const std::string &query() const
    {
        return query_;
    }
    void set_status(std::string status)
    {
        status_ = std::move(status);
    }
    // No catalog could be loaded: the loading animation gives way to the reason.
    void catalog_failed(std::string status)
    {
        loading_ = false;
        status_ = std::move(status);
    }
    std::string pending_detail;
    bool pending_search = false;
    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    // The opening: the store's mark assembles while the catalog loads, then hands
    // over to the top bar. Off unless asked for (the console build asks).
    void play_intro()
    {
        intro_on_ = true;
        intro_clock_ = intro_leave_ = 0.0f;
        intro_leaving_ = false;
    }
    bool intro_showing() const
    {
        return intro_on_ && intro_leave_ < 1.0f;
    }
    // The intro has begun to hand over (or there is none): the music may come in.
    bool intro_finishing() const
    {
        return !intro_on_ || intro_leaving_;
    }
    void draw(hui::gfx::Renderer &renderer, const hui::ui::Fonts &fonts);
    bool wants_quit() const
    {
        return quit_;
    }

  private:
    enum class Zone
    {
        banner,
        chips,
        grid
    };
    enum class Sort
    {
        name,
        released,
        updated,
        liked // offered once the likes are known
    };
    static constexpr int kSections = 7;
    // What the card and the banner print, fitted once per catalog, not per frame.
    struct Fit
    {
        std::string title, author;
        bool ready = false;
    };

    // What the page can say about an app on this console, and its one next step.
    struct Offer
    {
        std::string headline, note, label, reason;
        Order::Kind primary = Order::Kind::none;
        bool armed = false;     // the button can be pressed now
        bool uninstall = false; // Square uninstalls (the primary is Update)
        bool busy = false;      // a transaction for this app is running or queued
        int tone = 0;           // 0 ink, 1 accent, 2 installed
        float progress = -1.0f; // 0..1 while a measurable phase runs
    };
    Offer offer(const App &app) const;
    // Reduce motion: 0 when the setting is on, else 1.
    float motion() const
    {
        return settings_.reduce_motion ? 0.0f : 1.0f;
    }
    // A slow breath for glows, still when motion is reduced.
    float breath() const;
    void hold_fill(hui::gfx::DrawList &list, const hui::gfx::Rect &button, float radius) const;
    bool hold_shown() const;
    void order(const App &app, Order::Kind kind);
    bool in_section(const App &app, int section) const;
    void rebuild();
    // rebuild(), with the focus kept on the app (and the shelf) it was on.
    void rebuild_in_place();
    void refresh_detail();
    const App *focused() const;
    const App *page_app() const;
    bool banner_shown() const;
    std::uint32_t art(const App &app, bool large = false) const;
    float chips_rest() const;
    float chips_y() const;
    float grid_top() const;
    float window_top() const;
    float fade_at(float y) const;
    hui::gfx::Rect card_rect(int index) const;
    hui::gfx::Rect focus_target() const;
    float focus_radius() const;
    float scroll_target() const;
    void refuse(hui::ui::Feedback &feedback, bool repeat, float dx, float dy);
    // The categories the current section holds, in the catalog's order (none in Discover, a
    // search or a developer's apps), and L2 / R2 stepping through them.
    std::vector<std::string_view> categories_here() const;
    void step_category(int delta, hui::ui::Feedback &feedback);
    void step_section(int delta, bool repeat, hui::ui::Feedback &feedback);
    void show_banner(int slot);
    void update_home(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void update_page(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void layout(const hui::ui::Fonts &fonts);
    void draw_top_bar(const hui::ui::Fonts &fonts);
    void draw_stage(const hui::ui::Fonts &fonts);
    void draw_section_header(const hui::ui::Fonts &fonts);
    void draw_tile(const hui::ui::Fonts &fonts, int index, unsigned layers);
    void rows_in_view(float scroll, int &first, int &last) const;
    const App *busy_app(float &progress, const char *&phase) const;
    void draw_grid(const hui::ui::Fonts &fonts);
    bool draw_art(hui::gfx::DrawList &list, int index, const hui::gfx::Rect &rect, float alpha,
                  float radius) const;
    void draw_job_card(const hui::ui::Fonts &fonts, std::uint32_t glass);
    void draw_page(const hui::ui::Fonts &fonts, std::uint32_t glass);
    void update_panel(const hui::InputFrame &input, hui::ui::Feedback &feedback);
    void draw_panel(const hui::ui::Fonts &fonts, std::uint32_t glass);
    void write_about();
    std::vector<std::string> debug_lines_;
    std::vector<std::string> setup_notes_;
    std::string debug_file_;
    const App *self_app() const;
    std::string time_left() const;
    void draw_action_box(const hui::ui::Fonts &fonts, std::uint32_t glass, const App &app,
                         float content);
    // Likes and comments are shown: the setting is on and the counts have arrived.
    bool social_shown() const
    {
        return settings_.show_comments && likes_known_;
    }
    // An app's counts while they are shown (none listed is zero), else null.
    const social::Counts *counts_of(const App &app) const;
    std::string discussion_of(const App &app) const;
    void forget_social();
    void draw_discussion(const hui::ui::Fonts &fonts, std::uint32_t glass);
    social::Index likes_;
    bool likes_known_ = false;
    // Where "Comments (N)" is in the open page's article, or -1 when it has none.
    int comments_block_ = -1;
    bool at_comments_ = false;
    std::map<std::string, social::Thread> threads_;
    bool discussion_ = false;
    hui::tween::Spring discussion_value_;
    std::string discussion_link_, discussion_name_, discussion_qr_link_;
    std::uint32_t discussion_qr_ = 0;
    int discussion_qr_width_ = 0;

    hui::ui::Theme theme_;
    hui::ui::TextView article_;
    hui::ui::ToastStack toasts_;
    hui::ui::Dialog dialog_;
    Activity activity_;
    // How fast the running phase moves, smoothed, for the time left.
    float rate_ = 0.0f, rate_time_ = 0.0f;
    std::uint64_t rate_done_ = 0;
    Settings settings_;
    std::string active_catalog_url_ = catalog::kDefaultApi;
    bool active_with_official_ = false; // the custom catalog is shown beside the official one
    std::vector<std::pair<std::string, std::uint64_t>> locations_;
    std::string self_id_, self_version_, self_location_ = "/data/homebrew";
    bool restart_needed_ = false;
    struct Done
    {
        bool ok = false;
        std::string title, body;
    };
    std::vector<Done> history_;
    bool panel_ = false;
    bool development_options_ = false;
    int panel_tab_ = 1, queue_focus_ = 0, setting_focus_ = 0;
    hui::tween::Spring panel_value_;
    hui::ui::TextView about_, news_view_;
    std::vector<catalog::News> news_;
    void write_news();
    std::vector<std::string> running_;
    std::string auto_order_;
    // "Update all": the titles still to be asked for, one at a time, each
    // once its verified details have arrived.
    std::vector<std::string> update_all_;
    std::string update_asked_;
    enum class Ask
    {
        none,
        adopt,
        quit,
        disable_signatures,
        store_update
    } ask_ = Ask::none;
    enum class StoreOffer
    {
        none,
        waiting, // for the store's own details (the release notes) and a quiet screen
        asked,
        done
    } store_offer_ = StoreOffer::none;
    std::string store_offer_version_;
    float store_offer_wait_ = 0.0f;
    bool store_offer_detail_asked_ = false, store_offer_notes_ = false, notes_open_ = false;
    bool store_update_wanted_ = false; // "Update now" was chosen: ordered once it can be
    float store_update_wait_ = 0.0f;
    hui::ui::TextView notes_;
    void open_store_offer(hui::ui::Feedback &feedback);
    void start_store_update();
    // Uninstalling is a hold, not a question: the button fills while it is held.
    hui::ui::HoldButton hold_;
    bool chime_ = false; // a job finished well: its toast plays the completion sound
    bool intro_on_ = false, intro_leaving_ = false;
    float intro_clock_ = 0.0f, intro_leave_ = 0.0f;
    void draw_intro(const hui::ui::Fonts &fonts);
    std::string ask_id_;
    bool fresh_catalog_ = false;
    std::vector<hui::tween::Spring> appear_;
    bool installer_ = false, guard_ = false;
    std::string installer_reason_ = "Installing is not switched on in this build";
    std::string install_location_;
    hui::gfx::DrawList scene_, overlay_;
    std::vector<App> apps_;
    std::vector<std::size_t> visible_, featured_;
    std::vector<Fit> fits_;
    bool fits_stale_ = true, placed_ = false, loading_ = true;
    int counts_[kSections] = {};
    hui::gfx::Rect chips_[kSections] = {};
    std::string status_ = "Connecting to the catalog...";
    system::Inventory inventory_;
    bool inventory_ready_ = false, catalog_current_ = false;
    std::string query_;
    std::string category_; // the category a grid section is narrowed to; empty for all
    std::string developer_, developer_link_, developer_qr_link_;
    bool developer_support_ = false; // the link is a place to support them
    std::uint32_t developer_qr_ = 0;
    void close_developer();
    std::string qr_id_;
    std::uint32_t qr_texture_ = 0, coming_soon_art_ = 0;
    int qr_width_ = 0;
    Sort sort_ = Sort::name;

    Zone zone_ = Zone::grid;
    int section_ = 0, focus_ = 0, banner_ = 0, banner_previous_ = 0;
    bool details_ = false, quit_ = false;
    // Closing the store: Circle held on the home page, a bar filling until it closes. The
    // hold must begin there, so leaving a page with Circle never runs on into closing.
    bool close_armed_ = false;
    float close_hold_ = 0.0f;
    float time_ = 0.0f, banner_clock_ = 0.0f;
    hui::tween::Spring scroll_, page_, banner_focus_, plate_;
    hui::tween::Timer banner_fade_, swap_;
    hui::ui::SpringRect ring_, chip_pill_;
    hui::tween::Spring ring_radius_;
    hui::ui::Pulse nudge_, press_;
    float nudge_x_ = 0.0f, nudge_y_ = 0.0f;
    std::vector<hui::tween::Spring> lift_;

    // Discover: shelves of tiles under a stage that shows one app big.
    struct Shelf
    {
        std::string title;
        int start = 0, count = 0, pos = 0;
        bool ranked = false; // "Most liked": each tile says its place
        hui::ui::Scroller scroll;
    };
    std::vector<Shelf> shelves_;
    bool discover() const;
    int shelf_of(int index) const;
    int stage_index() const; // the app the stage (or the dimmed backdrop) shows
    int stage_ = -1, stage_previous_ = -1;
    hui::tween::Timer stage_fade_;
    hui::ui::SpringColor tint_;
    float art_clock_ = 0.0f;
};

class Fonts
{
  public:
    bool load(hui::gfx::Renderer &renderer, const std::string &assets);
    hui::ui::Fonts refs;

  private:
    hui::gfx::Font faces_[6];
};

} // namespace store
