// ProsperoStore - Strict bounded JSON, signed manifests and download policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog/catalog.hpp"
#include "third_party/monocypher/monocypher-ed25519.h"
#include "third_party/picosha2/picosha2.h"
#include "third_party/yyjson/yyjson.h"

#include <algorithm>
#include <charconv>
#include <memory>
#include <set>

namespace store::catalog
{
namespace
{
bool unique_members(yyjson_val *value, unsigned depth = 0)
{
    if (depth > 32)
        return false;
    if (yyjson_is_obj(value))
    {
        std::set<std::string_view> keys;
        std::size_t index = 0, count = 0;
        yyjson_val *key = nullptr, *child = nullptr;
        yyjson_obj_foreach(value, index, count, key, child)
        {
            if (!keys.insert({yyjson_get_str(key), yyjson_get_len(key)}).second ||
                !unique_members(child, depth + 1))
                return false;
        }
    }
    else if (yyjson_is_arr(value))
    {
        std::size_t index = 0, count = 0;
        yyjson_val *child = nullptr;
        yyjson_arr_foreach(value, index, count,
                           child) if (!unique_members(child, depth + 1)) return false;
    }
    return true;
}

struct Json
{
    std::vector<char> pool;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc{nullptr, yyjson_doc_free};
    yyjson_val *read(std::string_view body, std::size_t limit, unsigned expected_schema = 3)
    {
        if (body.empty() || body.size() > limit)
            return nullptr;
        pool.resize(yyjson_read_max_memory_usage(body.size(), 0));
        yyjson_alc allocator{};
        if (!yyjson_alc_pool_init(&allocator, pool.data(), pool.size()))
            return nullptr;
        doc.reset(
            yyjson_read_opts(const_cast<char *>(body.data()), body.size(), 0, &allocator, nullptr));
        if (!doc)
            return nullptr;
        auto *root = yyjson_doc_get_root(doc.get());
        auto *schema = yyjson_obj_get(root, "schema");
        return yyjson_is_obj(root) &&
                       (expected_schema == 0 ||
                        (yyjson_is_uint(schema) && yyjson_get_uint(schema) == expected_schema)) &&
                       unique_members(root)
                   ? root
                   : nullptr;
    }
};

bool field(yyjson_val *object, const char *key, std::string &out, std::size_t limit,
           bool required = false)
{
    auto *value = yyjson_obj_get(object, key);
    if (!value || yyjson_is_null(value))
    {
        out.clear();
        return !required;
    }
    if (!yyjson_is_str(value) || yyjson_get_len(value) > limit)
        return false;
    out.assign(yyjson_get_str(value), yyjson_get_len(value));
    return out.find('\0') == std::string::npos && (!required || !out.empty());
}

// One of the values this version knows, or nothing: a word from a newer catalog is not an error.
std::string known_word(yyjson_val *object, const char *key,
                       std::initializer_list<std::string_view> words)
{
    auto *value = yyjson_obj_get(object, key);
    if (!value || !yyjson_is_str(value))
        return {};
    const std::string_view word{yyjson_get_str(value), yyjson_get_len(value)};
    for (const auto known : words)
        if (word == known)
            return std::string{word};
    return {};
}

std::uint32_t small_count(yyjson_val *object, const char *key)
{
    auto *value = yyjson_obj_get(object, key);
    return value && yyjson_is_uint(value) && yyjson_get_uint(value) <= 1000
               ? static_cast<std::uint32_t>(yyjson_get_uint(value))
               : 0;
}

// An address a code may carry: HTTPS, a host, printable characters only.
bool link_url(std::string_view url)
{
    if (!url.starts_with("https://") || url.size() < 12 || url.size() > 512)
        return false;
    if (std::any_of(url.begin(), url.end(), [](unsigned char c)
                    { return c <= 32 || c >= 127 || c == '\\' || c == '"' || c == '<'; }))
        return false;
    const auto end = url.find('/', 8);
    const auto name = url.substr(8, end == std::string_view::npos ? end : end - 8);
    return name.find('.') != std::string_view::npos && name.find('@') == std::string_view::npos;
}

bool read_entry(yyjson_val *value, Entry &entry, bool detail)
{
    if (!yyjson_is_obj(value) || !field(value, "titleid", entry.id, 9, true) ||
        !title_id(entry.id) || !field(value, "name", entry.name, 256, true) ||
        !field(value, "author", entry.author, 256) || !field(value, "kind", entry.kind, 32, true) ||
        !field(value, "status", entry.status, 32, true) ||
        !field(value, "version", entry.version, 128) ||
        !field(value, "content_version", entry.content_version, 10) ||
        !field(value, "format", entry.format, 16) || !field(value, "icon_small", entry.icon, 512) ||
        !field(value, "icon_hash", entry.icon_hash, 64) ||
        !field(value, "icon", entry.large_icon, 512) ||
        !field(value, "released", entry.released, 40) ||
        !field(value, "updated", entry.updated, 40) || !field(value, "added", entry.added, 40))
        return false;
    if (entry.status != "available" && entry.status != "coming_soon")
        return false;
    if (!entry.icon.empty() && !api_url(entry.icon))
        return false;
    if (!entry.large_icon.empty() && !api_url(entry.large_icon))
        return false;
    if (!entry.content_version.empty() && !version(entry.content_version))
        return false;
    auto *size = yyjson_obj_get(value, "size");
    if (size && !yyjson_is_null(size))
    {
        if (!yyjson_is_uint(size))
            return false;
        entry.size = yyjson_get_uint(size);
    }
    entry.sandbox = known_word(value, "sandbox", {"stays", "leaves", "unclear"});
    entry.category =
        known_word(value, "category",
                   {"console-ports", "pc-ports", "homebrew-games", "emulators", "streaming",
                    "video", "music", "ai", "utilities", "tools", "showcases"});
    if (!detail)
        return true;
    // The safety facts are advice shown beside the app; a shape this version doesn't know is
    // ignored, never a reason to refuse the app's details.
    if (auto *safety = yyjson_obj_get(value, "safety"); safety && yyjson_is_obj(safety))
    {
        entry.sandbox = known_word(safety, "sandbox", {"stays", "leaves", "unclear"});
        entry.build = known_word(safety, "build", {"attested", "workflow", "developer"});
        entry.helpers = small_count(safety, "helpers");
        entry.helpers_unapproved = small_count(safety, "helpers_unapproved");
    }
    if (!field(value, "description", entry.description, 32768) ||
        !field(value, "license", entry.license, 256) ||
        !field(value, "source_repo", entry.source, 1024) ||
        !field(value, "page", entry.page, 512) ||
        !field(value, "artifact_url", entry.artifact, 4096, entry.status == "available") ||
        !field(value, "sha256", entry.digest, 64, entry.status == "available") ||
        !field(value, "release_notes", entry.release_notes, 32768))
        return false;
    // Advice for the developer's page; an address this version can't use is left out.
    for (const auto &[key, into] :
         {std::pair<const char *, std::string *>{"author_url", &entry.author_url},
          {"donate_url", &entry.donate_url}})
    {
        std::string address;
        if (auto *member = yyjson_obj_get(value, key); member && yyjson_is_str(member))
            address.assign(yyjson_get_str(member), yyjson_get_len(member));
        if (address.size() <= 512 && link_url(address))
            *into = std::move(address);
    }
    std::array<std::uint8_t, 32> digest{};
    return entry.status != "available" || hex_bytes(entry.digest, digest);
}

std::string_view host(std::string_view url)
{
    if (!url.starts_with("https://") || url.size() > 4096 ||
        std::any_of(url.begin(), url.end(),
                    [](unsigned char c) { return c <= 32 || c >= 127 || c == '\\'; }))
        return {};
    const auto slash = url.find('/', 8);
    if (slash == std::string_view::npos)
        return {};
    return url.substr(8, slash - 8);
}
} // namespace

std::string developer_link(const Entry &entry, bool &support)
{
    support = !entry.donate_url.empty() && link_url(entry.donate_url);
    if (support)
        return entry.donate_url;
    if (!entry.author_url.empty() && link_url(entry.author_url))
        return entry.author_url;
    // The owner of the source repository, on the hosts whose first path part is the owner.
    for (const std::string_view site :
         {"https://github.com/", "https://gitlab.com/", "https://codeberg.org/"})
        if (std::string_view(entry.source).starts_with(site))
        {
            const auto owner = std::string_view(entry.source).substr(site.size());
            const auto end = owner.find('/');
            const std::string link = std::string(site) + std::string(owner.substr(0, end));
            if (end != 0 && !owner.empty() && link_url(link))
                return link;
        }
    return {};
}

bool title_id(std::string_view value)
{
    return value.size() == 9 && value.starts_with("PPSA") &&
           std::all_of(value.begin() + 4, value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool version(std::string_view value)
{
    if (value.size() != 10 || value[2] != '.' || value[6] != '.')
        return false;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (i != 2 && i != 6 && (value[i] < '0' || value[i] > '9'))
            return false;
    return true;
}

bool update_available(std::string_view installed, std::string_view available)
{
    return version(installed) && version(available) && available > installed;
}

bool api_url(std::string_view url)
{
    auto authority = host(url);
    if (authority.empty() || url.find('#') != url.npos)
        return false;
    const auto colon = authority.find(':');
    if (colon != authority.npos)
    {
        const auto port = authority.substr(colon + 1);
        unsigned number = 0;
        const auto result = std::from_chars(port.data(), port.data() + port.size(), number);
        if (result.ec != std::errc{} || result.ptr != port.data() + port.size() || number == 0 ||
            number > 65535)
            return false;
        authority = authority.substr(0, colon);
    }
    return !authority.empty() &&
           std::all_of(authority.begin(), authority.end(),
                       [](char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '.' || c == '-';
                       });
}

bool official_url(std::string_view url)
{
    return host(url) == "prosperostore.net";
}

bool normalize_api(std::string_view value, std::string &out)
{
    while (!value.empty() && value.front() == ' ')
        value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ')
        value.remove_suffix(1);
    std::string candidate(value.empty() ? kDefaultApi : value);
    if (!candidate.ends_with('/'))
        candidate += '/';
    if (candidate.size() > 512 || candidate.find('?') != candidate.npos || !api_url(candidate))
        return false;
    out = std::move(candidate);
    return true;
}
bool artifact_url(std::string_view url, bool redirected)
{
    const auto domain = host(url);
    return domain == "github.com" ||
           (redirected && domain == "release-assets.githubusercontent.com");
}

std::string sha256(std::string_view bytes)
{
    std::array<std::uint8_t, 32> digest{};
    picosha2::hash256(bytes.begin(), bytes.end(), digest.begin(), digest.end());
    // Avoid the library's iostream formatter: the PS5 runtime has no locale backend.
    constexpr char digits[] = "0123456789abcdef";
    std::string text(64, '0');
    for (std::size_t i = 0; i < digest.size(); ++i)
    {
        text[i * 2] = digits[digest[i] >> 4];
        text[i * 2 + 1] = digits[digest[i] & 15];
    }
    return text;
}

bool hex_bytes(std::string_view text, std::span<std::uint8_t> bytes)
{
    if (text.size() != bytes.size() * 2)
        return false;
    const auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const int a = digit(text[i * 2]), b = digit(text[i * 2 + 1]);
        if (a < 0 || b < 0)
            return false;
        bytes[i] = static_cast<std::uint8_t>(a * 16 + b);
    }
    return true;
}

std::array<PublicKey, 2> public_keys()
{
    std::array<PublicKey, 2> keys{};
    hex_bytes("87391bf1698ecef101bf5e29dc8585ee5947d571e19470de7411c5d3b137b5cf", keys[0]);
    hex_bytes("509bcfab7edfb4e5ed23639488517c6ef2657c13b6b7f2bf699c8989d9b0dd7b", keys[1]);
    return keys;
}

bool Manifest::verifies(std::string_view path, std::string_view body) const
{
    const auto entry = files.find(std::string(path));
    return entry != files.end() && entry->second == sha256(body);
}

bool verify_manifest(std::string_view body, std::string_view signature, std::uint64_t highest,
                     std::span<const PublicKey> keys, Manifest &out, std::string &error)
{
    error = "The catalog could not be verified";
    if (body.empty() || body.size() > kVersionsLimit || signature.size() != 64)
        return false;
    bool signed_by_us = false;
    for (const auto &key : keys)
        signed_by_us |= crypto_ed25519_check(
                            reinterpret_cast<const std::uint8_t *>(signature.data()), key.data(),
                            reinterpret_cast<const std::uint8_t *>(body.data()), body.size()) == 0;
    if (!signed_by_us)
        return false;
    return parse_manifest(body, highest, out, error);
}

bool verify_signed(std::string_view body, std::string_view signature,
                   std::span<const PublicKey> keys)
{
    if (body.empty() || signature.size() != 64)
        return false;
    bool signed_by_us = false;
    for (const auto &key : keys)
        signed_by_us |= crypto_ed25519_check(
                            reinterpret_cast<const std::uint8_t *>(signature.data()), key.data(),
                            reinterpret_cast<const std::uint8_t *>(body.data()), body.size()) == 0;
    return signed_by_us;
}

bool parse_manifest(std::string_view body, std::uint64_t highest, Manifest &out, std::string &error)
{
    error = "The catalog manifest is invalid";
    Json json;
    auto *root = json.read(body, kVersionsLimit);
    auto *sequence = yyjson_obj_get(root, "sequence");
    auto *files = yyjson_obj_get(root, "files");
    if (!root || !yyjson_is_uint(sequence) || !yyjson_is_obj(files))
        return false;
    Manifest candidate;
    candidate.sequence = yyjson_get_uint(sequence);
    if (candidate.sequence < highest)
    {
        error = "An older catalog was refused";
        return false;
    }
    if (!field(root, "commit", candidate.commit, 64, true))
        return false;
    std::size_t index = 0, count = 0;
    yyjson_val *key = nullptr, *value = nullptr;
    yyjson_obj_foreach(files, index, count, key, value)
    {
        const std::string path(yyjson_get_str(key), yyjson_get_len(key));
        // A file this version doesn't know is passed over, so the catalog can add one later.
        if (path != "index.json" && path != "versions.json" &&
            !(path.size() == 19 && path.starts_with("apps/") && path.ends_with(".json") &&
              title_id(std::string_view(path).substr(5, 9))))
            continue;
        if (!yyjson_is_str(value))
            return false;
        const std::string digest(yyjson_get_str(value), yyjson_get_len(value));
        std::array<std::uint8_t, 32> bytes{};
        if (!hex_bytes(digest, bytes))
            return false;
        candidate.files.emplace(path, digest);
    }
    if (!candidate.files.contains("index.json") || !candidate.files.contains("versions.json"))
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

bool parse_index(std::string_view body, std::vector<Entry> &out, std::string &error)
{
    error = "The catalog response is invalid";
    Json json;
    auto *root = json.read(body, kIndexLimit);
    auto *apps = yyjson_obj_get(root, "apps");
    if (!root || !yyjson_is_arr(apps) || yyjson_arr_size(apps) > 10000)
        return false;
    std::vector<Entry> entries;
    std::set<std::string> ids;
    std::size_t index = 0, count = 0;
    yyjson_val *value = nullptr;
    yyjson_arr_foreach(apps, index, count, value)
    {
        Entry entry;
        if (!read_entry(value, entry, false) || !ids.insert(entry.id).second)
            return false;
        entries.push_back(std::move(entry));
    }
    out = std::move(entries);
    error.clear();
    return true;
}

bool parse_detail(std::string_view body, std::string_view expected, Entry &out, std::string &error)
{
    error = "The app response is invalid";
    Json json;
    auto *root = json.read(body, kDetailLimit);
    Entry candidate;
    if (!root || !read_entry(root, candidate, true) || candidate.id != expected)
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

bool parse_installed(std::string_view body, Entry &out, std::string &error)
{
    error = "Installed app metadata is invalid";
    Json json;
    auto *root = json.read(body, kDetailLimit, 0);
    Entry candidate;
    if (!root || !field(root, "titleId", candidate.id, 9, true) || !title_id(candidate.id) ||
        !field(root, "contentVersion", candidate.content_version, 10, true) ||
        !version(candidate.content_version))
        return false;
    auto *localized = yyjson_obj_get(root, "localizedParameters");
    if (localized && !yyjson_is_obj(localized))
        return false;
    std::string language;
    if (!field(localized, "defaultLanguage", language, 32))
        return false;
    auto *preferred = yyjson_obj_get(localized, language.empty() ? "en-US" : language.c_str());
    if (!preferred)
        preferred = yyjson_obj_get(localized, "en-US");
    if (preferred && !yyjson_is_obj(preferred))
        return false;
    if (!field(preferred, "titleName", candidate.name, 256))
        return false;
    if (candidate.name.empty())
        candidate.name = candidate.id;
    out = std::move(candidate);
    error.clear();
    return true;
}

bool parse_receipt(std::string_view body, Receipt &out, std::string &error)
{
    error = "The installation receipt is invalid";
    Json json;
    auto *root = json.read(body, 16 * 1024, 1);
    Receipt candidate;
    std::array<std::uint8_t, 32> digest{};
    if (!root || !field(root, "titleId", candidate.id, 9, true) || !title_id(candidate.id) ||
        !field(root, "location", candidate.location, 1023, true) ||
        !field(root, "contentVersion", candidate.content_version, 10, true) ||
        !version(candidate.content_version) ||
        !field(root, "releaseTag", candidate.release_tag, 128, true) ||
        !field(root, "sha256", candidate.digest, 64, true) ||
        !hex_bytes(candidate.digest, digest) ||
        !field(root, "installedAt", candidate.installed_at, 40, true))
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

namespace
{
void member(std::string &out, const char *key, std::string_view value)
{
    constexpr char digits[] = "0123456789abcdef";
    out += out.size() > 1 ? ",\"" : "\"";
    out += key;
    out += "\":\"";
    for (const unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += static_cast<char>(c);
        }
        else if (c < 32 || c == 127)
        {
            out += "\\u00";
            out += digits[c >> 4];
            out += digits[c & 15];
        }
        else
            out += static_cast<char>(c);
    }
    out += '"';
}
} // namespace

std::string format_receipt(const Receipt &receipt)
{
    std::string out = "{";
    out += "\"schema\":1";
    member(out, "titleId", receipt.id);
    member(out, "location", receipt.location);
    member(out, "contentVersion", receipt.content_version);
    member(out, "releaseTag", receipt.release_tag);
    member(out, "sha256", receipt.digest);
    member(out, "installedAt", receipt.installed_at);
    return out + "}\n";
}

bool parse_journal(std::string_view body, Journal &out, std::string &error)
{
    error = "The transaction journal is invalid";
    Json json;
    auto *root = json.read(body, 16 * 1024, 1);
    Journal candidate;
    std::array<std::uint8_t, 32> digest{};
    if (!root || !field(root, "operation", candidate.operation, 16, true) ||
        !field(root, "state", candidate.state, 16, true) ||
        !field(root, "titleId", candidate.id, 9, true) || !title_id(candidate.id) ||
        !field(root, "location", candidate.location, 1023, true) ||
        !field(root, "contentVersion", candidate.content_version, 10) ||
        (!candidate.content_version.empty() && !version(candidate.content_version)) ||
        !field(root, "releaseTag", candidate.release_tag, 128) ||
        !field(root, "sha256", candidate.digest, 64) ||
        (!candidate.digest.empty() && !hex_bytes(candidate.digest, digest)))
        return false;
    const auto &operation = candidate.operation;
    const auto &state = candidate.state;
    const bool placing = operation == "install" || operation == "update";
    if (!(placing && state == "staging") && !(operation == "install" && state == "activate") &&
        !(operation == "update" && state == "swap") &&
        !(operation == "uninstall" && state == "remove"))
        return false;
    if ((state == "activate" || state == "swap") &&
        (candidate.content_version.empty() || candidate.digest.empty()))
        return false;
    out = std::move(candidate);
    error.clear();
    return true;
}

std::string format_journal(const Journal &journal)
{
    std::string out = "{";
    out += "\"schema\":1";
    member(out, "operation", journal.operation);
    member(out, "state", journal.state);
    member(out, "titleId", journal.id);
    member(out, "location", journal.location);
    if (!journal.content_version.empty())
        member(out, "contentVersion", journal.content_version);
    if (!journal.release_tag.empty())
        member(out, "releaseTag", journal.release_tag);
    if (!journal.digest.empty())
        member(out, "sha256", journal.digest);
    return out + "}\n";
}

bool parse_versions(std::string_view body, std::map<std::string, std::string> &out,
                    std::string &error)
{
    error = "The versions response is invalid";
    Json json;
    auto *root = json.read(body, kVersionsLimit);
    auto *apps = yyjson_obj_get(root, "apps");
    if (!root || !yyjson_is_obj(apps))
        return false;
    std::map<std::string, std::string> versions;
    std::size_t index = 0, count = 0;
    yyjson_val *key = nullptr, *value = nullptr;
    yyjson_obj_foreach(apps, index, count, key, value)
    {
        const std::string id(yyjson_get_str(key), yyjson_get_len(key));
        std::string content;
        if (!title_id(id) || !yyjson_is_obj(value) ||
            !field(value, "content_version", content, 10) ||
            (!content.empty() && !version(content)))
            return false;
        versions.emplace(id, content);
    }
    out = std::move(versions);
    error.clear();
    return true;
}

bool parse_news(std::string_view body, std::vector<News> &out, std::string &error)
{
    error = "The news response is invalid";
    Json json;
    auto *root = json.read(body, kNewsLimit);
    auto *items = yyjson_obj_get(root, "news");
    if (!root || !yyjson_is_arr(items))
        return false;
    std::vector<News> news;
    std::size_t index = 0, count = 0;
    yyjson_val *item = nullptr;
    yyjson_arr_foreach(items, index, count, item)
    {
        News one;
        if (news.size() >= 20 || !yyjson_is_obj(item) || !field(item, "id", one.id, 64, true) ||
            !field(item, "date", one.date, 40, true) ||
            !field(item, "title", one.title, 120, true) || !field(item, "body", one.body, 2000))
            continue;
        news.push_back(std::move(one));
    }
    out = std::move(news);
    error.clear();
    return true;
}
} // namespace store::catalog
