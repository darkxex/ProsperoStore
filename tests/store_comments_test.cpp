// ProsperoStore - Likes and comments are untrusted text from one address, and nothing more.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fixtures have the shape of the catalog's comments files (schema 1), with the things a file
// nobody signed may hold beside the real ones. The network is a list in this file.
#include "catalog/catalog.hpp"
#include "net/http.hpp"
#include "social/comments.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

using namespace store;

namespace
{
std::vector<net::Response> responses;
std::vector<std::string> calls;

std::string fixture(const std::string &name)
{
    std::ifstream file(std::string(STORE_TEST_FIXTURES) + "/" + name, std::ios::binary);
    assert(file);
    std::ostringstream bytes;
    bytes << file.rdbuf();
    return bytes.str();
}
net::Response reply(int status, const std::string &location = {})
{
    net::Response response;
    response.status = status;
    response.location = location;
    return response;
}
std::size_t characters(std::string_view text)
{
    std::size_t count = 0;
    for (const char c : text)
        count += (static_cast<unsigned char>(c) & 0xc0) != 0x80;
    return count;
}
} // namespace

namespace store::net
{
Response request_once(const std::string &url, std::uint64_t, const Sink &sink, Control &,
                      const std::string &)
{
    calls.push_back(url);
    assert(calls.size() <= responses.size());
    const auto response = responses[calls.size() - 1];
    if (response.status == 200)
        assert(sink("body"));
    return response;
}
} // namespace store::net

static void check_addresses()
{
    using social::data_url, social::discussion_url;
    const std::string base(social::kBase);
    assert(social::index_url() == base + "v1/index.json" && data_url(social::index_url()));
    assert(social::thread_url("PPSA99008") == base + "v1/apps/PPSA99008.json");
    assert(data_url(social::thread_url("PPSA99008")));
    for (const char *url : {
             "https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/main/v1/"
             "index.json",
             "https://raw.githubusercontent.com/blackbearreloaded/other/comments-data/v1/"
             "index.json",
             "https://raw.githubusercontent.com/someone/ps5-homebrew-catalog/comments-data/v1/"
             "index.json",
             "http://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/comments-data/"
             "v1/index.json",
             "https://raw.githubusercontent.com.evil.example/darkxex/ps5-homebrew-catalog/"
             "comments-data/v1/index.json",
             "https://prosperostore.net/api/v1/index.json",
         })
        assert(!data_url(url));
    for (const char *path :
         {"", "v1/", "v1/index.json?x=1", "v1/index.json#x", "v1/../../main/apps/PPSA99008.json",
          "v1/apps/../index.json", "v1/apps/PPSA9900.json", "v1/apps/PPSA990088.json",
          "v1/apps/ppsa99008.json", "v1/apps/PPSA99008.json/", "v1/apps/PPSA99008.txt",
          "v1/apps/%50PSA99008.json", "v2/index.json", "v1/news.json"})
        assert(!data_url(base + path));
    // These files are their own purpose: the catalog's rule does not open their address to
    // anything else, and their rule opens nothing of the catalog's.
    assert(!catalog::official_url(social::index_url()));
    assert(!catalog::artifact_url(social::index_url()));
    assert(!data_url("https://prosperostore.net/api/v1/manifest.json"));
    assert(!data_url(std::string(catalog::kApi) + "index.json"));

    assert(discussion_url("https://github.com/darkxex/ps5-homebrew-catalog/discussions/193"));
    for (const char *url :
         {"", "https://github.com/", "http://github.com/a/b/discussions/1",
          "https://github.com.evil.example/a/b/discussions/1", "https://evil.example/github.com/",
          "https://github.com@evil.example/a", "https://github.com/a/b/discussions/1?x=1",
          "https://github.com/a/b/discussions/1#x", "https://github.com/a/../b",
          "https://github.com/a b", "https://github.com/a\nb", "https://github.com/a%2e%2e/b",
          "https://github.com/a\\b", "javascript:alert(1)", " https://github.com/a/b"})
        assert(!discussion_url(url));
    assert(!discussion_url("https://github.com/" + std::string(200, 'a')));
}

static void check_requests()
{
    net::Control control;
    std::string body = "old";
    // The two files are asked for where they are, and a redirect is followed only to them.
    responses = {reply(200)};
    assert(net::fetch(social::index_url(), net::Purpose::comments, 100, body, control).ok());
    assert(body == "body" && calls == std::vector<std::string>{social::index_url()});
    calls.clear();
    body = "old";
    responses = {reply(302, "https://evil.example/v1/index.json")};
    assert(!net::fetch(social::index_url(), net::Purpose::comments, 100, body, control).ok());
    assert(calls.size() == 1 && body == "old");
    calls.clear();
    responses = {reply(302, "https://prosperostore.net/api/v1/index.json")};
    assert(!net::fetch(social::index_url(), net::Purpose::comments, 100, body, control).ok());
    assert(calls.size() == 1 && body == "old");
    calls.clear();
    responses = {reply(302, "/darkxex/ps5-homebrew-catalog/main/README.md")};
    assert(!net::fetch(social::index_url(), net::Purpose::comments, 100, body, control).ok());
    assert(calls.size() == 1);
    calls.clear();
    // Nothing else is ever asked for under this purpose: not even a request is made.
    responses.clear();
    for (const std::string &url :
         {std::string("https://prosperostore.net/api/v1/index.json"),
          std::string("https://github.com/darkxex/ps5-homebrew-catalog/discussions/193"),
          std::string(social::kBase) + "v1/apps/../index.json",
          std::string("https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/"
                      "main/apps/PPSA99008.json")})
    {
        const auto response = net::fetch(url, net::Purpose::comments, 100, body, control);
        assert(!response.ok() && response.error == "The download address is not allowed");
    }
    // ... and an app's file is never downloaded from the comments address.
    assert(!net::fetch(social::index_url(), net::Purpose::artifact, 100, body, control).ok());
    assert(calls.empty() && body == "old");
}

static void check_plain_text()
{
    using social::plain;
    assert(plain("  Hello,\r\n\tworld  ", 500) == "Hello, world");
    assert(plain(std::string_view("a\0b\x07"
                                  "c\x1b[31m\x7f",
                                  12),
                 500) == "abc[31m");
    // Marks that reorder or hide text, and the look-alike blanks, are not kept.
    assert(plain("a\xE2\x80\xAE"
                 "b\xE2\x80\x8B"
                 "c\xE2\x80\x8F"
                 "d\xEF\xBB\xBF"
                 "e\xC2\xAD"
                 "f",
                 500) == "abcdef");
    assert(plain("a\xC2\xA0\xE3\x80\x80\xE2\x80\xA8"
                 "b",
                 500) == "a b");
    assert(plain("\xF3\xA0\x81\x81tag\xEF\xB8\x8F", 500) == "tag");
    // Bytes that are not UTF-8 (a stray lead, an overlong form, a surrogate) are left out.
    assert(plain("a\xFF"
                 "b\xC3"
                 "c\xC0\xAF"
                 "d\xED\xA0\x80"
                 "e\xE2\x82",
                 500) == "abcde");
    assert(plain("caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x90\xBB", 500) ==
           "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x90\xBB");
    // The limit counts characters, never cuts one in half and leaves no blank at the end.
    assert(plain("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9", 3) == "\xC3\xA9\xC3\xA9\xC3\xA9");
    assert(plain("ab cd", 3) == "ab");
    assert(plain("ab cd", 4) == "ab c");
    assert(plain(std::string(5000, 'x'), 500).size() == 500);
    assert(plain("", 500).empty() && plain(" \n\t ", 500).empty() && plain("abc", 0).empty());
    // Markup is only ever characters.
    assert(plain("<b onclick=x>[a](https://evil.example)</b>", 500) ==
           "<b onclick=x>[a](https://evil.example)</b>");
}

static void check_index()
{
    social::Index index;
    assert(social::parse_index(fixture("v1/index.json"), index));
    assert(index.size() == 10); // seven as written, three whose values are read as nothing
    assert(index.at("PPSA99008").likes == 41 && index.at("PPSA99008").comments == 37);
    assert(index.at("PPSA99008").url ==
           "https://github.com/darkxex/ps5-homebrew-catalog/discussions/193");
    assert(index.at("PPSA99007").likes == 0 && index.at("PPSA99007").comments == 0);
    // Less than none is none; a part of one is left off; an address that is not GitHub's is
    // dropped while the counts stay.
    assert(index.at("PPSA00003").likes == 0 && index.at("PPSA00003").comments == 2);
    assert(index.at("PPSA00003").url.empty());
    // Absurd numbers stop at the most a tile can say.
    assert(index.at("PPSA00004").likes == social::kMost && index.at("PPSA00004").comments == 0);
    assert(index.at("PPSA00004").url.empty());
    assert(index.at("PPSA00005").likes == social::kMost && index.at("PPSA00005").comments == 0);
    assert(index.at("PPSA00005").url.empty());
    // Keys that are not title IDs, and values that are not objects, are passed over.
    assert(!index.contains("PPSA99009") && !index.contains("../../PPSA99010") &&
           !index.contains("ppsa99011"));

    // Anything that is not the file this version knows changes nothing.
    social::Index kept = {{"PPSA99001", {7, 1, {}}}};
    for (const char *body :
         {"", "not json", "[]", "null", "{}", R"({"schema":1})", R"({"schema":1,"apps":[]})",
          R"({"schema":2,"apps":{}})", R"({"schema":"1","apps":{}})", R"({"schema":1.0,"apps":{}})",
          R"({"schema":-1,"apps":{}})", R"({"schema":1,"apps":{"PPSA99001":{"likes":3}})",
          "<!DOCTYPE html><html><body>Blocked by your network</body></html>"})
        assert(!social::parse_index(body, kept));
    assert(kept.size() == 1 && kept.at("PPSA99001").likes == 7);
    assert(!social::parse_index(std::string(social::kIndexLimit + 1, ' '), kept));
    assert(social::parse_index(R"({"schema":1,"apps":{}})", kept) && kept.empty());
}

static void check_thread()
{
    social::Thread thread;
    assert(social::parse_thread(fixture("v1/apps/PPSA99008.json"), "PPSA99008", thread));
    assert(thread.counts.likes == 41 && thread.counts.comments == 37);
    assert(social::discussion_url(thread.counts.url));
    // Ten at most, newest first; entries without words are not comments.
    assert(thread.latest.size() == social::kComments);
    assert(thread.latest[0].author == "quatrixone" && thread.latest[0].date == "2026-10-12");
    assert(thread.latest[1].author == "mihawk-99");
    assert(characters(thread.latest[1].text) == social::kTextLimit);
    assert(thread.latest[2].author == std::string(social::kAuthorLimit, 'a'));
    assert(thread.latest[3].author == "LedyBacer");
    assert(thread.latest[3].text == "Line one line two tabbed and desrever text with a zerowidth "
                                    "space and a softhyphen.");
    assert(thread.latest[4].author == "<script>alert(1)</script>" && thread.latest[4].date.empty());
    assert(thread.latest[4].text.starts_with("<img src=x onerror=alert(1)>"));
    assert(thread.latest[5].author == "someone" && thread.latest[5].date == "2026-10-10");
    assert(thread.latest[6].author == "someone" && thread.latest[6].date.empty());
    assert(thread.latest[7].author == "five");
    assert(thread.latest[8].author == "six" &&
           thread.latest[8].text.find("caf\xC3\xA9") != std::string::npos);
    assert(thread.latest[9].author == "seven");
    for (const auto &comment : thread.latest)
    {
        assert(!comment.text.empty() && characters(comment.text) <= social::kTextLimit);
        assert(!comment.author.empty() && characters(comment.author) <= social::kAuthorLimit);
        assert(comment.date.empty() || comment.date.size() == 10);
        for (const auto &text : {comment.author, comment.text, comment.date})
            for (const char c : text)
                assert(static_cast<unsigned char>(c) >= 32 && c != 127);
    }

    assert(social::parse_thread(fixture("v1/apps/PPSA99002.json"), "PPSA99002", thread));
    assert(thread.counts.likes == 12 && thread.counts.comments == 0 && thread.latest.empty());

    // Another app's file, another schema, or no file at all: nothing changes.
    social::Thread kept;
    kept.counts.likes = 5;
    assert(!social::parse_thread(fixture("v1/apps/PPSA99002.json"), "PPSA99008", kept));
    assert(!social::parse_thread(fixture("v1/apps/PPSA99002.json"), "../PPSA99002", kept));
    assert(!social::parse_thread(fixture("v1/index.json"), "PPSA99008", kept));
    for (const char *body :
         {"", "404: Not Found", "{}", R"({"schema":2,"titleid":"PPSA99008","latest":[]})",
          R"({"schema":1,"latest":[]})", R"({"schema":1,"titleid":99008})"})
        assert(!social::parse_thread(body, "PPSA99008", kept));
    assert(!social::parse_thread(std::string(social::kThreadLimit + 1, ' '), "PPSA99008", kept));
    assert(kept.counts.likes == 5);
    // A file without comments, or with something else in their place, has none.
    assert(social::parse_thread(R"({"schema":1,"titleid":"PPSA99008","latest":{"a":1}})",
                                "PPSA99008", kept));
    assert(kept.latest.empty() && kept.counts.likes == 0 && kept.counts.url.empty());
}

static void check_order()
{
    // More likes first, ties by name, and an app nobody liked is not ranked.
    std::vector<social::Ranked> apps = {{"PPSA99001", "prosperoradio", 5},
                                        {"PPSA99006", "prosperopuzzles", 12},
                                        {"PPSA99007", "prosperotv", 0},
                                        {"PPSA99002", "prosperolight", 12},
                                        {"PPSA99008", "prosperoeden", 41}};
    assert(social::most_liked(apps) ==
           (std::vector<std::string>{"PPSA99008", "PPSA99002", "PPSA99006", "PPSA99001"}));
    assert(social::most_liked(apps, 2) == (std::vector<std::string>{"PPSA99008", "PPSA99002"}));
    assert(social::most_liked({{"PPSA99007", "prosperotv", 0}}).empty());
    apps.clear();
    for (unsigned i = 0; i < 30; ++i)
        apps.push_back({"PPSA" + std::to_string(10000 + i), "app " + std::to_string(i), 1 + i % 3});
    assert(social::most_liked(apps).size() == social::kMostLiked);
    assert(social::more_liked(2, "b", 1, "a") && !social::more_liked(1, "a", 2, "b"));
    assert(social::more_liked(1, "a", 1, "b") && !social::more_liked(1, "b", 1, "a"));
    assert(!social::more_liked(1, "a", 1, "a"));
}

int main()
{
    check_addresses();
    check_requests();
    check_plain_text();
    check_index();
    check_thread();
    check_order();
    std::cout << "Likes and comments: addresses, plain text, counts and order checks passed.\n";
    return 0;
}
