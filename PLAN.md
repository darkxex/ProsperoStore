# ProsperoStore: implementation plan

Draft 5, 2026-10-02. Title ID **PPSA99000** (reserved in the catalog).

ProsperoStore is a native PS5 app store for the homebrew catalog at
[prosperostore.net](https://prosperostore.net). With the controller, from the couch,
you browse the catalog, install an app, see what you have installed, and keep
it up to date. It manages three things and nothing else: **install, uninstall
and update.**

**The bar:** it should feel like a first-party store. Beautiful, polished,
fast: 4K at a steady 60 frames per second, no screen that waits on the network,
no install that can leave the console in a broken state.

**The responsibility:** the store runs with elevated privileges and installs
code. Everything it reads from the network is treated as hostile until
verified, and the catalog it trusts is signed.

It is built on top of two of our own repositories, and on nothing that
duplicates them:

- **[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)**, the native app template: build,
  packaging, runtime, lint and tests, the sandbox elevation and the update
  check.
- **[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)** (Homebrew UI Lab, private), the UI library: its
  renderer, component library, themes, sound and its `store` design.

Rendering is [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) (OpenGL 4.6 Core, SDK 1.0.2). The catalog side
is the store API specified in the
[catalog repository](https://github.com/darkxex/ps5-homebrew-catalog)'s `docs/api.md`. Console work follows the
[ps5-agent-runbook](https://github.com/blackbearreloaded/ps5-agent-runbook). [Foundations](#foundations) says how the store
builds on the two repositories, and [The quality bar](#the-quality-bar) says
what "extremely high quality" means here, in checks a milestone passes or
fails.

Every statement about the console below is marked with how well it is known:

| Mark | Meaning |
| --- | --- |
| **[proven]** | Seen working on a console in one of our apps |
| **[source]** | Read from source code or documentation, not tried by us |
| **[assumed]** | A working assumption the owner accepted; to be confirmed when it is first used |
| **[open]** | Not known; a milestone finds out |

Changes since draft 1: the owner accepted thirteen additions (decisions D18 to
D30): a signed catalog, a recall list, GitHub-only downloads, hardened parsers,
an exact space check, proven drives first, ShadowMountPlus's own configuration,
interruption handling, a first-run check, logs and crash reports, a scripted
test mode, an update-check kit for other apps, and an in-app notice.

Changes since draft 2: the signing key is a plain Ed25519 key held as a
deployment secret of the catalog repository, with a spare kept offline (D18);
and a recall is simply the removal of the app's record from the catalog, which
the store detects from its own receipts, so no recall list is needed (D19).

Changes since draft 3: the two foundations are linked and their rules written
down, and the quality bar is stated as checks (D31, D32).

Changes since draft 4: the network layer is libcurl, because `sceSsl` rejects
public sites from an elevated app; the boilerplate now carries what libcurl
needs on the console (3.2, D7).

---

## Foundations

The store is an app **built from the boilerplate** whose interface is **built
from the UI library**. Both are ours, both have run on a console, and the store
is their first demanding customer. The rules:

### [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)

| The store takes from it | How |
| --- | --- |
| Project skeleton, build, packaging (`make`, the `.zip` release) | The repository is created from the template and keeps its `Makefile`, `tools/` and `tooling/` |
| Lint, formatting, static analysis, host test setup, CI | The store's own code passes the same `make lint` and `make test`; static analysis (clang-tidy) runs in a local `make lint` or `make tidy`, not in GitHub Actions |
| `sce_sys/` layout and versioning from `param.json` | As the template's README sets it; this is also what makes the store conformant with the catalog |
| Sandbox elevation ([`docs/SANDBOX_ELEVATION.md`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/blob/main/docs/SANDBOX_ELEVATION.md)) | The example's client and helper, with the helper built for PPSA99000 |
| Update check ([`docs/UPDATE_CHECK.md`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate/blob/main/docs/UPDATE_CHECK.md)) | Its HTTPS transport is the starting point for the store's network layer; the store's own update notice uses it as it is |

### [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)

| The store takes from it | How |
| --- | --- |
| Renderer, draw list, fonts, backdrops (`src/gfx`) | Taken as the kit, following its `docs/ADOPTING.md` |
| Component library: grids, lists, tabs, dialogs, sheets, toasts, forms, progress (`src/ui/components`) | Every screen is assembled from components; see its `docs/COMPONENTS.md` |
| Themes, motion, controller glyphs, sound sets, vibration (`src/ui`, `src/audio`) | One theme chosen for the store; nothing restyled by hand |
| The `store` design (`src/concepts/store.cpp`) | The starting point for Browse and the app page |
| The PC host renderer and the snapshot tools | Every screen is developed and photographed on the PC first |
| The console tour and its remote requests | The model for the store's test mode (section 6.9) |
| The craft rules (`docs/CRAFT.md`) | The checklist every screen passes before it is called done |

### Rules for building on them

1. **No copies that drift.** What the store takes is recorded with the commit
   it was taken from, in one file, and updated as a whole. The store never
   edits a taken file in place.
2. **Missing pieces go upstream first.** A component, a fix or a capability the
   store needs is added to the boilerplate or the UI library, tested there,
   and then taken. The store is not where they are developed.
3. **The store's own code is only what is specific to a store:** the catalog
   client, the installer, the system layer and the screens.
4. **Their checks are the store's checks.** The boilerplate's lint and tests and
   the UI library's craft checklist and console validation apply unchanged.

## The quality bar

"AAA" is not a feeling the plan can test. This is what it means here. A
milestone is done only when its work passes every line that applies; the
numbers are defaults to confirm with the owner.

| Area | The check | Measured by |
| --- | --- | --- |
| **Frame rate** | 4K, 60 frames per second on every screen: average frame at most 16.7 ms and no frame over 21 ms in steady use, with a full grid of icons, during a download, and while a dialog is open | The UI library's console validation, which its 21 designs already pass at 16.68 ms |
| **Responsiveness** | The focus moves on the frame after the button press. The render thread never waits on the network or the disk | A test build that fails on any blocking call from the render thread |
| **Start** | The first interactive screen within 2 seconds of the splash, showing the last catalog while the fresh one loads | Timed in the console tour |
| **Every state is designed** | Each screen has its loading, empty, error, offline and read-only states, and each has a reviewed picture | PC snapshots of every state in section 5.9, kept with the code |
| **Craft** | Every screen passes the UI library's `docs/CRAFT.md` checklist: safe area, text sizes, one visible focus, hint row, motion, sound, vibration | The checklist, per screen, in the milestone's review |
| **Text** | Nothing clips or overlaps in any supported language, with the longest names and descriptions the catalog allows | Snapshots with worst-case strings |
| **No broken installs** | At every instant an app's folder is the complete old version, the complete new one, or absent | Fault injection at every step of install, update and uninstall (section 6.4) |
| **No crashes** | No crash, hang or forced close across the whole test matrix and a soak test | Host tests under sanitizers, a 20-second fuzz run of the JSON and archive readers in `make test` (run by CI on pull requests, version tags and manual runs), the console tour, and a soak run of hours with repeated installs of a test title |
| **Hostile input** | Nothing read from the network can corrupt memory or escape its folder | Section 6.7, with fuzz targets as release blockers |
| **Code** | Zero warnings with warnings as errors, clean static analysis, formatted, every module with host tests | The boilerplate's `make lint` and `make test`: in CI without static analysis (clang-tidy), which runs in a local `make lint` or `make tidy` |
| **Honesty** | The interface never claims more than it knows: "unknown version", "not managed", "couldn't verify" are said plainly | Review of every message against sections 5.6 to 5.13 |
| **Console proof** | Each hardware gate is passed on a console and recorded: build, what was seen, how the app ended | The runbook's evidence, kept per milestone |

What the bar excludes: features added to look complete. A smaller store that
passes every line is the goal; a larger one that misses lines is not.

---

## 1. Scope

### In the first release

| Feature | What the user gets |
| --- | --- |
| **Browse** | The whole catalog as a grid of tiles: all, apps, games, tools, coming soon. Search, filter and sort. |
| **App page** | Name, developer, description, version, size, release date, license, source, and a button for the one thing that can be done next: Install, Update, Uninstall, or nothing. |
| **Install** | Download, verify, unpack and put in place, with progress and a clear result. ZIP artifacts only. |
| **Installed** | Everything found in the install locations, with its installed version. Apps the store didn't install are listed and marked as not managed. |
| **Updates** | Apps the store manages whose catalog version is higher than the installed one, with Update and Update all. Includes the store itself. |
| **Uninstall** | Removes an app the store installed. |
| **Install location** | A setting: the locations ShadowMountPlus scans on this console, internal storage and the M.2 drive first (section 3.3). |
| **Running-app guard** | An app that is running can't be updated or uninstalled. |
| **Recall warnings** | An app the store installed that has since been removed from the catalog is flagged as no longer listed. |
| **First-run check** | One screen that says what the console is missing for installs to work, and how to fix it. |
| **Works offline** | The last catalog and all icons it has seen stay available; installing needs the network. |
| **Interruptions handled** | A dropped connection, rest mode or a power cut never leaves a half-installed app. |

### Not in the first release

| Left out | Why |
| --- | --- |
| Image artifacts (`.ffpkg`, `.ffpfsc`) | Owner's decision: ZIP first. 14 of the 15 listed apps ship a ZIP. Images are shown with "can't be installed by this version". |
| USB drives as an install location | Offered only after milestone M1 proves renames and permissions on their filesystems (D23). |
| Launching apps | Owner's decision: the store manages install, uninstall and updates. Apps are started from the home screen. |
| Taking over apps installed by hand | Owner's decision: they are listed with a note, nothing more. |
| Restarting itself after its own update | Owner's decision: the store asks the user to restart it. |
| Waiting for ShadowMountPlus | Owner's decision: after an install the store says that ShadowMountPlus will add the app to the home screen shortly. |
| Other catalogs, accounts, ratings, payments | Out of scope. |

### Delivered alongside the store

| Deliverable | Where it lives |
| --- | --- |
| **Update-check kit** for other apps (section 8) | `ps5-native-app-boilerplate` |
| **Catalog signing** (section 7) | The catalog repository's automation and API |

---

## 2. What it builds on

| Piece | Used for | State |
| --- | --- | --- |
| Store API, `https://prosperostore.net/api/v1/` | Catalog data, versions, icons | Live. Specified in the catalog's `docs/api.md`; versioned (`schema` 3) and signed. |
| [`ps5-native-app-boilerplate`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Build, packaging, runtime, title layout (see [Foundations](#foundations)) | In use by every Prospero app. |
| Sandbox elevation (`docs/SANDBOX_ELEVATION.md` in the boilerplate) | Reaching `/data` and the other install locations | Used by ProsperoEden. |
| [`ps5-opengl`](https://github.com/blackbearreloaded/ps5-opengl) SDK 1.0.2 | Rendering | SDK 1.0.0 passed the OpenGL 4.6 conformance run.  |
| [`ps5-homebrew-ui`](https://github.com/blackbearreloaded/ps5-homebrew-ui) | Components, themes, sound, the `store` design, the PC host renderer, the remote test requests (see [Foundations](#foundations)) | All 21 designs validated on a console at 4K, 16.68 ms average frame. |
| libcurl 8.18.0 + OpenSSL 3.5.2 (PacBrew), with the boilerplate's `console_curl.c` | HTTPS, elevated | **[proven]** elevated in ProsperoRadio and sandboxed in ProsperoLichess, with certificate checks against the console's `CA_LIST.cer`. |
| `sceHttp` / `sceSsl` / `sceNet` | HTTPS, sandboxed only | **[proven]** in a sandbox; fails on every public site once elevated (3.2), so the store can't use it. |
| ProsperoEden's crash report and clean exit | Diagnostics, and closing without a forced kill | **[proven]** on a console. |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Puts installed apps on the home screen | Its behaviour below is **[source]** (branch 1.7). |

---

## 3. Facts that shape the plan

### 3.1 The sandbox and elevation

- A native title's sandbox has no `/data`. **[proven]**
- The boilerplate's elevation request gives the process filesystem access
  outside the sandbox. It needs a compatible `elfldr` listening on loopback
  port 9021, a helper ELF built for this title ID and shipped in the app, and
  it must run during single-threaded startup, before any worker starts.
  **[source]**; `/data` access through it is **[proven]** in ProsperoEden.
- The elevation is not limited to the filesystem: the boilerplate's own
  documentation says it grants broad process privileges. **[source]** This is
  why section 6.7 exists.
- Access to `/mnt/ext0`, `/mnt/ext1` and `/mnt/usb*` through the same
  elevation is **[open]** (milestone M1).
- Elevation can fail (no loader, unsupported firmware). The store must then
  still open, in a **read-only mode**: browse and see details, with a clear
  explanation of why nothing can be installed.
- All persistent store state lives in `/data/prosperostore`, after elevation,
  including settings, catalog cache, receipts, logs, crash reports and test receipts.
  There is no sandbox/download0 fallback. Without elevation, browsing uses memory only.

### 3.2 Networking

- **The store's HTTPS is libcurl, not `sceHttp`.** The store runs elevated,
  and in the elevated state the system TLS stack (`sceSsl`) rejects every
  public site with `0x8095f00c`; loading certificates with `sceHttpsLoadCert`
  doesn't help. **[proven]** in ProsperoRadio. The owner's fallback (D7) is
  therefore the plan.
- libcurl 8.18.0 with OpenSSL 3.5.2 from PacBrew v0.40.2 works in a native
  title, elevated (ProsperoRadio) and sandboxed (ProsperoLichess), verifying
  against the console's `CA_LIST.cer`. **[proven]** What it needs (the
  resolver and libc shims, the `fcntl` wrap, the certificate path) is in the
  boilerplate's `examples/update-check/console_curl.c`, with
  `PACBREW_PACKAGES += libcurl` and `APP_WRAP_SYMBOLS += fcntl`; the store takes
  it from there (Foundations).
- Against `prosperostore.net` (Cloudflare): `sceHttp` answered five API requests
  from a sandbox on 2026-10-02 in 71 to 154 ms each. **[proven]** The same
  five requests over libcurl, also from a sandbox, were answered in 147 to
  193 ms each with the certificate verified. **[proven]** (boilerplate
  `docs/UPDATE_CHECK.md`). libcurl from the elevated store against
  `prosperostore.net` is still to be shown (M2).
- **Every curl handle must go through the boilerplate's
  `console_curl_setup()`**, which sets the console's `SO_NBIO` option on each
  socket. With the sockets left blocking, a request answered in 160 ms did not
  return for 400 seconds, until the server closed the idle connection.
  **[proven]** in the boilerplate's validation; the `fcntl` wrap alone was not
  enough there. Blocking sockets also slow downloads: 11.8 MB took about 43 s
  instead of about 10 s. **[proven]** in ProsperoRadio.
- GitHub's release file host (a redirect from `github.com` to
  `release-assets.githubusercontent.com`) and a large release download through
  libcurl are **[assumed]** until M2. The redirect is followed only to that
  host (`CURLOPT_REDIR_PROTOCOLS_STR` https and a host check in the redirect
  callback). The signed catalog over libcurl from the elevated store itself
  is **[proven]** (`d783154`, Appendix D). Sandboxed browsing keeps `sceHttp`.
- Cancel: curl's progress callback returning non-zero ends the transfer at the
  next callback; progress comes from the same callback. **[source]** (libcurl
  documentation); to be shown on a console in M2.
- A timeout is `CURLE_OPERATION_TIMEDOUT` (28); errors are reported as
  `-(10000 + CURLcode)`, the convention of the boilerplate's update check.
- GitHub's redirect for a release file points at a temporary address on its
  file host; how long it stays valid is **[open]**. It is treated as expiring:
  a resumed or retried download always starts again from the app's
  `artifact_url`.
- libcurl decompresses gzip itself (zlib is linked), and resumes with
  `CURLOPT_RESUME_FROM_LARGE`; whether GitHub's file host honours `Range` for
  release files is **[open]**. Neither is required: the API files are small,
  and a failed download can restart from zero.
- Linking libcurl statically means shipping the notices of libcurl, OpenSSL,
  zlib, zstd and libpsl (the boilerplate's `THIRD_PARTY_NOTICES.md` lists them).
- Certificate verification is never switched off to make something work.

### 3.3 ShadowMountPlus

- Default [scan paths](https://github.com/drakmor/shadowMountPlus#scan-paths)
  include `/data/homebrew`, `/data/etaHEN/games`, external/USB drive roots and
  their `homebrew` and `etaHEN/games` subfolders, plus the managed image roots.
  `scanpath` overrides and scan depth come from `/data/shadowmount/config.ini`.
  `manual.lst` names individual app folders or images; its parents are not
  additional scan roots. **[source]**
- The store reads those two files to learn what this console actually scans,
  instead of assuming the defaults (D24). If they can't be read, it falls back
  to the defaults and says ShadowMountPlus wasn't found.
- A full scan runs every 15 seconds, and a folder is picked up about 10
  seconds after it stops changing. **[source]**
- **One copy per title ID.** A second folder or image of the same title gives
  a "duplicate titleId" notice. **[source]** The store never leaves two.
- It copies a title's metadata (`sce_sys`) into the system **once**; an
  updated app keeps the icon and version the console first recorded.
  **[source]** Consequence: the store reads the installed version from the
  app's own `sce_sys/param.json` on disk, never from the console's title
  database.
- Staging and backup folders must be outside every scanned location, or
  ShadowMountPlus would register half-written apps.

### 3.4 The catalog

- Each app has two versions. `version` is the developer's release tag, for
  display. `content_version` is the `contentVersion` of the release's
  `param.json` (`NN.NNN.NNN`) and is the only value compared.
- **An update exists when the catalog's `content_version` is higher than the
  installed `contentVersion`.** Unknown on either side means "unknown", never
  "update".
- Not every listed app has a usable `content_version` yet: some repositories
  don't hold a `param.json`, and several developers never raise it. Those apps
  install fine and simply never show an update.
- `sha256` and `artifact_url` are never missing for an available app.
- `icon_hash` tells the store when an icon changed, so icons are downloaded
  once.

### 3.5 Trust

- Today the only thing between the catalog and the console is HTTPS. Whoever
  could serve files as `prosperostore.net` (a hijacked domain, a compromised
  deployment) could publish their own download address with a matching hash,
  and the store would install it with elevated privileges.
- The record's `sha256` protects against a developer's release file being
  replaced. It does not protect against the catalog itself being replaced.
- So the catalog is signed at build time and the store carries the public key
  (D18, sections 6.3 and 7). A file whose signature doesn't verify is treated
  as if the network were down.
- Signed files can still be old. The store remembers the newest catalog it has
  accepted and refuses to go back to an older one, and it never installs a
  `content_version` lower than the one the current signed `versions.json`
  names for that app.
- **Removal is the recall.** Maintainers withdraw an app by deleting its
  record from the catalog repository. The store keeps a receipt for every app
  it installed, so an app with a receipt that is absent from the verified
  catalog was listed once and has been removed. No separate list is needed.
  The store can't know why it was removed, and it can't tell a removed app
  from a never-listed one when the app was installed by hand.

---

## 4. Decisions

Made by the owner on 2026-10-02 unless marked as a default.

| # | Decision |
| --- | --- |
| D1 | Name ProsperoStore, title ID PPSA99000, private repository until release. |
| D2 | ZIP artifacts only in the first release. |
| D3 | The install location is a setting, limited to locations ShadowMountPlus scans. |
| D4 | The installed version is the `contentVersion` in the app's `param.json`. |
| D5 | The store detects updates for other apps and for itself. |
| D6 | Filesystem access uses the boilerplate's elevation mechanism. |
| D7 | Networking is libcurl with OpenSSL and the console's certificate list (the boilerplate's `console_curl.c`): `sceHttp` can't reach public sites from an elevated app (3.2). |
| D8 | After an install, the store tells the user ShadowMountPlus will add the app; it doesn't wait for or verify the registration. |
| D9 | A running app is never updated or uninstalled. The check is the title's sandbox folder (`/mnt/sandbox/<TITLEID>_<n>`), which the elevated store can list (owner, 2026-10-03: the store finds this out itself). |
| D10 | After the store updates itself, it asks the user to restart it. If replacing itself can't be made safe, telling the user to update it by hand is acceptable. |
| D11 | The store does not launch apps. |
| D12 | Apps installed outside the store are listed with a note that the store doesn't manage them. No actions on them. |
| D13 | Uninstall is part of the first release. |
| D14 | The interface is based on Homebrew UI Lab. |
| D15 *(default)* | Uninstall removes the app's folder only. The app's own saved data (`/user/download/<TITLEID>` and anything the app wrote elsewhere) is left alone and the dialog says so. |
| D16 *(default)* | The store's own files live in `/data/prosperostore` (settings, receipts, cached catalog and icons, journal, logs), and in a `prosperostore` folder at the top of each other drive it installs to (staging only). |
| D17 *(default)* | Pre-releases are offered like any other release, because the catalog lists one current release per app. |
| D18 | **Signed catalog.** The catalog build signs what the store relies on with a plain Ed25519 key (not GPG: the console would need an OpenPGP parser). The private key is a secret of the catalog repository's deployment environment, never a file in the repository; a second key is generated at the same time and kept offline as the spare. The store carries both public keys and refuses anything unsigned, wrongly signed, or older than what it has already accepted. **The keys never expire** (owner's decision): they are raw keys with no validity period, the store checks no dates, and a key is retired only by switching to the spare. |
| D19 | **Recall by removal.** Withdrawing an app means deleting its record from the catalog. The store warns, in neutral words, about any app it installed that is no longer in the verified catalog. No recall list and no reason. |
| D20 | **GitHub-only downloads.** Artifacts are fetched over HTTPS from `github.com` and GitHub's release file host only, including every redirect. The API is fetched from `prosperostore.net` only. |
| D21 | **Hardened parsers.** Size limits on everything read from the network, strict validation, and fuzz tests for the JSON and ZIP code on the PC. |
| D22 | **Exact space check.** After the download, the unpacked size is read from the archive's own directory and checked against free space before anything is unpacked. |
| D23 | **Proven drives first.** Internal storage and the M.2 drive in the first release; a USB location appears only once M1 has proven it on that filesystem. |
| D24 | **ShadowMountPlus's own configuration** decides which locations are offered, not a hard-coded list. |
| D25 | **Interruptions.** Retries with increasing waits, resume where the server allows it, and clean handling of rest mode and a lost connection. |
| D26 | **First-run check**, also reachable from Settings. |
| D27 | **Logs and crash reports**: a rotating log and ProsperoEden's crash-report handler. |
| D28 | **Scripted test mode**: console runs are driven and closed by a script, never by a forced kill. |
| D29 | **Update-check kit** for other apps, delivered in the boilerplate. |
| D30 | **In-app notice**: the website's disclaimer, shown at first start and in About. |
| D31 | **Built on our two repositories.** The store is created from `ps5-native-app-boilerplate` and its interface comes from `ps5-homebrew-ui`. Nothing they provide is reimplemented or forked; what is missing is added to them first (see Foundations). |
| D32 | **Extremely high quality, as checks.** A milestone is done only when it passes the quality bar; scope is cut before quality is. |
| D33 | **The look.** The screens follow the UI library's Storefront design, in the Farlight colours as its Aurora Shelf design shows them (changed from Glass Orchard on 2026-10-03). Loading is shown by an arc completing its circle. A coming-soon app without artwork shows the rocket picture (`assets/images/coming-soon.png`). This answers open question 6. |
| D35 | **Hand-installed apps can be handed over** (owner, 2026-10-03; replaces D12's "no actions"): an app installed by hand that is listed in the catalog can be taken over from its page after a question. Only a receipt is written; updates then replace its whole folder. |
| D36 | **The store updates itself in place** (answers M6): its running folder is swapped, the old one kept until the new store starts. Shown on a console on 2026-10-03. |
| D34 | **The store's own update notice.** A newer listed ProsperoStore is announced by a floating notice in the top-right corner that stays for ten seconds, decided by the boilerplate's update check. |

---

## 5. Features in detail

### 5.1 Browse and search

- Data: `index.json`, fetched at start and on demand, with `If-None-Match`.
  The last good copy is kept on disk and shown immediately at the next start;
  the fresh one replaces it when it arrives and verifies. The header shows
  when the catalog was last refreshed and whether the store is offline.
- Icons: `icon_small` for tiles, `icon` for the app page, kept on disk with
  their `icon_hash` and fetched again only when the hash changes. A few
  downloads run at a time, nearest tiles first; a tile shows a placeholder
  until its icon arrives and never blocks scrolling.
- Sections: All, Apps, Games, Tools, Coming soon (reservations), plus
  Installed and Updates.
- Search by name and developer with the system keyboard; sort by name, newest
  release, recently updated.
- Each tile carries a state badge: Installed, Update, Coming soon, or
  nothing.

### 5.2 App page

- Data: `apps/<TITLEID>.json`, fetched when the page opens (cached with its
  `ETag`), verified before it is shown as installable.
- Shows: icon, name, developer, kind, description, release tag, release date,
  download size, license, source repository, and installed version when there
  is one.
- One primary action, chosen by state (section 5.9).
- A QR code and the short address of the app's page on prosperostore.net, for the
  release notes and the source on a phone.
- A standing line: the app comes from its developer, who is responsible for
  it; some apps need a payload or extra setup described in their release
  notes.

### 5.3 Install

One install runs at a time; others wait in a queue the user can see and edit.

1. **Refuse early.** Not a ZIP; its file doesn't verify
   against the signed catalog; the download address isn't on GitHub (D20);
   already installed outside the store; the target location is missing or
   read-only; not enough free space for the download itself; no elevation.
2. **Download** `artifact_url` to the staging folder. Every redirect is
   checked against the allowed hosts. Progress shows bytes, speed and time
   left. Cancel aborts the request and deletes the partial file. A failure is
   retried as section 5.12 describes.
3. **Verify.** The SHA-256 of the complete file must equal the catalog's
   `sha256`. A mismatch deletes the file and reports "the file doesn't match
   the listing"; nothing is unpacked.
4. **Read the archive's directory, unpack nothing yet.** The archive must
   contain exactly one top-level folder named after the title ID. Rejected:
   absolute paths, `..`, symbolic links, names outside that folder, more
   entries or longer names than the limits in 6.7, and a
   `sce_sys/param.json` that is missing.
5. **Exact space check (D22).** The directory gives every file's unpacked
   size. Their sum, plus the previous version during an update, plus a margin,
   must fit in the location's free space. If not, the install stops here with
   the exact numbers.
6. **Unpack** into the staging folder. A file that turns out larger than the
   directory declared stops the install. Afterwards `sce_sys/param.json` must
   name this `titleId`.
7. **Put in place** with one rename from staging to `<location>/<TITLEID>`.
   Staging is on the same filesystem as the location so the rename is a single
   step.
8. **Record** a receipt (section 6.5) and show: "Installed. ShadowMountPlus
   will add it to your home screen in a moment."

If the unpacked app's `contentVersion` differs from the catalog's
`content_version`, the install still succeeds and the receipt records what is
actually on disk.

### 5.4 Update

- Offered when the store manages the app and the catalog's `content_version`
  is higher than the installed `contentVersion` (section 3.4).
- Refused while the app is running (section 5.7), with a message to close it.
- Same steps as an install up to "put in place", which becomes: rename the
  current folder to a backup outside the scanned locations, rename the new
  folder in, then delete the backup. If the second rename fails, the backup is
  renamed back.
- The app's saved data is not touched.
- **Update all** queues every available update and skips the running ones,
  listing them at the end.

### 5.5 Uninstall

- Only for apps the store manages.
- Refused while the app is running.
- A confirmation that names the app and says its saved data stays (D15).
- Renames the folder out of the scanned location, then deletes it, then
  removes the receipt. A deletion interrupted half-way leaves nothing in the
  scanned location.
- ShadowMountPlus and the console may keep showing the title's tile until
  they notice; the store says so.

### 5.6 Installed

- At start and after every change, the store looks in each install location
  for folders with a `sce_sys/param.json` and reads `titleId`, `titleName` and
  `contentVersion` from it.
- A folder with a receipt that matches is **managed**. Anything else is
  **not managed**: shown with its name, version and location and the note
  "Installed outside ProsperoStore. Not managed by this app." No actions.
- Image files found in a location are listed as not managed too.
- A receipt whose folder is gone is dropped (the user removed the app by
  hand).

### 5.7 Running-app guard

- Before an update or uninstall starts, and again immediately before the
  folder is touched, the store asks the system whether that title is running.
- The call to use is supplied by the owner (D9). Known so far: the folder
  `/mnt/sandbox/<TITLEID>_000` exists only while the title runs **[proven]**
  from outside the console; whether an elevated app can rely on it is
  **[open]**, so it is at most a second check.
- If the answer can't be obtained, the store treats the app as running and
  refuses.

### 5.8 Updating the store itself

- The store is a catalog app like any other, so the Updates screen shows its
  own update when `versions.json` has a higher `content_version` for
  PPSA99000 than the running build.
- Download, verify and unpack as for any app. The new version is fully staged
  before anything is replaced.
- Replacing the running store's own folder is **[open]** (milestone M6). The
  preferred method is the same two renames as an update, done while the store
  runs, followed by "Restart ProsperoStore to finish the update". If that
  proves unsafe on a console, the store instead shows that a new version
  exists and how to update it by hand (D10).
- The running-app guard never lets the store update itself through the normal
  path; self-update is its own, separately tested path.

### 5.9 States

| App state | Primary action | Notes |
| --- | --- | --- |
| Not installed, ZIP | Install | |
| Not installed, image | none | "Can't be installed by this version of ProsperoStore" |
| Coming soon | none | Reservation |
| Managed, up to date | Uninstall | |
| Managed, update available | Update | Uninstall as second action |
| Managed, version unknown | Uninstall | "This app doesn't publish a comparable version" |
| Not managed | none | The note from 5.6 |
| Managed, no longer in the catalog | Uninstall | The warning from 5.11 |
| Running | actions disabled | "Close the app first" |
| In the queue / downloading / verifying / unpacking | Cancel | |
| Failed | Retry | With the reason |

Failure reasons shown to the user: no network; the catalog can't be reached;
the catalog couldn't be verified; the download failed; the download address
isn't allowed; the file doesn't match the listing; the archive isn't a valid
app; not enough space (with the numbers); the location isn't available; the
app is running; no permission to write (elevation missing).

### 5.10 Settings

- **Install location**: the locations ShadowMountPlus scans on this console
  (D24) that the first release supports (D23), that exist and can be written,
  each with its free space. Default `/data/homebrew`. Changing it affects new
  installs only; an update stays where the app is.
- Check for updates at start: on by default.
- Language (follows the system; can be forced), theme, sound and vibration.
- **Check this console**: reopens the first-run check (5.13).
- **About**: version, the catalog's build and signature status, licences, the
  notice from 5.14, a storage summary, and "Save logs", which copies the logs
  to a folder the user can reach.

### 5.11 Recall warnings

- An app is recalled by removing its record from the catalog (D19).
- After every verified catalog refresh, the store compares its receipts with
  the catalog. A managed app whose title ID is not there any more is marked
  **No longer listed** in Installed, and the store shows a warning once per
  app: "This app is no longer in the catalog. It may have been withdrawn by
  its developer or removed by the catalog. You can keep it or uninstall it."
- The store never removes anything by itself. The user decides.
- The check uses only a catalog that verified and isn't older than the last
  one accepted (3.5), so a network problem or a stale copy can't produce the
  warning. If the app comes back to the catalog, the mark goes away.
- Apps installed by hand get no warning: without a receipt the store can't
  tell a removed app from one that was never listed.

### 5.12 Interruptions

| Event | What the store does |
| --- | --- |
| A request fails or times out | Retries up to three times, waiting longer each time; then reports the failure with Retry. |
| A download breaks part-way | Resumes with a `Range` request if that is proven to work (3.2), always starting again from `artifact_url`; otherwise restarts. The hash is always computed over the complete file before it is accepted. |
| The connection is lost | Downloads pause and say so; browsing continues from the cache; they continue when the network returns. |
| Rest mode, or the console is switched off | Treated as a lost connection or a power cut: at the next start the journal finishes or undoes what was in progress (6.4). |
| The drive disappears | The job fails with "the location isn't available"; staging on that drive is cleaned at the next start it is present. |
| The store is closed during a job | It asks for confirmation; closing cancels the job cleanly. |

### 5.13 First-run check

Shown at first start, after an update of the store, whenever a requirement
fails, and from Settings. One row per requirement, each with its state and,
when it fails, what to do:

| Requirement | Checked by |
| --- | --- |
| The loader the elevation needs | The elevation request's result |
| Write access to the install location | Creating and deleting a test file |
| ShadowMountPlus | Its configuration folder and log |
| Network, and the catalog reachable | Fetching `versions.json` |
| The catalog verifies | Its signature |
| Free space | The location's free space against a sensible minimum |

When something required for installing fails, the store continues in
read-only mode and the Install buttons explain why.

### 5.14 Notice

At first start, and always in About: the catalog lists apps published by their
own developers; each developer is solely responsible for their app's licensing
and content; ProsperoStore and the catalog come without warranty; a listing is
not a security audit. The wording follows the website's disclaimer.

---

## 6. Architecture

### 6.1 Repository layout

```text
src/app/          shell, navigation, screens
src/catalog/      API client, models, version comparison, signature check, on-disk cache
src/net/          HTTP transport (libcurl on console and on the PC host, a fake for tests)
src/install/      queue, download, verify, unpack, transaction, journal, receipts
src/system/       elevation, install locations, free space, running check, installed scan
src/diag/         log, crash report, test mode
src/ui/           taken from ps5-homebrew-ui: components, themes, sound
platform/ps5/     console implementations
platform/host/    PC implementations for development and tests
payload/          the elevation helper, built for PPSA99000
tests/            host tests, fixtures (saved API responses, sample archives), fuzz targets
tools/            build, packaging, host snapshots, console scripts
sce_sys/          param.json, icon and home-screen art
docs/             user guide, architecture notes
```

### 6.2 Runtime model

| Thread | Does | Never does |
| --- | --- | --- |
| Main (render) | Input, UI, drawing | Network or disk waits |
| Network | Catalog and icon requests | Touch the UI |
| Installer | One job at a time: download, hash, unpack, transaction | Touch the UI |
| Disk | Icon decode, cache reads and writes | |

Workers report through a queue the main thread drains once per frame. The
elevation request runs before any of these threads exists (3.1).

### 6.3 Catalog client

- Reads `index.json` for lists, `apps/<TITLEID>.json` for one app, and
  `versions.json` for the update check and the recall comparison (5.11).
- **Verification (D18).** Each of those files is accepted only with a valid
  Ed25519 signature from a key the store carries. The store carries two public
  keys, the one the catalog signs with and the offline spare, so the key can
  be replaced without stranding installed stores. A small, audited Ed25519
  implementation is vendored for this. The form is settled and live: one signed list, `manifest.json`
  with `manifest.sig` (64 raw bytes), naming every API file by its SHA-256.
  The store verifies it once per refresh and then checks each file it
  downloads against it (the catalog's `docs/api.md`, "Verifying the catalog").
- **No going back.** The signed data carries the catalog's build sequence. The
  store keeps the highest it has accepted and rejects lower ones, and it
  refuses to install a `content_version` below the one in the current
  `versions.json`.
- Tolerant by rule: ignores unknown fields, accepts `null` wherever the API
  allows it, and refuses to act on a `schema` it can't read rather than
  guessing.
- Version comparison is one function with its own tests, implementing the
  table in the API specification.
- Icons are not signed. They are images shown on screen and are decoded with
  the limits in 6.7.

### 6.4 The install transaction

Paths, for a location `L` (for example `/data/homebrew`) on a drive whose top
folder is `R` (`/data`):

```text
R/prosperostore/staging/<TITLEID>.zip        download
R/prosperostore/staging/<TITLEID>/           unpacked
R/prosperostore/backup/<TITLEID>/            previous version during an update
L/<TITLEID>/                                 the installed app
```

Every step that changes `L` is first written to a journal
(`/data/prosperostore/journal.json`): intent, paths, state. At start the store
reads the journal and finishes or undoes whatever was interrupted:

| Found at start | Action |
| --- | --- |
| Download or unpack in progress | Delete the staging files |
| Old folder moved to backup, new one not in place | Move the backup back |
| New folder in place, backup still there | Delete the backup, write the receipt |
| Uninstall started | Finish deleting |

The rule the transaction keeps: at every instant, `L/<TITLEID>` is either the
complete old version, the complete new version, or absent. Never a mix.

A location is offered only if a rename inside it is a single step on its
filesystem, which M1 establishes per filesystem (D23).

### 6.5 Receipts

`/data/prosperostore/receipts/<TITLEID>.json`: title ID, location, the
`contentVersion` and release tag installed, the artifact's `sha256`, and when.
A receipt is what makes an app managed. It is written last in an install and
removed last in an uninstall.

### 6.6 Interface

- Start from Homebrew UI Lab's `store` design and component library (lists,
  grids, dialogs, forms, progress, toasts), on the kit's 1920 x 1080 virtual
  canvas rendered at the display's resolution.
- Screens: Browse, App page, Installed, Updates, Queue, Settings, First-run
  check, and the dialogs (confirm, error, recall warning, notice, restart
  after self-update, read-only mode).
- Controls follow the kit: D-pad and left stick move, Cross confirms, Circle
  goes back, Triangle opens search, Square opens the queue, Options opens
  settings, L1/R1 switch sections.
- Motion, sound and vibration come from the kit's themes. Every wait has a
  visible state; nothing freezes the picture.
- Frame time, responsiveness, designed states, craft and text all follow
  [The quality bar](#the-quality-bar).

### 6.7 Security rules

The store is elevated for its whole life (3.1), so these are hard rules, each
with a test.

**Where it connects**

| For | Allowed hosts |
| --- | --- |
| The API and icons | `prosperostore.net` |
| Artifacts | `github.com`, and GitHub's release file host reached by redirect from it |

HTTPS only, certificates verified, at most five redirects, each checked. A
catalog entry pointing anywhere else is refused before any request is made.

**How much it reads** (defaults; adjust when real data says so)

| Input | Limit |
| --- | --- |
| `versions.json` | 1 MiB |
| `index.json` | 4 MiB |
| `apps/<TITLEID>.json` | 64 KiB |
| An icon | 2 MiB, and at most 1024 pixels on a side once decoded |
| An artifact | The catalog's `size` when known, never more than 2 GiB |
| Entries in an archive | 100,000 |
| A path inside an archive | 512 bytes |
| Unpacked size | What the directory declares, enforced while unpacking |

**How it reads**

- JSON and ZIP are parsed by small, well-known libraries with every length
  checked; no parser is handed more than its limit.
- Nothing from the network is ever used as a path without validation, and
  nothing is executed, interpreted or passed to a shell.
- The archive rules of section 5.3 apply to every archive, including the
  store's own update.
- **Fuzz tests on the PC** feed the JSON reader and the archive reader with
  mutated inputs, with the sanitizers on: 20 seconds in every `make test`
  (`tools/store-test.sh`), which CI runs on pull requests, version tags and
  manual runs, not on pushes to main. The icon decoder is not fuzzed yet
  (open). A crash is a release blocker.

### 6.8 Diagnostics

- **Log:** `/data/prosperostore/logs/store.log`, rotated by size with a few
  generations kept; inside the sandbox (`/download0`) when not elevated. It
  records every state change of the installer and every refusal with its
  reason. No personal data, no full addresses beyond the host and title ID.
- **Crash report:** ProsperoEden's handler: it writes a report file, restarts
  the store, and the store shows a notice that it recovered. The journal then
  repairs any transaction that was in progress.
- **Save logs** in About copies the logs and reports to a folder the user can
  reach, for attaching to a problem report.

### 6.9 Test mode

For development builds only; a release build ignores it.

- A request file in the app's folder tells the store what to do: run a named
  tour (a list of screens and actions), install a test title, or quit. Each
  request carries a token and is honoured once, as in the UI kit.
- Results (log, screenshots, a report) are written where the PC can fetch them
  after the store has closed.
- The store closes itself through the clean exit that ProsperoEden uses. A
  console run never ends with a forced kill.
- Test installs use dedicated test title IDs and their own folders, never a
  real app.

---

## 7. Work the catalog side owes this plan

Done by the catalog's automation and API, not by the store. Each is an
addition to the API (a higher `schema`), so existing clients keep working.

| Item | What it is | Needed by |
| --- | --- | --- |
| **Signing** | **Done** (API `schema` 3). The deploy signs `manifest.json` with the Ed25519 key in its `CATALOG_SIGNING_KEY` secret, using the runner's `openssl`, and refuses to deploy unsigned or with an unpublished key. `sequence` is the number of commits in the catalog's history. Both public keys are in the catalog repository's `keys/` folder and in its `docs/api.md`. Switching to the spare is replacing the secret. | M2 |
| **Release notes** | The release's notes as text in `apps/<TITLEID>.json`, with a length limit. | M8 |
| **Icons** | A lighter 256-pixel icon, and a cache of converted icons so catalog builds stay within their time limit as the catalog grows. | M3 |
| **The store's listing** | PPSA99000 moves from a reservation to a release, with a `param.json` in its repository so `content_version` is right. | M6 |
| **Developer guidance** | Already in place: the catalog warns developers who don't raise `contentVersion`. | |

---

## 8. Update-check kit for other apps

Every app should be able to tell its user that a newer version exists. This is
a small, separate deliverable in `ps5-native-app-boilerplate`, usable by any
developer, not only by Prospero apps.

**Status: delivered and validated on a console (2026-10-02).** It is
`examples/update-check/` in the boilerplate (`update_check.h`, `update_check.c`,
an example title, host tests under sanitizers) with `docs/UPDATE_CHECK.md`. What
remains from this section is one Prospero app adopting it.

- **What it does:** once per launch, in the background, it fetches the app's
  own `apps/<TITLEID>.json`, compares `content_version` with the
  `contentVersion` the app was built with (the rule in the API specification),
  and reports one of: up to date, update available (with the release name and
  the app's page), or unknown.
- **What the app shows** is up to the app; the kit ships a ready-made, unobtrusive
  notice: "Update available: <version>. Update it in ProsperoStore." It
  doesn't launch the store and it downloads nothing.
- **Rules it enforces:** never blocks start-up or the UI; one request with a
  short timeout; any failure means "unknown" and nothing is shown; the same
  size limit and tolerant parsing as the store; HTTPS to `prosperostore.net` only.
- **No elevation and no signature check:** it only displays a notice, so the
  transport's certificate check is enough, and it works inside the sandbox.
- **Deliverables:** the source in the boilerplate with an example, host tests
  for the comparison and the parser, and a section in the catalog's
  `docs/api.md` pointing developers to it.

---

## 9. Milestones

Each ends with something that can be shown, and is done only when its work
passes [The quality bar](#the-quality-bar). Hardware gates are marked.

| # | Milestone | Done when |
| --- | --- | --- |
| M0 | Bootstrap | The repository is created from the boilerplate template and builds an empty title for PPSA99000 with the OpenGL SDK and the UI library taken as its `docs/ADOPTING.md` describes, on the console build and on the PC host; the file recording what was taken, and from which commits, exists. The log, the crash report and the test mode with a clean remote exit are in from the first build. |
| M1 | **Gate: filesystem** | On a console, after elevation, the store creates, renames and deletes a test folder in `/data/homebrew`; reads ShadowMountPlus's configuration and lists the scanned locations; and, for each other location, reports its filesystem and whether a rename is a single step there. Read-only mode appears when elevation is refused. This decides which locations the first release offers. |
| M2 | **Gate: network and trust** | On a console, the store fetches the signed catalog from prosperostore.net with certificate checks and verifies it; refuses a tampered and an outdated copy; and downloads one real release from GitHub through the redirect with a correct SHA-256, refusing a redirect to another host. Resume and Cancel are tried and recorded. All of it through libcurl from the elevated store. |
| M3 | Catalog and browse | The full catalog scrolls at 60 frames per second with icons, sections, search and sort, from live data and from the offline cache. PC host first, then console. |
| M4 | Install | A real catalog app installs from the store and appears on the home screen. Every refusal and failure in 5.3 has a test, including the exact space check and the archive limits. The fuzz tests of the JSON and archive readers run in CI through `make test`; the icon decoder's is still open. |
| M5 | Installed, update, uninstall, running guard, recall | The Installed and Updates screens are correct for managed and unmanaged apps; update and uninstall work; a running app is refused; a test app removed from a test catalog shows the "no longer listed" warning. Needs the owner's running check (D9). |
| M6 | **Gate: self-update** | Either the store replaces itself safely and asks for a restart, or the manual path is in place. |
| M7 | Recovery and interruptions | Power loss is simulated at every step of install, update and uninstall on the PC host, and the journal brings the folder back to a complete state each time. Lost connections, timeouts and a removed drive behave as in 5.12. A subset is repeated on a console with a test title. |
| M8 | First-run check, notice, polish | The first-run check, the notice, languages, sound, art, release notes on the app page, the settings screen and the user guide. |
| M9 | Update-check kit | **Kit done** (in the boilerplate, validated on a console). Remaining: one Prospero app uses it. |
| M10 | Release | A soak test, the security rules of 6.7 reviewed against the code, the first tagged release, and the catalog listing. |

---

## 10. Testing

- **On the PC host:** everything that isn't the console. The API client
  against saved responses of the live API; signature checks against good,
  tampered, wrongly signed and outdated files; the version table; archive
  validation against hostile archives (path escapes, links, wrong title,
  oversized, lying about sizes); the transaction with a failure injected at
  every step; the retry and resume logic against a test server that drops
  connections; screen snapshots of every state in 5.9.
- **Fuzzing:** the JSON reader and the archive reader, in `make test` and so in
  CI, with the sanitizers on; the icon decoder is still to be added.
- **On the console:** only what the host can't answer: the gates M1, M2 and
  M6, real installs, the running check, frame time. Runs are driven by the
  test mode (6.9), follow `ps5-agent-runbook`, and need the owner's go-ahead.
- **Never against a real install.** Destructive tests use dedicated test title
  IDs and their own folders.

---

## 11. Risks

| Risk | Effect | Answer |
| --- | --- | --- |
| The catalog's domain or deployment is compromised | Malicious installs with elevated privileges | Signed catalog, no going back to older data, GitHub-only downloads |
| The signing key leaks or is lost | Signatures stop meaning anything, or stores can't be updated | The store carries a second public key whose private half is kept offline; a written procedure to switch to it before release |
| Both signing keys are lost | No installed store can verify a catalog again until it is updated by hand | The keys don't expire, so the offline backups of both are the only safeguard; they are made when the keys are generated |
| A listed app turns out to be harmful | Users keep running it | Its record is removed; the store warns everyone who installed it through the store. Apps installed by hand aren't covered |
| A record is removed by mistake | False "no longer listed" warnings | Neutral wording, nothing is removed automatically, and the mark clears when the record returns |
| A hostile archive or response | Code execution in an elevated process | Limits, strict validation, fuzz tests, hash check before unpacking |
| Elevation unavailable on a user's setup | Nothing can be installed | First-run check and read-only mode with a clear explanation |
| A crash or power loss during an install | A half-written app | Staging outside scanned folders, single-rename activation, journal recovery |
| Replacing a running app's files | Console instability | The running guard; the store's own update handled separately |
| A drive's filesystem doesn't rename atomically | A mixed or missing app after an interruption | Only locations proven in M1 are offered |
| Developers not raising `contentVersion` | Updates never appear for their apps | Shown honestly as "no comparable version"; the catalog warns the developer |
| A developer replaces a release file | The download no longer matches | Refused by the hash check; the catalog's health check flags the listing |
| A drive is removed during an install | Failed install | Location checked before each step; the journal cleans up at the next start |
| GitHub or prosperostore.net unreachable | No installs | Offline browsing from the cache; retries; clear messages |
| Two copies of one title | ShadowMountPlus "duplicate titleId" | Refuse to install what is already present in any location |

---

## 12. Open questions for the owner

1. **Running check (D9):** which call reports that a title is running, and
   does it need anything beyond the filesystem elevation?
2. **Saved data on uninstall (D15):** keep it, as planned, or offer "also
   delete this app's data"?
3. **Pre-releases (D17):** offer them like any release, or add a "stable only"
   setting once the catalog can tell the store what the last stable release
   was?
4. **Unmanaged apps:** should the page of an app installed by hand still show
   that the catalog has a newer version, with no action, or show nothing?
5. **Languages** for the first release: the seven ProsperoEden has, or English
   first?
6. **Design:** start from the kit's `store` design as it is, or a variant that
   matches the website's Holo look?
7. **Recall wording:** is the neutral message in 5.11 what you want users to
   read, given the store can't say why an app was removed?

---

## Appendix A: API calls the store makes

| When | Request |
| --- | --- |
| Start, and on refresh | `GET /api/v1/index.json` |
| Start, and on refresh | `GET /api/v1/versions.json` |
| Opening an app | `GET /api/v1/apps/<TITLEID>.json` |
| Start, and on refresh, before anything else is trusted | `GET /api/v1/manifest.json` and `manifest.sig` |
| A tile or page needs an icon it doesn't have, or its `icon_hash` changed | `GET` the `icon_small` or `icon` address |
| Install or update | `GET` the app's `artifact_url` |

All with `If-None-Match` where a copy is held. The store sends a `User-Agent`
naming itself and its version.

## Appendix B: Where things are on the console

| Path | Holds |
| --- | --- |
| `/data/homebrew/PPSA99000/` | The store itself |
| `/data/prosperostore/settings.json` | Settings |
| `/data/prosperostore/receipts/` | One receipt per managed app |
| `/data/prosperostore/journal.json` | The transaction in progress, if any |
| `/data/prosperostore/cache/` | The last verified catalog files and the icons |
| `/data/prosperostore/logs/` | The log and crash reports |
| `<drive>/prosperostore/staging/`, `backup/` | Work folders, one set per drive |

## Appendix C: Third-party code the plan expects

To be confirmed and listed in `THIRD_PARTY_NOTICES.md` when each is added.

| For | Candidate |
| --- | --- |
| ZIP reading | zlib with a small reader, or miniz |
| JSON | A small, strict parser |
| SHA-256 | A small public-domain implementation, or the console's own |
| Ed25519 signature check | A small audited implementation such as Monocypher or TweetNaCl |
| QR codes | Project Nayuki's QR Code generator |
| Icons (PNG decode) | stb_image |

## Appendix D: Related repositories

### Implementation evidence

- 2026-10-02 | M0 | 24601ac | host | partial-pass: signed catalog, offline cache, sanitizers, lint, native build | build/candidate-tests.log | console startup
- 2026-10-02 | M0 | 61a38a3 | PS5 6.02 | failed: elevated startup, asset path failure, runtime released | results/m0-smoke-raw | correct post-elevation paths
- 2026-10-02 | M0 | fa52645 | PS5 6.02 | partial-pass: 4K, audio, controller, clean exit; catalog fails and timing spikes | results/m0-smoke-paths | worker logging, HTTPS diagnostics
- 2026-10-02 | M0/M2 | 59a7670 | PS5 6.02 | failed: 4K steady frames and app teardown; elevated TLS rejects root CA; console services lost after exit | results/m0-preserve-pem-roots | offline analysis; owner recovery requested, no automatic rerun
- 2026-10-02 | M0 | 6bbf1e5 | PS5 6.02 | inconclusive: two launches, shell close; services healthy | results/recovery-control-6bbf1e5 | correlate each run with a unique token
- 2026-10-02 | M0/M2 | d783154 | PS5 6.02 | partial-pass: signed online catalog, steady 16.68ms, clean exit, healthy | results/m2-pacbrew-curl | filesystem gate and artifact downloads
- 2026-10-02 | M1 | 59df017 | PS5 6.02 | failed: directory open EINVAL; shell closed title, services healthy | results/m1-storage-idle | isolate root open from openat
- 2026-10-02 | M1 | 785b4d9 | PS5 6.02 | failed: _openat EINVAL; M.2 BFS readable, clean exit, healthy | results/m1-storage-root | use direct-open component checks
- 2026-10-02 | M1 filesystem | 3afd238 | PS5 6.02 | passed: internal nullfs and M.2 BFS create/rename/cleanup, configured scan roots, signed online catalog; clean exit, healthy | results/m1-storage-direct | read-only gate and production location selection remain
- 2026-10-02 | M3 artwork | b3ac1fb | PS5 6.02 | incomplete: 12 icons loaded, recurring ~1.15 s frame stalls; shell switched to ProsperoLight before requested exit, final health check failed | results/m3-native-artwork-idle | investigate synchronous driver profile output; repeat lifecycle only on an idle console
- 2026-10-02 | M3 search/sort | db73b0c | host | partial-pass: search, sorting, square artwork snapshots and native build | build/search-final-build.log | qualify system keyboard on console
- 2026-10-02 | M3 performance control | 59a8868 | PS5 6.02 | inconclusive: launch rejected 0x80940033, no current-run startup; services healthy | results/m3-profile-control | diagnose registration before comparison
- 2026-10-02 | M5 inventory | 5e7c287 | host | partial-pass: bounded scan, receipt ownership, duplicates, installed/update views; sanitized tests, lint and native build | build/inventory-tests.log | console inventory and transactions remain
- 2026-10-02 | M3 startup | 59a8868 | PS5 6.02 | partial-pass: catalog, icons, own exit, healthy; controller rejected before a later launch, so launch attribution unresolved | results/m3-launch-context | require controller acknowledgement
- 2026-10-02 | M4 engine | (this commit) | host | partial-pass: verified download, archive rules, space checks, install/update/uninstall, journal recovery with a cut at every step, fuzzing; sanitized tests, native build | build/store-test.log | installer worker, queue and screens; console install
- 2026-10-02 | M8 look, notice | (this commit) | host | partial-pass: Storefront layout in Glass Orchard colours, coming-soon picture, update notice; snapshots reviewed, tests, native build | build/snapshots | console frame time with the aurora backdrop and glass
- 2026-10-02 | M4 installer | (this commit) | host | partial-pass: installer worker, queue, progress, cancel, recovery at start, page actions and uninstall confirmation in development builds; service test, snapshots, native build | build/store-test.log | first install on a console with a test title; running check (D9)
- 2026-10-03 | M4/M5 | 7c7ee17 | PS5 6.02, two consoles | passed: install (two archive layouts), update with staged metadata refreshed, uninstall, running titles listed; 600/600 frames at 60 Hz in every window after start, during a tour, an install, a six-texture budget and 1000 apps; six sessions, each ended by the app's quit, consoles healthy | session logs kept locally | queue screen, location setting, self-update
- 2026-10-03 | M6/M8 | (this commit) | PS5 6.02, second console | passed: Queue, Settings and About panel opened; the store updated itself from a test archive and the next start finished it; a hand-installed app taken over; keyboard opened and closed; frames at 60 Hz throughout | session logs kept locally | the old store starting once more after the swap is guarded in code, host-tested only
- 2026-10-03 | M3 performance | (this commit) | PS5 log + host | finding: frames over 100 ms (max 1.9 s) and repeated icon loads while moving, from texture create/delete on the frame; fix: pictures kept for the session, buffered log, splash to first frame, UI library at 456cf57; tests, lint on changed files, native build | results of the owner's session on the second console | measure on a console

| Repository | Role |
| --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | The template the store is created from; elevation and update check |
| [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) (private) | The UI library: renderer, components, themes, sound, the `store` design |
| [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) | The OpenGL 4.6 SDK both of them render with |
| [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog) | The catalog, its signed store API (`docs/api.md`) and version rules (`docs/versioning.md`) |
| [ps5-agent-runbook](https://github.com/blackbearreloaded/ps5-agent-runbook) (private) | The contract for every console run |
| [ps5-homebrew-dev-protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol) | The launch helper and console tooling |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Registers installed apps on the console |
