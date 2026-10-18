// ProsperoStore - Likes and comments: unsigned decoration, read as untrusted text.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Every listed app has a discussion on GitHub: its replies are the app's comments, its upvotes
// the app's likes. A workflow of the catalog writes them to two kinds of file, which the store
// reads only while "Show likes and comments" is on. They are not in the signed manifest and
// never take part in what the catalog verifies: nothing here decides what is installed.
namespace store::social
{
// The one place these files are read from, and the two shapes a file's address can have.
inline constexpr std::string_view kBase =
    "https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/comments-data/";
inline constexpr std::string_view kDiscussions = "https://github.com/";
constexpr std::size_t kIndexLimit = 1024 * 1024;
constexpr std::size_t kThreadLimit = 64 * 1024;
constexpr std::size_t kComments = 10;    // the latest comments kept for a page
constexpr std::size_t kAuthorLimit = 40; // characters
constexpr std::size_t kTextLimit = 500;  // characters
constexpr std::uint32_t kMost = 9999999; // a count is never shown higher than this
constexpr std::size_t kMostLiked = 10;   // how many apps the "Most liked" shelf ranks
constexpr std::size_t kApps = 20000;     // entries read from the index

inline bool title_id(std::string_view value)
{
    return value.size() == 9 && value.starts_with("PPSA") &&
           std::all_of(value.begin() + 4, value.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline std::string index_url()
{
    return std::string(kBase) + "v1/index.json";
}
inline std::string thread_url(std::string_view id)
{
    return std::string(kBase) + "v1/apps/" + std::string(id) + ".json";
}
// Exactly one of the two files under kBase: nothing else on that host, and no other host.
inline bool data_url(std::string_view url)
{
    if (!url.starts_with(kBase))
        return false;
    const auto path = url.substr(kBase.size());
    if (path == "v1/index.json")
        return true;
    constexpr std::string_view folder = "v1/apps/", ending = ".json";
    return path.size() == folder.size() + 9 + ending.size() && path.starts_with(folder) &&
           path.ends_with(ending) && title_id(path.substr(folder.size(), 9));
}
// The address a code may carry: a page on github.com, written in plain address characters.
inline bool discussion_url(std::string_view url)
{
    return url.starts_with(kDiscussions) && url.size() > kDiscussions.size() && url.size() <= 200 &&
           url.find("..") == std::string_view::npos &&
           std::all_of(url.begin() + static_cast<std::ptrdiff_t>(kDiscussions.size()), url.end(),
                       [](char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
                                  c == '.';
                       });
}

struct Counts
{
    std::uint32_t likes = 0, comments = 0;
    std::string url; // the discussion; empty when the file's address was not acceptable
};
struct Comment
{
    std::string author, date, text; // date: YYYY-MM-DD, or empty
};
struct Thread
{
    Counts counts;
    std::vector<Comment> latest; // newest first, kComments at most
};
using Index = std::map<std::string, Counts>;

// One line of plain text: control and formatting characters are dropped, runs of white space
// become one space, bytes that are not UTF-8 are left out, and it ends after limit characters.
std::string plain(std::string_view text, std::size_t limit);
// False for anything that is not the file this version knows (schema 1); out is then untouched.
bool parse_index(std::string_view body, Index &out);
bool parse_thread(std::string_view body, std::string_view id, Thread &out);

// "Most liked": more likes first, then by name; an app without a like is not ranked.
struct Ranked
{
    std::string id, name;
    std::uint32_t likes = 0;
};
inline bool more_liked(std::uint32_t likes, std::string_view name, std::uint32_t other_likes,
                       std::string_view other_name)
{
    return likes != other_likes ? likes > other_likes : name < other_name;
}
std::vector<std::string> most_liked(std::vector<Ranked> apps, std::size_t limit = kMostLiked);
} // namespace store::social
