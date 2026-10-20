// ProsperoStore - Trust, parser, identity and URL regression checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "catalog/client.hpp"
#include "catalog/catalog.hpp"
#include "third_party/monocypher/monocypher-ed25519.h"

#include <cassert>
#include <iostream>

using namespace store::catalog;

int main()
{
    assert(title_id("PPSA99000") && !title_id("PPSA99/00") && !title_id("../PPSA99"));
    assert(update_available("01.000.009", "01.000.010"));
    assert(!update_available("01.000.010", "01.000.010"));
    assert(!update_available("02.000.000", "01.999.999"));
    assert(!update_available("unknown", "01.000.000"));
    assert(!update_available("1.000.000", "01.000.000"));
    assert(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(api_url("https://prosperostore.net/api/v1/index.json"));
    std::string base = "unchanged";
    assert(normalize_api(" https://dev.example:8443/api/v1 ", base));
    assert(base == "https://dev.example:8443/api/v1/");
    assert(normalize_api("", base) && base == kDefaultApi);
    for (const auto *bad :
         {"http://dev.example/api/v1/", "file:///tmp/feed/", "https://user@dev.example/api/",
          "https://dev.example:0/api/", "https://dev.example:65536/api/",
          "https://dev.example:/api/", "https://dev.example/api/?q=x", "https://dev.example/api/#x",
          "https://dev.example/api/\nverify_signatures=0"})
    {
        assert(!normalize_api(bad, base));
        assert(base == kDefaultApi);
    }
    assert(!normalize_api("https://dev.example/" + std::string(512, 'a'), base));
    // A catalog kept on GitHub, named as owner/repository in any of the ways it is written.
    std::string name = "unchanged";
    for (const char *typed :
         {"janedoe/ps5-homebrew-catalog", " JaneDoe/ps5-homebrew-catalog ",
          "github.com/janedoe/ps5-homebrew-catalog",
          "https://github.com/janedoe/ps5-homebrew-catalog",
          "https://www.github.com/janedoe/ps5-homebrew-catalog/",
          "HTTPS://GitHub.com/janedoe/ps5-homebrew-catalog.git", "@janedoe/ps5-homebrew-catalog"})
    {
        assert(github_catalog(typed, name));
        assert(name == "janedoe/ps5-homebrew-catalog");
    }
    assert(github_catalog_api("janedoe/ps5-homebrew-catalog") ==
           "https://janedoe.github.io/ps5-homebrew-catalog/api/v1/");
    assert(github_catalog_api("JaneDoe/My.Catalog_2") ==
           "https://janedoe.github.io/My.Catalog_2/api/v1/");
    assert(github_catalog_api("not a repository").empty());
    name = "unchanged";
    for (const char *bad :
         {"", "janedoe", "janedoe/", "/catalog", "janedoe/a/b", "jane doe/catalog",
          "-janedoe/catalog", "jane--doe/catalog", "janedoe/.", "janedoe/..", "janedoe/cat alog",
          "janedoe/cat/alog", "https://gitlab.com/janedoe/catalog", "janedoe/catalog?x=1",
          "janedoe/catalog#readme", "https://github.com/janedoe/catalog/releases",
          "janedoe/catalog\nverify_signatures=0"})
    {
        assert(!github_catalog(bad, name));
        assert(name == "unchanged");
    }
    assert(!github_catalog(std::string(40, 'a') + "/catalog", name));
    assert(github_catalog(std::string(39, 'a') + "/catalog", name));
    assert(!github_catalog("janedoe/" + std::string(101, 'a'), name));
    assert(github_catalog("janedoe/" + std::string(100, 'a'), name));
    // An address kept by an older version, read back as a name when it is a repository's.
    assert(github_catalog_of("https://janedoe.github.io/ps5-homebrew-catalog/api/v1/") ==
           "janedoe/ps5-homebrew-catalog");
    assert(github_catalog_of(std::string(kApiMirror) + "api/v1/") ==
           "darkxex/ps5-homebrew-catalog");
    for (const char *url :
         {"https://dev.example/api/v1/", "https://janedoe.github.io/api/v1/",
          "https://janedoe.github.io/catalog/", "https://janedoe.github.io//api/v1/",
          "https://janedoe.github.io/a/b/api/v1/", "http://janedoe.github.io/catalog/api/v1/",
          "https://github.io/catalog/api/v1/", "https://janedoe.github.io.evil/catalog/api/v1/"})
        assert(github_catalog_of(url).empty());
    // The mirror is one path on a shared host: nothing else on that host is the catalog.
    assert(official_url("https://darkxex.github.io/ps5-homebrew-catalog/api/v1/index.json"));
    assert(official_url("https://darkxex.github.io/ps5-homebrew-catalog/api/v1/icons/"
                        "PPSA99000.png?v=0f"));
    for (const char *url :
         {"https://darkxex.github.io/", "https://darkxex.github.io/other-repo/api/v1/index.json",
          "https://someone-else.github.io/ps5-homebrew-catalog/api/v1/index.json",
          "https://darkxex.github.io/ps5-homebrew-catalog/../x/index.json",
          "https://darkxex.github.io/ps5-homebrew-catalog/%2e%2e/x",
          "https://darkxex.github.io.evil.example/ps5-homebrew-catalog/a",
          "http://darkxex.github.io/ps5-homebrew-catalog/api/v1/index.json"})
        assert(!official_url(url));
    assert(artifact_url("https://github.com/owner/repo/releases/download/v1/a.zip"));
    assert(!artifact_url("https://release-assets.githubusercontent.com/a"));
    assert(artifact_url("https://release-assets.githubusercontent.com/a", true));
    for (const char *url :
         {"http://github.com/a", "https://github.com.evil/a", "https://github.com@evil/a",
          "https://evil/github.com/a", "https://github.com:443/a", "https://github.com/\r\na"})
        assert(!artifact_url(url, true));
    // The custom catalog's host serves its own files; GitHub stays allowed beside it.
    const std::string_view own = "https://store.example:8443/api/v1/";
    assert(artifact_url("https://store.example:8443/files/a.zip", false, own));
    assert(artifact_url("https://STORE.example:8443/a.zip", true, own));
    assert(artifact_url("https://github.com/owner/repo/releases/download/v1/a.zip", false, own));
    for (const char *url :
         {"https://store.example/a.zip", "https://store.example:8443.evil/a",
          "https://evil.store.example:8443/a", "https://store.example:8443@evil/a",
          "http://store.example:8443/a", "https://store.example:8443"})
        assert(!artifact_url(url, true, own));
    assert(!artifact_url("https://evil.example/a", true, "http://evil.example/"));
    // No custom catalog in use (none, or the official address): GitHub's rules alone.
    for (const std::string_view none : {std::string_view{}, std::string_view(kDefaultApi)})
    {
        assert(
            artifact_url("https://github.com/owner/repo/releases/download/v1/a.zip", false, none));
        assert(artifact_url("https://release-assets.githubusercontent.com/a", true, none));
        assert(!artifact_url("https://release-assets.githubusercontent.com/a", false, none));
        assert(!artifact_url("https://store.example:8443/files/a.zip", true, none));
    }
    // The official catalog's places never serve release files.
    assert(!artifact_url("https://prosperostore.net/a.zip", true, kDefaultApi));
    assert(!artifact_url("https://darkxex.github.io/ps5-homebrew-catalog/a.zip", true, kApiMirror));
    const std::string index =
        R"({"schema":3,"apps":[{"titleid":"PPSA99000","name":"ProsperoStore","kind":"app","status":"coming_soon","content_version":null,"unknown":{"future":true}}]})";
    std::vector<Entry> entries;
    std::string error;
    assert(parse_index(index, entries, error) && entries.size() == 1);
    for (const char *bad :
         {"{}", R"({"schema":3,"schema":3,"apps":[]})", R"({"schema":4,"apps":[]})",
          R"({"schema":3,"apps":[{"titleid":"../../etc"}]})"})
        assert(!parse_index(bad, entries, error));
    assert(entries.size() == 1); // Failed refresh never replaces the last good catalog.
    Entry detail;
    const std::string full_icon =
        R"({"schema":3,"titleid":"PPSA99000","name":"Store","kind":"app","status":"coming_soon","icon":"https://prosperostore.net/icons/PPSA99000.png"})";
    assert(parse_detail(full_icon, "PPSA99000", detail, error));
    assert(detail.large_icon == "https://prosperostore.net/icons/PPSA99000.png");
    // Safety facts: read when present, ignored when unknown, never a reason to refuse an app.
    assert(detail.sandbox.empty() && detail.build.empty() && detail.helpers == 0);
    const std::string with_safety =
        R"({"schema":3,"titleid":"PPSA99000","name":"Store","kind":"app","status":"coming_soon","safety":{"sandbox":"leaves","routes":["payload"],"helpers":3,"helpers_unapproved":1,"network":true,"build":"attested","build_workflow":"o/r/.github/workflows/x.yml@refs/tags/v1","future":1}})";
    Entry scanned;
    assert(parse_detail(with_safety, "PPSA99000", scanned, error));
    assert(scanned.sandbox == "leaves" && scanned.build == "attested");
    assert(scanned.helpers == 3 && scanned.helpers_unapproved == 1);
    const std::string odd_safety =
        R"({"schema":3,"titleid":"PPSA99000","name":"Store","kind":"app","status":"coming_soon","safety":{"sandbox":"safe","build":7,"helpers":-1,"helpers_unapproved":99999999}})";
    assert(parse_detail(odd_safety, "PPSA99000", scanned, error));
    assert(scanned.sandbox.empty() && scanned.build.empty());
    assert(scanned.helpers == 0 && scanned.helpers_unapproved == 0);
    assert(parse_detail(
        R"({"schema":3,"titleid":"PPSA99000","name":"Store","kind":"app","status":"coming_soon","safety":"no"})",
        "PPSA99000", scanned, error));
    const std::string listed =
        R"({"schema":3,"apps":[{"titleid":"PPSA99001","name":"A","kind":"app","status":"coming_soon","sandbox":"stays"},{"titleid":"PPSA99002","name":"B","kind":"app","status":"coming_soon","sandbox":null}]})";
    std::vector<Entry> labelled;
    assert(parse_index(listed, labelled, error) && labelled.size() == 2);
    assert(labelled[0].sandbox == "stays" && labelled[1].sandbox.empty());
    // The category: a key this version knows, and nothing (never an error) for one it doesn't.
    const char *shelves =
        R"({"schema":3,"apps":[{"titleid":"PPSA99001","name":"A","kind":"app","status":"coming_soon","category":"emulators"},)"
        R"({"titleid":"PPSA99002","name":"B","kind":"app","status":"coming_soon","category":"holograms"},)"
        R"({"titleid":"PPSA99003","name":"C","kind":"app","status":"coming_soon"}]})";
    std::vector<store::catalog::Entry> sorted;
    assert(store::catalog::parse_index(shelves, sorted, error));
    assert(sorted[0].category == "emulators" && sorted[1].category.empty() &&
           sorted[2].category.empty());
    auto bad_icon = full_icon;
    bad_icon.replace(bad_icon.find("https://prosperostore.net"), 21, "https://untrusted.example");
    assert(parse_detail(bad_icon, "PPSA99000", detail, error)); // Custom HTTPS icon host.
    bad_icon.replace(bad_icon.find("https://"), 8, "http://");
    assert(!parse_detail(bad_icon, "PPSA99000", detail, error));
    std::map<std::string, std::string> versions;
    assert(parse_versions(R"({"schema":3,"apps":{"PPSA99000":{"content_version":null}}})", versions,
                          error));
    assert(versions.at("PPSA99000").empty());
    assert(!parse_versions(R"({"schema":3,"apps":{"PPSA99000":{"content_version":"latest"}}})",
                           versions, error));

    // Two apps can share a title ID: the second is in "shared" and has its own id.
    assert(record_id("PPSA99303") && record_id("PPSA99303-swordpdf") && !record_id("PPSA99303-") &&
           !record_id("PPSA99303-Sword") && !record_id("PPSA99303/x") &&
           !record_id("../PPSA99303"));
    const std::string twins =
        R"({"schema":3,"apps":[{"titleid":"PPSA99303","id":"PPSA99303","name":"A","kind":"game","status":"available","conflicts_with":["PPSA99303-swordpdf"]}],)"
        R"("shared":[{"titleid":"PPSA99303","id":"PPSA99303-swordpdf","name":"B","kind":"game","status":"available","conflicts_with":["PPSA99303"]}]})";
    std::vector<Entry> pair;
    assert(parse_index(twins, pair, error) && pair.size() == 2);
    assert(pair[0].id == "PPSA99303" && pair[0].console_id() == "PPSA99303" &&
           pair[0].conflicts == std::vector<std::string>{"PPSA99303-swordpdf"});
    assert(pair[1].id == "PPSA99303-swordpdf" && pair[1].titleid == "PPSA99303" &&
           pair[1].console_id() == "PPSA99303" &&
           pair[1].conflicts == std::vector<std::string>{"PPSA99303"});
    // An older catalog has no ids of its own; a title ID twice in "apps" is still refused.
    assert(parse_index(index, pair, error) && pair.size() == 1 &&
           pair[0].console_id() == pair[0].id);
    for (
        const char *bad :
        {R"({"schema":3,"apps":[{"titleid":"PPSA99303","name":"A","kind":"game","status":"available"},{"titleid":"PPSA99303","name":"B","kind":"game","status":"available"}]})",
         R"({"schema":3,"apps":[{"titleid":"PPSA99303","id":"PPSA11111-x","name":"A","kind":"game","status":"available"}]})",
         R"({"schema":3,"apps":[],"shared":{}})"})
        assert(!parse_index(bad, pair, error));
    std::map<std::string, std::string> shared_versions;
    assert(parse_versions(R"({"schema":3,"apps":{"PPSA99303":{"content_version":"01.000.001"}},)"
                          R"("shared":{"PPSA99303-swordpdf":{"content_version":"01.000.002"}}})",
                          shared_versions, error));
    assert(shared_versions.size() == 2 && shared_versions.at("PPSA99303-swordpdf") == "01.000.002");
    assert(parse_versions(R"({"schema":3,"apps":{"PPSA99303-x":{"content_version":null}}})",
                          shared_versions, error)); // a shared id is a valid key
    Receipt twin{"PPSA99303", "/data/homebrew",    "01.000.002", "v", std::string(64, 'a'),
                 "now",       "PPSA99303-swordpdf"};
    Receipt back;
    assert(parse_receipt(format_receipt(twin), back, error) && back.catalog_id == twin.catalog_id);
    twin.catalog_id.clear();
    assert(parse_receipt(format_receipt(twin), back, error) && back.catalog_id.empty());

    PublicKey key{};
    std::array<std::uint8_t, 32> seed{};
    std::array<std::uint8_t, 64> secret{};
    crypto_ed25519_key_pair(secret.data(), key.data(), seed.data());
    const std::string manifest =
        "{\"schema\":3,\"sequence\":8,\"commit\":\"fixture\",\"files\":{\"index.json\":\"" +
        sha256(index) + "\",\"versions.json\":\"" + sha256("versions") + "\"}}";
    std::string signature(64, '\0');
    crypto_ed25519_sign(reinterpret_cast<std::uint8_t *>(signature.data()), secret.data(),
                        reinterpret_cast<const std::uint8_t *>(manifest.data()), manifest.size());
    Manifest verified;
    const std::array<PublicKey, 1> keys{key};
    assert(verify_manifest(manifest, signature, 8, keys, verified, error));
    assert(verified.verifies("index.json", index));
    assert(!verified.verifies("index.json", index + " "));
    assert(!verified.verifies("../index.json", index));
    assert(!verify_manifest(manifest, signature, 9, keys, verified, error));
    assert(!verify_manifest(manifest + " ", signature, 0, keys, verified, error));
    assert(!verify_manifest(manifest, signature, 0, public_keys(), verified, error));
    signature[0] ^= 1;
    assert(!verify_manifest(manifest, signature, 0, keys, verified, error));
    assert(verified.sequence == 8);
    {
        // A custom catalog beside the official one only adds: a shared title ID stays official.
        using store::catalog::Entry;
        using store::catalog::Snapshot;
        Snapshot official, custom;
        official.accepted = custom.accepted = true;
        Entry store_app, game, mine, fake;
        store_app.id = "PPSA99000";
        store_app.name = "ProsperoStore";
        game.id = "PPSA99001";
        mine.id = "PPSA12345";
        mine.name = "Mine";
        fake.id = "PPSA99000";
        fake.name = "Not the store";
        official.entries = {store_app, game};
        official.versions = {{"PPSA99000", "01.000.040"}};
        custom.entries = {fake, mine};
        custom.versions = {{"PPSA99000", "09.000.000"}, {"PPSA12345", "01.000.000"}};
        std::vector<std::string> clashes;
        const auto both = store::catalog::with_extra(official, custom, &clashes);
        assert(both.entries.size() == 3 && both.entries[0].name == "ProsperoStore" &&
               !both.entries[0].extra && both.entries[2].id == "PPSA12345" &&
               both.entries[2].extra);
        assert(both.versions.at("PPSA99000") == "01.000.040" &&
               both.versions.at("PPSA12345") == "01.000.000");
        assert(clashes.size() == 1 && clashes[0] == "PPSA99000");
        // A custom catalog that wasn't accepted adds nothing.
        custom.accepted = false;
        assert(store::catalog::with_extra(official, custom, &clashes).entries.size() == 2);
    }
    std::cout << "Catalog trust and parser checks passed\n";
}
