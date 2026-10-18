// ProsperoStore - Storefront composition and navigation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/store.hpp"
#include "core/save_file.hpp"
#include "install/transaction.hpp"
#include "system/locations.hpp"
#include "ui/glyphs.hpp"
#include "ui/components/data_common.hpp"

#include <utility>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace store
{
using namespace hui;
namespace
{
using gfx::Color;
using gfx::Rect;

// ---- the design language: Storefront's shapes in Farlight's colours ----

const Color kWhite = Color::rgb(0xffffff);
const Color kBlack = Color::rgb(0x000000);
const Color kClear = Color::rgb(0x000000, 0.0f);
const Color kInk = Color::rgb(0xf4efe6);      // warm white: all text
const Color kDeep = Color::rgb(0x0e0f24);     // Farlight: its dark,
const Color kMid = Color::rgb(0x42358f);      // ... its mid tone
const Color kAccent = Color::rgb(0xffd166);   // ... and its light: calls to action
const Color kOnAccent = Color::rgb(0x241a05); // text on the accent
const Color kCoal = Color::rgb(0x0e0f12);
const Color kPanel = gfx::mix(Color::rgb(0x202228), kDeep, 0.5f);
const Color kOwned = Color::rgb(0x8fdab2); // "this is installed"

// The headline face has no Greek letters. A name it cannot draw whole is set in
// the title face, which has every letter the store's fonts carry.
const ui::FontRef &face_for(std::string_view text, const ui::FontRef &wanted,
                            const ui::FontRef &complete)
{
    for (std::size_t index = 0; index < text.size();)
    {
        if (!wanted.font->has_glyph(gfx::next_codepoint(text, &index)))
            return complete;
    }
    return wanted;
}

constexpr float kWidth = gfx::kVirtualWidth;
constexpr float kHeight = gfx::kVirtualHeight;
constexpr float kMargin = 96.0f;
constexpr float kRight = kWidth - kMargin;
constexpr float kTopY = 64.0f;     // the top bar's centre line
constexpr float kPageTop = 104.0f; // content starts under the top bar

// Discover: a stage that shows one app big, its own key art behind its name,
// over shelves of 16:9 tiles. The stage follows the focus.
constexpr float kStageKickerY = 240.0f;
constexpr float kStageTitleY = 338.0f;
constexpr float kStageMetaY = 392.0f;
constexpr float kStageTextY = 446.0f;
constexpr float kStageTextW = 880.0f;
constexpr Rect kStageButton{96.0f, 520.0f, 276.0f, 64.0f};
constexpr float kBannerSeconds = 8.0f;
constexpr std::size_t kFeatured = 5;
constexpr float kShelvesTop = 640.0f;  // the shelves' window starts here,
constexpr float kShelfHeadY = 694.0f;  // ... with the first shelf's title,
constexpr float kShelfTilesY = 718.0f; // ... its tiles,
constexpr float kShelfPitch = 290.0f;  // ... and the next shelf this far down
constexpr float kTileW = 368.0f;
constexpr float kTileH = 207.0f; // 16:9, the key art's shape
constexpr float kTileGap = 24.0f;
constexpr float kTileRadius = 18.0f;
constexpr float kLift = 0.06f; // how much the focused tile grows
// The other sections: a title over a grid of the same tiles, four across.
constexpr int kColumns = 4;
constexpr float kGridTileW = (kWidth - 2.0f * kMargin - (kColumns - 1) * kTileGap) / kColumns;
constexpr float kGridTileH = kGridTileW * 9.0f / 16.0f;
constexpr float kHeaderY = 206.0f;
constexpr float kGridTop = 248.0f;
constexpr float kRowPitch = kGridTileH + 32.0f;
constexpr float kViewBottom = 984.0f; // tiles end above the hint row
constexpr float kFadeFoot = 44.0f;
constexpr float kFadeHead = 24.0f;
constexpr float kTabH = 44.0f; // the sections in the top bar

// The product page: key art behind, words on the left, a glass panel on the right.
constexpr Rect kPageIcon{96.0f, 146.0f, 144.0f, 144.0f};
constexpr float kInfoX = 96.0f;
constexpr float kInfoW = 900.0f;
constexpr float kFactsW = 1180.0f; // the row of chips may run as far as the title does
constexpr Rect kActionBox{1336.0f, 212.0f, 488.0f, 604.0f};
constexpr Rect kPageHero{96.0f, 84.0f, 212.0f, 212.0f}; // the icon, when it is the app's picture
constexpr float kButtonRadius = 18.0f;
// Closing with Circle: the bar appears after a moment, so a tap never shows it, and fills.
constexpr float kCloseDelay = 0.3f;
constexpr float kCloseHold = 1.2f;

constexpr const char *kSectionNames[] = {"Discover",    "Apps",      "Games",  "Tools",
                                         "Coming soon", "Installed", "Updates"};

// The catalog's categories, in the order its shelves are shown (the catalog's docs/metadata.md).
constexpr std::pair<std::string_view, std::string_view> kCategories[] = {
    {"console-ports", "Console game ports"},
    {"pc-ports", "PC game ports"},
    {"homebrew-games", "Homebrew games"},
    {"emulators", "Emulators"},
    {"streaming", "Game streaming"},
    {"video", "Video & TV"},
    {"music", "Music & audio"},
    {"ai", "AI"},
    {"utilities", "Utilities"},
    {"tools", "Tools & tests"},
    {"showcases", "Showcases"},
};

std::string category_label(std::string_view key)
{
    for (const auto &[known, label] : kCategories)
        if (known == key)
            return std::string(label);
    return {};
}

// What one pass over the cards records: all covers, then all shapes, then each
// face, so a grid costs a few draw calls instead of several per card.
enum Layer : unsigned
{
    kImages = 1,
    kShapes = 2,
    kSemibold = 4,
    kRegular = 8,
    kPlates = 16, // what lies under the pictures
    kAllLayers = 31,
};

// A key art window that drifts and breathes over half a minute: the picture
// is never still, and never moves enough to be noticed.
Rect drift(float time)
{
    const float zoom = 1.035f + 0.035f * (0.5f - 0.5f * std::cos(time * 0.2094f));
    const float size = 1.0f / zoom, room = (1.0f - size) * 0.5f;
    return {room + room * 0.6f * std::sin(time * 0.071f),
            room + room * 0.5f * std::cos(time * 0.053f), size, size};
}

std::string folded(std::string text)
{
    for (auto &c : text)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// "31.8 MB", "548 GB": sizes as people say them.
std::string size_text(std::uint64_t bytes)
{
    char text[32];
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (megabytes >= 10240.0)
        std::snprintf(text, sizeof(text), "%.0f GB", megabytes / 1024.0);
    else if (megabytes >= 1024.0)
        std::snprintf(text, sizeof(text), "%.1f GB", megabytes / 1024.0);
    else if (megabytes >= 10.0)
        std::snprintf(text, sizeof(text), "%.0f MB", megabytes);
    else
        std::snprintf(text, sizeof(text), "%.1f MB", megabytes);
    return text;
}

// Baseline that centres a line of the given size on cy.
float centred(float cy, float size)
{
    return cy + size * 0.35f;
}

void draw_check(gfx::DrawList &list, float cx, float cy, float size, Color colour)
{
    const float stroke = std::max(2.0f, size * 0.16f);
    list.line(cx - size * 0.42f, cy + size * 0.02f, cx - size * 0.12f, cy + size * 0.32f, stroke,
              colour);
    list.line(cx - size * 0.12f, cy + size * 0.32f, cx + size * 0.44f, cy - size * 0.3f, stroke,
              colour);
}

// A pill with a word on it. Returns its width.
float pill(gfx::DrawList &list, const ui::Fonts &fonts, std::string_view word, float x, float cy,
           float height, Color fill, Color ink, float alpha, unsigned layers)
{
    const float size = height * 0.58f;
    const float width = fonts.semibold.measure(word, size) + height * 0.9f;
    if (layers & kShapes)
        list.rounded_rect({x, cy - height * 0.5f, width, height}, height * 0.5f,
                          fill.with_alpha(alpha));
    if (layers & kSemibold)
        ui::text(list, fonts.semibold, word, x + width * 0.5f, centred(cy, size), size,
                 ink.with_alpha(alpha), gfx::Align::center);
    return width;
}

// The like sign: an arrow head pointing up, as beside a discussion's title on GitHub.
void like_sign(gfx::DrawList &list, float x, float cy, float size, Color ink)
{
    list.triangle({x, cy - size * 0.5f, size, size * 0.9f}, ink);
}

// The comment sign: a speech bubble.
void comment_sign(gfx::DrawList &list, float x, float cy, float size, Color ink)
{
    const float height = size * 0.72f;
    list.bordered_rect({x, cy - height * 0.62f, size, height}, size * 0.22f, kClear,
                       std::max(1.5f, size * 0.12f), ink);
    list.triangle({x + size * 0.16f, cy + height * 0.3f, size * 0.38f, size * 0.32f}, ink, 0.0f,
                  3.14159f);
}

std::string counted(std::uint32_t count, const char *one, const char *many)
{
    return std::to_string(count) + (count == 1 ? one : many);
}

// The state a card or the banner announces, and how loudly.
struct Mark
{
    const char *word = nullptr;
    bool loud = false; // on the accent
};
Mark mark(const App &app)
{
    if (app.badge == "Update")
        return {"Update", true};
    if (app.badge == "Duplicate")
        return {"Duplicate", false};
    if (app.badge == "Coming soon")
        return {"Coming soon", false};
    return {};
}

ui::Theme farlight_theme()
{
    ui::Theme theme = ui::themes()[0];
    theme.page = kCoal;
    theme.page_text = kInk;
    theme.page_text_muted = kInk.with_alpha(0.62f);
    theme.surface = kPanel;
    theme.surface_high = gfx::mix(kPanel, kMid, 0.18f);
    theme.text = kInk;
    theme.text_muted = kInk.with_alpha(0.62f);
    theme.primary = kAccent;
    theme.on_primary = kOnAccent;
    theme.accent = kAccent;
    theme.outline = kInk.with_alpha(0.2f);
    theme.focus = kInk;
    theme.success = kOwned;
    return theme;
}
} // namespace

Screen::Screen() : theme_(farlight_theme())
{
    article_.style.theme = theme_;
    article_.style.body_size = 26;
    article_.style.footer = false;
    article_.style.panel = false;
    article_.style.focus_ring = false;
    article_.style.padding = 4;
    article_.set_bounds({kInfoX - 4.0f, 604.0f, kInfoW + 8.0f, 322.0f});
    dialog_.style.theme = theme_;
    notes_.style.theme = theme_;
    notes_.style.body_size = 25;
    notes_.style.footer = false;
    notes_.style.padding = 44;
    notes_.set_bounds({460.0f, 132.0f, 1000.0f, 760.0f});
    about_.style.theme = theme_;
    about_.style.body_size = 26;
    about_.style.footer = false;
    about_.style.panel = false;
    about_.style.focus_ring = false;
    about_.style.padding = 4;
    about_.set_bounds({636.0f, 350.0f, 1150.0f, 570.0f});
    news_view_.style = about_.style;
    news_view_.set_bounds({636.0f, 212.0f, 1150.0f, 708.0f});
    toasts_.style.theme = theme_;
    toasts_.style.frosted = true;
    toasts_.style.duration = 10.0f;
    plate_.snap(1.0f);
}

void Screen::notify(std::string title, std::string body)
{
    toasts_.push(ui::StatusKind::info, std::move(title), std::move(body), 10.0f);
}

void Screen::set_setup_notes(std::vector<std::string> notes)
{
    setup_notes_ = std::move(notes);
    write_about();
    if (setup_notes_.empty())
        return;
    const auto more = setup_notes_.size() - 1;
    toasts_.push(ui::StatusKind::warning, "Check your ShadowMountPlus settings",
                 more == 0 ? "Press Options and open About to see what the store found."
                           : std::to_string(setup_notes_.size()) +
                                 " things to check: press Options and open About.",
                 12.0f);
}

void Screen::offer_store_update(std::string version)
{
    if (store_offer_ != StoreOffer::none)
        return;
    store_offer_version_ = std::move(version);
    store_offer_ = StoreOffer::waiting;
    store_offer_wait_ = 0.0f;
}

void Screen::open_store_offer(ui::Feedback &feedback)
{
    const auto self = std::find_if(apps_.begin(), apps_.end(),
                                   [&](const auto &app) { return app.title_id == self_id_; });
    const std::string notes =
        self != apps_.end() && self->detail ? self->detail->release_notes : std::string{};
    // The version as the console writes it (01.000.010), when the catalog gives it.
    if (self != apps_.end() && !self->available_version.empty())
        store_offer_version_ = self->available_version;
    store_offer_notes_ = !notes.empty();
    if (store_offer_notes_)
    {
        // The catalog's notes are plain text: lines, with list items starting "- ".
        using Block = ui::TextBlock;
        std::vector<Block> blocks;
        blocks.push_back(Block::heading("What's new in " + store_offer_version_, 2));
        std::size_t at = 0;
        while (at <= notes.size())
        {
            const std::size_t end = std::min(notes.find('\n', at), notes.size());
            std::string line = notes.substr(at, end - at);
            at = end + 1;
            while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
                line.pop_back();
            if (line.empty())
                continue;
            if (line.starts_with("- "))
                blocks.push_back(Block::bullet(line.substr(2)));
            else
                blocks.push_back(Block::paragraph(std::move(line)));
        }
        notes_.set_content(std::move(blocks));
        notes_.scroll_to(0, true);
    }
    ask_ = Ask::store_update;
    std::vector<ui::DialogButton> buttons;
    buttons.push_back({"Skip"});
    if (store_offer_notes_)
        buttons.push_back({"What's new"});
    buttons.push_back({"Update now", ui::ButtonKind::primary});
    const int update_now = static_cast<int>(buttons.size()) - 1;
    dialog_.open({ui::StatusKind::info, "ProsperoStore " + store_offer_version_ + " is available",
                  "You have " + self_version_ +
                      ". The update is checked against the signed catalog, and ProsperoStore "
                      "closes when it is installed.",
                  std::move(buttons), update_now},
                 feedback);
    store_offer_ = StoreOffer::asked;
}

void Screen::start_store_update()
{
    // Ordered from update(), as soon as the store's own page could order it.
    store_offer_ = StoreOffer::done;
    store_update_wanted_ = true;
    store_update_wait_ = 0.0f;
}

// ---- data -------------------------------------------------------------------

bool Screen::in_section(const App &app, int section) const
{
    if (app.local_only && section != 5)
        return false;
    switch (section)
    {
    case 1:
        return app.kind == "app";
    case 2:
        return app.kind == "game";
    case 3:
        return app.kind == "tool";
    case 4:
        return app.catalog_badge == "Coming soon";
    case 5:
        return !app.installed.empty();
    case 6:
        return app.badge == "Update";
    default:
        return true;
    }
}

// When the app's listed release came out; the record's last change when the catalog doesn't say.
static const std::string &release_day(const App &app)
{
    return app.released.empty() ? app.updated : app.released;
}

void Screen::rebuild()
{
    visible_.clear();
    for (int &count : counts_)
        count = 0;
    const auto query = folded(query_);
    // A grid section narrowed to one category (L2 / R2); its counts still hold the whole section.
    const bool narrowed = !category_.empty() && !categories_here().empty();
    for (std::size_t i = 0; i < apps_.size(); ++i)
    {
        const App &app = apps_[i];
        if (!query.empty() && app.folded_name.find(query) == std::string::npos &&
            app.folded_author.find(query) == std::string::npos)
            continue;
        if (!developer_.empty() && (app.local_only || app.author != developer_))
            continue;
        for (int section = 0; section < kSections; ++section)
            counts_[section] += in_section(app, section) ? 1 : 0;
        if (in_section(app, section_) && (!narrowed || app.category == category_))
            visible_.push_back(i);
    }
    const auto likes = [&](const App &app)
    {
        const auto *counts = social_shown() ? counts_of(app) : nullptr;
        return counts ? counts->likes : 0u;
    };
    if (sort_ == Sort::liked && !social_shown())
        sort_ = Sort::name;
    std::stable_sort(visible_.begin(), visible_.end(),
                     [&](auto first, auto second)
                     {
                         const auto &a = apps_[first];
                         const auto &b = apps_[second];
                         if (sort_ == Sort::liked && likes(a) != likes(b))
                             return likes(a) > likes(b);
                         if (sort_ == Sort::released && a.released != b.released)
                             return a.released > b.released;
                         if (sort_ == Sort::updated && a.updated != b.updated)
                             return a.updated > b.updated;
                         return a.folded_name == b.folded_name ? a.title_id < b.title_id
                                                               : a.folded_name < b.folded_name;
                     });
    // Discover is shelves: the same apps in groups, one after another in
    // visible_, each shelf remembering where its focus was.
    auto previous = std::move(shelves_);
    shelves_.clear();
    if (discover())
    {
        const auto all = visible_;
        visible_.clear();
        // order: 0 as listed, 1 the twelve with the newest releases, 2 the twelve listed last, 3
        // the ten with the most likes (ties by name).
        const auto shelf = [&](std::string title, auto keep, int order)
        {
            std::vector<std::size_t> members;
            for (const auto index : all)
                if (keep(apps_[index]))
                    members.push_back(index);
            if (order == 3)
            {
                std::stable_sort(members.begin(), members.end(),
                                 [&](auto a, auto b)
                                 {
                                     return social::more_liked(
                                         likes(apps_[a]), apps_[a].folded_name, likes(apps_[b]),
                                         apps_[b].folded_name);
                                 });
                if (members.size() > social::kMostLiked)
                    members.resize(social::kMostLiked);
            }
            else if (order)
            {
                std::stable_sort(members.begin(), members.end(),
                                 [&](auto a, auto b)
                                 {
                                     return order == 2
                                                ? apps_[a].added > apps_[b].added
                                                : release_day(apps_[a]) > release_day(apps_[b]);
                                 });
                if (members.size() > 12)
                    members.resize(12);
            }
            if (members.empty())
                return;
            Shelf next;
            next.title = std::move(title);
            next.start = static_cast<int>(visible_.size());
            next.count = static_cast<int>(members.size());
            next.ranked = order == 3;
            for (const auto &old : previous)
                if (old.title == next.title)
                {
                    next.pos = std::min(old.pos, next.count - 1);
                    next.scroll = old.scroll;
                }
            visible_.insert(visible_.end(), members.begin(), members.end());
            shelves_.push_back(std::move(next));
        };
        const auto listed = [](const App &a) { return a.catalog_badge != "Coming soon"; };
        // What's new: apps the catalog listed in the last thirty days, then the others it
        // changed last. "Today" is the catalog's newest date, so the console's clock is not
        // trusted; a catalog that doesn't say when it listed an app keeps the single shelf.
        const auto day = [](const std::string &iso)
        {
            return iso.size() < 10 ? 0L
                                   : std::atol(iso.substr(0, 4).c_str()) * 372L +
                                         std::atol(iso.substr(5, 2).c_str()) * 31L +
                                         std::atol(iso.substr(8, 2).c_str());
        };
        long today = 0;
        for (const auto index : all)
            today = std::max({today, day(apps_[index].added), day(apps_[index].updated)});
        const auto fresh = [&](const App &a)
        { return !a.local_only && listed(a) && !a.added.empty() && today - day(a.added) <= 30; };
        const auto current = [&](const App &a) { return !a.local_only && listed(a); };
        const auto renewed = [](const App &a)
        { return !a.released.empty() && (a.added.empty() || a.released > a.added); };
        shelf("Updates for you", [](const App &a) { return a.badge == "Update"; }, 0);
        if (std::any_of(all.begin(), all.end(), [&](auto index) { return fresh(apps_[index]); }))
        {
            shelf("New in the catalog", fresh, 2);
            // The other shelf leaves out the apps that one shows.
            std::vector<std::string> shown_new;
            for (int k = 0; k < shelves_.back().count; ++k)
                shown_new.push_back(
                    apps_[visible_[static_cast<std::size_t>(shelves_.back().start + k)]].title_id);
            shelf(
                "Recently updated",
                [&](const App &a)
                {
                    // An update is a release published after the catalog listed the app; an edit to
                    // the listing (a new category, a corrected description) is not.
                    return current(a) && renewed(a) &&
                           std::find(shown_new.begin(), shown_new.end(), a.title_id) ==
                               shown_new.end();
                },
                1);
        }
        else
            shelf("New and updated", current, 1);
        // The apps people upvoted, in order: there once the likes are known and some app has one.
        if (social_shown())
            shelf("Most liked", [&](const App &a) { return current(a) && likes(a) > 0; }, 3);
        // A shelf for each category, in the catalog's order. An app without a known category (from
        // an older or a custom catalog) stays on the shelf of its kind.
        for (const auto &[key, label] : kCategories)
            shelf(
                std::string(label),
                [&, key = key](const App &a) { return a.category == key && listed(a); }, 0);
        shelf(
            "Apps",
            [&](const App &a) { return a.category.empty() && a.kind == "app" && listed(a); }, 0);
        shelf(
            "Games",
            [&](const App &a) { return a.category.empty() && a.kind == "game" && listed(a); }, 0);
        shelf(
            "Tools",
            [&](const App &a) { return a.category.empty() && a.kind == "tool" && listed(a); }, 0);
        shelf("Coming soon", [](const App &a) { return a.catalog_badge == "Coming soon"; }, 0);
        shelf("On this console", [](const App &a) { return !a.installed.empty(); }, 0);
    }
    // The stage features the newest releases.
    featured_.clear();
    for (std::size_t i = 0; i < apps_.size(); ++i)
        if (!apps_[i].local_only && !apps_[i].released.empty() &&
            apps_[i].catalog_badge != "Coming soon")
            featured_.push_back(i);
    std::stable_sort(featured_.begin(), featured_.end(), [&](auto first, auto second)
                     { return apps_[first].released > apps_[second].released; });
    if (featured_.size() > kFeatured)
        featured_.resize(kFeatured);
    banner_ = std::min(banner_, std::max(0, static_cast<int>(featured_.size()) - 1));
    banner_previous_ = banner_;
    const int count = static_cast<int>(visible_.size());
    focus_ = std::clamp(focus_, 0, std::max(0, count - 1));
    if (count == 0 && zone_ == Zone::grid)
        zone_ = banner_shown() ? Zone::banner : Zone::chips;
    if (zone_ == Zone::banner && !banner_shown())
        zone_ = count ? Zone::grid : Zone::chips;
    lift_.resize(visible_.size());
    appear_.resize(apps_.size());
    fits_stale_ = true;
}

void Screen::rebuild_in_place()
{
    const std::string id = focused() ? focused()->title_id : std::string{};
    const std::string shelf = focused() && discover() && !shelves_.empty()
                                  ? shelves_[static_cast<std::size_t>(shelf_of(focus_))].title
                                  : std::string{};
    rebuild();
    if (id.empty())
        return;
    int found = -1;
    for (const auto &next : shelves_)
        for (int k = 0; next.title == shelf && found < 0 && k < next.count; ++k)
            if (apps_[visible_[static_cast<std::size_t>(next.start + k)]].title_id == id)
                found = next.start + k;
    for (std::size_t k = 0; found < 0 && k < visible_.size(); ++k)
        if (apps_[visible_[k]].title_id == id)
            found = static_cast<int>(k);
    if (found < 0)
    {
        details_ = false;
        return;
    }
    focus_ = found;
    if (discover() && !shelves_.empty())
    {
        auto &now = shelves_[static_cast<std::size_t>(shelf_of(focus_))];
        now.pos = focus_ - now.start;
    }
}

// ---- likes and comments ---------------------------------------------------------

const social::Counts *Screen::counts_of(const App &app) const
{
    static const social::Counts none;
    if (!settings_.show_comments || app.local_only || app.extra)
        return nullptr;
    // The app's own file, read when its page opened, is the newer word.
    if (const auto thread = threads_.find(app.title_id); thread != threads_.end())
        return &thread->second.counts;
    if (!likes_known_)
        return nullptr;
    const auto listed = likes_.find(app.title_id);
    return listed == likes_.end() ? &none : &listed->second;
}

// Where the app is liked and commented on: the only thing from these files that is ever used
// as an address, and only in a code for the player to scan.
std::string Screen::discussion_of(const App &app) const
{
    const auto *counts = counts_of(app);
    if (counts && social::discussion_url(counts->url))
        return counts->url;
    const auto listed = likes_.find(app.title_id);
    if (counts && listed != likes_.end() && social::discussion_url(listed->second.url))
        return listed->second.url;
    return {};
}

void Screen::set_likes(social::Index likes)
{
    if (!settings_.show_comments)
        return;
    likes_ = std::move(likes);
    likes_known_ = true;
    rebuild_in_place();
}

void Screen::set_comments(const std::string &id, social::Thread thread)
{
    if (!settings_.show_comments)
        return;
    threads_.insert_or_assign(id, std::move(thread));
    if (page_app() && page_app()->title_id == id)
    {
        // The reader stays where they were in the article.
        const float place = article_.scroll();
        const bool there = at_comments_;
        refresh_detail();
        article_.scroll_to(place, true);
        at_comments_ = there && comments_block_ >= 0;
    }
}

void Screen::forget_social()
{
    likes_.clear();
    threads_.clear();
    likes_known_ = false;
    discussion_ = false;
    pending_discussion_link.clear();
    rebuild_in_place();
    if (page_app())
        refresh_detail();
}

// The grid's cards whose rows are on screen at this scroll, and one row beyond.
void Screen::rows_in_view(float scroll, int &first, int &last) const
{
    const int count = static_cast<int>(visible_.size());
    const float top = scroll + window_top() - kGridTop - kRowPitch;
    const float bottom = scroll + kViewBottom - kGridTop + kRowPitch;
    first = std::clamp(static_cast<int>(std::floor(top / kRowPitch)) * kColumns, 0, count);
    last = std::clamp((static_cast<int>(std::floor(bottom / kRowPitch)) + 1) * kColumns, 0, count);
}

void Screen::stress(std::size_t count)
{
    std::vector<App> apps;
    for (const auto &app : apps_)
        if (!app.local_only)
            apps.push_back(app);
    const std::size_t real = apps.size();
    for (std::size_t i = 0; real && apps.size() < count; ++i)
    {
        App copy = apps[i % real];
        const auto number = std::to_string(10000 + i);
        copy.title_id = "PPSA" + number;
        copy.name += " " + number.substr(1);
        copy.badge = copy.catalog_badge;
        copy.installed.clear();
        apps.push_back(std::move(copy));
    }
    const auto icons = apps;
    set_catalog(std::move(apps), status_, catalog_current_);
    for (const auto &app : icons)
    {
        set_icon(app.title_id, app.icon);
        set_art(app.title_id, app.art);
    }
}

const App *Screen::focused() const
{
    return visible_.empty() ? nullptr : &apps_[visible_[static_cast<std::size_t>(focus_)]];
}

const App *Screen::page_app() const
{
    return details_ ? focused() : nullptr;
}

bool Screen::banner_shown() const
{
    return discover() && !featured_.empty();
}

std::uint32_t Screen::art(const App &app, bool large) const
{
    return large && app.art                     ? app.art
           : app.icon                           ? app.icon
           : app.art                            ? app.art
           : app.catalog_badge == "Coming soon" ? coming_soon_art_
                                                : 0;
}

void Screen::set_catalog(std::vector<App> apps, std::string status, bool current)
{
    const auto previous = focused() ? focused()->title_id : std::string{};
    const auto shown = details_ ? previous : std::string{};
    apps_ = std::move(apps);
    catalog_current_ = current;
    for (auto &app : apps_)
    {
        app.catalog_badge = app.badge;
        app.installed.clear();
        app.folded_name = folded(app.name);
        app.folded_author = folded(app.author);
    }
    fresh_catalog_ = true;
    // With a thousand apps and a hundred installed, a search per installed
    // app is a hundred thousand comparisons on the frame: look them up instead.
    // (Room for the local ones first: the keys point into the apps.)
    apps_.reserve(apps_.size() + inventory_.apps.size());
    std::map<std::string_view, std::size_t> listed;
    for (std::size_t i = 0; i < apps_.size(); ++i)
        listed.emplace(apps_[i].title_id, i);
    for (const auto &installed : inventory_.apps)
    {
        const auto id = installed.id.empty() ? "local:" + installed.path : installed.id;
        const auto known = listed.find(id);
        auto found = known == listed.end()
                         ? apps_.end()
                         : apps_.begin() + static_cast<std::ptrdiff_t>(known->second);
        if (found == apps_.end())
        {
            App local;
            local.title_id = id;
            local.name = installed.name;
            local.author = installed.image ? "Installed image" : "Installed app";
            local.local_only = true;
            local.folded_name = folded(local.name);
            local.folded_author = folded(local.author);
            apps_.push_back(std::move(local));
            found = apps_.end() - 1;
            listed.emplace(found->title_id, apps_.size() - 1);
        }
        found->installed.push_back(installed);
    }
    for (auto &app : apps_)
        if (!app.installed.empty())
        {
            const auto &installed = app.installed.front();
            app.badge = installed.duplicate ? "Duplicate"
                        : installed.managed &&
                                catalog::update_available(installed.version, app.available_version)
                            ? "Update"
                            : "Installed";
        }
    status_ = std::move(status);
    loading_ = loading_ && apps_.empty();
    rebuild();
    const auto found = std::find_if(visible_.begin(), visible_.end(),
                                    [&](auto index) { return apps_[index].title_id == previous; });
    if (found != visible_.end())
        focus_ = static_cast<int>(found - visible_.begin());
    details_ = details_ && found != visible_.end() && shown == previous;
    if (details_)
    {
        refresh_detail();
        pending_detail = apps_[*found].local_only ? std::string{} : previous;
    }
}

void Screen::set_inventory(system::Inventory inventory)
{
    inventory_ = std::move(inventory);
    // Where the store itself is installed, for its own update.
    for (const auto &app : inventory_.apps)
        if (app.id == self_id_ && !app.image && app.path.ends_with("/" + self_id_))
            self_location_ = app.path.substr(0, app.path.find_last_of('/'));
    inventory_ready_ = true;
    auto catalog_apps = apps_;
    std::erase_if(catalog_apps, [](const auto &app) { return app.local_only; });
    for (auto &app : catalog_apps)
        app.badge = app.catalog_badge;
    set_catalog(std::move(catalog_apps), status_, catalog_current_);
}

void Screen::set_query(std::string query)
{
    query_ = std::move(query);
    focus_ = 0;
    developer_.clear();
    rebuild();
    if (!visible_.empty())
        zone_ = Zone::grid;
}

void Screen::set_detail(const catalog::Entry &entry)
{
    for (auto &app : apps_)
        if (app.title_id == entry.id)
        {
            app.description = entry.description;
            app.version = entry.version;
            app.detail = entry;
            app.detail_error.clear();
            break;
        }
    if (page_app() && page_app()->title_id == entry.id)
        refresh_detail();
}

void Screen::set_detail_error(const std::string &id, std::string message)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.detail_error = message;
    if (page_app() && page_app()->title_id == id)
        refresh_detail();
}

void Screen::refresh_detail()
{
    const auto &app = *focused();
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    comments_block_ = -1;
    at_comments_ = false;
    if (app.local_only && catalog_current_ &&
        std::any_of(app.installed.begin(), app.installed.end(),
                    [](const auto &installed) { return installed.managed; }))
        blocks.push_back(Block::paragraph("This app is no longer listed in the catalog. "
                                          "Check with its developer before using it."));
    if (!app.detail_error.empty())
        blocks.push_back(Block::paragraph("Details unavailable: " + app.detail_error));
    if (app.detail)
    {
        // The facts are chips above; the article is the words.
        const auto &entry = *app.detail;
        blocks.push_back(Block::paragraph(entry.description.empty() ? "No description provided."
                                                                    : entry.description));
        if (!entry.release_notes.empty())
        {
            blocks.push_back(Block::heading("What's new", 3));
            blocks.push_back(Block::paragraph(entry.release_notes));
        }
        // The latest comments, once the app's own file is here. Text nobody checked: it was
        // made plain when it was read, and is only ever drawn as words.
        if (const auto found = threads_.find(app.title_id);
            settings_.show_comments && !app.extra && found != threads_.end())
        {
            const auto &thread = found->second;
            comments_block_ = static_cast<int>(blocks.size());
            blocks.push_back(
                Block::heading("Comments (" + std::to_string(thread.counts.comments) + ")", 3));
            for (const auto &comment : thread.latest)
            {
                // Who and when on a line of its own, their words set in from a bar under it.
                blocks.push_back(Block::paragraph(
                    comment.date.empty() ? comment.author
                                         : comment.author + "  \xC2\xB7  " + comment.date));
                blocks.push_back(Block::quote(comment.text));
            }
            if (thread.latest.empty())
                blocks.push_back(Block::paragraph(
                    thread.counts.comments == 0 ? "No comments yet."
                                                : "They are in the app's discussion on GitHub."));
            else if (thread.counts.comments > thread.latest.size())
                blocks.push_back(Block::paragraph(
                    "These are the latest " + std::to_string(thread.latest.size()) +
                    ". The others are in the app's discussion on GitHub."));
        }
        // What the catalog's scan found, in words. It is advice: say what was found, not "safe".
        if (!entry.sandbox.empty())
        {
            blocks.push_back(Block::heading("Safety", 3));
            if (entry.sandbox == "stays")
                blocks.push_back(Block::paragraph(
                    "The catalog's scan found no way for this app to reach beyond its own files."));
            else if (entry.sandbox == "leaves")
                blocks.push_back(Block::paragraph(
                    "This app can get full access to the console, usually to read and write files "
                    "outside its own folder. Install it only if you trust its developer."));
            else
                blocks.push_back(Block::paragraph("The catalog's scan could not tell whether this "
                                                  "app reaches beyond its own files."));
            if (entry.helpers_unapproved > 0)
                blocks.push_back(Block::paragraph(
                    std::to_string(entry.helpers_unapproved) + " of its " +
                    std::to_string(entry.helpers) +
                    " helper program(s) that run outside the sandbox have not been reviewed by the "
                    "catalog's maintainers."));
            else if (entry.helpers > 0)
                blocks.push_back(Block::paragraph("Its " + std::to_string(entry.helpers) +
                                                  " helper program(s) that run outside the sandbox "
                                                  "are on the catalog's reviewed list."));
            if (entry.build == "attested")
                blocks.push_back(Block::paragraph(
                    "Built by GitHub Actions: a signed statement ties this file to its source."));
            else if (entry.build == "workflow")
                blocks.push_back(Block::paragraph("Released by a workflow of its repository. "
                                                  "Nothing proves where it was built."));
            else if (entry.build == "developer")
                blocks.push_back(Block::paragraph("Built and uploaded by its developer. Nothing "
                                                  "ties this file to the published source."));
            blocks.push_back(Block::paragraph("From an automatic scan that reads the file and "
                                              "never runs it. It can miss things."));
        }
        if (!entry.source.empty())
        {
            blocks.push_back(Block::heading("Source", 3));
            blocks.push_back(Block::paragraph(entry.source));
        }
    }
    else if (app.detail_error.empty() && !app.local_only)
        blocks.push_back(Block::paragraph("Loading app details..."));
    for (const auto &installed : app.installed)
    {
        blocks.push_back(
            Block::heading(installed.image ? "Installed image" : "On this console", 3));
        blocks.push_back(Block::paragraph(installed.path));
    }
    article_.set_content(std::move(blocks));
    article_.scroll_to(0, true);
}

void Screen::set_icon(const std::string &id, std::uint32_t texture)
{
    for (std::size_t i = 0; i < apps_.size(); ++i)
        if (apps_[i].title_id == id)
        {
            apps_[i].icon = texture;
            if (texture && fresh_catalog_ && i < appear_.size())
                appear_[i].snap(1.0f);
        }
}

void Screen::set_art(const std::string &id, std::uint32_t texture)
{
    for (auto &app : apps_)
        if (app.title_id == id)
            app.art = texture;
}

void Screen::set_background(const std::string &id, std::uint32_t texture)
{
    for (auto &app : apps_)
        if (app.title_id == id)
        {
            app.background = texture;
            app.ambient = false;
        }
}

void Screen::set_ambient(const std::string &id, std::uint32_t texture)
{
    for (auto &app : apps_)
        if (app.title_id == id && (app.ambient || !app.background))
        {
            app.background = texture;
            app.ambient = texture != 0;
        }
}

void Screen::set_accent(const std::string &id, Color accent)
{
    // A colour to lean toward, not to fill with: a very light one is darkened
    // until the plate under white words stays dark.
    const float light = 0.2126f * accent.r + 0.7152f * accent.g + 0.0722f * accent.b;
    if (light > 0.42f)
        accent = gfx::mix(accent, kDeep, std::min(0.7f, (light - 0.42f) * 1.6f + 0.3f));
    for (auto &app : apps_)
        if (app.title_id == id)
            app.accent = accent;
}

std::vector<std::string> Screen::artwork_backlog() const
{
    auto wanted = artwork();
    for (const auto &app : apps_)
        if (!app.local_only && !app.icon &&
            std::find(wanted.begin(), wanted.end(), app.title_id) == wanted.end())
            wanted.push_back(app.title_id);
    return wanted;
}

void Screen::set_qr(std::string id, std::uint32_t texture, int width)
{
    qr_id_ = std::move(id);
    qr_texture_ = texture;
    qr_width_ = width;
}

std::vector<std::string> Screen::artwork() const
{
    std::vector<std::string> wanted;
    const App *first = focused();
    if (first && !first->local_only)
        wanted.push_back(first->title_id);
    if (details_ || !first)
        return wanted;
    wanted.reserve(16);
    const auto add = [&](const App &app)
    {
        if (!app.local_only && wanted.size() < 15 &&
            std::find(wanted.begin(), wanted.end(), app.title_id) == wanted.end())
            wanted.push_back(app.title_id);
    };
    if (discover())
    {
        // The tiles on screen, shelf by shelf, and the shelf beyond.
        for (int k = 0; k < static_cast<int>(visible_.size()); ++k)
        {
            Rect r = card_rect(k);
            r.y -= scroll_.target;
            if (r.y + r.h > kShelvesTop && r.y < kHeight + kShelfPitch && r.x + r.w > 0.0f &&
                r.x < kWidth)
                add(apps_[visible_[static_cast<std::size_t>(k)]]);
        }
    }
    else
    {
        int begin = 0, end = 0;
        rows_in_view(scroll_.target, begin, end);
        for (int k = begin; k < end; ++k)
            add(apps_[visible_[static_cast<std::size_t>(k)]]);
    }
    if (banner_shown())
        add(apps_[featured_[static_cast<std::size_t>(banner_)]]);
    return wanted;
}

// ---- geometry ---------------------------------------------------------------

bool Screen::discover() const
{
    return section_ == 0 && query_.empty() && developer_.empty();
}
int Screen::shelf_of(int index) const
{
    for (int s = static_cast<int>(shelves_.size()) - 1; s > 0; --s)
        if (index >= shelves_[static_cast<std::size_t>(s)].start)
            return s;
    return 0;
}
// The app the stage shows: the focused tile, else the featured one.
int Screen::stage_index() const
{
    if (zone_ == Zone::grid && focused())
        return static_cast<int>(visible_[static_cast<std::size_t>(focus_)]);
    if (banner_shown())
        return static_cast<int>(featured_[static_cast<std::size_t>(banner_)]);
    return focused() ? static_cast<int>(visible_[static_cast<std::size_t>(focus_)]) : -1;
}
float Screen::window_top() const
{
    return discover() ? kShelvesTop : kGridTop - 20.0f;
}
// How visible something at screen height y is inside the tiles' window.
float Screen::fade_at(float y) const
{
    return std::min(tween::clamp01((y - window_top()) / kFadeHead),
                    tween::clamp01((kViewBottom - y) / kFadeFoot));
}
// A tile's place: on Discover in its shelf (scrolled sideways), elsewhere in
// the grid. The page scroll is taken off when it is drawn.
Rect Screen::card_rect(int index) const
{
    if (discover() && !shelves_.empty())
    {
        const int s = shelf_of(index);
        const auto &shelf = shelves_[static_cast<std::size_t>(s)];
        return {kMargin + static_cast<float>(index - shelf.start) * (kTileW + kTileGap) -
                    shelf.scroll.offset(),
                kShelfTilesY + static_cast<float>(s) * kShelfPitch, kTileW, kTileH};
    }
    return {kMargin + static_cast<float>(index % kColumns) * (kGridTileW + kTileGap),
            kGridTop + static_cast<float>(index / kColumns) * kRowPitch, kGridTileW, kGridTileH};
}
// What the focus highlight surrounds, in page coordinates.
Rect Screen::focus_target() const
{
    if (zone_ == Zone::banner)
    {
        Rect button = kStageButton.inset(-7.0f);
        button.y += scroll_.value;
        return button;
    }
    if (zone_ == Zone::chips || visible_.empty())
    {
        Rect tab = chips_[section_].inset(-4.0f);
        tab.y += scroll_.value;
        return tab;
    }
    // Around the tile as it is drawn, grown by kLift, so the frame shows on every side.
    const Rect card = card_rect(focus_);
    const float grow_x = card.w * kLift * 0.5f, grow_y = card.h * kLift * 0.5f;
    return {card.x - grow_x - 7.0f, card.y - grow_y - 7.0f, card.w + 2.0f * grow_x + 14.0f,
            card.h + 2.0f * grow_y + 14.0f};
}
float Screen::focus_radius() const
{
    if (zone_ == Zone::banner)
        return kStageButton.h * 0.5f + 7.0f;
    if (zone_ == Zone::chips || visible_.empty())
        return kTabH * 0.5f + 4.0f;
    return kTileRadius * (1.0f + kLift) + 7.0f;
}
// Discover moves a shelf at a time; the grid keeps the row above in view.
float Screen::scroll_target() const
{
    if (zone_ != Zone::grid || visible_.empty())
        return 0.0f;
    if (discover())
        return static_cast<float>(shelf_of(focus_)) * kShelfPitch;
    const int row = focus_ / kColumns;
    return row < 2 ? 0.0f : static_cast<float>(row - 1) * kRowPitch;
}

// ---- input ------------------------------------------------------------------

// The edge of something: a quiet "no" (and nothing at all for a held
// direction, which only means the player has not let go yet).
void Screen::refuse(ui::Feedback &feedback, bool repeat, float dx, float dy)
{
    if (repeat)
        return;
    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    feedback.rumble(0.25f, 0.05f);
    nudge_.trigger();
    nudge_x_ = dx;
    nudge_y_ = dy;
}

void Screen::step_section(int delta, bool repeat, ui::Feedback &feedback)
{
    int next = section_ + delta;
    if (next < 0 || next >= kSections)
    {
        // A press goes round to the other end; a held button stops at the edge.
        if (repeat)
            return refuse(feedback, repeat, static_cast<float>(delta), 0.0f);
        next = (next + kSections) % kSections;
    }
    section_ = next;
    focus_ = 0;
    developer_.clear();
    category_.clear();
    rebuild();
    swap_.start(0.4f);
    feedback.play(audio::Cue::tab, 0.92f + 0.04f * static_cast<float>(next),
                  ui::pan_for_x(chips_[next].cx()));
}

std::vector<std::string_view> Screen::categories_here() const
{
    std::vector<std::string_view> found;
    if (section_ < 1 || section_ > 4 || !query_.empty() || !developer_.empty())
        return found;
    for (const auto &[key, label] : kCategories)
        if (std::any_of(apps_.begin(), apps_.end(), [&, key = key](const App &a)
                        { return a.category == key && in_section(a, section_); }))
            found.push_back(key);
    return found;
}

void Screen::step_category(int delta, ui::Feedback &feedback)
{
    const auto here = categories_here();
    if (here.size() < 2)
        return refuse(feedback, false, static_cast<float>(delta), 0.0f);
    // All of the section first, then each category; L2 and R2 go round.
    const auto at = std::find(here.begin(), here.end(), category_);
    const int total = static_cast<int>(here.size()) + 1;
    int index = at == here.end() ? 0 : static_cast<int>(at - here.begin()) + 1;
    index = ((index + delta) % total + total) % total;
    category_ = index == 0 ? std::string() : std::string(here[static_cast<std::size_t>(index - 1)]);
    focus_ = 0;
    rebuild();
    swap_.start(0.3f);
    feedback.play(audio::Cue::select);
}

void Screen::show_banner(int slot)
{
    if (slot == banner_)
        return;
    banner_previous_ = banner_;
    banner_ = slot;
    banner_fade_.start(0.6f);
    banner_clock_ = 0.0f;
}

void Screen::update_home(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::north))
    {
        pending_search = true;
        feedback.play(audio::Cue::select);
        return;
    }
    if (input.is_pressed(Action::r3))
    {
        // "Most liked" joins the orders once the likes are known.
        sort_ = static_cast<Sort>((static_cast<unsigned>(sort_) + 1) % (social_shown() ? 4 : 3));
        focus_ = 0;
        rebuild();
        swap_.start(0.3f);
        feedback.play(audio::Cue::select);
        return;
    }
    if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
        return step_section(input.is_pressed(Action::page_next) ? 1 : -1, false, feedback);
    if (!discover() && (input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev)))
        return step_category(input.is_pressed(Action::jump_next) ? 1 : -1, feedback);
    if (section_ == 6 && input.is_pressed(Action::west) && !visible_.empty())
    {
        // Update all: every update on this shelf, in the order shown.
        if (!installer_ || !guard_)
            return refuse(feedback, false, 0.0f, 1.0f);
        update_all_.clear();
        for (const auto index : visible_)
            update_all_.push_back(apps_[index].title_id);
        toasts_.push(ui::StatusKind::info,
                     "Updating " + std::to_string(update_all_.size()) +
                         (update_all_.size() == 1 ? " app" : " apps"),
                     "Apps that are running are skipped.", 6.0f);
        feedback.play(audio::Cue::select);
        return;
    }

    const int count = static_cast<int>(visible_.size());
    const int featured = static_cast<int>(featured_.size());
    if (zone_ == Zone::grid && count > 0)
    {
        const int before = focus_;
        if (discover() && !shelves_.empty())
        {
            // Left and right along a shelf; up and down to the place each
            // shelf remembers, and from the first shelf up to the stage.
            const int s = shelf_of(focus_);
            const auto &shelf = shelves_[static_cast<std::size_t>(s)];
            const int pos = focus_ - shelf.start;
            switch (input.nav)
            {
            case Direction::left:
                if (pos == 0)
                    refuse(feedback, input.nav_repeat, -1.0f, 0.0f);
                else
                    --focus_;
                break;
            case Direction::right:
                if (pos + 1 >= shelf.count)
                    refuse(feedback, input.nav_repeat, 1.0f, 0.0f);
                else
                    ++focus_;
                break;
            case Direction::up:
                if (s == 0)
                {
                    zone_ = Zone::banner;
                    feedback.play(audio::Cue::focus, 1.12f);
                }
                else
                {
                    const auto &above = shelves_[static_cast<std::size_t>(s - 1)];
                    focus_ = above.start + std::min(above.pos, above.count - 1);
                }
                break;
            case Direction::down:
                if (s + 1 >= static_cast<int>(shelves_.size()))
                    refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
                else
                {
                    const auto &below = shelves_[static_cast<std::size_t>(s + 1)];
                    focus_ = below.start + std::min(below.pos, below.count - 1);
                }
                break;
            case Direction::none:
                break;
            }
            if (focus_ != before)
            {
                auto &now = shelves_[static_cast<std::size_t>(shelf_of(focus_))];
                now.pos = focus_ - now.start;
                // Shelves further down sound a little lower.
                feedback.play(audio::Cue::focus,
                              std::max(0.82f, 1.06f - 0.04f * static_cast<float>(shelf_of(focus_))),
                              ui::pan_for_x(card_rect(focus_).cx()));
            }
        }
        else
        {
            const int column = focus_ % kColumns, row = focus_ / kColumns;
            switch (input.nav)
            {
            case Direction::left:
                if (column == 0)
                    refuse(feedback, input.nav_repeat, -1.0f, 0.0f);
                else
                    --focus_;
                break;
            case Direction::right:
                if (column == kColumns - 1 || focus_ + 1 >= count)
                    refuse(feedback, input.nav_repeat, 1.0f, 0.0f);
                else
                    ++focus_;
                break;
            case Direction::up:
                if (row == 0)
                {
                    zone_ = Zone::chips;
                    feedback.play(audio::Cue::focus, 1.1f);
                }
                else
                    focus_ -= kColumns;
                break;
            case Direction::down:
                if (focus_ + kColumns < count)
                    focus_ += kColumns;
                else if ((count - 1) / kColumns > row)
                    focus_ = count - 1; // a short last row: land on its last card
                else
                    refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
                break;
            case Direction::none:
                break;
            }
            if (focus_ != before) // rows further down sound a little lower
                feedback.play(
                    audio::Cue::focus,
                    std::max(0.82f, 1.06f - 0.04f * static_cast<float>(focus_ / kColumns)),
                    ui::pan_for_x(card_rect(focus_).cx()));
        }
    }
    else if (zone_ == Zone::banner)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            // The featured apps are a loop: no ends to refuse at.
            const int step = input.nav == Direction::right ? 1 : -1;
            show_banner((banner_ + step + featured) % featured);
            feedback.play(audio::Cue::slider, 1.0f + 0.04f * static_cast<float>(banner_));
        }
        else if (input.nav == Direction::up)
        {
            zone_ = Zone::chips;
            feedback.play(audio::Cue::focus, 1.16f);
        }
        else if (input.nav == Direction::down)
        {
            if (count == 0)
                refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
            else
            {
                zone_ = Zone::grid;
                feedback.play(audio::Cue::focus);
            }
        }
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
        step_section(input.nav == Direction::right ? 1 : -1, input.nav_repeat, feedback);
    else if (input.nav == Direction::up)
        refuse(feedback, input.nav_repeat, 0.0f, -1.0f);
    else if (input.nav == Direction::down)
    {
        if (banner_shown())
        {
            zone_ = Zone::banner;
            feedback.play(audio::Cue::focus, 1.1f);
        }
        else if (count == 0)
            refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
        else
        {
            zone_ = Zone::grid;
            feedback.play(audio::Cue::focus);
        }
    }

    if (input.is_pressed(Action::confirm))
    {
        if (zone_ == Zone::banner)
        {
            // The featured app opens where it stands on its shelf.
            const auto wanted = featured_[static_cast<std::size_t>(banner_)];
            const auto found = std::find(visible_.begin(), visible_.end(), wanted);
            if (found == visible_.end())
                return refuse(feedback, false, 0.0f, 1.0f);
            focus_ = static_cast<int>(found - visible_.begin());
            if (discover())
            {
                auto &shelf = shelves_[static_cast<std::size_t>(shelf_of(focus_))];
                shelf.pos = focus_ - shelf.start;
            }
            zone_ = Zone::grid;
        }
        if (zone_ == Zone::grid && count > 0)
        {
            details_ = true;
            press_.trigger();
            pending_detail = focused()->local_only ? std::string{} : focused()->title_id;
            refresh_detail();
            feedback.play(audio::Cue::open);
        }
        else if (count > 0)
        {
            zone_ = discover() && banner_shown() ? Zone::banner : Zone::grid;
            feedback.play(audio::Cue::select);
        }
        else
            refuse(feedback, false, 0.0f, 1.0f);
    }
    else if (input.is_pressed(Action::back))
    {
        // Back has one step to take first: from deep in the page to its top.
        if (zone_ == Zone::grid && discover() && shelf_of(focus_) > 0)
            focus_ = shelves_.front().start + shelves_.front().pos;
        else if (zone_ == Zone::grid && !discover() && focus_ >= kColumns)
            focus_ %= kColumns;
        else if (!developer_.empty())
            close_developer();
        else
            return; // At the top a tap does nothing: holding Circle closes the store.
        feedback.play(audio::Cue::back);
    }
}

void Screen::update_page(const InputFrame &input, ui::Feedback &feedback)
{
    const App &shown = *focused();
    const Offer state = offer(shown);
    // Uninstalling: Cross when it is the page's action, else Square under it.
    const bool by_cross = state.primary == Order::Kind::uninstall && state.armed && !state.busy;
    const bool by_square = !by_cross && state.uninstall && installer_ && guard_ && !state.busy;
    if (by_cross || by_square)
    {
        hold_.style.action = by_cross ? Action::confirm : Action::west;
        hold_.style.glyph = by_cross ? ui::Button::cross : ui::Button::square;
        if (hold_.handle(input, feedback) == ui::Event::activated)
        {
            press_.trigger();
            order(shown, Order::Kind::uninstall);
            return;
        }
        if (input.is_pressed(hold_.style.action))
            return; // the hold has begun, or a tap shows "Hold to uninstall"
    }
    if (input.is_pressed(Action::confirm) && state.primary != Order::Kind::none)
    {
        if (!state.armed)
        {
            // The line under the button already says why.
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            feedback.rumble(0.25f, 0.05f);
            nudge_.trigger();
        }
        else if (state.primary == Order::Kind::adopt)
        {
            ask_ = Ask::adopt;
            ask_id_ = shown.title_id;
            dialog_.open({ui::StatusKind::question,
                          "Manage " + shown.name + " with ProsperoStore?",
                          "Nothing is changed now. From then on its updates are offered here. An "
                          "update replaces the app's whole folder; the previous one, with "
                          "anything you added inside it, is kept in the store's \"previous\" "
                          "folder until the next update.",
                          {{"Cancel"}, {"Manage", ui::ButtonKind::primary}}},
                         feedback);
        }
        else
        {
            press_.trigger();
            feedback.play(audio::Cue::select);
            order(shown, state.primary);
        }
    }
    else if (input.is_pressed(Action::back))
    {
        details_ = false;
        feedback.play(audio::Cue::back);
    }
    else if (input.is_pressed(Action::north))
    {
        auto &app = apps_[visible_[static_cast<std::size_t>(focus_)]];
        app.detail_error.clear();
        pending_detail = app.local_only ? std::string{} : app.title_id;
        refresh_detail();
        feedback.play(audio::Cue::select);
    }
    else if (input.is_pressed(Action::r3) && !shown.local_only && !shown.author.empty())
    {
        const std::string author = shown.author;
        open_developer(author);
        feedback.play(audio::Cue::select);
    }
    else if (comments_block_ >= 0 &&
             (input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev)))
    {
        // R2 goes straight to the comments, however long the release notes are; R2 again, or
        // L2, goes back to the top. Having scrolled there by hand counts as being there.
        const float limit = article_.scroll_limit();
        const float place = std::min(article_.block_offset(comments_block_), limit);
        const bool there = at_comments_ || (place > 0.5f && article_.scroll() >= place - 1.0f);
        if (there || input.is_pressed(Action::jump_prev))
        {
            article_.scroll_to(0.0f);
            at_comments_ = false;
        }
        else
        {
            article_.scroll_to_block(comments_block_);
            at_comments_ = true;
        }
        feedback.play(audio::Cue::select);
    }
    else if (input.is_pressed(Action::touch) && !discussion_of(shown).empty())
    {
        // Like or comment: on GitHub, from a phone. The store only shows the way there.
        discussion_link_ = discussion_of(shown);
        discussion_name_ = shown.name;
        discussion_ = true;
        if (discussion_link_ != discussion_qr_link_)
            pending_discussion_link = discussion_link_;
        feedback.play(audio::Cue::open);
    }
    else
    {
        article_.handle(input, feedback);
        if (article_.scroll() <= 0.5f)
            at_comments_ = false; // back at the top by hand
    }
}

bool Screen::open_developer(const std::string &author)
{
    if (author.empty())
        return false;
    // Their link comes from whichever of their apps has its details here: the page this was
    // opened from, first.
    const std::string from = focused() ? focused()->title_id : std::string();
    developer_link_.clear();
    developer_support_ = false;
    for (int pass = 0; pass < 2 && developer_link_.empty(); ++pass)
        for (const auto &app : apps_)
            if (app.author == author && app.detail && (pass == 1 || app.title_id == from))
            {
                developer_link_ = catalog::developer_link(*app.detail, developer_support_);
                if (!developer_link_.empty())
                    break;
            }
    developer_ = author;
    details_ = false;
    section_ = 0;
    query_.clear();
    focus_ = 0;
    rebuild();
    // The page closes back into the tile it was opened from.
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (apps_[visible_[k]].title_id == from)
            focus_ = static_cast<int>(k);
    if (!visible_.empty())
        zone_ = Zone::grid;
    if (!developer_link_.empty() && developer_link_ != developer_qr_link_)
        pending_developer_link = developer_link_;
    swap_.start(0.3f);
    return !visible_.empty();
}

void Screen::close_developer()
{
    developer_.clear();
    focus_ = 0;
    rebuild();
    swap_.start(0.3f);
}

void Screen::update(const InputFrame &raw, float dt, ui::Feedback &feedback)
{
    time_ += dt;
    // Where this frame's input lands, before it changes anything: the home page, idle.
    const bool at_home = !panel_ && !details_ && !dialog_.is_open() && !notes_open_ &&
                         !intro_showing() && activity_.id.empty() && activity_.waiting.empty();
    // The intro stays at least a moment, and until the catalog has answered (six
    // seconds at most: the home page has its own loading state). Input waits for it.
    if (intro_on_ && intro_leave_ < 1.0f)
    {
        intro_clock_ += dt;
        const bool ready = !loading_ || !apps_.empty();
        const float least = settings_.reduce_motion ? 0.5f : 1.6f;
        if (!intro_leaving_ && ((ready && intro_clock_ >= least) || intro_clock_ >= 6.0f))
            intro_leaving_ = true;
        if (intro_leaving_)
            intro_leave_ = std::min(1.0f, intro_leave_ + dt / 0.75f);
    }
    const InputFrame quiet{};
    const InputFrame &input = intro_on_ && intro_leave_ < 0.5f ? quiet : raw;
    // A scripted install presses the button as soon as it can be pressed.
    if (!auto_order_.empty() && details_ && focused() && focused()->title_id == auto_order_)
    {
        const Offer state = offer(*focused());
        if (state.armed && !state.busy && state.primary == Order::Kind::install)
        {
            order(*focused(), Order::Kind::install);
            auto_order_.clear();
        }
    }
    fresh_catalog_ = false;
    // "Update all" hands the installer one app at a time: each needs its
    // verified details first, and one that can't be updated now is passed over.
    if (!update_all_.empty() && pending_order.kind == Order::Kind::none)
    {
        const auto &id = update_all_.front();
        const auto found = std::find_if(apps_.begin(), apps_.end(),
                                        [&](const auto &app) { return app.title_id == id; });
        if (found == apps_.end() || found->badge != "Update" || !found->detail_error.empty())
            update_all_.erase(update_all_.begin());
        else if (!found->detail)
        {
            if (update_asked_ != id && pending_detail.empty())
                pending_detail = update_asked_ = id;
        }
        else
        {
            const Offer state = offer(*found);
            if (state.armed && !state.busy && state.primary == Order::Kind::install)
                order(*found, Order::Kind::install);
            update_all_.erase(update_all_.begin());
        }
    }
    // The offer waits for a quiet screen and, a few seconds at most, for the release notes.
    if (store_offer_ == StoreOffer::waiting && !intro_showing() && !dialog_.is_open() && !panel_ &&
        !notes_open_)
    {
        store_offer_wait_ += dt;
        const auto self = std::find_if(apps_.begin(), apps_.end(),
                                       [&](const auto &app) { return app.title_id == self_id_; });
        const bool wanted = self != apps_.end() && !self->detail && self->detail_error.empty() &&
                            store_offer_wait_ < 8.0f;
        if (!wanted)
            open_store_offer(feedback);
        else if (!store_offer_detail_asked_ && pending_detail.empty())
        {
            pending_detail = self_id_;
            store_offer_detail_asked_ = true;
        }
    }
    // "Update now": what Cross does on the store's own page, once its verified details
    // are here. If that can't be, the page's own words say why.
    if (store_update_wanted_ && pending_order.kind == Order::Kind::none)
    {
        store_update_wait_ += dt;
        const auto self = std::find_if(apps_.begin(), apps_.end(),
                                       [&](const auto &app) { return app.title_id == self_id_; });
        const bool waiting = self != apps_.end() && !self->detail && self->detail_error.empty() &&
                             store_update_wait_ < 10.0f;
        if (waiting)
        {
            if (pending_detail.empty())
                pending_detail = self_id_;
        }
        else
        {
            store_update_wanted_ = false;
            const Offer state = self != apps_.end() ? offer(*self) : Offer{};
            if (self != apps_.end() && state.armed && !state.busy &&
                state.primary == Order::Kind::install)
                order(*self, Order::Kind::install);
            else
                notify("The update couldn't start",
                       state.reason.empty() ? "Open ProsperoStore's page in About to update it."
                                            : state.reason);
        }
    }
    notes_.set_active(notes_open_);
    notes_.update(dt);
    if (notes_open_)
    {
        // The notes take every input: Cross updates, Circle goes back to the question.
        if (input.is_pressed(Action::confirm))
        {
            notes_open_ = false;
            feedback.play(audio::Cue::select);
            start_store_update();
        }
        else if (input.is_pressed(Action::back))
        {
            notes_open_ = false;
            open_store_offer(feedback);
        }
        else
            notes_.handle(input, feedback);
    }
    else if (dialog_.is_open() && ask_ == Ask::store_update)
    {
        if (dialog_.handle(input, feedback) == ui::Event::activated)
        {
            const int choice = dialog_.choice();
            if (choice == (store_offer_notes_ ? 2 : 1))
                start_store_update();
            else if (store_offer_notes_ && choice == 1)
                notes_open_ = true;
            else
                store_offer_ = StoreOffer::done;
        }
    }
    else if (dialog_.is_open())
    {
        // The question takes every input until it is answered.
        if (dialog_.handle(input, feedback) == ui::Event::activated && dialog_.choice() == 1)
        {
            if (ask_ == Ask::quit)
                quit_ = true;
            else if (ask_ == Ask::disable_signatures)
            {
                settings_.verify_signatures = false;
                settings_changed = true;
                notify("Catalog settings saved",
                       "Signatures will not be checked after reopening the store.");
            }
            else
                for (const auto &app : apps_)
                    if (app.title_id == ask_id_ && !app.installed.empty())
                        order(app, Order::Kind::adopt);
        }
    }
    else if (discussion_)
    {
        // The code takes every input until it is closed.
        if (!details_ || input.is_pressed(Action::back) || input.is_pressed(Action::confirm) ||
            input.is_pressed(Action::touch))
        {
            discussion_ = false;
            feedback.play(audio::Cue::back);
        }
    }
    else if (panel_)
        update_panel(input, feedback);
    else if (input.is_pressed(Action::menu) ||
             (input.is_pressed(Action::west) && !details_ && section_ != 6))
    {
        // Options opens the settings; Square, the queue.
        open_panel(input.is_pressed(Action::menu) ? 1 : 0);
        feedback.play(audio::Cue::open);
    }
    else if (details_ && focused())
        update_page(input, feedback);
    else
    {
        details_ = false;
        update_home(input, feedback);
    }
    // Circle held on the home page closes the store. Not while something is being
    // installed: that is cancelled or left to finish first.
    {
        const bool home = at_home && !panel_ && !details_ && !dialog_.is_open();
        if (!home || !input.is_held(Action::back))
            close_armed_ = false;
        else if (input.is_pressed(Action::back))
            close_armed_ = true;
        close_hold_ = close_armed_ ? close_hold_ + dt : 0.0f;
        if (close_hold_ >= kCloseDelay + kCloseHold && !quit_)
        {
            quit_ = true;
            feedback.play(audio::Cue::back);
        }
    }
    panel_value_.target = panel_ ? 1.0f : 0.0f;
    panel_value_.update(dt, 14.0f);
    discussion_ = discussion_ && details_;
    discussion_value_.target = discussion_ ? 1.0f : 0.0f;
    discussion_value_.update(dt, 16.0f);
    about_.set_active(panel_ && panel_tab_ == 2);
    about_.update(dt);
    news_view_.set_active(panel_ && panel_tab_ == 3);
    news_view_.update(dt);

    // The featured app moves on by itself while the stage shows it.
    const bool home = !details_ && banner_shown();
    if (home && zone_ != Zone::grid && featured_.size() > 1)
    {
        banner_clock_ += dt;
        if (banner_clock_ >= kBannerSeconds)
            show_banner((banner_ + 1) % static_cast<int>(featured_.size()));
    }
    banner_fade_.update(dt);
    swap_.update(dt);
    // The stage, and the key art behind everything, follow the focus; the
    // whole screen leans toward the focused app's colour.
    if (const int stage = stage_index(); stage != stage_)
    {
        stage_previous_ = stage_;
        stage_ = stage;
        stage_fade_.start(0.55f);
    }
    stage_fade_.update(dt);
    tint_.target(stage_ >= 0 ? apps_[static_cast<std::size_t>(stage_)].accent : kMid);
    tint_.update(dt, 3.0f);
    art_clock_ += dt * motion(); // the field's drift stops when motion is reduced
    for (auto &shelf : shelves_)
    {
        // A shelf keeps its focused tile in its first four places.
        const float pitch = kTileW + kTileGap;
        const float limit = std::max(0.0f, static_cast<float>(shelf.count) * pitch - kTileGap -
                                               (kWidth - 2.0f * kMargin));
        shelf.scroll.position.target =
            std::clamp(static_cast<float>(shelf.pos - 3) * pitch, 0.0f, limit);
        shelf.scroll.update(dt, 14.0f);
    }
    banner_focus_.target = zone_ == Zone::banner ? 1.0f : 0.0f;
    banner_focus_.update(dt, 16.0f);
    scroll_.target = scroll_target();
    scroll_.update(dt, 11.0f);
    page_.target = details_ ? 1.0f : 0.0f;
    page_.update(dt, 11.0f);
    plate_.target = zone_ == Zone::grid && !visible_.empty() ? 1.0f : 0.0f;
    plate_.update(dt, 18.0f);
    ring_.target(focus_target());
    ring_.update(dt, 20.0f);
    ring_radius_.target = focus_radius();
    ring_radius_.update(dt, 20.0f);
    chip_pill_.target(chips_[section_]);
    chip_pill_.update(dt, 18.0f);
    nudge_.update(dt, 9.0f);
    press_.update(dt, 7.0f);
    for (std::size_t k = 0; k < lift_.size(); ++k)
    {
        lift_[k].target = zone_ == Zone::grid && static_cast<int>(k) == focus_ ? 1.0f : 0.0f;
        if (!lift_[k].settled())
            lift_[k].update(dt, 16.0f);
    }
    for (std::size_t i = 0; i < appear_.size(); ++i)
    {
        // A picture fades in over the plate that stood in for it.
        appear_[i].target = art(apps_[i]) ? 1.0f : 0.0f;
        if (!appear_[i].settled())
            appear_[i].update(dt, 9.0f);
    }
    article_.set_active(details_);
    article_.update(dt);
    dialog_.update(dt);
    hold_.update(dt);
    if (chime_)
    {
        // The toast of a job that went well plays the completion sound.
        const auto notify = toasts_.style.sounds.notify;
        toasts_.style.sounds.notify = audio::Cue::complete;
        toasts_.update(dt, feedback);
        toasts_.style.sounds.notify = notify;
        chime_ = false;
    }
    else
        toasts_.update(dt, feedback);
}

// ---- drawing: the home page ---------------------------------------------------

// Fitting and measuring allocate: once per catalog, not per frame.
void Screen::layout(const ui::Fonts &fonts)
{
    if (fits_stale_)
    {
        fits_.assign(apps_.size(), {});
        fits_stale_ = false;
    }
    // The sections are tabs in the top bar, after the store's name.
    float x = 664.0f;
    for (int i = 0; i < kSections; ++i)
    {
        const float w = fonts.semibold.measure(kSectionNames[i], 22) + 36.0f;
        chips_[i] = {x, kTopY - kTabH * 0.5f, w, kTabH};
        x += w + 4.0f;
    }
    if (!placed_)
    {
        // The first frame: the highlights start where they belong, not at the origin.
        chip_pill_.snap(chips_[section_]);
        ring_.snap(focus_target());
        ring_radius_.snap(focus_radius());
        tint_.snap(kMid);
        placed_ = true;
    }
}

// The app the installer is working on, for the top bar and the job card.
const App *Screen::busy_app(float &progress, const char *&phase) const
{
    if (activity_.id.empty())
        return nullptr;
    for (const auto &app : apps_)
        if (app.title_id == activity_.id)
        {
            using Phase = install::Phase;
            const auto now = static_cast<Phase>(activity_.phase);
            phase = now == Phase::downloading ? "Downloading"
                    : now == Phase::verifying ? "Verifying"
                    : now == Phase::unpacking ? "Unpacking"
                    : now == Phase::removing  ? "Removing"
                                              : "Installing";
            const bool measured =
                (now == Phase::downloading || now == Phase::unpacking) && activity_.total > 0;
            progress = measured ? tween::clamp01(static_cast<float>(activity_.done) /
                                                 static_cast<float>(activity_.total))
                                : -1.0f;
            return &app;
        }
    return nullptr;
}

// One app's key art over a rectangle, drifting slowly. False when it has none.
bool Screen::draw_art(gfx::DrawList &list, int index, const Rect &rect, float alpha,
                      float radius) const
{
    if (index < 0 || alpha <= 0.004f)
        return index >= 0 && apps_[static_cast<std::size_t>(index)].background != 0;
    const App &app = apps_[static_cast<std::size_t>(index)];
    if (!app.background)
        return false;
    // A full-screen picture keeps 16:9; the window into it drifts.
    list.image(app.background, rect, drift(art_clock_), kWhite.with_alpha(alpha), radius);
    return true;
}

void Screen::draw_top_bar(const ui::Fonts &fonts)
{
    auto &list = scene_;
    // A soft band of Farlight's dark, so the bar reads over any key art.
    list.gradient_rect({0, 0, kWidth, 200.0f}, 0, kDeep.with_alpha(0.7f), kDeep.with_alpha(0.0f));
    list.rotated_rect({kMargin + 2.0f, kTopY - 11.0f, 22.0f, 22.0f}, 5.0f, 0.7854f, kAccent);
    list.rotated_rect({kMargin + 8.0f, kTopY - 5.0f, 10.0f, 10.0f}, 2.0f, 0.7854f, kDeep);
    const float brand = ui::text(list, fonts.semibold, "PROSPEROSTORE", kMargin + 42.0f,
                                 centred(kTopY, 22), 22, kInk, gfx::Align::left, 5.0f);
    // Where the apps come from, said once and quietly beside the name.
    const float x = kMargin + 42.0f + brand + 20.0f;
    list.rounded_rect({x, kTopY - 11.0f, 1.5f, 22.0f}, 0, kInk.with_alpha(0.25f));
    ui::text(list, fonts.semibold,
             active_catalog_url_ == catalog::kDefaultApi ? "prosperostore.net"
             : active_with_official_                     ? "Two catalogs"
                                                         : "Custom catalog",
             x + 20.0f, centred(kTopY, 20), 20, kAccent.with_alpha(0.92f));

    // The sections: words, the active one lit, one gold line gliding under it.
    const auto glyphs = ui::GlyphStyle::dark();
    ui::draw_button(list, fonts, glyphs, ui::Button::l1,
                    chips_[0].x - ui::button_width(ui::Button::l1, 26) - 8.0f, kTopY, 26);
    const Rect under = chip_pill_.value();
    for (int i = 0; i < kSections; ++i)
    {
        const Rect &tab = chips_[i];
        const float overlap = std::min(under.x + under.w, tab.x + tab.w) - std::max(under.x, tab.x);
        const float on = tween::clamp01(overlap / tab.w);
        ui::text(list, fonts.semibold, kSectionNames[i], tab.cx(), centred(kTopY, 22), 22,
                 kInk.with_alpha(0.52f + 0.48f * on), gfx::Align::center);
    }
    list.rounded_rect({under.x + 18.0f, kTopY + 21.0f, under.w - 36.0f, 3.0f}, 1.5f, kAccent);
    const Rect &last = chips_[kSections - 1];
    ui::draw_button(list, fonts, glyphs, ui::Button::r1, last.x + last.w + 8.0f, kTopY, 26);
    // The right end only has the room R1 leaves: its words are fitted into it, never over it.
    const float room =
        kRight - (last.x + last.w + 8.0f + ui::button_width(ui::Button::r1, 26) + 28.0f);

    // The right end: what the installer is doing, or the catalog's state.
    float progress = -1.0f;
    const char *phase = "";
    if (const App *busy = busy_app(progress, phase))
    {
        std::string line = progress >= 0.0f
                               ? std::to_string(static_cast<int>(progress * 100.0f)) + "%"
                               : std::string(phase);
        line = fonts.semibold.font->fit(line, 20, std::max(0.0f, room - 40.0f));
        const float width = ui::text(list, fonts.semibold, line, kRight, centred(kTopY, 20), 20,
                                     kInk, gfx::Align::right);
        const float cx = kRight - width - 24.0f;
        list.ring(cx, kTopY, 11.0f, 3.0f, kInk.with_alpha(0.16f));
        if (progress >= 0.0f)
            list.arc(cx, kTopY, 11.0f, 3.0f, -1.5708f, 6.2832f * progress, kAccent);
        else
            list.arc(cx, kTopY, 11.0f, 3.0f, time_ * 4.0f, 1.9f, kAccent);
        (void)busy;
    }
    else if (room > 60.0f)
    {
        // A status too long for the room keeps its first part whole ("Catalog verified")
        // rather than ending in a cut word.
        const float width = std::min(280.0f, room);
        std::string line = status_;
        if (fonts.regular.measure(line, 20) > width)
            if (const auto dot = line.find(" \xE2\x80\xA2 "); dot != std::string::npos)
                line.resize(dot);
        ui::text(list, fonts.regular, fonts.regular.font->fit(line, 20, width), kRight,
                 centred(kTopY, 20), 20, kInk.with_alpha(0.55f), gfx::Align::right);
    }
}

// Discover's stage: the app in focus (or the featured one), big, over its own
// key art. An app without key art shows its icon large on the right instead.
void Screen::draw_stage(const ui::Fonts &fonts)
{
    auto &list = scene_;
    const Rect full{0, 0, kWidth, kHeight};
    const float fade = stage_fade_.running ? tween::smoothstep(stage_fade_.progress()) : 1.0f;
    const bool now_art = stage_ >= 0 && apps_[static_cast<std::size_t>(stage_)].background;
    if (stage_previous_ >= 0 && fade < 1.0f)
        draw_art(list, stage_previous_, full, now_art ? 1.0f : 1.0f - fade, 0);
    draw_art(list, stage_, full, stage_previous_ >= 0 ? fade : 1.0f, 0);
    const auto icon_stage = [&](int index, float alpha)
    {
        if (index < 0 || alpha <= 0.01f)
            return;
        const App &app = apps_[static_cast<std::size_t>(index)];
        if (app.background && !app.ambient)
            return;
        // The icon is the picture: large, floating a little over its own colours.
        const float bob = 7.0f * motion() * std::sin(time_ * 0.9f);
        const Rect cover{1252.0f, 240.0f + bob, 352.0f, 352.0f};
        list.glow(cover.inset(-10.0f), 90, 230,
                  gfx::mix(app.accent, kInk, 0.18f).with_alpha(0.4f * alpha));
        list.shadow({cover.x + 60.0f, 606.0f, cover.w - 120.0f, 18.0f}, 9, 30.0f - bob,
                    kBlack.with_alpha(0.55f * alpha));
        list.shadow({cover.x, cover.y + 30.0f, cover.w, cover.h}, 40, 70,
                    kBlack.with_alpha(0.5f * alpha));
        if (const auto texture = art(app, true))
            list.image(texture, cover, gfx::kFullUv, kWhite.with_alpha(alpha), 78);
        // A sheen across its top: an object under light, not a sticker.
        list.gradient_rect({cover.x, cover.y, cover.w, cover.h * 0.46f}, 78,
                           kWhite.with_alpha(0.1f * alpha), kWhite.with_alpha(0.0f));
        list.bordered_rect(cover, 78, kClear, 1.5f, kInk.with_alpha(0.2f * alpha));
    };
    if (stage_previous_ >= 0 && fade < 1.0f)
        icon_stage(stage_previous_, 1.0f - fade);
    icon_stage(stage_, stage_previous_ >= 0 ? fade : 1.0f);

    // The words sit on Farlight's dark: from the left, and under the shelves.
    list.gradient_rect_h({0, 0, 1480.0f, kHeight}, 0, kDeep.with_alpha(0.93f),
                         kDeep.with_alpha(0.0f));
    list.gradient_rect({0, 380.0f, kWidth, 390.0f}, 0, kDeep.with_alpha(0.0f),
                       kDeep.with_alpha(0.95f));
    list.rounded_rect({0, 770.0f, kWidth, kHeight - 770.0f}, 0, kDeep.with_alpha(0.95f));

    // The words: the old ones leave quickly to the left, the new arrive a beat later.
    const auto words = [&](int index, float alpha, float dx)
    {
        if (index < 0 || alpha <= 0.01f)
            return;
        const App &app = apps_[static_cast<std::size_t>(index)];
        const bool featured_now =
            zone_ != Zone::grid && !featured_.empty() &&
            featured_[static_cast<std::size_t>(banner_)] == static_cast<std::size_t>(index);
        const std::string kicker =
            app.badge == "Update"                ? "UPDATE AVAILABLE"
            : app.badge == "Installed"           ? "ON THIS CONSOLE"
            : app.catalog_badge == "Coming soon" ? "COMING SOON"
            : featured_now && banner_ == 0       ? "NEW RELEASE"
            : featured_now                       ? "FEATURED"
            : !app.category.empty()              ? ui::upper(category_label(app.category))
                                    : ui::upper(app.kind.empty() ? std::string("app") : app.kind);
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0, 0, dx, 0);
        list.rounded_rect({kMargin, kStageKickerY - 9.0f, 28.0f, 3.0f}, 1.5f, kAccent);
        ui::text(list, fonts.semibold, kicker, kMargin + 40.0f, kStageKickerY, 18, kAccent,
                 gfx::Align::left, 4.0f);
        const ui::FontRef &headline = face_for(app.name, fonts.display, fonts.semibold);
        ui::text(list, headline, headline.font->fit(app.name, 92, 1180.0f), kMargin - 4.0f,
                 kStageTitleY, 92, kInk);
        std::string meta = app.author;
        const auto add = [&](const std::string &part)
        {
            if (!part.empty())
                meta += (meta.empty() ? "" : "  \xC2\xB7  ") + part;
        };
        if (!app.category.empty())
            add(category_label(app.category));
        else if (!app.kind.empty())
        {
            std::string kind = app.kind;
            kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
            add(kind);
        }
        add(app.version);
        if (app.size)
            add(size_text(app.size));
        ui::text(list, fonts.regular, fonts.regular.font->fit(meta, 26, 1180.0f), kMargin,
                 kStageMetaY, 26, kInk.with_alpha(0.8f));
        if (!app.description.empty())
            ui::paragraph(list, fonts.regular, app.description, kMargin, kStageTextY, 26,
                          kStageTextW, 38, kInk.with_alpha(0.84f), 2);
        list.pop_transform();
        list.pop_opacity();
    };
    if (stage_previous_ >= 0 && fade < 1.0f)
        words(stage_previous_, tween::clamp01(1.0f - fade * 2.2f), -26.0f * motion() * fade);
    words(stage_, stage_previous_ >= 0 ? tween::smoothstep((fade - 0.25f) / 0.75f) : 1.0f,
          24.0f * motion() * (1.0f - fade));

    // The call to action lights up when the stage holds the focus; beside
    // it, what the app is on this console.
    if (stage_ < 0)
        return; // nothing on the stage yet: no call to action
    const float lit = banner_focus_.value;
    const Rect b = kStageButton;
    list.glow(b, b.h * 0.5f, 26, kAccent.with_alpha((0.22f + 0.08f * breath()) * lit));
    list.bordered_rect(b, b.h * 0.5f, gfx::mix(kInk.with_alpha(0.1f), kAccent, lit), 1.5f,
                       gfx::mix(kInk.with_alpha(0.34f), kAccent, lit));
    const auto glyphs = lit > 0.5f ? ui::GlyphStyle::light() : ui::GlyphStyle::dark();
    ui::draw_button(list, fonts, glyphs, ui::Button::cross, b.x + 22.0f, b.cy(), 32);
    ui::text(list, fonts.semibold, "View details", b.x + 70.0f, centred(b.cy(), 24), 24,
             gfx::mix(kInk, kOnAccent, lit));
    if (stage_ >= 0)
    {
        const App &app = apps_[static_cast<std::size_t>(stage_)];
        const float x = b.x + b.w + 24.0f, cy = b.cy();
        if (app.badge == "Installed")
        {
            draw_check(list, x + 12.0f, cy, 22.0f, kOwned);
            ui::text(list, fonts.semibold, "Installed", x + 34.0f, centred(cy, 22), 22, kOwned);
        }
        else if (app.badge == "Update")
            pill(list, fonts, "Update ready", x, cy, 40.0f, kAccent, kOnAccent, 1.0f, kAllLayers);
        else if (app.catalog_badge == "Coming soon")
            pill(list, fonts, "Coming soon", x, cy, 40.0f, kInk.with_alpha(0.14f), kInk, 1.0f,
                 kAllLayers);
    }
    // The featured apps: dots, the current one a bar that fills as its seconds pass.
    const float dots = 1.0f - plate_.value;
    const int count = static_cast<int>(featured_.size());
    if (dots > 0.01f && count > 1)
    {
        list.push_opacity(dots);
        float x = kMargin + 4.0f;
        const float cy = b.y + b.h + 42.0f;
        for (int i = 0; i < count; ++i)
        {
            if (i == banner_)
            {
                list.rounded_rect({x, cy - 4.0f, 48.0f, 8.0f}, 4, kInk.with_alpha(0.28f));
                list.rounded_rect({x, cy - 4.0f,
                                   8.0f + 40.0f * tween::clamp01(banner_clock_ / kBannerSeconds),
                                   8.0f},
                                  4, kInk);
                x += 60.0f;
            }
            else
            {
                list.circle(x + 4.0f, cy, 4.0f, kInk.with_alpha(0.42f));
                x += 20.0f;
            }
        }
        list.pop_opacity();
    }
}

// The other sections: a big title, its count and the order, over the
// focused app's key art far back.
void Screen::draw_section_header(const ui::Fonts &fonts)
{
    auto &list = scene_;
    std::string title = !developer_.empty() ? "By " + developer_
                        : query_.empty()    ? std::string(kSectionNames[section_])
                                            : "Results for \"" + query_ + "\"";
    if (!category_.empty() && !categories_here().empty())
        title += " \xC2\xB7 " + category_label(category_);
    const float w = ui::text(list, fonts.display, fonts.display.font->fit(title, 60, 1100.0f),
                             kMargin - 3.0f, kHeaderY, 60, kInk);
    ui::text(list, fonts.mono, std::to_string(visible_.size()), kMargin + w + 18.0f, kHeaderY, 26,
             kInk.with_alpha(0.5f));
    const bool all = section_ == 6 && !visible_.empty() && installer_ && guard_;
    if (!developer_.empty())
    {
        // The developer's code, where the sorting is said on other pages.
        if (developer_qr_ != 0 && !developer_link_.empty() && developer_qr_link_ == developer_link_)
        {
            const Rect code{kRight - 84.0f, kHeaderY - 62.0f, 84.0f, 84.0f};
            list.rounded_rect(code.inset(-6.0f), 12, kWhite);
            list.image(developer_qr_, code, gfx::kFullUv, kWhite);
            const std::string line = developer_support_ ? "Scan to support this developer"
                                                        : "Scan for this developer's page";
            ui::text(list, fonts.semibold, line, code.x - 24.0f, kHeaderY - 26.0f, 20,
                     kInk.with_alpha(0.86f), gfx::Align::right);
            ui::text(list, fonts.mono, fonts.mono.font->fit(developer_link_, 17, 520.0f),
                     code.x - 24.0f, kHeaderY + 4.0f, 17, kInk.with_alpha(0.55f),
                     gfx::Align::right);
        }
    }
    else if (all)
    {
        const auto glyphs = ui::GlyphStyle::dark();
        const float tw = fonts.semibold.measure("Update all", 22);
        const Rect button{kRight - tw - 92.0f, kHeaderY - 38.0f, tw + 92.0f, 52.0f};
        list.rounded_rect(button, 26, kAccent);
        ui::draw_button(list, fonts, ui::GlyphStyle::light(), ui::Button::square, button.x + 16.0f,
                        button.cy(), 28);
        ui::text(list, fonts.semibold, "Update all", button.x + 56.0f, centred(button.cy(), 22), 22,
                 kOnAccent);
        (void)glyphs;
    }
    else
    {
        const char *sort_name = sort_ == Sort::name       ? "Name"
                                : sort_ == Sort::released ? "Newest release"
                                : sort_ == Sort::liked    ? "Most liked"
                                                          : "Recently updated";
        const float tw =
            ui::text(list, fonts.regular, std::string("Sorted by ") + sort_name, kRight,
                     kHeaderY - 8.0f, 22, kInk.with_alpha(0.6f), gfx::Align::right);
        ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::right_stick,
                        kRight - tw - 46.0f, kHeaderY - 16.0f, 30);
    }
}

// One tile: the key art (or the icon on the app's colour), its icon and name
// on the picture's dark foot, and what the app is on this console.
void Screen::draw_tile(const ui::Fonts &fonts, int k, unsigned layers)
{
    Rect r = card_rect(k);
    r.y -= scroll_.value;
    if (r.y > kViewBottom || r.y + r.h < window_top() - 40.0f || r.x > kWidth || r.x + r.w < 0.0f)
        return;
    auto &list = scene_;
    const std::size_t index = visible_[static_cast<std::size_t>(k)];
    const App &app = apps_[index];
    const bool picture = app.background != 0;    // key art, or the field made from the icon
    const bool keyart = picture && !app.ambient; // a picture that speaks for itself
    // Fitting allocates: once per app, the first time it is drawn.
    if (auto &fit = fits_[index]; !fit.ready)
        fit = {fonts.semibold.font->fit(app.name, 24, kTileW - 100.0f),
               fonts.regular.font->fit(app.author, 18, kTileW - 100.0f), true};
    const float lift = lift_[static_cast<std::size_t>(k)].value;
    const float in = swap_.running ? tween::stagger(swap_.elapsed, k % 8, 0.035f, 0.3f) : 1.0f;
    const float shake = k == focus_ && zone_ == Zone::grid ? ui::shake(nudge_.value, time_) : 0.0f;
    list.push_transform(1.0f + kLift * lift, r.cx(), r.cy(), shake * nudge_x_,
                        shake * nudge_y_ + 18.0f * motion() * (1.0f - in));
    const float top = in * fade_at(r.y + 1.0f), foot = in * fade_at(r.y + r.h);
    const float middle = in * fade_at(r.y + r.h * 0.4f); // where a centred icon sits
    const float light = 0.74f + 0.26f * lift;            // resting tiles sit back a little
    const float shown = appear_[index].value;
    if (layers & kPlates)
    {
        list.gradient_rect(r, kTileRadius, gfx::mix(app.accent, kMid, 0.25f).with_alpha(0.9f * top),
                           gfx::mix(app.accent, kDeep, 0.86f).with_alpha(foot));
        if (!keyart)
        {
            const float size = r.h * 0.48f;
            list.glow({r.cx() - size * 0.5f, r.y + r.h * 0.4f - size * 0.5f, size, size},
                      size * 0.3f, 60, gfx::mix(app.accent, kInk, 0.2f).with_alpha(0.35f * top));
        }
    }
    if (layers & kImages)
    {
        if (picture)
            // The picture darkens toward its foot, where the words go.
            list.image_gradient(app.background, r, gfx::kFullUv, Color{light, light, light, top},
                                Color{light * 0.36f, light * 0.36f, light * 0.4f, foot},
                                kTileRadius);
        if (keyart)
        {
            if (app.icon)
                list.image(app.icon, {r.x + 16.0f, r.y + r.h - 64.0f, 48.0f, 48.0f}, gfx::kFullUv,
                           kWhite.with_alpha(foot), 11.0f);
        }
        else if (const auto texture = art(app); texture && shown > 0.01f)
        {
            const float size = r.h * 0.48f;
            list.image(texture, {r.cx() - size * 0.5f, r.y + r.h * 0.4f - size * 0.5f, size, size},
                       gfx::kFullUv, kWhite.with_alpha(middle * shown), size * 0.22f);
        }
    }
    const bool queued = std::find(activity_.waiting.begin(), activity_.waiting.end(),
                                  app.title_id) != activity_.waiting.end();
    const bool working = activity_.id == app.title_id;
    if (layers & kShapes)
    {
        list.bordered_rect(r, kTileRadius, kClear, 1.5f, kInk.with_alpha(0.1f * top));
        if (keyart && app.icon)
            list.bordered_rect({r.x + 16.0f, r.y + r.h - 64.0f, 48.0f, 48.0f}, 11, kClear, 1.0f,
                               kInk.with_alpha(0.22f * foot));
        else if (picture && art(app) && shown > 0.01f)
        {
            const float size = r.h * 0.48f;
            list.bordered_rect({r.cx() - size * 0.5f, r.y + r.h * 0.4f - size * 0.5f, size, size},
                               size * 0.22f, kClear, 1.0f, kInk.with_alpha(0.2f * middle * shown));
        }
        if ((working || queued) && foot > 0.01f)
        {
            const Offer state = offer(app);
            const Rect track{r.x + 14.0f, r.y + r.h - 7.0f, r.w - 28.0f, 3.0f};
            list.rounded_rect(track, 1.5f, kInk.with_alpha(0.22f * foot));
            if (state.progress >= 0.0f)
                list.rounded_rect(
                    {track.x, track.y, std::max(4.0f, track.w * state.progress), 3.0f}, 1.5f,
                    kAccent.with_alpha(foot));
        }
    }
    const float marks = in * fade_at(r.y + 26.0f);
    // Likes and comments, zero included, once they are known: a quiet plate in the tile's
    // corner. On the "Most liked" shelf the tile says its place, and the counts in words.
    const auto *counts = social_shown() ? counts_of(app) : nullptr;
    const bool ranked = counts && discover() && !shelves_.empty() &&
                        shelves_[static_cast<std::size_t>(shelf_of(k))].ranked;
    if (ranked && marks > 0.01f)
    {
        if (layers & kShapes)
            list.circle(r.x + 30.0f, r.y + 28.0f, 17.0f, kAccent.with_alpha(marks));
        if (layers & kSemibold)
            ui::text(list, fonts.semibold,
                     std::to_string(k - shelves_[static_cast<std::size_t>(shelf_of(k))].start + 1),
                     r.x + 30.0f, centred(r.y + 28.0f, 19), 19, kOnAccent.with_alpha(marks),
                     gfx::Align::center);
    }
    else if (counts && marks > 0.01f && (layers & (kShapes | kSemibold)))
    {
        const std::string likes = std::to_string(counts->likes),
                          comments = std::to_string(counts->comments);
        const float likes_w = fonts.semibold.measure(likes, 17),
                    comments_w = fonts.semibold.measure(comments, 17);
        const float width =
            12.0f + 11.0f + 6.0f + likes_w + 14.0f + 15.0f + 6.0f + comments_w + 13.0f;
        const float x0 = r.x + r.w - 12.0f - width, cy = r.y + 28.0f;
        const float bubble = x0 + 29.0f + likes_w + 14.0f;
        if (layers & kShapes)
        {
            list.rounded_rect({x0, cy - 15.0f, width, 30.0f}, 15, kBlack.with_alpha(0.6f * marks));
            like_sign(list, x0 + 12.0f, cy, 11.0f, kAccent.with_alpha(marks));
            comment_sign(list, bubble, cy, 15.0f, kInk.with_alpha(0.82f * marks));
        }
        if (layers & kSemibold)
        {
            ui::text(list, fonts.semibold, likes, x0 + 29.0f, centred(cy, 17), 17,
                     kInk.with_alpha(marks));
            ui::text(list, fonts.semibold, comments, bubble + 21.0f, centred(cy, 17), 17,
                     kInk.with_alpha(marks));
        }
    }
    const float mark_x = r.x + 12.0f + (ranked ? 42.0f : 0.0f);
    if ((working || queued) && marks > 0.01f)
        pill(list, fonts, offer(app).headline, mark_x, r.y + 28.0f, 32.0f, kAccent, kOnAccent,
             marks, layers & (kShapes | kSemibold));
    else if (const auto state = mark(app); state.word && marks > 0.01f)
        pill(list, fonts, state.word, mark_x, r.y + 28.0f, 32.0f,
             state.loud ? kAccent : kBlack.with_alpha(0.6f), state.loud ? kOnAccent : kInk, marks,
             layers & (kShapes | kSemibold));
    const float x = r.x + (keyart && app.icon ? 78.0f : 20.0f);
    if (layers & kSemibold)
        ui::text(list, fonts.semibold, fits_[index].title, x, r.y + r.h - 40.0f, 24,
                 kInk.with_alpha(foot * (0.9f + 0.1f * lift)));
    if (ranked)
    {
        if (layers & kShapes)
            like_sign(list, x, r.y + r.h - 22.0f, 12.0f, kAccent.with_alpha(foot));
        if (layers & kRegular)
            ui::text(list, fonts.regular,
                     counted(counts->likes, " like", " likes") + "  \xC2\xB7  " +
                         counted(counts->comments, " comment", " comments"),
                     x + 20.0f, r.y + r.h - 16.0f, 18, kInk.with_alpha(0.8f * foot));
    }
    else if (app.badge == "Installed")
    {
        if (layers & kShapes)
            draw_check(list, x + 7.0f, r.y + r.h - 22.0f, 14.0f, kOwned.with_alpha(foot));
        if (layers & kSemibold)
            ui::text(list, fonts.semibold, "Installed", x + 22.0f, r.y + r.h - 16.0f, 18,
                     kOwned.with_alpha(foot));
    }
    else if (layers & kRegular)
        ui::text(list, fonts.regular, fits_[index].author, x, r.y + r.h - 16.0f, 18,
                 kInk.with_alpha(0.64f * foot));
    list.pop_transform();
}

void Screen::draw_grid(const ui::Fonts &fonts)
{
    auto &list = scene_;
    const Rect window{0, window_top(), kWidth, kViewBottom - window_top()};
    const int count = static_cast<int>(visible_.size());
    const int focused = zone_ == Zone::grid && count > 0 ? focus_ : -1;
    // The focus highlight: one object that glides between the tabs, the
    // stage's button and the tiles. Under a tile it is the shadow and the
    // light in that app's own colour. It is drawn before the tiles, so on its
    // way to the next one it passes under the tiles in between.
    Rect ring = ring_.value();
    const float shake = ui::shake(nudge_.value, time_);
    ring.x += shake * nudge_x_;
    ring.y += shake * nudge_y_ - scroll_.value;
    const float radius = ring_radius_.value;
    const float plate = plate_.value;
    const Color light =
        focused >= 0 ? apps_[visible_[static_cast<std::size_t>(focused)]].accent : kAccent;
    if (plate > 0.01f)
    {
        list.shadow({ring.x, ring.y + 18.0f, ring.w, ring.h}, radius, 42,
                    kBlack.with_alpha(0.6f * plate));
        list.glow(ring, radius, 34,
                  gfx::mix(light, kAccent, 0.35f).with_alpha((0.3f + 0.12f * breath()) * plate));
    }
    list.bordered_rect(ring, radius, kClear, 3.0f, kInk.with_alpha(0.95f));
    list.push_clip({0, window.y - 30.0f, kWidth, window.h + 30.0f});
    if (discover())
    {
        // The shelves' titles, each with a quiet count.
        for (std::size_t s = 0; s < shelves_.size(); ++s)
        {
            const float y = kShelfHeadY + static_cast<float>(s) * kShelfPitch - scroll_.value;
            const float alpha = fade_at(y - 8.0f);
            if (alpha <= 0.01f)
                continue;
            const float w = ui::text(list, fonts.semibold, shelves_[s].title, kMargin, y, 28,
                                     kInk.with_alpha(0.94f * alpha));
            ui::text(list, fonts.mono, std::to_string(shelves_[s].count), kMargin + w + 14.0f, y,
                     20, kInk.with_alpha(0.42f * alpha));
        }
        for (const unsigned layer : {kPlates, kImages, kShapes, kSemibold, kRegular})
            for (int k = 0; k < count; ++k)
                if (k != focused) // that one is drawn last, on top of its neighbours
                    draw_tile(fonts, k, layer);
    }
    else
    {
        int begin = 0, end = 0;
        rows_in_view(scroll_.value, begin, end);
        for (const unsigned layer : {kPlates, kImages, kShapes, kSemibold, kRegular})
            for (int k = begin; k < end; ++k)
                if (k != focused)
                    draw_tile(fonts, k, layer);
    }
    if (count == 0)
    {
        const bool library = section_ == 5;
        const char *title = library && !inventory_ready_      ? "Checking your library"
                            : library && !inventory_.complete ? "Library unavailable"
                            : !query_.empty()                 ? "No matches"
                            : library                         ? "No installed apps found"
                            : section_ == 6                   ? "Everything is up to date"
                            : apps_.empty()                   ? "The catalog is on its way"
                                                              : "Nothing on this shelf right now";
        const char *note =
            library && !inventory_ready_ ? "Reading installed apps and receipts."
            : library && !inventory_.complete
                ? "Installed apps are unavailable until their locations can be read."
            : !query_.empty() ? "Try another app name or developer."
            : section_ == 6   ? "Updates for apps installed by ProsperoStore appear here."
            : apps_.empty()   ? "Verified apps will appear here when the catalog is ready."
                              : "Try another section.";
        // While something is on its way an arc draws itself into a full
        // circle, fades, and starts again a little further round.
        const bool waiting =
            library ? !inventory_ready_ : apps_.empty() && loading_ && query_.empty();
        const float y = window.y + (kViewBottom - window.y) * (waiting ? 0.56f : 0.45f);
        if (waiting)
        {
            constexpr float kTurn = 6.2831853f, kPeriod = 1.7f;
            const float t = std::fmod(time_, kPeriod) / kPeriod;
            const float sweep = kTurn * tween::cubic_in_out(t / 0.72f);
            const float alpha = 1.0f - tween::smoothstep((t - 0.8f) / 0.2f);
            const float start = std::floor(time_ / kPeriod) * 2.2f + time_ * 0.5f;
            const float cx = 960.0f, cy = y - 118.0f, radius = 46.0f;
            list.glow({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, radius, 36,
                      kAccent.with_alpha(0.1f + 0.08f * alpha * sweep / kTurn));
            list.ring(cx, cy, radius, 6.0f, kInk.with_alpha(0.12f));
            list.arc(cx, cy, radius, 6.0f, start, std::max(0.02f, sweep),
                     kAccent.with_alpha(alpha));
        }
        title = waiting && !library ? "Loading the catalog" : title;
        ui::text(list, fonts.semibold, title, 960, y, 30, kInk.with_alpha(0.86f),
                 gfx::Align::center);
        ui::text(list, fonts.regular, note, 960, y + 42.0f, 24, kInk.with_alpha(0.6f),
                 gfx::Align::center);
    }
    list.pop_clip();

    if (focused >= 0)
    {
        list.push_clip({0, window.y - 30.0f, kWidth, window.h + 30.0f});
        draw_tile(fonts, focused, kAllLayers);
        list.pop_clip();
    }
}

// ---- drawing: the product page ----------------------------------------------

void Screen::order(const App &app, Order::Kind kind)
{
    pending_order = {};
    pending_order.kind = kind;
    pending_order.id = app.title_id;
    if (!app.installed.empty())
    {
        const auto &path = app.installed.front().path;
        pending_order.location = path.substr(0, path.find_last_of('/'));
    }
    else
        pending_order.location = settings_.location;
    if (app.title_id == self_id_)
        pending_order.location = self_location_;
    if (kind == Order::Kind::install && app.detail)
        pending_order.entry = *app.detail;
    pending_order.entry.id = app.title_id;
    if (pending_order.entry.name.empty())
        pending_order.entry.name = app.name;
}

bool Screen::open_app(const std::string &id)
{
    section_ = 0;
    query_.clear();
    developer_.clear();
    rebuild();
    for (std::size_t k = 0; k < visible_.size(); ++k)
        if (apps_[visible_[k]].title_id == id)
        {
            focus_ = static_cast<int>(k);
            zone_ = Zone::grid;
            details_ = true;
            pending_detail = id;
            refresh_detail();
            return true;
        }
    return false;
}

void Screen::remote_install(const std::string &id)
{
    if (open_app(id))
        auto_order_ = id;
}

bool Screen::remote_uninstall(const std::string &id)
{
    for (const auto &app : apps_)
        if (app.title_id == id && !app.installed.empty() && offer(app).armed)
        {
            order(app, Order::Kind::uninstall);
            return true;
        }
    return false;
}

bool Screen::remote_adopt(const std::string &id)
{
    for (const auto &app : apps_)
        if (app.title_id == id)
        {
            const Offer state = offer(app);
            if (state.armed && state.primary == Order::Kind::adopt)
            {
                order(app, Order::Kind::adopt);
                return true;
            }
        }
    return false;
}

bool Screen::remote_order()
{
    if (!details_ || !focused() || !focused()->detail)
        return false;
    order(*focused(), Order::Kind::install);
    return true;
}

std::size_t Screen::remote_update_all()
{
    update_all_.clear();
    for (const auto &app : apps_)
        if (app.badge == "Update" && app.title_id != self_id_)
            update_all_.push_back(app.title_id);
    return update_all_.size();
}

void Screen::set_activity(Activity activity)
{
    // The speed is measured over half-second steps and smoothed, so the time
    // left settles instead of jumping with every block that arrives.
    const bool same = activity.id == activity_.id && activity.phase == activity_.phase &&
                      activity.done >= rate_done_;
    if (!same)
    {
        rate_ = 0.0f;
        rate_time_ = time_;
        rate_done_ = activity.done;
    }
    else if (time_ - rate_time_ >= 0.5f)
    {
        const float speed = static_cast<float>(activity.done - rate_done_) / (time_ - rate_time_);
        rate_ = rate_ > 0.0f ? rate_ * 0.75f + speed * 0.25f : speed;
        rate_time_ = time_;
        rate_done_ = activity.done;
    }
    activity_ = std::move(activity);
}

// "about 40 s left", "about 3 min left"; nothing until the speed is known.
std::string Screen::time_left() const
{
    if (rate_ < 1024.0f || activity_.total <= activity_.done)
        return {};
    const float seconds = static_cast<float>(activity_.total - activity_.done) / rate_;
    if (seconds < 8.0f)
        return "a few seconds left";
    if (seconds < 55.0f)
        return "about " + std::to_string(static_cast<int>(std::ceil(seconds / 5.0f)) * 5) +
               " s left";
    if (seconds < 3600.0f * 3.0f)
        return "about " + std::to_string(static_cast<int>(std::ceil(seconds / 60.0f))) +
               " min left";
    return {};
}

void Screen::set_installer(bool available, bool guard, std::string reason, std::string location)
{
    installer_ = available;
    guard_ = guard;
    installer_reason_ = std::move(reason);
    install_location_ = std::move(location);
}

void Screen::finish_job(bool ok, bool restart, std::string title, std::string body)
{
    const bool cancelled = title.ends_with(": cancelled");
    chime_ = ok && !cancelled;
    restart_needed_ = restart_needed_ || restart;
    history_.push_back({ok, title, body});
    if (history_.size() > 12)
        history_.erase(history_.begin());
    toasts_.push(ok          ? ui::StatusKind::success
                 : cancelled ? ui::StatusKind::info
                             : ui::StatusKind::danger,
                 std::move(title), std::move(body), 10.0f);
}

Screen::Offer Screen::offer(const App &app) const
{
    Offer out;
    const system::InstalledApp *installed = app.installed.empty() ? nullptr : &app.installed[0];
    const bool soon = app.catalog_badge == "Coming soon";
    const bool image = app.detail && !app.detail->format.empty() && app.detail->format != "zip";
    const auto megabytes = [](std::uint64_t bytes) { return size_text(bytes); };

    // A transaction for this app comes first: what it is doing, and Cancel.
    const bool waiting = std::find(activity_.waiting.begin(), activity_.waiting.end(),
                                   app.title_id) != activity_.waiting.end();
    if (activity_.id == app.title_id || waiting)
    {
        using Phase = install::Phase;
        const auto phase = waiting ? Phase::idle : static_cast<Phase>(activity_.phase);
        out.busy = true;
        out.tone = 1;
        out.headline = phase == Phase::downloading  ? "Downloading"
                       : phase == Phase::verifying  ? "Verifying"
                       : phase == Phase::unpacking  ? "Unpacking"
                       : phase == Phase::activating ? "Finishing"
                       : phase == Phase::removing   ? "Removing"
                                                    : "Waiting";
        const bool measured =
            (phase == Phase::downloading || phase == Phase::unpacking) && activity_.total > 0;
        if (measured)
        {
            out.progress = tween::clamp01(static_cast<float>(activity_.done) /
                                          static_cast<float>(activity_.total));
            out.note = megabytes(activity_.done) + " of " + megabytes(activity_.total);
            if (const auto left = time_left(); !waiting && !left.empty())
                out.note += "  \xC2\xB7  " + left;
        }
        else
            out.note = waiting ? "Queued behind another app" : "One moment";
        // Once the folder is being put in place there is nothing left to cancel.
        if (phase != Phase::activating && phase != Phase::removing)
        {
            out.label = "Cancel";
            out.primary = Order::Kind::cancel;
            out.armed = true;
        }
        out.reason = "The app's folder is only changed once everything is verified and unpacked.";
        return out;
    }

    if (app.title_id == self_id_ && !self_id_.empty())
    {
        // The store's own page: it can't be uninstalled from inside, and its
        // update is finished by restarting it.
        const bool newer = catalog::update_available(self_version_, app.available_version);
        out.headline = restart_needed_ ? "Restart to finish"
                       : newer         ? "Update"
                                       : "This is ProsperoStore";
        out.note = newer && !restart_needed_
                       ? self_version_ + "  \xE2\x86\x92  " + app.available_version
                       : "Version " + self_version_;
        out.tone = newer || restart_needed_ ? 1 : 0;
        out.reason = restart_needed_
                         ? "Close ProsperoStore and open it again to use the new version."
                     : newer ? ""
                             : "You are using it right now.";
        if (newer && !restart_needed_)
        {
            out.label = "Update";
            out.primary = Order::Kind::install;
            if (!installer_)
                out.reason = installer_reason_;
            else if (!app.detail)
                out.reason = "Waiting for the app's details.";
            else
            {
                out.armed = true;
                out.reason = "The new version is put in place now and starts the next time "
                             "ProsperoStore is opened.";
            }
        }
        return out;
    }
    if (installed && app.badge == "Update")
    {
        out.headline = "Update";
        out.note = installed->version + "  \xE2\x86\x92  " + app.available_version;
        out.label = "Update";
        out.primary = Order::Kind::install;
        out.uninstall = true;
        out.tone = 1;
    }
    else if (installed)
    {
        out.headline = "Installed";
        out.note =
            installed->version.empty() ? "Installed as an image" : "Version " + installed->version;
        out.tone = 2;
        if (installed->managed)
        {
            out.label = "Uninstall";
            out.primary = Order::Kind::uninstall;
        }
        else if (!app.local_only && !installed->image && !installed->duplicate &&
                 installed->path.ends_with("/" + app.title_id))
        {
            // Installed by hand, and listed in the catalog: it can be handed over.
            out.label = "Manage with ProsperoStore";
            out.primary = Order::Kind::adopt;
            out.armed = installer_;
            out.reason = installer_ ? "Installed outside ProsperoStore. Hand it over to get its "
                                      "updates here."
                                    : installer_reason_;
            return out;
        }
        else
            out.reason = installed->reason;
    }
    else if (soon)
    {
        out.headline = "Coming soon";
        out.note = "No release has been published yet";
        out.reason = "It will appear here when it is released";
    }
    else if (image)
    {
        out.headline = "Not installable";
        out.note = "Published as a disk image";
        out.reason = "Can't be installed by this version of ProsperoStore";
    }
    else if (!app.local_only)
    {
        out.headline = "Ready";
        out.note = app.detail && app.detail->size ? megabytes(app.detail->size) + " download"
                                                  : "Verified by the signed catalog";
        out.label = "Install";
        out.primary = Order::Kind::install;
    }
    if (out.primary == Order::Kind::none)
        return out;
    if (installed && std::find(running_.begin(), running_.end(), app.title_id) != running_.end())
    {
        out.reason = app.name + " is running. Close it first.";
        return out;
    }
    // Why the button rests, or what pressing it does.
    const bool changes = installed != nullptr; // an update or an uninstall
    if (!installer_)
        out.reason = installer_reason_;
    else if (changes && !guard_)
        out.reason = "Needs the running-app check, which this build doesn't have yet.";
    else if (out.primary == Order::Kind::install && !app.detail)
        out.reason = "Waiting for the app's details.";
    else
    {
        out.armed = true;
        out.reason = out.primary == Order::Kind::uninstall
                         ? "Removes the app's folder. Its saved data stays on the console."
                         : "Checked against the signed catalog before anything is installed.";
    }
    return out;
}

// A small card in the top-right corner while an app is installed and its
// page is not open: what, how far, and how long it still takes.
void Screen::draw_job_card(const ui::Fonts &fonts, std::uint32_t glass)
{
    float progress = -1.0f;
    const char *phase = "";
    const App *busy = busy_app(progress, phase);
    if (!busy || panel_)
        return;
    const float alpha = 1.0f - (focused() == busy ? page_.value : 0.0f);
    if (alpha <= 0.01f)
        return;
    auto &list = overlay_;
    const Rect card{kRight - 520.0f, 112.0f, 520.0f, 112.0f};
    list.push_opacity(alpha);
    list.shadow({card.x, card.y + 14.0f, card.w, card.h}, 24, 40, kBlack.with_alpha(0.45f));
    list.glass(glass, card, 24, kWhite);
    list.rounded_rect(card, 24, gfx::mix(kDeep, kMid, 0.2f).with_alpha(0.62f));
    list.bordered_rect(card, 24, kClear, 1.5f, kInk.with_alpha(0.2f));
    const Rect icon{card.x + 20.0f, card.y + 20.0f, 72.0f, 72.0f};
    if (const auto texture = art(*busy))
        list.image(texture, icon, gfx::kFullUv, kWhite, 16);
    else
        list.rounded_rect(icon, 16, busy->accent);
    const float x = icon.x + icon.w + 20.0f, w = card.x + card.w - 24.0f - x;
    ui::text(list, fonts.semibold,
             fonts.semibold.font->fit(std::string(phase) + " " + busy->name, 24, w), x,
             card.y + 46.0f, 24, kInk);
    std::string line = progress >= 0.0f ? std::to_string(static_cast<int>(progress * 100.0f)) + "%"
                                        : std::string("One moment");
    if (const auto left = time_left(); !left.empty() && progress >= 0.0f)
        line += "  \xC2\xB7  " + left;
    if (!activity_.waiting.empty())
        line += "  \xC2\xB7  " + std::to_string(activity_.waiting.size()) + " queued";
    ui::text(list, fonts.regular, fonts.regular.font->fit(line, 19, w), x, card.y + 74.0f, 19,
             kInk.with_alpha(0.7f));
    const Rect track{x, card.y + 88.0f, w, 5.0f};
    list.rounded_rect(track, 2.5f, kInk.with_alpha(0.16f));
    if (progress >= 0.0f)
        list.rounded_rect({track.x, track.y, std::max(5.0f, track.w * progress), track.h}, 2.5f,
                          kAccent);
    else
    {
        const float t = std::fmod(time_ * 0.8f, 1.0f);
        list.rounded_rect({track.x + track.w * 0.7f * t, track.y, track.w * 0.3f, track.h}, 2.5f,
                          kAccent);
    }
    list.pop_opacity();
}

// The hold's progress as a wash filling the button from the left.
void Screen::hold_fill(gfx::DrawList &list, const Rect &button, float radius) const
{
    const float k = hold_.progress();
    if (k <= 0.004f)
        return;
    list.rounded_rect({button.x, button.y, std::max(radius * 2.0f, button.w * k), button.h}, radius,
                      kInk.with_alpha(0.1f + 0.14f * k));
}

bool Screen::hold_shown() const
{
    return hold_.holding() || hold_.hinting() || hold_.progress() > 0.004f;
}

float Screen::breath() const
{
    return settings_.reduce_motion ? 0.5f : ui::breathe(time_);
}

void Screen::draw_action_box(const ui::Fonts &fonts, std::uint32_t glass, const App &app,
                             float content)
{
    auto &list = overlay_;
    Rect box = kActionBox;
    box.y += 30.0f * motion() * (1.0f - content);
    list.push_opacity(content);
    list.shadow({box.x, box.y + 24.0f, box.w, box.h}, 32, 60, kBlack.with_alpha(0.5f));
    // Frosted glass over the key art: the blurred picture, a tint, a sheen
    // along the top and a hairline of light.
    list.glass(glass, box, 32, kWhite);
    list.rounded_rect(box, 32, gfx::mix(kDeep, kMid, 0.16f).with_alpha(0.68f));
    list.gradient_rect({box.x, box.y, box.w, 140.0f}, 32, kInk.with_alpha(0.07f),
                       kInk.with_alpha(0.0f));
    list.bordered_rect(box, 32, kClear, 1.5f, kInk.with_alpha(0.22f));

    const Offer state = offer(app);
    const float x = box.x + 34.0f, w = box.w - 68.0f;
    ui::text(list, fonts.semibold, "ON THIS CONSOLE", x, box.y + 54.0f, 16, kInk.with_alpha(0.55f),
             gfx::Align::left, 3.5f);
    const float cx = box.cx(), cy = box.y + 196.0f;
    if (state.busy)
    {
        // A ring for the part that can be measured, and the whole job as steps.
        using Phase = install::Phase;
        const bool mine = activity_.id == app.title_id;
        const auto phase = static_cast<Phase>(activity_.phase);
        const float radius = 86.0f;
        list.ring(cx, cy, radius, 10.0f, kInk.with_alpha(0.12f));
        if (state.progress >= 0.0f)
        {
            list.glow({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, radius, 40,
                      kAccent.with_alpha(0.12f));
            list.arc(cx, cy, radius, 10.0f, -1.5708f, 6.2832f * state.progress, kAccent);
            ui::text(list, fonts.mono,
                     std::to_string(static_cast<int>(state.progress * 100.0f)) + "%", cx,
                     cy + 16.0f, 46, kInk, gfx::Align::center);
        }
        else
            list.arc(cx, cy, radius, 10.0f, time_ * 3.4f, 1.7f, kAccent);
        // Inside the ring: at this height its inner edge leaves about 130 px.
        ui::text(list, fonts.semibold,
                 fonts.semibold.font->fit(ui::upper(state.headline), 13, 110.0f), cx, cy + 42.0f,
                 13, kInk.with_alpha(0.62f), gfx::Align::center, 2.0f);
        const char *names[] = {"Download", "Verify", "Unpack", "Finish"};
        const int step = !mine                         ? -1
                         : phase == Phase::downloading ? 0
                         : phase == Phase::verifying   ? 1
                         : phase == Phase::unpacking   ? 2
                                                       : 3;
        const float y = box.y + 352.0f, gap = (w - 40.0f) / 3.0f;
        for (int i = 0; i < 4; ++i)
        {
            const float px = x + 20.0f + gap * static_cast<float>(i);
            if (i < 3)
                list.rounded_rect({px + 14.0f, y - 1.5f, gap - 28.0f, 3.0f}, 1.5f,
                                  i < step ? kAccent : kInk.with_alpha(0.16f));
            if (i < step)
            {
                list.circle(px, y, 12.0f, kAccent);
                draw_check(list, px, y, 13.0f, kOnAccent);
            }
            else if (i == step)
            {
                list.circle(px, y, 12.0f + 3.0f * breath(), kAccent.with_alpha(0.25f));
                list.ring(px, y, 11.0f, 3.0f, kAccent);
                list.circle(px, y, 4.5f, kAccent);
            }
            else
                list.ring(px, y, 10.0f, 2.0f, kInk.with_alpha(0.28f));
            ui::text(list, fonts.semibold, ui::upper(names[i]), px, y + 38.0f, 14,
                     kInk.with_alpha(i <= step ? 0.9f : 0.42f), gfx::Align::center, 2.0f);
        }
        ui::text(list, fonts.regular, fonts.regular.font->fit(state.note, 20, w), cx,
                 box.y + 430.0f, 20, kInk.with_alpha(0.74f), gfx::Align::center);
    }
    else
    {
        // At rest: what the app is here, big; installed apps get a lit check.
        const Color tone = state.tone == 1 ? kAccent : state.tone == 2 ? kOwned : kInk;
        if (state.tone == 2)
        {
            list.glow({cx - 64.0f, cy - 64.0f, 128.0f, 128.0f}, 64, 40, kOwned.with_alpha(0.18f));
            list.ring(cx, cy, 62.0f, 6.0f, kOwned);
            draw_check(list, cx, cy + 2.0f, 50.0f, kOwned);
        }
        else
        {
            // Ready: an arrow down into a ring; an update: an arrow up.
            const Color ink = state.primary == Order::Kind::none ? kInk.with_alpha(0.5f) : tone;
            const float up = state.tone == 1 ? -1.0f : 1.0f;
            list.glow({cx - 64.0f, cy - 64.0f, 128.0f, 128.0f}, 64, 40, ink.with_alpha(0.12f));
            list.ring(cx, cy, 62.0f, 3.0f, ink.with_alpha(0.55f));
            list.line(cx, cy - 24.0f * up, cx, cy + 22.0f * up, 5.0f, ink);
            list.line(cx - 17.0f, cy + 6.0f * up, cx, cy + 24.0f * up, 5.0f, ink);
            list.line(cx + 17.0f, cy + 6.0f * up, cx, cy + 24.0f * up, 5.0f, ink);
        }
        ui::text(list, fonts.display, fonts.display.font->fit(state.headline, 46, w), cx,
                 box.y + 322.0f, 46, tone, gfx::Align::center);
        ui::text(list, fonts.regular, fonts.regular.font->fit(state.note, 21, w), cx,
                 box.y + 362.0f, 21, kInk.with_alpha(0.66f), gfx::Align::center);
        // Where it goes, and the room there; or where it is.
        std::string where = "Installs to " + settings_.location;
        for (const auto &[path, free] : locations_)
            if (path == settings_.location)
                where += "  \xC2\xB7  " + size_text(free) + " free";
        if (!app.installed.empty())
            where = app.installed.front().path;
        if (state.primary != Order::Kind::none)
            ui::text(list, fonts.regular, fonts.regular.font->fit(where, 18, w), cx, box.y + 396.0f,
                     18, kInk.with_alpha(0.46f), gfx::Align::center);
    }

    // The primary button: filled when it can be pressed, at rest when it
    // can't, and the line under it always says why.
    Rect button{x, box.y + 452.0f, w, 70.0f};
    button.x += ui::shake(nudge_.value, time_, 10.0f) * (details_ ? 1.0f : 0.0f);
    if (!state.label.empty())
    {
        list.push_transform(1.0f - 0.035f * press_.value, button.cx(), button.cy(), 0, 0);
        // Gold is for getting an app; taking one away is never the loudest thing here.
        const bool destructive = state.primary == Order::Kind::uninstall;
        const bool filled = state.armed && !state.busy && !destructive;
        if (destructive)
        {
            list.bordered_rect(button, kButtonRadius, kInk.with_alpha(0.06f), 1.5f,
                               kInk.with_alpha(state.armed ? 0.5f : 0.25f));
            hold_fill(list, button, kButtonRadius);
        }
        else if (filled)
        {
            list.glow(button, kButtonRadius, 22, kAccent.with_alpha(0.22f + 0.1f * breath()));
            list.rounded_rect(button, kButtonRadius, kAccent);
        }
        else
            list.bordered_rect(button, kButtonRadius, kAccent.with_alpha(0.08f), 2.0f,
                               kAccent.with_alpha(state.armed ? 1.0f : 0.5f));
        const std::string label = destructive && hold_shown() ? "Hold to uninstall" : state.label;
        const float tw = fonts.semibold.measure(label, 28);
        ui::draw_button(list, fonts, filled ? ui::GlyphStyle::light() : ui::GlyphStyle::dark(),
                        ui::Button::cross, button.cx() - tw * 0.5f - 22.0f, button.cy(), 32);
        ui::text(list, fonts.semibold, label, button.cx() + 22.0f, centred(button.cy(), 28), 28,
                 destructive ? kInk.with_alpha(0.86f)
                 : filled    ? kOnAccent
                             : kAccent.with_alpha(state.armed ? 1.0f : 0.72f),
                 gfx::Align::center);
        list.pop_transform();
    }
    if (state.uninstall && installer_ && guard_ && !state.busy)
    {
        const Rect second{x, button.y + 82.0f, w, 50.0f};
        list.bordered_rect(second, 16, kClear, 1.5f, kInk.with_alpha(0.3f));
        hold_fill(list, second, 16);
        const char *label = hold_shown() ? "Hold to uninstall" : "Uninstall";
        const float tw = fonts.semibold.measure(label, 22);
        ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::square,
                        second.cx() - tw * 0.5f - 20.0f, second.cy(), 28);
        ui::text(list, fonts.semibold, label, second.cx() + 18.0f, centred(second.cy(), 22), 22,
                 kInk.with_alpha(0.86f), gfx::Align::center);
    }
    else
        ui::paragraph(list, fonts.regular, state.reason, x,
                      button.y + (state.label.empty() ? 30.0f : 104.0f), 19, w, 27,
                      kInk.with_alpha(0.6f), 2);
    list.pop_opacity();
}

void Screen::draw_page(const ui::Fonts &fonts, std::uint32_t glass)
{
    const float t = page_.value;
    const App *shown = focused();
    if (t <= 0.004f || !shown)
        return;
    const App &app = *shown;
    const int index = static_cast<int>(visible_[static_cast<std::size_t>(focus_)]);
    // Reduced motion: no growing out of the tile, the page fades in where it is.
    const float open = settings_.reduce_motion ? 1.0f : tween::cubic_out(t);
    // In the scene: the tile's key art opens out to fill the screen. An app
    // without key art darkens the home page and lights its own colour.
    Rect from = card_rect(focus_);
    from.y -= scroll_.value;
    const Rect full{0, 0, kWidth, kHeight};
    const auto lerp = [&](const Rect &a, const Rect &b, float k)
    {
        return Rect{tween::lerp(a.x, b.x, k), tween::lerp(a.y, b.y, k), tween::lerp(a.w, b.w, k),
                    tween::lerp(a.h, b.h, k)};
    };
    auto &scene = scene_;
    const float veil = tween::smoothstep(t);
    if (app.background)
    {
        draw_art(scene, index, lerp(from, full, open), tween::clamp01(t * 6.0f),
                 tween::lerp(kTileRadius, 0.0f, open));
        scene.gradient_rect_h({0, 0, 1460.0f, kHeight}, 0, kDeep.with_alpha(0.95f * veil),
                              kDeep.with_alpha(0.0f));
        scene.gradient_rect({0, 520.0f, kWidth, 560.0f}, 0, kDeep.with_alpha(0.0f),
                            kDeep.with_alpha(0.9f * veil));
        scene.gradient_rect({0, 0, kWidth, 220.0f}, 0, kDeep.with_alpha(0.5f * veil),
                            kDeep.with_alpha(0.0f));
    }

    // In the overlay: the words on the left and the glass panel on the right.
    auto &list = overlay_;
    if (!app.background)
    {
        // No key art: the home page blurs away behind glass, lit in the app's colour.
        list.glass(glass, full, 0, kWhite.with_alpha(veil));
        list.rounded_rect(full, 0, gfx::mix(kCoal, kDeep, 0.55f).with_alpha(0.9f * veil));
        list.glow({1180.0f, 60.0f, 700.0f, 700.0f}, 350, 320, app.accent.with_alpha(0.3f * veil));
    }
    const float content = tween::smoothstep((t - 0.35f) / 0.65f);
    const float slide = 44.0f * motion() * (1.0f - tween::cubic_out(t));
    list.push_opacity(content);
    list.push_transform(1.0f, 0, 0, slide, 0);
    if (app.background && !app.ambient)
    {
        list.shadow({kPageIcon.x, kPageIcon.y + 16.0f, kPageIcon.w, kPageIcon.h}, 30, 40,
                    kBlack.with_alpha(0.55f));
        if (const auto texture = art(app, true))
            list.image(texture, kPageIcon, gfx::kFullUv, kWhite, 30);
        list.bordered_rect(kPageIcon, 30, kClear, 1.5f, kInk.with_alpha(0.2f));
    }
    const std::string kicker = app.local_only         ? std::string("installed")
                               : app.category.empty() ? app.kind
                                                      : category_label(app.category);
    ui::text(list, fonts.semibold, ui::upper(kicker), kInfoX, 362, 20, kAccent, gfx::Align::left,
             4.0f);
    const ui::FontRef &headline = face_for(app.name, fonts.display, fonts.semibold);
    ui::text(list, headline, headline.font->fit(app.name, 84, 1180.0f), kInfoX - 4.0f, 450, 84,
             kInk);
    std::string line = app.author;
    if (!app.released.empty())
        line += (line.empty() ? "" : "  \xC2\xB7  ") + std::string("Released ") +
                app.released.substr(0, app.released.find('T'));
    ui::text(list, fonts.regular, fonts.regular.font->fit(line, 26, kInfoW), kInfoX, 502, 26,
             kInk.with_alpha(0.78f));
    // The facts as chips: version, size, licence.
    float cx = kInfoX;
    const auto chip = [&](const std::string &word)
    {
        if (word.empty())
            return;
        const float width = fonts.semibold.measure(word, 20) + 36.0f;
        if (cx + width >
            kInfoX + kFactsW) // a chip that doesn't fit is left out; the article says it
            return;
        list.bordered_rect({cx, 532.0f, width, 42.0f}, 21, kInk.with_alpha(0.08f), 1.5f,
                           kInk.with_alpha(0.22f));
        ui::text(list, fonts.semibold, word, cx + width * 0.5f, centred(553.0f, 20), 20,
                 kInk.with_alpha(0.88f), gfx::Align::center);
        cx += width + 12.0f;
    };
    if (app.detail)
    {
        chip(app.detail->version.empty() ? std::string() : "v" + app.detail->version);
        chip(app.detail->size ? size_text(app.detail->size) : std::string());
        chip(app.detail->license);
        chip(app.detail->format.empty() ? std::string() : ui::upper(app.detail->format));
        // From the custom catalog shown beside the official one: said on its page.
        chip(app.detail->extra ? std::string("Custom catalog") : std::string());
        chip(app.detail->sandbox == "stays"    ? std::string("Stays in sandbox")
             : app.detail->sandbox == "leaves" ? std::string("Leaves sandbox")
                                               : std::string());
    }
    // Likes and comments join the facts, zero included, each behind its sign.
    if (const auto *counts = counts_of(app))
    {
        const auto fact = [&](bool like, const std::string &word)
        {
            const float width = fonts.semibold.measure(word, 20) + 62.0f;
            if (cx + width > kInfoX + kFactsW)
                return;
            list.bordered_rect({cx, 532.0f, width, 42.0f}, 21, kInk.with_alpha(0.08f), 1.5f,
                               kInk.with_alpha(0.22f));
            if (like)
                like_sign(list, cx + 18.0f, 553.0f, 14.0f, kAccent);
            else
                comment_sign(list, cx + 17.0f, 554.0f, 17.0f, kInk.with_alpha(0.88f));
            ui::text(list, fonts.semibold, word, cx + 44.0f, centred(553.0f, 20), 20,
                     kInk.with_alpha(0.88f));
            cx += width + 12.0f;
        };
        fact(true, counted(counts->likes, " like", " likes"));
        fact(false, counted(counts->comments, " comment", " comments"));
    }
    ui::Canvas canvas{list, fonts, glass, time_};
    article_.draw(canvas);
    list.pop_transform();
    list.pop_opacity();

    draw_action_box(fonts, glass, app, content);

    // The QR code for the app's page, under the panel.
    if (qr_texture_ != 0 && qr_id_ == app.title_id)
    {
        list.push_opacity(content);
        const Rect code{kActionBox.x + 4.0f, kActionBox.y + kActionBox.h + 26.0f, 92.0f, 92.0f};
        list.rounded_rect(code.inset(-6.0f), 12, kWhite);
        list.image(qr_texture_, code, gfx::kFullUv, kWhite);
        ui::text(list, fonts.semibold, "Scan for its page and source", code.x + code.w + 26.0f,
                 code.y + 38.0f, 20, kInk.with_alpha(0.86f));
        ui::text(list, fonts.mono,
                 fonts.mono.font->fit(app.detail ? app.detail->page : "", 17, 350.0f),
                 code.x + code.w + 26.0f, code.y + 68.0f, 17, kInk.with_alpha(0.55f));
        list.pop_opacity();
    }

    // An app without key art: its icon flies from the tile to the page.
    if (!app.background || app.ambient)
    {
        const float size = from.h * 0.48f;
        const Rect icon_from{from.cx() - size * 0.5f, from.y + from.h * 0.4f - size * 0.5f, size,
                             size};
        const Rect to = app.ambient ? kPageHero : kPageIcon;
        const Rect cover = lerp(icon_from, to, open);
        const float radius = tween::lerp(size * 0.22f, to.w * 0.21f, open);
        if (app.ambient)
            list.glow(cover.inset(-6.0f), radius + 6.0f, 110,
                      gfx::mix(app.accent, kInk, 0.18f).with_alpha(0.34f * veil));
        list.shadow({cover.x, cover.y + 18.0f * t, cover.w, cover.h}, radius, 40,
                    kBlack.with_alpha(0.55f * veil));
        if (const auto texture = art(app, true))
            list.image(texture, cover, gfx::kFullUv, kWhite.with_alpha(tween::clamp01(t * 14.0f)),
                       radius);
        list.bordered_rect(cover, radius, kClear, 1.5f, kInk.with_alpha(0.18f * veil));
    }
}

// "Like or comment": a code for the app's discussion on GitHub, to scan with a phone. The
// address was checked when it was read (social::discussion_url) and again before it got here.
void Screen::draw_discussion(const ui::Fonts &fonts, std::uint32_t glass)
{
    const float t = tween::clamp01(discussion_value_.value);
    if (t <= 0.004f)
        return;
    auto &list = overlay_;
    list.rounded_rect({0, 0, kWidth, kHeight}, 0, kBlack.with_alpha(0.66f * t));
    list.push_opacity(tween::smoothstep(t));
    list.push_transform(1.0f, 0, 0, 0, 24.0f * motion() * (1.0f - tween::cubic_out(t)));
    const Rect panel{530.0f, 122.0f, 860.0f, 800.0f};
    list.shadow({panel.x, panel.y + 24.0f, panel.w, panel.h}, 32, 60, kBlack.with_alpha(0.5f));
    list.glass(glass, panel, 32, kWhite);
    list.rounded_rect(panel, 32, gfx::mix(kDeep, kMid, 0.16f).with_alpha(0.86f));
    list.gradient_rect({panel.x, panel.y, panel.w, 140.0f}, 32, kInk.with_alpha(0.07f),
                       kInk.with_alpha(0.0f));
    list.bordered_rect(panel, 32, kClear, 1.5f, kInk.with_alpha(0.22f));
    const float cx = panel.cx();
    ui::text(list, fonts.semibold, "LIKE OR COMMENT", cx, panel.y + 66.0f, 18, kAccent,
             gfx::Align::center, 4.0f);
    ui::text(list, fonts.display, fonts.display.font->fit(discussion_name_, 44, panel.w - 96.0f),
             cx, panel.y + 124.0f, 44, kInk, gfx::Align::center);
    // Twice the code's own size when that fits: every module lands on whole pixels.
    const float doubled = static_cast<float>(discussion_qr_width_) * 2.0f;
    const float size = doubled >= 300.0f && doubled <= 420.0f ? doubled : 400.0f;
    const Rect code{cx - size * 0.5f, panel.y + 372.0f - size * 0.5f, size, size};
    list.rounded_rect(code.inset(-14.0f), 22, kWhite);
    if (discussion_qr_ != 0 && discussion_qr_link_ == discussion_link_)
        list.image(discussion_qr_, code, gfx::kFullUv, kWhite);
    else
        list.arc(cx, code.cy(), 40.0f, 6.0f, time_ * 3.4f, 1.7f, kOnAccent);
    ui::text(list, fonts.semibold, "Scan it to open this app's discussion on GitHub on your phone.",
             cx, panel.y + 644.0f, 24, kInk, gfx::Align::center);
    ui::text(list, fonts.regular, "Liking and commenting need a GitHub account.", cx,
             panel.y + 682.0f, 22, kInk.with_alpha(0.72f), gfx::Align::center);
    ui::text(list, fonts.mono, fonts.mono.font->fit(discussion_link_, 17, panel.w - 96.0f), cx,
             panel.y + 722.0f, 17, kInk.with_alpha(0.5f), gfx::Align::center);
    const float close = fonts.regular.measure("Close", 22);
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::circle,
                    cx - (30.0f + 12.0f + close) * 0.5f, panel.y + 762.0f, 30);
    ui::text(list, fonts.regular, "Close", cx - (30.0f + 12.0f + close) * 0.5f + 42.0f,
             centred(panel.y + 762.0f, 22), 22, kInk.with_alpha(0.8f));
    list.pop_transform();
    list.pop_opacity();
}

// The opening. The store's mark, as on its icon: three glass tiles and a gold one
// with the diamond, landing one after another on springs, a sheen across the gold,
// the name spacing in. When the catalog has answered, the mark flies into the top
// bar's diamond and the home page shows through.
void Screen::draw_intro(const ui::Fonts &fonts)
{
    auto &list = overlay_;
    const float t = intro_clock_, leave = tween::cubic_in_out(intro_leave_);
    const bool still = settings_.reduce_motion;
    const Color teal = Color::rgb(0x2fd4f0), gold = kAccent, gold_deep = Color::rgb(0xc98a1b);
    const Rect full{0, 0, kWidth, kHeight};

    // The room: Farlight's dark with a teal and a gold light breathing in it.
    list.push_opacity(1.0f - tween::smoothstep(intro_leave_ * 1.3f));
    list.rounded_rect(full, 0, Color::rgb(0x08111f));
    const float breathe = still ? 0.5f : 0.5f + 0.5f * std::sin(t * 1.6f);
    list.glow({560.0f, 180.0f, 800.0f, 600.0f}, 300, 360, teal.with_alpha(0.1f + 0.05f * breathe));
    list.glow({900.0f, 420.0f, 420.0f, 320.0f}, 160, 260, gold.with_alpha(0.05f + 0.03f * breathe));
    list.pop_opacity();

    // The mark: a 2 x 2 grid centred above the name, flying to the top bar as it leaves.
    constexpr float kTile = 132.0f, kGap = 22.0f, kRadius = 30.0f;
    const float cx = 960.0f, cy = 450.0f;
    const float logo_x = kMargin + 13.0f, logo_y = kTopY; // the top bar's diamond
    const float scale = tween::lerp(1.0f, 0.075f, leave);
    const float gold_cx = cx + (kTile + kGap) * 0.5f, gold_cy = cy + (kTile + kGap) * 0.5f;
    list.push_transform(scale, gold_cx, gold_cy, (logo_x - gold_cx) * leave,
                        (logo_y - gold_cy) * leave);
    const auto landed = [&](int i)
    {
        if (still)
            return tween::clamp01(t / 0.35f);
        return tween::stagger(t, i, 0.09f, 0.55f);
    };
    for (int i = 0; i < 4; ++i)
    {
        const float k = landed(i);
        if (k <= 0.0f)
            continue;
        const float pop = still ? 1.0f : tween::back_out(k);
        const float x = cx + (i % 2 ? kGap * 0.5f : -kTile - kGap * 0.5f);
        const float y = cy + (i / 2 ? kGap * 0.5f : -kTile - kGap * 0.5f) -
                        36.0f * (1.0f - k) * (still ? 0.0f : 1.0f);
        const Rect r{x, y, kTile, kTile};
        const float alpha = tween::clamp01(k * 1.6f);
        list.push_transform(0.62f + 0.38f * pop, r.cx(), r.cy(), 0, 0);
        if (i < 3)
        {
            // Glass: a teal rim, a faint fill, light pooling at the top. Fades as the mark leaves.
            const float a = alpha * (1.0f - tween::smoothstep(intro_leave_ * 2.2f));
            list.glow(r.inset(-6.0f), kRadius + 6.0f, 46, teal.with_alpha(0.32f * a));
            list.bordered_rect(r, kRadius, teal.with_alpha(0.14f * a), 3.5f,
                               teal.with_alpha(0.95f * a));
            list.gradient_rect({r.x + 8.0f, r.y + 8.0f, r.w - 16.0f, r.h * 0.45f}, kRadius - 8.0f,
                               kWhite.with_alpha(0.22f * a), kWhite.with_alpha(0.0f));
        }
        else
        {
            // Gold, turning into the top bar's diamond as it flies there.
            const float turn = 0.7854f * leave;
            list.glow(r.inset(-10.0f), kRadius + 10.0f, 70,
                      gold.with_alpha((0.34f + 0.12f * breathe) * alpha));
            if (leave > 0.0f)
                list.rotated_rect(r, kRadius, turn, gold.with_alpha(alpha));
            else
            {
                list.gradient_rect(r, kRadius, gold.with_alpha(alpha), gold_deep.with_alpha(alpha));
                // A sheen sweeps across once it has landed, then every few seconds.
                const float sweep = still ? -1.0f : std::fmod(t - 0.75f, 3.2f) / 0.7f;
                if (t > 0.75f && sweep >= 0.0f && sweep <= 1.0f)
                {
                    list.push_clip(r.inset(2.0f));
                    const float sx = r.x - 60.0f + (r.w + 120.0f) * tween::cubic_in_out(sweep);
                    list.rotated_rect({sx - 14.0f, r.y - 40.0f, 28.0f, r.h + 80.0f}, 4.0f, 0.42f,
                                      kWhite.with_alpha(0.35f * alpha));
                    list.pop_clip();
                }
            }
            const float hole = 46.0f;
            list.rotated_rect({r.cx() - hole * 0.5f, r.cy() - hole * 0.5f, hole, hole}, 9.0f,
                              0.7854f, Color::rgb(0x08111f).with_alpha(alpha));
        }
        list.pop_transform();
    }
    list.pop_transform();

    // The name, spacing in under the mark, then where the apps come from.
    list.push_opacity(1.0f - tween::smoothstep(intro_leave_ * 2.0f));
    const float name = still ? 1.0f : tween::cubic_out(tween::clamp01((t - 0.45f) / 0.7f));
    if (name > 0.0f)
        ui::text(list, fonts.semibold, "PROSPEROSTORE", cx, centred(660.0f, 40), 40,
                 kInk.with_alpha(name), gfx::Align::center, tween::lerp(26.0f, 11.0f, name));
    const float source = still ? 1.0f : tween::smoothstep((t - 0.8f) / 0.5f);
    if (source > 0.0f)
        ui::text(list, fonts.semibold, "prosperostore.net", cx, centred(716.0f, 24), 24,
                 gold.with_alpha(0.92f * source), gfx::Align::center, 2.0f);
    // A longer wait says what it is waiting for, with a line of light running along a track.
    const float waiting = tween::smoothstep((t - 2.2f) / 0.5f);
    if (waiting > 0.0f && !intro_leaving_)
    {
        const Rect track{cx - 110.0f, 790.0f, 220.0f, 3.0f};
        list.rounded_rect(track, 1.5f, kInk.with_alpha(0.14f * waiting));
        const float run = still ? 0.5f : std::fmod(t * 0.8f, 1.0f);
        const float head = track.x + (track.w + 80.0f) * run - 80.0f;
        list.push_clip(track);
        list.gradient_rect_h({head, track.y, 80.0f, 3.0f}, 1.5f, teal.with_alpha(0.0f),
                             teal.with_alpha(0.9f * waiting));
        list.pop_clip();
        ui::text(list, fonts.regular, "Loading the catalog", cx, centred(834.0f, 20), 20,
                 kInk.with_alpha(0.5f * waiting), gfx::Align::center);
    }
    list.pop_opacity();
}

void Screen::draw(gfx::Renderer &renderer, const ui::Fonts &fonts)
{
    layout(fonts);
    scene_.clear();
    overlay_.clear();
    const float page = page_.value;
    const Rect full{0, 0, kWidth, kHeight};

    // Anything opened over the home page pushes it back a little.
    scene_.push_transform(1.0f - 0.03f * page, 960, 540, 0, 0);
    if (discover())
        draw_stage(fonts);
    else
    {
        // The focused app's key art, far back: the room takes its colour.
        const float fade = stage_fade_.running ? tween::smoothstep(stage_fade_.progress()) : 1.0f;
        if (stage_previous_ >= 0 && fade < 1.0f)
            draw_art(scene_, stage_previous_, full, 0.4f * (1.0f - fade), 0);
        draw_art(scene_, stage_, full, 0.4f * (stage_previous_ >= 0 ? fade : 1.0f), 0);
        scene_.rounded_rect(full, 0, kDeep.with_alpha(0.62f));
        scene_.gradient_rect_h({0, 0, 1200.0f, kHeight}, 0, kDeep.with_alpha(0.5f),
                               kDeep.with_alpha(0.0f));
        draw_section_header(fonts);
    }
    scene_.push_clip({0, kPageTop, kWidth, kHeight - kPageTop});
    draw_grid(fonts);
    scene_.pop_clip();
    draw_top_bar(fonts);
    scene_.pop_transform();

    const auto glyphs = ui::GlyphStyle::dark();
    const bool all = section_ == 6 && !visible_.empty() && installer_ && guard_;
    ui::Hint home[8];
    int homes = 0;
    if (!visible_.empty())
        home[homes++] = {ui::Button::cross, "Details"};
    home[homes++] = {all ? ui::Button::square : ui::Button::triangle,
                     all ? "Update all" : "Search"};
    if (!discover())
        home[homes++] = {ui::Button::right_stick, "Sort"};
    if (categories_here().size() >= 2)
        home[homes++] = {ui::Button::l2, "Category", ui::Button::r2};
    if (!all)
        home[homes++] = {ui::Button::square, "Downloads"};
    home[homes++] = {ui::Button::l1, "Sections", ui::Button::r1};
    if (!developer_.empty())
        home[homes++] = {ui::Button::circle, "All apps"};
    else
        home[homes++] = {ui::Button::circle, "Hold to close"};
    // The page's row names the one thing Cross does for this app, when it can.
    const Offer state = focused() ? offer(*focused()) : Offer{};
    ui::Hint detail[8];
    int details = 0;
    if (state.armed)
        detail[details++] = {ui::Button::cross, state.label.c_str()};
    if (state.uninstall && installer_ && guard_ && !state.busy)
        detail[details++] = {ui::Button::square, "Uninstall"};
    // With comments on the page R2 jumps to them; the row then has no room to also say that
    // the D-pad scrolls when Uninstall is on it.
    const bool jump = details_ && comments_block_ >= 0;
    if (!jump || details < 2)
        detail[details++] = {ui::Button::dpad, "Scroll"};
    if (jump)
        detail[details++] = {ui::Button::r2, at_comments_ ? "Top" : "Comments"};
    if (focused() && !focused()->local_only)
        detail[details++] = {ui::Button::triangle, "Refresh"};
    if (focused() && !focused()->local_only && !focused()->author.empty())
        detail[details++] = {ui::Button::right_stick, "More by this developer"};
    if (focused() && !discussion_of(*focused()).empty())
        detail[details++] = {ui::Button::touchpad, "Like or comment"};
    detail[details++] = {ui::Button::circle, "Back"};
    // A hint row leaves as the next layer arrives, so two are never legible at once.
    scene_.push_opacity(1.0f - tween::clamp01(page * 3.0f));
    ui::draw_hints(scene_, fonts, glyphs, home, homes, kRight, true);
    scene_.pop_opacity();

    const std::uint32_t glass = renderer.glass_texture();
    draw_page(fonts, glass);
    if (page > 0.004f)
    {
        overlay_.push_opacity(tween::clamp01((page - 0.4f) / 0.6f));
        ui::draw_hints(overlay_, fonts, glyphs, detail, details, kRight, true);
        overlay_.pop_opacity();
    }
    draw_job_card(fonts, glass);
    draw_discussion(fonts, glass);
    draw_panel(fonts, glass);
    ui::Canvas canvas{overlay_, fonts, glass, time_};
    if (notes_open_)
    {
        // The release notes, over everything but the question they came from.
        overlay_.rounded_rect({0.0f, 0.0f, 1920.0f, 1080.0f}, 0,
                              gfx::Color{0.0f, 0.0f, 0.0f, 0.95f});
        notes_.draw(canvas);
        const ui::Hint reading[] = {{ui::Button::cross, "Update now"},
                                    {ui::Button::dpad, "Scroll"},
                                    {ui::Button::circle, "Back"}};
        ui::draw_hints(overlay_, fonts, glyphs, reading, 3, kRight, true);
    }
    dialog_.draw(canvas);
    toasts_.draw(canvas);
    if (close_hold_ > kCloseDelay)
    {
        // The bar that fills while Circle is held; full, the store closes.
        const float filled = tween::clamp01((close_hold_ - kCloseDelay) / kCloseHold);
        const float shown = tween::clamp01((close_hold_ - kCloseDelay) / 0.12f);
        const char *words = "Keep holding to close ProsperoStore";
        const float width = fonts.semibold.measure(words, 24) + 132.0f;
        const Rect pill{(kWidth - width) * 0.5f, 842.0f, width, 76.0f};
        overlay_.push_opacity(shown);
        overlay_.shadow({pill.x, pill.y + 12.0f, pill.w, pill.h}, 38, 40, kBlack.with_alpha(0.5f));
        overlay_.rounded_rect(pill, 38, gfx::mix(kCoal, kDeep, 0.5f).with_alpha(0.96f));
        overlay_.bordered_rect(pill, 38, kClear, 1.5f, kInk.with_alpha(0.22f));
        ui::draw_button(overlay_, fonts, glyphs, ui::Button::circle, pill.x + 30.0f, pill.y + 32.0f,
                        30);
        ui::text(overlay_, fonts.semibold, words, pill.x + 78.0f, centred(pill.y + 32.0f, 24), 24,
                 kInk);
        const Rect track{pill.x + 34.0f, pill.y + 58.0f, pill.w - 68.0f, 6.0f};
        overlay_.rounded_rect(track, 3, kInk.with_alpha(0.16f));
        overlay_.rounded_rect({track.x, track.y, std::max(6.0f, track.w * filled), track.h}, 3,
                              kAccent);
        overlay_.pop_opacity();
    }
    if (intro_showing())
        draw_intro(fonts);

    // Farlight's clouds, leaning toward the focused app's colour.
    const Color tint = tint_.value();
    gfx::BackdropSpec backdrop;
    backdrop.mode = gfx::BackdropMode::aurora;
    backdrop.colors[0] = gfx::mix(kDeep, Color::rgb(0x05060c), 0.35f);
    backdrop.colors[1] = gfx::mix(kDeep, tint, 0.3f);
    backdrop.colors[2] = gfx::mix(kMid, tint, 0.6f);
    backdrop.colors[3] = gfx::mix(tint, kAccent, 0.2f);
    backdrop.time = time_;
    renderer.begin();
    renderer.backdrop(backdrop);
    renderer.draw(scene_);
    if (!overlay_.empty())
    {
        renderer.glass();
        renderer.draw(overlay_);
    }
}

// ---- settings ---------------------------------------------------------------

std::string format_settings(const Settings &settings)
{
    return "location=" + settings.location + "\nupdates=" + (settings.check_updates ? "1" : "0") +
           "\nsounds=" + (settings.sounds ? "1" : "0") +
           "\nvibration=" + (settings.vibration ? "1" : "0") +
           "\nmotion=" + (settings.reduce_motion ? "reduced" : "full") +
           "\ndebug=" + (settings.debug_log ? "1" : "0") + "\ncatalog_url=" + settings.catalog_url +
           "\nverify_signatures=" + (settings.verify_signatures ? "1" : "0") +
           "\nuse_official=" + (settings.use_official ? "1" : "0") +
           "\nuse_custom=" + (settings.use_custom ? "1" : "0") +
           "\nnews_seen=" + settings.news_seen +
           "\nshow_comments=" + (settings.show_comments ? "1" : "0") + "\n";
}

Settings parse_settings(std::string_view text)
{
    Settings settings;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        const auto equals = line.find('=');
        if (equals == line.npos)
            continue;
        const auto key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "location" && system::clean_absolute_path(value))
            settings.location = value;
        else if (key == "updates")
            settings.check_updates = value != "0";
        else if (key == "sounds")
            settings.sounds = value != "0";
        else if (key == "vibration")
            settings.vibration = value != "0";
        else if (key == "motion")
            settings.reduce_motion = value == "reduced";
        else if (key == "debug")
            settings.debug_log = value == "1";
        else if (key == "catalog_url")
            catalog::normalize_api(value, settings.catalog_url);
        else if (key == "verify_signatures")
            settings.verify_signatures = value != "0";
        else if (key == "use_official")
            settings.use_official = value != "0";
        else if (key == "use_custom")
            settings.use_custom = value == "1";
        else if (key == "news_seen" && value.size() <= 64)
            settings.news_seen = value;
        else if (key == "show_comments")
            settings.show_comments = value == "1";
    }
    // A custom catalog with no address is not one: the official catalog alone.
    if (!settings.custom_active())
    {
        settings.use_official = true;
        settings.use_custom = false;
    }
    return settings;
}

void Screen::catalog_url_cancelled()
{
    if (settings_.use_custom && settings_.catalog_url == catalog::kDefaultApi)
    {
        settings_.use_custom = false;
        settings_.use_official = true;
        settings_changed = true;
        notify("Custom catalog is off", "No address was entered.");
    }
}

void Screen::set_catalog_url(std::string_view value)
{
    // As it was typed: spaces and line ends around it go, and a bare host gets https://.
    const auto blank = [](char c) { return static_cast<unsigned char>(c) <= 32; };
    while (!value.empty() && blank(value.front()))
        value.remove_prefix(1);
    while (!value.empty() && blank(value.back()))
        value.remove_suffix(1);
    std::string typed(value);
    if (typed.empty() || typed == "https://")
        return catalog_url_cancelled();
    if (typed.find("://") == typed.npos)
        typed = "https://" + typed;
    std::string url;
    if (!catalog::normalize_api(typed, url))
    {
        notify("Not a catalog address",
               "Use an HTTPS address such as https://example.com/api/v1/. Nothing was changed.");
        return catalog_url_cancelled();
    }
    if (url == catalog::kDefaultApi)
    {
        // The official address is the Official catalog switch, not a custom catalog.
        notify("That is the official catalog",
               "Enter your own feed's address. prosperostore.net has its own switch.");
        return catalog_url_cancelled();
    }
    settings_.catalog_url = std::move(url);
    settings_.use_custom = true;
    settings_changed = true;
    notify("Custom catalog saved", settings_.catalog_url + " - close and reopen the store.");
}

void Screen::set_locations(std::vector<std::pair<std::string, std::uint64_t>> locations)
{
    locations_ = std::move(locations);
    // A saved location that isn't offered on this console gives way to the first that is.
    const auto offered = [&](const auto &place) { return place.first == settings_.location; };
    if (!locations_.empty() && std::none_of(locations_.begin(), locations_.end(), offered))
        settings_.location = locations_.front().first;
}

const App *Screen::self_app() const
{
    for (const auto &app : apps_)
        if (app.title_id == self_id_)
            return &app;
    return nullptr;
}

// ---- the panel: Queue, Settings, About ----------------------------------------

void Screen::open_panel(int tab)
{
    panel_ = true;
    development_options_ = false;
    setting_focus_ = 0;
    panel_tab_ = std::clamp(tab, 0, 3);
    queue_focus_ = 0;
    if (panel_tab_ == 2)
        write_about();
    if (panel_tab_ == 3 && !news_.empty() && settings_.news_seen != news_.front().id)
    {
        // Read: the notice for this item is not shown again.
        settings_.news_seen = news_.front().id;
        settings_changed = true;
    }
}

void Screen::set_news(std::vector<catalog::News> news)
{
    news_ = std::move(news);
    write_news();
    if (!news_.empty() && settings_.news_seen != news_.front().id)
        notify("News: " + news_.front().title, "Press Options and open News to read it.");
}

void Screen::write_news()
{
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    for (const auto &item : news_)
    {
        if (!blocks.empty())
            blocks.push_back(Block::divider());
        blocks.push_back(Block::heading(item.title, 2));
        blocks.push_back(Block::heading(item.date.substr(0, item.date.find('T')), 3));
        // A blank line starts a new paragraph.
        std::size_t at = 0;
        while (at < item.body.size())
        {
            auto end = item.body.find("\n\n", at);
            if (end == std::string::npos)
                end = item.body.size();
            if (end > at)
                blocks.push_back(Block::paragraph(item.body.substr(at, end - at)));
            at = end + 2;
        }
    }
    if (blocks.empty())
        blocks.push_back(Block::paragraph("No news from the catalog yet."));
    news_view_.set_content(std::move(blocks));
    news_view_.scroll_to(0, true);
}

void Screen::write_about()
{
    using Block = ui::TextBlock;
    std::vector<Block> blocks;
    if (!setup_notes_.empty())
    {
        // What the store found in /data/shadowmount and what it did about it.
        blocks.push_back(Block::heading("ShadowMountPlus settings to check", 3));
        blocks.push_back(Block::paragraph(
            "Found in /data/shadowmount/config.ini and manual.lst. The store keeps working "
            "where it says so; fixing the lines removes this note."));
        for (const auto &line : setup_notes_)
            blocks.push_back(Block::bullet(line));
    }
    if (!debug_lines_.empty())
    {
        // The debug build: what happened at each step, to photograph or send.
        blocks.push_back(Block::heading("Debug trace", 3));
        blocks.push_back(Block::paragraph(
            debug_file_.empty()
                ? std::string("Not saved to a file: no writable /data or USB drive.")
                : "Also saved to " + debug_file_ + "."));
        for (const auto &line : debug_lines_)
            blocks.push_back(Block::bullet(line));
    }
    blocks.push_back(Block::paragraph("ProsperoStore installs, updates and uninstalls apps from "
                                      "your selected homebrew catalog."));
    blocks.push_back(
        Block::key_value("Version", self_version_.empty() ? "Unknown" : self_version_));
    blocks.push_back(Block::key_value("Catalog", status_));
    blocks.push_back(Block::key_value("Active API URL", active_catalog_url_));
    blocks.push_back(Block::key_value("New apps go to", settings_.location));
    blocks.push_back(Block::heading("How it keeps installs safe", 3));
    blocks.push_back(
        Block::bullet("Signature verification is on by default. Turning it off trusts the "
                      "selected feed's publisher without authenticating its catalog."));
    blocks.push_back(
        Block::bullet("A download is checked against the catalog before it is unpacked."));
    blocks.push_back(
        Block::bullet("An app's folder is replaced in one step, only when the new one is "
                      "complete. A running app is never touched."));
    blocks.push_back(Block::heading("Please note", 3));
    blocks.push_back(Block::paragraph(
        "Apps are published by their own developers, who are responsible for their content "
        "and licensing. A listing is not a security audit. ProsperoStore and the catalog come "
        "without warranty."));
    blocks.push_back(Block::heading("Where things are", 3));
    blocks.push_back(Block::key_value("Store files", "/data/prosperostore"));
    blocks.push_back(Block::key_value("Logs", "/data/prosperostore/logs"));
    blocks.push_back(Block::heading("Thanks", 3));
    blocks.push_back(Block::paragraph(
        "ShadowMountPlus by drakmor puts installed apps on the home screen. Built with "
        "ps5-opengl, the Homebrew UI Lab and the PS5 native app boilerplate, and with curl, "
        "OpenSSL, zlib, miniz, Monocypher, yyjson, PicoSHA2, QR Code generator and stb. "
        "Fonts: Inter, Montserrat and DejaVu Sans Mono."));
    blocks.push_back(
        Block::paragraph("Brought to you by BlackBearReloaded. Free software under the GPL, "
                         "version 3 or later."));
    about_.set_content(std::move(blocks));
    about_.scroll_to(0, true);
}

void Screen::update_panel(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back) || input.is_pressed(Action::menu))
    {
        if (development_options_ && input.is_pressed(Action::back))
        {
            development_options_ = false;
            setting_focus_ = 7;
            feedback.play(audio::Cue::back);
            return;
        }
        panel_ = false;
        feedback.play(audio::Cue::back);
        return;
    }
    if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
    {
        // The rooms go round, like the sections.
        const int next = (panel_tab_ + (input.is_pressed(Action::page_next) ? 1 : 3)) % 4;
        open_panel(next);
        feedback.play(audio::Cue::tab, 0.96f + 0.04f * static_cast<float>(next));
        return;
    }
    const int step = input.nav == Direction::down ? 1 : input.nav == Direction::up ? -1 : 0;
    if (panel_tab_ == 0)
    {
        // What is running first, then what waits, in order.
        std::vector<std::string> ids;
        if (!activity_.id.empty())
            ids.push_back(activity_.id);
        ids.insert(ids.end(), activity_.waiting.begin(), activity_.waiting.end());
        const int count = static_cast<int>(ids.size());
        queue_focus_ = std::clamp(queue_focus_, 0, std::max(0, count - 1));
        if (step && count)
        {
            const int next = queue_focus_ + step;
            if (next < 0 || next >= count)
                return refuse(feedback, input.nav_repeat, 0.0f, static_cast<float>(step));
            queue_focus_ = next;
            feedback.play(audio::Cue::focus);
        }
        else if (input.is_pressed(Action::confirm) && count)
        {
            pending_order = {};
            pending_order.kind = Order::Kind::cancel;
            pending_order.id = ids[static_cast<std::size_t>(queue_focus_)];
            feedback.play(audio::Cue::select);
        }
        return;
    }
    if (panel_tab_ == 2)
    {
        about_.handle(input, feedback);
        return;
    }
    if (panel_tab_ == 3)
    {
        news_view_.handle(input, feedback);
        return;
    }
    const int kRows = development_options_ ? 5 : 9;
    if (step)
    {
        const int next = setting_focus_ + step;
        if (next < 0 || next >= kRows)
            return refuse(feedback, input.nav_repeat, 0.0f, static_cast<float>(step));
        setting_focus_ = next;
        feedback.play(audio::Cue::focus);
        return;
    }
    const int turn = input.nav == Direction::right || input.is_pressed(Action::confirm) ? 1
                     : input.nav == Direction::left                                     ? -1
                                                                                        : 0;
    if (!turn)
        return;
    const int setting = development_options_  ? setting_focus_ + 5
                        : setting_focus_ < 5  ? setting_focus_
                        : setting_focus_ == 5 ? 13
                        : setting_focus_ == 6 ? 12
                                              : setting_focus_ + 3;
    if (setting == 13)
    {
        // Off: nothing more is read from GitHub, and what was read is forgotten at once.
        settings_.show_comments = !settings_.show_comments;
        if (!settings_.show_comments)
            forget_social();
    }
    else if (setting == 12)
    {
        settings_.debug_log = !settings_.debug_log;
        if (settings_.debug_log)
            notify("Debug log is on", "Repeat the problem, then open About.");
    }
    else if (setting == 11)
    {
        if (input.is_pressed(Action::confirm))
        {
            development_options_ = true;
            setting_focus_ = 0;
            feedback.play(audio::Cue::open);
        }
        return;
    }
    else if (setting == 0)
    {
        if (locations_.size() < 2)
            return refuse(feedback, input.nav_repeat, static_cast<float>(turn), 0.0f);
        const auto current =
            std::find_if(locations_.begin(), locations_.end(),
                         [&](const auto &place) { return place.first == settings_.location; });
        const auto count = static_cast<int>(locations_.size());
        const int index =
            current == locations_.end() ? 0 : static_cast<int>(current - locations_.begin());
        settings_.location =
            locations_[static_cast<std::size_t>((index + turn + count) % count)].first;
    }
    else if (setting == 1)
        settings_.check_updates = !settings_.check_updates;
    else if (setting == 2)
        settings_.sounds = !settings_.sounds;
    else if (setting == 3)
        settings_.vibration = !settings_.vibration;
    else if (setting == 4)
        settings_.reduce_motion = !settings_.reduce_motion;
    else if (setting == 5)
    {
        // The official catalog can only go while a custom one is in use.
        if (settings_.use_official && !settings_.custom_active())
        {
            notify("Switch a custom catalog on first",
                   "The store needs one catalog: set a custom catalog before switching "
                   "prosperostore.net off.");
            return refuse(feedback, input.nav_repeat, 0.0f, 0.0f);
        }
        settings_.use_official = !settings_.use_official;
        notify("Catalog settings saved", "Close and reopen ProsperoStore to apply.");
    }
    else if (setting == 6)
    {
        settings_.use_custom = !settings_.use_custom;
        if (settings_.use_custom && settings_.catalog_url == catalog::kDefaultApi)
            pending_catalog_url = true; // no address yet: ask for it now
        if (!settings_.custom_active())
            settings_.use_official = true;
        notify("Catalog settings saved", settings_.use_custom
                                             ? "Close and reopen ProsperoStore to add your catalog."
                                             : "Close and reopen ProsperoStore to apply.");
    }
    else if (setting == 7)
    {
        if (input.is_pressed(Action::confirm))
            pending_catalog_url = true;
        return;
    }
    else if (setting == 8)
    {
        if (settings_.verify_signatures)
        {
            if (!input.is_pressed(Action::confirm))
                return;
            ask_ = Ask::disable_signatures;
            dialog_.open({ui::StatusKind::question,
                          "Disable catalog signature checks?",
                          "Only do this for a feed you trust. Its publisher can choose which "
                          "apps and updates you install. Download hashes are still checked, "
                          "but they do not authenticate the publisher. Applies after reopening "
                          "the store.",
                          {{"Cancel"}, {"Disable checks", ui::ButtonKind::primary}}},
                         feedback);
            return;
        }
        settings_.verify_signatures = true;
        notify("Catalog settings saved", "Signature checks will resume after reopening the store.");
    }
    else if (setting == 9)
    {
        if (!input.is_pressed(Action::confirm))
            return;
        settings_.verify_signatures = true;
        settings_.use_official = true;
        settings_.use_custom = false;
        settings_.catalog_url = catalog::kDefaultApi;
        notify("Official catalog restored", "Close and reopen ProsperoStore to apply.");
    }
    else
    {
        // The store's own page says what can be done about its version.
        if (!input.is_pressed(Action::confirm) || !self_app())
            return refuse(feedback, input.nav_repeat, 0.0f, 0.0f);
        panel_ = false;
        open_app(self_id_);
        feedback.play(audio::Cue::open);
        return;
    }
    settings_changed = true;
    feedback.play(audio::Cue::toggle);
}

namespace
{
// Small drawn signs for the panel's rows and side list, in one ink.
void sign(gfx::DrawList &list, int which, float cx, float cy, Color ink)
{
    switch (which)
    {
    case 0: // downloads: an arrow into a tray
        list.line(cx, cy - 13.0f, cx, cy + 5.0f, 3.0f, ink);
        list.line(cx - 8.0f, cy - 3.0f, cx, cy + 5.0f, 3.0f, ink);
        list.line(cx + 8.0f, cy - 3.0f, cx, cy + 5.0f, 3.0f, ink);
        list.line(cx - 13.0f, cy + 12.0f, cx + 13.0f, cy + 12.0f, 3.0f, ink);
        break;
    case 1: // settings: three sliders
        for (int i = 0; i < 3; ++i)
        {
            const float y = cy - 10.0f + 10.0f * static_cast<float>(i);
            list.line(cx - 13.0f, y, cx + 13.0f, y, 2.5f, ink.with_alpha(ink.a * 0.55f));
            list.circle(cx - 7.0f + 7.0f * static_cast<float>((i * 2) % 3), y, 4.0f, ink);
        }
        break;
    case 2: // about: an i in a ring
        list.ring(cx, cy, 13.0f, 2.5f, ink);
        list.circle(cx, cy - 6.0f, 2.2f, ink);
        list.line(cx, cy - 1.0f, cx, cy + 7.0f, 3.0f, ink);
        break;
    case 3: // a drive
        list.bordered_rect({cx - 14.0f, cy - 9.0f, 28.0f, 18.0f}, 4, kClear, 2.5f, ink);
        list.circle(cx + 7.0f, cy + 2.0f, 2.2f, ink);
        list.line(cx - 9.0f, cy + 2.0f, cx + 1.0f, cy + 2.0f, 2.0f, ink.with_alpha(ink.a * 0.6f));
        break;
    case 4: // a newer version: an arrow going round
        list.arc(cx, cy, 11.0f, 2.5f, -0.9f, 4.6f, ink);
        list.line(cx + 6.0f, cy - 13.0f, cx + 12.0f, cy - 7.0f, 2.5f, ink);
        list.line(cx + 12.0f, cy - 7.0f, cx + 4.0f, cy - 4.0f, 2.5f, ink);
        break;
    case 5: // sound: a speaker
        list.rounded_rect({cx - 13.0f, cy - 5.0f, 7.0f, 10.0f}, 1.5f, ink);
        {
            const float xy[] = {cx - 7.0f, cy - 5.0f,  cx + 1.0f, cy - 11.0f,
                                cx + 1.0f, cy + 11.0f, cx - 7.0f, cy + 5.0f};
            list.polygon(xy, 4, ink);
        }
        list.arc(cx + 3.0f, cy, 7.0f, 2.2f, -0.8f, 1.6f, ink);
        list.arc(cx + 3.0f, cy, 12.0f, 2.2f, -0.8f, 1.6f, ink.with_alpha(ink.a * 0.6f));
        break;
    case 6: // vibration: a pad between two waves
        list.bordered_rect({cx - 8.0f, cy - 11.0f, 16.0f, 22.0f}, 4, kClear, 2.5f, ink);
        list.line(cx - 14.0f, cy - 5.0f, cx - 14.0f, cy + 5.0f, 2.5f, ink.with_alpha(ink.a * 0.6f));
        list.line(cx + 14.0f, cy - 5.0f, cx + 14.0f, cy + 5.0f, 2.5f, ink.with_alpha(ink.a * 0.6f));
        break;
    case 8: // motion: a dot that has stopped, its trail fading
        list.circle(cx + 7.0f, cy, 6.0f, ink);
        list.line(cx - 14.0f, cy - 6.0f, cx - 3.0f, cy - 6.0f, 2.5f, ink.with_alpha(ink.a * 0.35f));
        list.line(cx - 16.0f, cy, cx - 4.0f, cy, 2.5f, ink.with_alpha(ink.a * 0.6f));
        list.line(cx - 14.0f, cy + 6.0f, cx - 3.0f, cy + 6.0f, 2.5f, ink.with_alpha(ink.a * 0.35f));
        break;
    case 9: // likes and comments: a speech bubble
        list.bordered_rect({cx - 13.0f, cy - 12.0f, 26.0f, 19.0f}, 5, kClear, 2.5f, ink);
        list.triangle({cx - 8.0f, cy + 5.0f, 9.0f, 8.0f}, ink, 0.0f, 3.14159f);
        list.line(cx - 6.0f, cy - 3.0f, cx + 6.0f, cy - 3.0f, 2.0f, ink.with_alpha(ink.a * 0.6f));
        break;
    case 30: // news: a page with lines
        list.bordered_rect({cx - 11.0f, cy - 13.0f, 22.0f, 26.0f}, 4, kClear, 2.5f, ink);
        list.line(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy - 5.0f, 2.5f, ink);
        list.line(cx - 5.0f, cy + 1.0f, cx + 5.0f, cy + 1.0f, 2.0f, ink.with_alpha(ink.a * 0.6f));
        list.line(cx - 5.0f, cy + 6.0f, cx + 2.0f, cy + 6.0f, 2.0f, ink.with_alpha(ink.a * 0.6f));
        break;
    default: // the store: its diamond
        list.rotated_rect({cx - 11.0f, cy - 11.0f, 22.0f, 22.0f}, 5.0f, 0.7854f, ink);
        list.rotated_rect({cx - 5.0f, cy - 5.0f, 10.0f, 10.0f}, 2.0f, 0.7854f, kDeep);
        break;
    }
}

// A switch: a track that fills with the accent and a knob that travels.
void toggle(gfx::DrawList &list, float right, float cy, bool on)
{
    const Rect track{right - 68.0f, cy - 19.0f, 68.0f, 38.0f};
    list.rounded_rect(track, 19, on ? kAccent : kInk.with_alpha(0.18f));
    const float knob = on ? track.x + track.w - 19.0f : track.x + 19.0f;
    list.shadow({knob - 14.0f, cy - 12.0f, 28.0f, 28.0f}, 14, 8, kBlack.with_alpha(0.35f));
    list.circle(knob, cy, 14.0f, on ? kOnAccent : kInk);
}
} // namespace

void Screen::draw_panel(const ui::Fonts &fonts, std::uint32_t glass)
{
    const float t = panel_value_.value;
    if (t <= 0.004f)
        return;
    auto &list = overlay_;
    const float veil = tween::clamp01(t * 1.6f);
    list.glass(glass, {0, 0, kWidth, kHeight}, 0, kWhite.with_alpha(veil));
    list.rounded_rect({0, 0, kWidth, kHeight}, 0,
                      gfx::mix(kCoal, kDeep, 0.5f).with_alpha(0.82f * veil));
    list.push_opacity(tween::smoothstep(t));
    list.push_transform(1.0f, 0, 0, 0, 24.0f * motion() * (1.0f - tween::cubic_out(t)));

    // The left: whose room this is, the room's name, and the four rooms.
    constexpr const char *kTabs[] = {"Downloads", "Settings", "About", "News"};
    ui::text(list, fonts.semibold, "PROSPEROSTORE", kMargin, 170.0f, 18, kAccent, gfx::Align::left,
             4.0f);
    ui::text(list, fonts.display, kTabs[panel_tab_], kMargin - 3.0f, 248.0f, 64, kInk);
    for (int i = 0; i < 4; ++i)
    {
        const Rect entry{kMargin, 312.0f + 72.0f * static_cast<float>(i), 400.0f, 60.0f};
        const bool on = i == panel_tab_;
        if (on)
        {
            list.rounded_rect(entry, 16, kInk.with_alpha(0.1f));
            list.rounded_rect({entry.x + 10.0f, entry.y + 16.0f, 4.0f, 28.0f}, 2, kAccent);
        }
        sign(list, i == 3 ? 30 : i, entry.x + 50.0f, entry.cy(),
             kInk.with_alpha(on ? 1.0f : 0.55f));
        ui::text(list, fonts.semibold, kTabs[i], entry.x + 86.0f, centred(entry.cy(), 24), 24,
                 kInk.with_alpha(on ? 1.0f : 0.6f));
        if (i == 3 && !news_.empty() && settings_.news_seen != news_.front().id)
            pill(list, fonts, "New", entry.x + entry.w - 76.0f, entry.cy(), 30.0f, kAccent,
                 kOnAccent, 1.0f, kAllLayers);
        if (i == 0 && (!activity_.id.empty() || !activity_.waiting.empty()))
        {
            const int jobs =
                (activity_.id.empty() ? 0 : 1) + static_cast<int>(activity_.waiting.size());
            pill(list, fonts, std::to_string(jobs), entry.x + entry.w - 52.0f, entry.cy(), 30.0f,
                 kAccent, kOnAccent, 1.0f, kAllLayers);
        }
    }
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::l1, kMargin, 628.0f, 26);
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::r1,
                    kMargin + ui::button_width(ui::Button::l1, 26) + 8.0f, 628.0f, 26);
    ui::text(list, fonts.regular, "Switch rooms",
             kMargin + 2.0f * ui::button_width(ui::Button::l1, 26) + 24.0f, centred(628.0f, 20), 20,
             kInk.with_alpha(0.5f));

    // The right: one glass panel with the room's content.
    const Rect panel{600.0f, 144.0f, kRight - 600.0f, 800.0f};
    list.shadow({panel.x, panel.y + 24.0f, panel.w, panel.h}, 32, 60, kBlack.with_alpha(0.45f));
    list.glass(glass, panel, 32, kWhite);
    list.rounded_rect(panel, 32, gfx::mix(kDeep, kMid, 0.14f).with_alpha(0.6f));
    list.gradient_rect({panel.x, panel.y, panel.w, 140.0f}, 32, kInk.with_alpha(0.06f),
                       kInk.with_alpha(0.0f));
    list.bordered_rect(panel, 32, kClear, 1.5f, kInk.with_alpha(0.18f));
    const float px = panel.x + 40.0f, pw = panel.w - 80.0f;

    const auto app_of = [&](const std::string &id) -> const App *
    {
        for (const auto &app : apps_)
            if (app.title_id == id)
                return &app;
        return nullptr;
    };
    const auto row_focus = [&](const Rect &row)
    {
        list.rounded_rect(row, 20, kInk.with_alpha(0.08f));
        list.bordered_rect(row.inset(-4.0f), 24, kClear, 3.0f, kInk.with_alpha(0.94f));
    };
    if (panel_tab_ == 0)
    {
        std::vector<std::string> ids;
        if (!activity_.id.empty())
            ids.push_back(activity_.id);
        ids.insert(ids.end(), activity_.waiting.begin(), activity_.waiting.end());
        float y = panel.y + 36.0f;
        for (std::size_t i = 0; i < ids.size() && i < 4; ++i)
        {
            const Rect row{px - 16.0f, y, pw + 32.0f, 122.0f};
            const bool focused = static_cast<int>(i) == queue_focus_;
            if (focused)
                row_focus(row);
            const App *app = app_of(ids[i]);
            // A 16:9 thumbnail: the key art, else the icon on the app's colour.
            const Rect thumb{px, row.y + 16.0f, 160.0f, 90.0f};
            const Color accent = app ? app->accent : kMid;
            list.gradient_rect(thumb, 12, gfx::mix(accent, kMid, 0.25f),
                               gfx::mix(accent, kDeep, 0.85f));
            if (app && app->background)
                list.image(app->background, thumb, gfx::kFullUv, kWhite, 12);
            if (app && (!app->background || app->ambient) && art(*app))
                list.image(art(*app), {thumb.cx() - 30.0f, thumb.cy() - 30.0f, 60.0f, 60.0f},
                           gfx::kFullUv, kWhite, 13);
            list.bordered_rect(thumb, 12, kClear, 1.0f, kInk.with_alpha(0.18f));
            const bool working = i == 0 && !activity_.id.empty();
            float progress = -1.0f;
            const char *phase = "Waiting";
            if (working)
                busy_app(progress, phase);
            const float tx = thumb.x + thumb.w + 28.0f;
            ui::text(list, fonts.semibold,
                     fonts.semibold.font->fit(app ? app->name : ids[i], 28, 560.0f), tx,
                     row.y + 50.0f, 28, kInk);
            std::string line =
                working ? phase : "Waiting  \xC2\xB7  " + std::to_string(i) + " ahead";
            if (working && progress >= 0.0f)
            {
                line += "  \xC2\xB7  " + size_text(activity_.done) + " of " +
                        size_text(activity_.total);
                if (const auto left = time_left(); !left.empty())
                    line += "  \xC2\xB7  " + left;
            }
            ui::text(list, fonts.regular, fonts.regular.font->fit(line, 21, 620.0f), tx,
                     row.y + 84.0f, 21, kInk.with_alpha(0.66f));
            const Rect track{px + pw - 300.0f, row.cy() - 3.0f, 220.0f, 6.0f};
            list.rounded_rect(track, 3, kInk.with_alpha(0.14f));
            if (progress >= 0.0f)
            {
                list.rounded_rect({track.x, track.y, std::max(6.0f, track.w * progress), 6.0f}, 3,
                                  kAccent);
                ui::text(list, fonts.mono,
                         std::to_string(static_cast<int>(progress * 100.0f)) + "%", px + pw,
                         centred(row.cy(), 22), 22, kInk, gfx::Align::right);
            }
            else if (working)
            {
                const float k = std::fmod(time_ * 0.8f, 1.0f);
                list.rounded_rect({track.x + track.w * 0.7f * k, track.y, track.w * 0.3f, 6.0f}, 3,
                                  kAccent);
            }
            if (focused)
            {
                const float tw = fonts.semibold.measure("Cancel", 20);
                const Rect chip{px + pw - tw - 76.0f, row.y + 12.0f, tw + 76.0f, 36.0f};
                (void)chip;
            }
            y += 136.0f;
        }
        if (ids.empty())
        {
            sign(list, 0, panel.cx(), panel.y + 170.0f, kInk.with_alpha(0.5f));
            ui::text(list, fonts.semibold, "Nothing is being installed", panel.cx(),
                     panel.y + 236.0f, 30, kInk, gfx::Align::center);
            ui::text(list, fonts.regular,
                     "Installs, updates and removals line up here, one at a time.", panel.cx(),
                     panel.y + 276.0f, 22, kInk.with_alpha(0.6f), gfx::Align::center);
            y = panel.y + 330.0f;
        }
        // What finished since the store was opened, newest first, as a timeline.
        if (!history_.empty())
        {
            list.rounded_rect({px, y + 8.0f, pw, 1.0f}, 0, kInk.with_alpha(0.12f));
            ui::text(list, fonts.semibold, "FINISHED", px, y + 52.0f, 16, kInk.with_alpha(0.55f),
                     gfx::Align::left, 3.5f);
            y += 88.0f;
            const float line_x = px + 14.0f;
            int n = 0;
            for (auto done = history_.rbegin();
                 done != history_.rend() && y < panel.y + panel.h - 40.0f; ++done, ++n)
            {
                if (n > 0)
                    list.rounded_rect({line_x - 1.0f, y - 44.0f, 2.0f, 34.0f}, 1,
                                      kInk.with_alpha(0.16f));
                const Color tone = done->ok ? kOwned : Color::rgb(0xe5484d);
                list.circle(line_x, y - 2.0f, 13.0f, tone.with_alpha(0.18f));
                if (done->ok)
                    draw_check(list, line_x, y - 2.0f, 13.0f, tone);
                else
                {
                    list.line(line_x - 5.0f, y - 7.0f, line_x + 5.0f, y + 3.0f, 2.5f, tone);
                    list.line(line_x + 5.0f, y - 7.0f, line_x - 5.0f, y + 3.0f, 2.5f, tone);
                }
                ui::text(list, fonts.semibold, fonts.semibold.font->fit(done->title, 24, 520.0f),
                         px + 46.0f, y + 6.0f, 24, kInk);
                ui::text(list, fonts.regular, fonts.regular.font->fit(done->body, 21, pw - 600.0f),
                         px + 600.0f, y + 6.0f, 21, kInk.with_alpha(0.6f));
                y += 58.0f;
            }
        }
    }
    else if (panel_tab_ == 1)
    {
        const App *self = self_app();
        const bool newer =
            self && catalog::update_available(self_version_, self->available_version);
        std::string place = settings_.location;
        std::string room;
        for (const auto &[path, free] : locations_)
            if (path == settings_.location)
                room = size_text(free) + " free";
        const struct Row
        {
            int sign;
            const char *label, *note;
            int kind; // 0 value, 1 switch, 2 chip
            std::string value;
            bool on;
        } rows[] = {
            {3, "Install location", "Where new apps go: a folder ShadowMountPlus scans.", 0, place,
             true},
            {4, "Check for a newer ProsperoStore", "Asked once at start; a notice appears.", 1, "",
             settings_.check_updates},
            {5, "Sounds", "The interface's own sounds.", 1, "", settings_.sounds},
            {6, "Vibration", "A light answer from the controller.", 1, "", settings_.vibration},
            {8, "Reduce motion", "Nothing drifts, floats or slides; things fade instead.", 1, "",
             settings_.reduce_motion},
            {4, "Official catalog",
             "prosperostore.net, signed. With both on, its apps win a clash.", 1, "",
             settings_.use_official},
            {4, "Custom catalog", "Your own feed. With both on, its apps are added.", 1, "",
             settings_.use_custom},
            {4, "Custom catalog URL", "HTTPS API directory. Applies when the store next opens.", 3,
             settings_.catalog_url == catalog::kDefaultApi ? std::string("Not set")
                                                           : settings_.catalog_url,
             false},
            {4, "Verify custom catalog signatures",
             "The official catalog is always checked. Applies next launch.", 1, "",
             settings_.verify_signatures},
            {4, "Restore official catalog",
             "Only prosperostore.net, with signature checks, next launch.", 2, "Restore", false},
            {7, "ProsperoStore", "Its page updates it.", 2,
             restart_needed_ ? "Restart to finish"
             : newer         ? "Version " + self->available_version + " available"
                             : "Version " + self_version_ + (self ? ", up to date" : ""),
             newer || restart_needed_},
            {1, "Development options", "Custom catalogs and signature verification.", 2, "Open",
             false},
            {4, "Debug log", "Records every step for a report: see About, or debug-trace.txt.", 1,
             "", settings_.debug_log},
            {9, "Show likes and comments",
             "From each app's discussion on GitHub; read from GitHub while this is on.", 1, "",
             settings_.show_comments},
        };
        float y = panel.y + 32.0f;
        if (development_options_)
        {
            ui::text(list, fonts.display, "Development options", px, y + 40.0f, 36, kInk);
            ui::text(list, fonts.regular,
                     "Use your own feed for development. Changes apply next launch.", px, y + 78.0f,
                     21, kInk.with_alpha(0.6f));
            y += 112.0f;
        }
        const int first = std::max(0, setting_focus_ - 5);
        const int count = development_options_ ? 5 : 9;
        for (int focus = first; focus < std::min(first + 6, count); ++focus)
        {
            const int i = development_options_ ? focus + 5
                          : focus < 5          ? focus
                          : focus == 5         ? 13
                          : focus == 6         ? 12
                                               : focus + 3;
            const Rect row{px - 16.0f, y, pw + 32.0f, 108.0f};
            if (focus == setting_focus_)
                row_focus(row);
            else if (focus > first)
                list.rounded_rect({px, y - 8.0f, pw, 1.0f}, 0, kInk.with_alpha(0.08f));
            list.circle(px + 28.0f, row.cy(), 28.0f, kInk.with_alpha(0.08f));
            sign(list, rows[i].sign, px + 28.0f, row.cy(), kInk.with_alpha(0.9f));
            ui::text(list, fonts.semibold, rows[i].label, px + 82.0f, row.y + 50.0f, 26, kInk);
            ui::text(list, fonts.regular,
                     rows[i].kind == 3 ? fonts.regular.font->fit(rows[i].value, 20, pw - 220.0f)
                                       : rows[i].note,
                     px + 82.0f, row.y + 84.0f, 20, kInk.with_alpha(0.6f));
            const float right = px + pw;
            if (rows[i].kind == 1)
                toggle(list, right, row.cy(), rows[i].on);
            else if (rows[i].kind == 3)
                ui::text(list, fonts.semibold, "Edit", right - 16.0f, row.y + 50.0f, 24, kAccent,
                         gfx::Align::right);
            else if (rows[i].kind == 0)
            {
                // The place, the room there, and arrows: left and right choose.
                const float tw =
                    ui::text(list, fonts.semibold, rows[i].value, right - 40.0f,
                             centred(row.cy() - 12.0f, 24), 24, kAccent, gfx::Align::right);
                ui::text(list, fonts.regular, room, right - 40.0f, centred(row.cy() + 18.0f, 19),
                         19, kInk.with_alpha(0.6f), gfx::Align::right);
                const Color arrows =
                    locations_.size() > 1 ? kInk.with_alpha(0.8f) : kInk.with_alpha(0.25f);
                list.line(right - 14.0f, row.cy() - 8.0f, right - 6.0f, row.cy(), 2.5f, arrows);
                list.line(right - 14.0f, row.cy() + 8.0f, right - 6.0f, row.cy(), 2.5f, arrows);
                list.line(right - tw - 54.0f, row.cy() - 20.0f, right - tw - 62.0f,
                          row.cy() - 12.0f, 2.5f, arrows);
                list.line(right - tw - 54.0f, row.cy() - 4.0f, right - tw - 62.0f, row.cy() - 12.0f,
                          2.5f, arrows);
            }
            else
                pill(list, fonts, rows[i].value,
                     right - fonts.semibold.measure(rows[i].value, 40.0f * 0.58f) - 36.0f, row.cy(),
                     40.0f, rows[i].on ? kAccent : kInk.with_alpha(0.12f),
                     rows[i].on ? kOnAccent : kInk, 1.0f, kAllLayers);
            y += 106.0f;
        }
        ui::text(list, fonts.regular, "Changes are saved as you make them.", kMargin, 640.0f, 20,
                 kInk.with_alpha(0.5f));
        ui::text(list, fonts.regular, "Catalog changes apply next launch.", kMargin, 674.0f, 20,
                 kInk.with_alpha(0.5f));
        ui::text(list, fonts.regular,
                 std::to_string(setting_focus_ + 1) + " / " + std::to_string(count), kMargin,
                 716.0f, 20, kInk.with_alpha(0.5f));
    }
    else if (panel_tab_ == 3)
    {
        // News: what the catalog's maintainers wrote, newest first.
        ui::text(list, fonts.semibold, "FROM THE CATALOG", px, panel.y + 52.0f, 16,
                 kInk.with_alpha(0.55f), gfx::Align::left, 3.0f);
        ui::Canvas canvas{list, fonts, glass, time_};
        news_view_.draw(canvas);
    }
    else
    {
        // About: the store's mark, its name and version, then the article.
        const float cx = px + 64.0f, cy = panel.y + 96.0f;
        list.glow({cx - 56.0f, cy - 56.0f, 112.0f, 112.0f}, 40, 50, kAccent.with_alpha(0.2f));
        list.rotated_rect({cx - 40.0f, cy - 40.0f, 80.0f, 80.0f}, 16.0f, 0.7854f, kAccent);
        list.rotated_rect({cx - 18.0f, cy - 18.0f, 36.0f, 36.0f}, 7.0f, 0.7854f, kDeep);
        ui::text(list, fonts.display, "ProsperoStore", px + 150.0f, panel.y + 98.0f, 52, kInk);
        float words = ui::text(list, fonts.regular, "Apps from ", px + 152.0f, panel.y + 136.0f, 22,
                               kInk.with_alpha(0.62f));
        ui::text(list, fonts.semibold,
                 active_catalog_url_ == catalog::kDefaultApi ? "prosperostore.net"
                 : active_with_official_ ? "prosperostore.net and your custom catalog"
                                         : "your custom catalog",
                 px + 152.0f + words, panel.y + 136.0f, 22, kAccent);
        words = ui::text(list, fonts.regular, "Brought to you by ", px + 152.0f, panel.y + 168.0f,
                         22, kInk.with_alpha(0.62f));
        ui::text(list, fonts.semibold, "BlackBearReloaded", px + 152.0f + words, panel.y + 168.0f,
                 22, kInk);
        if (!self_version_.empty())
            pill(list, fonts, "Version " + self_version_,
                 px + pw - fonts.semibold.measure("Version " + self_version_, 21.0f) - 34.0f,
                 panel.y + 92.0f, 36.0f, kInk.with_alpha(0.12f), kInk, 1.0f, kAllLayers);
        list.rounded_rect({px, panel.y + 194.0f, pw, 1.0f}, 0, kInk.with_alpha(0.12f));
        ui::Canvas canvas{list, fonts, glass, time_};
        about_.draw(canvas);
    }
    const ui::Hint queue[] = {{ui::Button::cross, "Cancel"},
                              {ui::Button::l1, "Rooms", ui::Button::r1},
                              {ui::Button::circle, "Close"}};
    const ui::Hint change[] = {{ui::Button::cross, "Change"},
                               {ui::Button::l1, "Rooms", ui::Button::r1},
                               {ui::Button::circle, development_options_ ? "Back" : "Close"}};
    const ui::Hint read[] = {{ui::Button::dpad, "Scroll"},
                             {ui::Button::l1, "Rooms", ui::Button::r1},
                             {ui::Button::circle, "Close"}};
    const bool jobs = !activity_.id.empty() || !activity_.waiting.empty();
    if (panel_tab_ == 0)
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), queue + (jobs ? 0 : 1), jobs ? 3 : 2,
                       kRight, true);
    else
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), panel_tab_ == 1 ? change : read, 3,
                       kRight, true);
    list.pop_transform();
    list.pop_opacity();
}

bool Fonts::load(gfx::Renderer &renderer, const std::string &assets)
{
    constexpr const char *names[] = {"inter-regular",    "inter-semibold", "montserrat-medium",
                                     "dejavu-sans-mono", "press-start-2p", "patrick-hand"};
    ui::FontRef *refs_by_index[] = {&refs.regular, &refs.semibold, &refs.display,
                                    &refs.mono,    &refs.pixel,    &refs.hand};
    for (std::size_t i = 0; i < 6; ++i)
    {
        std::string data;
        if (!save::read_file(assets + "/fonts/" + names[i] + ".huifont", &data) ||
            !faces_[i].load(data))
            return false;
        *refs_by_index[i] = {&faces_[i], renderer.batch().create_font_texture(faces_[i])};
    }
    return true;
}
} // namespace store
