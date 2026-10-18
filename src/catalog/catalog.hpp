// ProsperoStore - Verified catalog models and trust boundary.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace store::catalog
{
constexpr std::size_t kIndexLimit = 4 * 1024 * 1024;
constexpr std::size_t kDetailLimit = 64 * 1024;
constexpr std::size_t kVersionsLimit = 1024 * 1024;
constexpr std::size_t kNewsLimit = 64 * 1024;
constexpr std::uint64_t kArtifactLimit = 2ULL * 1024 * 1024 * 1024;

bool title_id(std::string_view value);
bool version(std::string_view value);
bool update_available(std::string_view installed, std::string_view available);
// The official catalog's one home: prosperostore.net, served by GitHub Pages.
inline constexpr std::string_view kApi = "https://prosperostore.net/api/v1/";
bool api_url(std::string_view url);
// The official catalog's own place: prosperostore.net.
bool official_url(std::string_view url);
inline constexpr const char *kDefaultApi = "https://prosperostore.net/api/v1/";
// Empty input restores the official API; otherwise require an HTTPS directory URL.
bool normalize_api(std::string_view value, std::string &out);
bool artifact_url(std::string_view url, bool redirected = false);
// Where a developer can be found, for the code on their page: the catalog's donation address
// (support is set), else their own page, else the owner of the source repository on a known
// host. Empty when none of them is an HTTPS address.
std::string developer_link(const struct Entry &entry, bool &support);
std::string sha256(std::string_view bytes);
bool hex_bytes(std::string_view text, std::span<std::uint8_t> bytes);

struct Entry
{
    std::string id, name, author, kind, status;
    std::string version, content_version, format, icon, icon_hash, released, updated, large_icon;
    std::string description, license, source, page, artifact, digest, release_notes;
    std::string added; // when the catalog first listed the app; empty from an older catalog
    // The app's shelf, one of the catalog's category keys; empty from an older catalog or for a
    // key this version doesn't know.
    std::string category;
    // Optional: the developer's own page and a place to support them (HTTPS addresses).
    std::string author_url, donate_url;
    std::uint64_t size = 0;
    // Not from the catalog's files: set by the store on entries that come from the custom
    // catalog shown beside the official one.
    bool extra = false;
    // What the catalog's scan of the release file found. Advice, not a guarantee: the scan reads
    // the file and can miss things. Empty strings mean "not known", never "stays".
    std::string sandbox;       // "stays", "leaves", "unclear"
    std::string build;         // "attested", "workflow", "developer"
    std::uint32_t helpers = 0; // helper programs that run outside the sandbox
    std::uint32_t helpers_unapproved = 0;
};

struct Manifest
{
    std::uint64_t sequence = 0;
    std::string commit;
    std::map<std::string, std::string> files;
    bool verifies(std::string_view path, std::string_view body) const;
};

struct Receipt
{
    std::string id, location, content_version, release_tag, digest, installed_at;
};

// The one transaction in progress. content_version is the version being put in
// place (empty until it has been read from the unpacked app).
struct Journal
{
    std::string operation, state, id, location, content_version, release_tag, digest;
};

bool parse_installed(std::string_view body, Entry &out, std::string &error);
bool parse_receipt(std::string_view body, Receipt &out, std::string &error);
std::string format_receipt(const Receipt &receipt);
bool parse_journal(std::string_view body, Journal &out, std::string &error);
std::string format_journal(const Journal &journal);

using PublicKey = std::array<std::uint8_t, 32>;
std::array<PublicKey, 2> public_keys();
bool verify_manifest(std::string_view body, std::string_view signature, std::uint64_t highest,
                     std::span<const PublicKey> keys, Manifest &out, std::string &error);
bool parse_manifest(std::string_view body, std::uint64_t highest, Manifest &out,
                    std::string &error);
// A file the catalog signs by itself (news.json with news.sig): the released stores refuse a
// manifest that names a file they don't know, so a new file can't be listed there.
bool verify_signed(std::string_view body, std::string_view signature,
                   std::span<const PublicKey> keys);
bool parse_index(std::string_view body, std::vector<Entry> &out, std::string &error);
bool parse_detail(std::string_view body, std::string_view expected, Entry &out, std::string &error);
bool parse_versions(std::string_view body, std::map<std::string, std::string> &out,
                    std::string &error);
// A short message from the catalog's maintainers ("news.json", newest first).
struct News
{
    std::string id, date, title, body;
};
// At most twenty items are kept. An item this version can't read is left out; a file that
// is not the feed at all is an error.
bool parse_news(std::string_view body, std::vector<News> &out, std::string &error);
} // namespace store::catalog
