// ProsperoStore - Link-refusing file helpers for the installer's work folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace store::install
{
enum class Kind
{
    absent,
    directory,
    file,
    other, // A symbolic link or special file: never followed, never trusted.
    unknown
};
Kind kind(const std::string &path);
// One level; an existing real directory is accepted.
bool make_directory(const std::string &path);
bool sync_directory(const std::string &path);
// Regular file of at most limit bytes, opened without following a link.
bool read_small(const std::string &path, std::size_t limit, std::string &body);
// Copies a regular file (never through a link) to a temporary name beside
// the destination, then renames it over the destination.
bool copy_file(const std::string &from, const std::string &to);
// The names of the regular files directly in a folder; at most limit of them.
bool list_files(const std::string &folder, std::size_t limit, std::vector<std::string> &names);
// Removes a file or a whole folder without following links. True when nothing
// is left at path, including when nothing was there.
bool remove_tree(const std::string &path);
// Moves every file of the folder from into the folder to, on the same filesystem, replacing the
// file of the same name and making the folders it needs; whatever else is in to stays. Never
// follows a link, and refuses a name that is a file on one side and a folder on the other.
// sce_sys/param.json goes last, so the version on disk changes only once the rest is in place.
// It is not cancellable: a half-applied update is worse than a finished one. On failure some
// files may have moved.
bool overlay_tree(const std::string &from, const std::string &to, std::string &error);
} // namespace store::install
