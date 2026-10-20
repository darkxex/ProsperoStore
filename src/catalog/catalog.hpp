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
// A catalog id: a title ID, or TITLEID-owner for an app that shares its title ID with another
// (apps/PPSA01234-janedoe.json). The console only knows the title ID.
bool record_id(std::string_view value);
bool version(std::string_view value);
bool update_available(std::string_view installed, std::string_view available);
// The catalog is published twice: on its own site, and as a mirror for networks that block that
// site. Both are signed with the same keys; each has its own manifest, because the icon
// addresses inside the files name the place they are served from.
inline constexpr std::string_view kApi = "https://prosperostore.net/api/v1/";
inline constexpr std::string_view kApiMirror = "https://darkxex.github.io/ps5-homebrew-catalog/";
bool api_url(std::string_view url);
// One of the official catalog's own places: prosperostore.net, or its mirror (one repository's
// pages on a host shared with everyone's, so the path is part of its identity).
bool official_url(std::string_view url);
inline constexpr const char *kDefaultApi = "https://prosperostore.net/api/v1/";
// Empty input restores the official API; otherwise require an HTTPS directory URL.
bool normalize_api(std::string_view value, std::string &out);
// A homebrew catalog kept on GitHub, named as "owner/repository": a repository laid out like
// ps5-homebrew-catalog, whose GitHub Pages site serves the store API. Accepts the shorthand as
// typed, with or without "github.com/" or "https://github.com/" in front, a ".git" or a slash
// at the end; out is the plain "owner/repository" (the owner in lower case, as GitHub treats
// it). False, and out untouched, for anything that is not one repository's name.
bool github_catalog(std::string_view typed, std::string &out);
// The API directory of such a catalog: https://<owner>.github.io/<repository>/api/v1/.
std::string github_catalog_api(std::string_view shorthand);
// The reverse, for a saved address: "owner/repository" when the URL is a repository's GitHub
// Pages API directory, empty otherwise.
std::string github_catalog_of(std::string_view api);
// A release file's address: github.com, GitHub's release file host after a redirect, or the
// host of the custom catalog the user set (custom; ignored when it is an official address).
bool artifact_url(std::string_view url, bool redirected = false, std::string_view custom = {});
// Where a developer can be found, for the code on their page: the catalog's donation address
// (support is set), else their own page, else the owner of the source repository on a known
// host. Empty when none of them is an HTTPS address.
std::string developer_link(const struct Entry &entry, bool &support);
std::string sha256(std::string_view bytes);
bool hex_bytes(std::string_view text, std::span<std::uint8_t> bytes);

struct Entry
{
    // id is the catalog's key for the app; titleid is what the console knows it as. They differ
    // only for an app that shares its title ID with another; titleid is empty when they don't.
    std::string id, titleid, name, author, kind, status;
    // The other listed apps with the same title ID: installing this one replaces them.
    std::vector<std::string> conflicts = {};
    const std::string &console_id() const
    {
        return titleid.empty() ? id : titleid;
    }
    std::string version, content_version, format, icon, icon_hash, released, updated, large_icon;
    std::string description, license, source, page, artifact, digest, release_notes;
    std::string added; // when the catalog first listed the app; empty from an older catalog
    // The app's shelf, one of the catalog's category keys; empty from an older catalog or for a
    // key this version doesn't know.
    std::string category;
    // Optional: the developer's own page and a place to support them (HTTPS addresses).
    std::string author_url, donate_url;
    std::uint64_t size = 0;
    // Not from the catalog's files: set by the store on entries that come from a custom
    // catalog shown beside the official one, with that catalog's name ("owner/repository").
    bool extra = false;
    std::string catalog_label = {};
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
    // The catalog id of the app that was installed; empty for an app with its title ID to itself
    // and for a receipt written before title IDs could be shared.
    std::string catalog_id = {};
};

// The one transaction in progress. content_version is the version being put in
// place (empty until it has been read from the unpacked app).
struct Journal
{
    std::string operation, state, id, location, content_version, release_tag, digest;
    std::string catalog_id = {};
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
