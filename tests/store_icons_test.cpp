// ProsperoStore - Invalid artwork never replaces an accepted cached image.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/icons.hpp"
#include "core/save_file.hpp"
#include <cassert>
#include <filesystem>
#include <cstdlib>

int main()
{
    char temporary[] = "/tmp/prospero-icons-XXXXXX";
    assert(mkdtemp(temporary));
    const std::string root = temporary;
    store::catalog::Icons cache(root);
    store::catalog::Entry entry;
    entry.id = "PPSA99000";
    entry.icon = "https://prosperostore.net/icons/PPSA99000.png";
    entry.icon_hash = "revision-one";
    const auto key = store::catalog::Icons::key(entry);
    assert(key.size() == 64 && key.find('/') == std::string::npos);
    const unsigned char png[] = {137, 80, 78, 71,  13,  10, 26, 10, 0,   0,  0,   13,  73, 72,
                                 68,  82, 0,  0,   0,   1,  0,  0,  0,   1,  8,   6,   0,  0,
                                 0,   31, 21, 196, 137, 0,  0,  0,  11,  73, 68,  65,  84, 120,
                                 156, 99, 96, 0,   2,   0,  0,  5,  0,   1,  165, 246, 69, 64,
                                 0,   0,  0,  0,   73,  69, 78, 68, 174, 66, 96,  130};
    const std::string encoded(reinterpret_cast<const char *>(png), sizeof(png));
    hui::Image image;
    assert(!cache.cached(entry, image));
    assert(cache.store(entry, encoded, image) && image.width == 1 && image.rgba.size() == 4);
    assert(cache.cached(entry, image));
    store::catalog::Icons memory("");
    assert(!memory.cached(entry, image));
    assert(memory.store(entry, encoded, image) && image.width == 1);
    assert(!memory.cached(entry, image));
    assert(!cache.store(entry, "invalid PNG", image));
    assert(cache.cached(entry, image));
    entry.icon_hash = "revision-two";
    assert(store::catalog::Icons::key(entry) != key && !cache.cached(entry, image));
    entry.icon = "https://example.com/icon.png";
    assert(!store::catalog::Icons::key(entry).empty() && cache.store(entry, encoded, image));
    entry.icon = "http://example.com/icon.png";
    assert(store::catalog::Icons::key(entry).empty() && !cache.store(entry, encoded, image));
    entry.icon = "https://prosperostore.net/icon.png";
    entry.id = "../PPSA99000";
    assert(store::catalog::Icons::key(entry).empty());
    std::filesystem::remove_all(root);
}
