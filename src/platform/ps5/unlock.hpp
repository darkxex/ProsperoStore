// ProsperoStore - Ways out of the sandbox, tried in an order and remembered.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../../examples/sandbox-elevation/elevation.hpp"

namespace store::ps5
{
// Gets /data (listable, readable, writable) for the store. Ways, in the order SwanStationPS5 tries
// them:
//  - a request file for a HEN that is loaded (etaHEN, OnionHEN, LegacyJB, PS5SX2 Helper), then
//  etaHEN's
//    command server on 127.0.0.1:9028;
//  - the boilerplate's Lapy client (elevation::request), resident service first, then the packaged
//  helper.
// The way that worked at the last start goes first. A marker file written before a HEN request and
// removed after it makes the next start skip that way once if the request closed the store. Only ok
// means /data was proved. Call once during single-threaded startup, like elevation::request.
[[nodiscard]] elevation::Status unlock_filesystem() noexcept;
// For diagnostics: "existing", "hen", "resident", "helper" or "none".
[[nodiscard]] const char *unlock_route() noexcept;
} // namespace store::ps5
