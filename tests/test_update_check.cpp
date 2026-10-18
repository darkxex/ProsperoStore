/*
 * ps5-native-app-boilerplate - Update-check regression: versions, parsing, decisions.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <cstdio>
#include <cstring>
#include <string>

// The kit itself, without the console transport, compiled into this test.
#define UPDATE_CHECK_NO_NETWORK
extern "C"
{
#include "../examples/update-check/update_check.c"
}

namespace
{
int failures = 0;

void check(bool condition, const char *what, int line)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(condition) check((condition), #condition, __LINE__)

// A real answer of https://prosperostore.net/api/v1/apps/PPSA99039.json (schema 3), reformatted.
const std::string listed = R"({
  "schema": 3,
  "titleid": "PPSA99039",
  "name": "EVO Player",
  "kind": "app",
  "description": "Media player for PS5 that plays video from USB or internal storage, with hardware decoding up to 4K, 10-bit HDR and surround audio.",
  "license": "GPL-3.0",
  "author": "Sain Saji",
  "version": "0.10.0",
  "source_repo": "https://github.com/sainsaji/EVO-PLAYER-PS5",
  "artifact_url": "https://github.com/sainsaji/EVO-PLAYER-PS5/releases/download/v0.10.0/EVOPlayer-v0.10.0-PPSA99039.ffpfsc",
  "sha256": "a2b14616a2662a5e8401aad9012ba1c0d3df68211e5b70039eedfb9a0845a0be",
  "icon_url": "https://raw.githubusercontent.com/sainsaji/EVO-PLAYER-PS5/v0.10.0/projects/evoplayer/sce_sys/icon0.png",
  "status": "available",
  "content_version": "01.000.001",
  "format": "ffpfsc",
  "artifact_name": "EVOPlayer-v0.10.0-PPSA99039.ffpfsc",
  "size": 21561344,
  "tag": "v0.10.0",
  "released": "2026-09-22T18:23:50Z",
  "prerelease": false,
  "release_url": "https://github.com/sainsaji/EVO-PLAYER-PS5/releases/tag/v0.10.0",
  "updated": "2026-09-29T17:20:37Z",
  "page": "https://prosperostore.net/app/PPSA99039/",
  "icon": "https://prosperostore.net/api/v1/icons/PPSA99039.png",
  "icon_small": "https://prosperostore.net/api/v1/icons/PPSA99039-256.png",
  "icon_hash": "5b0c1e7a9d3f4a26"
})";

const std::string reservation =
    R"({"schema":3,"titleid":"PPSA99009","name":"ProsperoLichess","version":null,)"
    R"("status":"coming_soon","content_version":null,"page":"https://prosperostore.net/app/PPSA99009/"})";

const std::string unversioned =
    R"({"status":"available","version":"0.9","content_version":null,"page":"https://prosperostore.net/app/PPSA99420/"})";

update_check_result evaluate(const std::string &json, const char *installed)
{
    update_check_result result{};
    update_check_evaluate(json.data(), json.size(), installed, &result);
    return result;
}

void test_versions()
{
    unsigned parts[3] = {};
    CHECK(update_check_version_parse("01.000.070", parts) == 1);
    CHECK(parts[0] == 1 && parts[1] == 0 && parts[2] == 70);
    for (const char *bad : {"", "1.000.070", "01.000.07", "01.000.0700", "01-000-070", "0a.000.070",
                            "v01.000.070", "01.000.070 ", "0.10.0"})
        CHECK(update_check_version_parse(bad, nullptr) == 0);
    CHECK(update_check_version_parse(nullptr, nullptr) == 0);

    int comparable = 0;
    CHECK(update_check_version_compare("01.000.070", "01.000.060", &comparable) > 0 && comparable);
    CHECK(update_check_version_compare("01.000.009", "01.000.010", &comparable) < 0 && comparable);
    CHECK(update_check_version_compare("02.000.000", "01.999.999", &comparable) > 0 && comparable);
    CHECK(update_check_version_compare("01.001.000", "01.000.999", &comparable) > 0 && comparable);
    CHECK(update_check_version_compare("01.000.070", "01.000.070", &comparable) == 0 && comparable);
    CHECK(update_check_version_compare("0.10.0", "01.000.070", &comparable) == 0 && !comparable);
}

void test_url()
{
    char url[96];
    CHECK(update_check_url(url, sizeof(url), "PPSA99039") == 52);
    CHECK(std::strcmp(url, "https://prosperostore.net/api/v1/apps/PPSA99039.json") == 0);
    // Nothing but a title ID ever becomes part of the address.
    for (const char *bad :
         {"", "PPSA9903", "PPSA990390", "ppsa99039", "PPSA9903/", "../../x.j", "PPSA9903?"})
        CHECK(update_check_url(url, sizeof(url), bad) == 0);
    CHECK(update_check_url(url, sizeof(url), nullptr) == 0);
    CHECK(update_check_url(url, 52, "PPSA99039") == 0);
    CHECK(update_check_url(url, 53, "PPSA99039") == 52);
}

int read(const std::string &json, const char *key, char *out, std::size_t size)
{
    return update_check_json_string(json.data(), json.size(), key, out, size);
}

void test_json()
{
    char out[64];
    CHECK(read(listed, "content_version", out, sizeof(out)) == 1 &&
          std::strcmp(out, "01.000.001") == 0);
    CHECK(read(listed, "icon_hash", out, sizeof(out)) == 1 &&
          std::strcmp(out, "5b0c1e7a9d3f4a26") == 0);
    CHECK(read(listed, "missing", out, sizeof(out)) == 0);
    CHECK(read(reservation, "content_version", out, sizeof(out)) == 2);
    CHECK(read(listed, "size", out, sizeof(out)) == -1);        // a number, not a string
    CHECK(read(listed, "description", out, sizeof(out)) == -1); // longer than the buffer

    // Values of other types and nested data are skipped, wherever the key is.
    const std::string nested =
        R"({"a":{"content_version":"99.999.999","b":[1,{"c":"}"}]},"n":-1.5e3,"t":true,"f":false,)"
        R"("z":null,"content_version":"01.000.002"})";
    CHECK(read(nested, "content_version", out, sizeof(out)) == 1 &&
          std::strcmp(out, "01.000.002") == 0);

    // Escapes, and text that is already UTF-8.
    const std::string escaped = "{\"k\":\"a\\\"b\\\\c\\/d\\u00e9\\u20ac\\n\",\"u\":\"\xC3\xA9\"}";
    CHECK(read(escaped, "k", out, sizeof(out)) == 1 &&
          std::strcmp(out, "a\"b\\c/d\xC3\xA9\xE2\x82\xAC\n") == 0);
    CHECK(read(escaped, "u", out, sizeof(out)) == 1 && std::strcmp(out, "\xC3\xA9") == 0);

    // A name longer than any the kit asks for is skipped, not mistaken for an error.
    const std::string long_name = "{\"" + std::string(200, 'k') + "\":\"x\",\"version\":\"1\"}";
    CHECK(read(long_name, "version", out, sizeof(out)) == 1 && std::strcmp(out, "1") == 0);

    for (const char *bad : {"", "[]", "\"x\"", "{", "{\"a\"", "{\"a\":", "{\"a\":\"b",
                            "{\"a\":\"b\"", "{\"a\":\"b\" \"c\":1}", "{a:1}", "{\"a\":\"\\q\"}",
                            "{\"a\":\"\\u12\"}", "{\"a\":\"\x01\"}", "{\"a\":[1,2}"})
        CHECK(read(bad, "zzz", out, sizeof(out)) == -1);

    // Nesting deeper than the limit is refused rather than followed.
    const std::string deep =
        "{\"a\":" + std::string(100, '[') + std::string(100, ']') + ",\"k\":\"v\"}";
    CHECK(read(deep, "k", out, sizeof(out)) == -1);
    CHECK(update_check_json_string(nullptr, 0, "k", out, sizeof(out)) == -1);
    CHECK(update_check_json_string(listed.data(), listed.size(), "k", out, 0) == -1);
}

void test_evaluate()
{
    auto result = evaluate(listed, "01.000.000");
    CHECK(result.state == UPDATE_CHECK_AVAILABLE && result.reason == UPDATE_CHECK_OK);
    CHECK(std::strcmp(result.available, "01.000.001") == 0);
    CHECK(std::strcmp(result.installed, "01.000.000") == 0);
    CHECK(std::strcmp(result.version, "0.10.0") == 0);
    CHECK(std::strcmp(result.page, "https://prosperostore.net/app/PPSA99039/") == 0);

    CHECK(evaluate(listed, "01.000.001").state == UPDATE_CHECK_UP_TO_DATE);
    // A copy newer than the listing (a development build) is not offered a downgrade.
    CHECK(evaluate(listed, "01.000.002").state == UPDATE_CHECK_UP_TO_DATE);

    result = evaluate(reservation, "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NOT_AVAILABLE);
    result = evaluate(unversioned, "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NO_CATALOG_VERSION);
    result = evaluate(listed, "0.10.0");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN &&
          result.reason == UPDATE_CHECK_BAD_INSTALLED_VERSION);
    result = evaluate("<html>404</html>", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_RESPONSE);
    result = evaluate(R"({"status":"available","content_version":"1.2"})", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_RESPONSE);
    CHECK(result.available[0] == '\0');
    result = evaluate(R"({"status":"available","content_version":"01.000.0019999"})", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_BAD_RESPONSE);
    // Display values that don't fit are left out; the decision still stands.
    result = evaluate(R"({"status":"available","content_version":"01.000.005","version":")" +
                          std::string(80, 'v') + R"("})",
                      "01.000.000");
    CHECK(result.state == UPDATE_CHECK_AVAILABLE && result.version[0] == '\0' &&
          result.page[0] == '\0');
}

// A stand-in for the console's HTTPS.
struct FakeServer
{
    int code = 0;
    int status = 200;
    std::string body;
    std::string url;
    std::string agent;
    int calls = 0;
} server;

int fake_fetch(const char *url, const char *user_agent, char *body, std::size_t capacity,
               std::size_t *length, int *http_status)
{
    ++server.calls;
    server.url = url;
    server.agent = user_agent;
    *http_status = server.status;
    if (server.code != 0)
        return server.code;
    if (server.body.size() > capacity)
        return UPDATE_CHECK_FETCH_TOO_LARGE;
    std::memcpy(body, server.body.data(), server.body.size());
    *length = server.body.size();
    return 0;
}

update_check_result run(const char *title, const char *installed)
{
    update_check_result result{};
    update_check_run_with(fake_fetch, title, installed, &result);
    return result;
}

void test_run()
{
    server = FakeServer{};
    server.body = listed;
    auto result = run("PPSA99039", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_AVAILABLE && result.http_status == 200);
    CHECK(server.url == "https://prosperostore.net/api/v1/apps/PPSA99039.json");
    CHECK(server.agent == "homebrew-update-check/1 (PPSA99039)");

    server.status = 404;
    server.body = "<html>not found</html>";
    result = run("PPSA00000", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NOT_LISTED &&
          result.http_status == 404);

    server.status = 503;
    result = run("PPSA99039", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_HTTP_STATUS);

    server = FakeServer{};
    server.code = static_cast<int>(0x80431068); // the console's timeout code
    server.status = 0;
    result = run("PPSA99039", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_NETWORK &&
          result.platform_error == static_cast<int>(0x80431068) && result.http_status == 0);

    server = FakeServer{};
    server.body = std::string(UPDATE_CHECK_MAX_RESPONSE + 1, ' ');
    result = run("PPSA99039", "01.000.000");
    CHECK(result.state == UPDATE_CHECK_UNKNOWN && result.reason == UPDATE_CHECK_TOO_LARGE);

    // Bad input never reaches the network.
    server = FakeServer{};
    result = run("../../etc", "01.000.000");
    CHECK(result.reason == UPDATE_CHECK_BAD_ARGUMENT && server.calls == 0);
    result = run("PPSA99039", "1.0");
    CHECK(result.reason == UPDATE_CHECK_BAD_INSTALLED_VERSION && server.calls == 0);
    update_check_run_with(nullptr, "PPSA99039", "01.000.000", &result);
    CHECK(result.reason == UPDATE_CHECK_BAD_ARGUMENT);
    update_check_run_with(fake_fetch, "PPSA99039", "01.000.000", nullptr); // must not crash
}

// Whatever arrives, the kit must neither crash nor claim an update it can't justify.
void test_hostile_input()
{
    int updates = 0;
    for (std::size_t cut = 0; cut <= listed.size(); ++cut)
    {
        const auto result = evaluate(listed.substr(0, cut), "01.000.000");
        if (cut < listed.size())
            CHECK(result.state == UPDATE_CHECK_UNKNOWN);
    }
    for (std::size_t at = 0; at < listed.size(); ++at)
    {
        for (const char replacement : {'"', '\\', '{', '}', '[', ']', ',', ':', '\0', '9', 'u'})
        {
            std::string mutated = listed;
            mutated[at] = replacement;
            const auto result = evaluate(mutated, "01.000.000");
            if (result.state == UPDATE_CHECK_AVAILABLE)
            {
                ++updates;
                CHECK(update_check_version_parse(result.available, nullptr) == 1);
            }
            CHECK(std::memchr(result.available, '\0', sizeof(result.available)) != nullptr);
            CHECK(std::memchr(result.version, '\0', sizeof(result.version)) != nullptr);
            CHECK(std::memchr(result.page, '\0', sizeof(result.page)) != nullptr);
        }
    }
    CHECK(updates > 0); // most mutations touch fields the decision doesn't depend on
}
} // namespace

int main()
{
    test_versions();
    test_url();
    test_json();
    test_evaluate();
    test_run();
    test_hostile_input();
    for (int reason = UPDATE_CHECK_OK; reason <= UPDATE_CHECK_NO_CATALOG_VERSION; ++reason)
        CHECK(std::strcmp(update_check_reason_text(static_cast<update_check_reason>(reason)),
                          "unknown") != 0);
    if (failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
