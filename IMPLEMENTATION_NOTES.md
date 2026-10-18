# ProsperoStore implementation notes

Status: 2026-10-03. Title ID: `PPSA99000`.

This is a development snapshot, not a release. Browsing, verified catalog
access, artwork, app details, search, the installed library, and **install,
update and uninstall** are implemented and have run on two consoles (firmware
6.02) on 2026-10-03.
`PLAN.md` remains the specification; the distinctions below separate implemented
code from features qualified on the console.

## Implemented

| Area | Current behavior |
| --- | --- |
| Native application | PS5 build and packaging, 4K rendering, controller navigation, audio, diagnostic logging, and development-only remote exit requests. |
| Storefront | "Farlight Prime" (approved 2026-10-03): Discover, Apps, Games, Tools, Coming soon, Installed, and Updates in the top bar with a gliding gold line. Discover is a stage that shows one app big (the featured apps, advancing every 8 s, or the focused tile) over shelves: Updates for you, New and updated, Apps, Games, Tools, Coming soon, On this console. The other sections are a grid of the same 16:9 tiles. The product page opens out of its tile; a glass panel holds the ring, the four install steps and the one action. Downloads, Settings and About are rooms with a side list (Square, Options, L1/R1). Farlight colours (Aurora Shelf); coming-soon apps without artwork show `assets/images/coming-soon.png`. |
| App pictures | The apps' own home-screen backgrounds are not used (3840x2160, up to 8.3 MB, missing for 4 of 15 apps; owner's decision). Each app's picture is made from its icon on the icon worker (`src/app/ambient.hpp`): the icon's two main hues painted as soft light over Farlight's dark in a 96x54 field (20 KB texture, about 0.14 ms per icon on a PC), kept under the icon budget. Its main hue is the colour the screen eases toward; grey icons get Farlight's violet. The stage shows the icon at 352 px, floating, over its field; tiles centre it on the field. |
| Motion and feel | Springs from the UI library throughout; the field drifts over 30 s, the stage icon floats, focus ticks are panned and lower shelves sound lower. Uninstalling is a hold (the kit's `HoldButton`: the button fills, a tap says "Hold to uninstall") instead of a question. A job that ends well plays the completion sound with its toast. **Reduce motion** (Settings) stops drift, floating and breathing and turns slides into fades. |
| Install engine | `src/install/`: verified download (size and SHA-256 from the signed catalog, three attempts), ZIP validation from the archive's directory, exact space checks, unpacking into staging, single-rename activation, update by backup swap, uninstall, receipts, a journal and start-up recovery. After an update the console's own copies of the app's `sce_sys` (`/user/appmeta/<TITLEID>`, `/user/app/<TITLEID>/sce_sys`) are refreshed, so a new icon or background reaches the home screen. |
| Archive layouts | The app is the one folder in the ZIP that holds `sce_sys/param.json` and `eboot.bin`: at the top, in a folder named after the title, or up to three folders deep. What lies outside it is never unpacked. All fifteen ZIP releases in the catalog on 2026-10-03 fit (three different layouts). |
| Running titles | A running title has a folder `<TITLEID>_<n>` in `/mnt/sandbox`; the store lists that folder every two seconds. A running app's update and uninstall are refused with "Close it first", checked again right before its folder is touched. If the folder can't be listed, nothing installed is changed. |
| Installer worker | Only when elevated. One transaction at a time on its own worker, a queue of up to 16, journal recovery before the first job, an inventory rescan after each, and a result notice. The product page's button installs into `/data/homebrew`, shows the phase and a progress bar, and cancels. Uninstall is a hold of its button; closing the store during a job asks first. After an uninstall the console drops the title (libSceAppInstUtil `sceAppInstUtilAppUnInstall`), so its home-screen tile and its `/user/app` and `/user/appmeta` copies go; nothing under `/data` is touched. The file worker does it (`unregister` in `install/worker.hpp`): on firmware 12.70 the store itself may not load the library once elevated (0x80020063), and a payload that imports it never starts, so the worker loads it by path and finds its two functions with the payload SDK's `kernel_dynlib_dlsym`. The store's own call stays as the fallback (it works on 6.02). Verified 2026-10-04 on .30 (6.02) and .40 (12.70) with PPSA99109: rc 0, entries gone, a reinstall registered again; earlier on .30 a marker under `/data` was kept. A job shows on the app's card and in the top bar. |
| Queue, Settings, About | Options opens a panel with three tabs (L1/R1). **Queue**: the running job with its progress, what waits behind it, Cross cancels, and what finished since the store was opened; Square opens it directly. **Settings**: install location (the folders ShadowMountPlus scans that exist on this console, with free space), the check for a newer store, sounds, vibration, reduce motion, and the store's own version; kept in `/data/prosperostore/settings.txt`, written by a worker. **About**: what the store is, how it keeps installs safe, the notice about developers' responsibility, where its files are, credits. |
| The store's own update | From its own page (Settings leads there), through the boilerplate's self-update kit (`examples/self-update*`, imported at 2966cac as ProsperoEden uses it). The offer is the store's own verified catalog record. The store sends `self-updater.elf` (in its folder) to the payload loader, streams the release ZIP to it while downloading (its libcurl transport, GitHub only), and the helper checks the size and SHA-256, unpacks it beside the store and reports "staged". The store gives the go-ahead and closes after 3.5 s; the helper waits until the store's sandbox is gone, swaps the top-level entries of the store's folder (the folder ShadowMountPlus mounted stays, so the next start runs the new version with no remount or restart), refreshes the console's `sce_sys` copies and posts a system notification. Verified on .30 and .40 on 2026-10-04 (01.000.000 -> 01.000.010 from a development archive; files byte-for-byte the new build, the next launch showed 01.000.010). Repeated the same day with the release downloaded from GitHub (a temporary public repository, deleted afterwards), on both consoles. Development builds take the archive from `/data/prosperostore/dev/self.zip`, or from the GitHub address in `dev/self.url` with `dev/self.size` (request `selfupdate <version>`). The older in-place swap remains only for an interrupted job found at start. |
| Hand-installed apps | An app that was installed by hand, is listed in the catalog and sits in a scanned folder named after its title can be handed over from its page ("Manage with ProsperoStore", after a question that says an update replaces the whole folder). Only a receipt is written. From then on it is updated and uninstalled like any other. |
| Update notice | Once per launch, after the catalog refresh, the boilerplate's update-check decision code runs over the store's own HTTPS for `PPSA99000`. A newer listed version shows a top-right notice for ten seconds. Unknown shows nothing. |
| Catalog trust | Ed25519 verification of the catalog manifest, SHA-256 verification of catalog documents, bounded JSON parsing, schema checks, sequence rollback protection, and verified offline cache. |
| HTTPS | Elevated PacBrew curl/OpenSSL with certificate verification using the console CA list, URL/redirect restrictions, bounded responses, cancellation, and connection reuse. |
| Artwork | Background loading, persistent cache, bounded PNG decoding, a bounded result queue, one texture upload per frame. Pictures are kept: 160 icons, 12 full-size pictures and 12 QR codes; past that the one longest off screen gives its texture to the new one (about 1 ms on the console). A picture fades in over its placeholder. |
| Scale | Built for up to a thousand apps: only the rows in view are drawn and asked for, card text is fitted when first drawn, names are folded once for sorting and search. |
| Search and sorting | Triangle opens the native keyboard; name/developer filtering and R3 sorting by name, newest release, or recently updated. Native keyboard interaction still needs console qualification. |
| App details | Scrollable verified metadata and release notes, full-size artwork, retry behavior, and a QR code linking to the app's canonical prosperostore.net page. |
| Installed inventory | Read-only bounded scans using ShadowMountPlus locations/depth, folder metadata parsing, image-file listing, duplicate-title detection, and receipt-based ownership. |
| Updates view | Newer catalog versions are shown for managed installations, and the app page updates them. |
| State storage | Persistent store state uses `/data/prosperostore` after elevation: cache, logs, receipts, and development controls. Browsing falls back to memory when elevation is unavailable. Pre-elevation sandbox logging is disabled. |
| Filesystem probes | Development checks inspect configured locations and test create/rename/cleanup behavior. These are not yet the production location-selection workflow. |

## Implementation structure

- `src/app/`: screen state and background service orchestration.
- `src/catalog/`: signed catalog parsing/client, cache, versions, artwork, and
  installed metadata/receipt parsing.
- `src/net/`: HTTP policy and the shared curl request implementation.
- `src/install/`: archive validation and unpacking, link-refusing file helpers,
  and the journaled install/update/uninstall transactions with recovery.
- `src/system/`: scan-location policy, filesystem probes, and installed inventory.
- `src/main.cpp`: native application lifecycle and delivery of worker results to
  the UI; `host/store_main.cpp` supplies host previews and screen checks.
- `tests/store_*` and `tools/store-test.sh`: store-specific host checks.
- `tools/store-smoke.py`: locked, bounded console validation with exact title,
  artifact verification, run tokens, exit checks, and service-health checks.
- `tools/store-deploy.py` and `tools/store-console.py`: FTP-only install of a
  frozen build verified by hash, and one scripted session (launch, requests such
  as `install`, `uninstall`, `tour`, `stress`, clean quit, log collection).

The background service currently uses separate catalog/detail and icon workers.
The render thread consumes a bounded result queue. Curl handles retain reusable
connections while resetting request-specific options. Workers stop before curl
cleanup. Icon requests use the cache before the network; first-run downloads can
still take longer, but current hardware evidence does not establish final
cold-cache versus warm-cache loading times.

Inventory results are advisory snapshots. Future transactions must revalidate
paths, ownership, running status, and space immediately before changing files.
Receipts must match title ID, location, and installed version to establish
ownership. Duplicate IDs disable management. Image files are listed without
mounting them and currently have no resolved title identity. Local-only artwork,
full image discovery parity, and stale-receipt cleanup remain incomplete.

## Foundations and dependencies

`FOUNDATIONS.json` records the complete adopted file sets and hashes:

- Native boilerplate: `209914dc8a5ee67ff0bd044dfd40d18f877eae2c`.
- Homebrew UI: the commit in `FOUNDATIONS.json` (the store's eight local UI commits
  rebased on the library's `456cf57`: grid over a count, held-step, program build
  times, `mkstemp` for the shader cache, splash held to the first frame).
- Rendering uses the pinned ps5-opengl SDK 1.0.2 tooling configuration.

Foundation files are imported as whole pinned sets. Generic fixes belong in the
foundation first; store-specific screens, catalog, system policy, and installer
logic belong here. The adopted source is included in this repository. Foundation
development commits may still be local to their separate repositories; uploading
this snapshot does not imply those repositories were published or updated.

Vendor versions, licenses, and source hashes are recorded in
`third_party/STORE_SOURCES.json`, `third_party/STORE_NOTICES.md`, the other
third-party notices, and `assets/licenses/`. No proprietary system libraries or
local SDK cache are included in this source snapshot.

## Validation completed

- Signed-catalog, cache, HTTP/TLS policy, icon, filesystem, and worker checks have
  passed during development, including AddressSanitizer/UndefinedBehaviorSanitizer
  runs. TLS tests cover connection reuse and rejection of invalid trust/hosts.
- The inventory change (`5e7c287`) passed its sanitized parser/inventory tests,
  integrated service checks, targeted clang-tidy checks, and native build.
  Checks cover receipt mismatches, duplicates, scan depth, cancellation,
  symlink rejection, and malformed/oversized metadata.
- Host previews were inspected for square artwork, search/empty states, details,
  installed titles, and updates. A rendered QR code was independently decoded.
- On PS5 firmware 6.02, earlier committed candidates demonstrated elevated
  catalog access, 4K rendering, artwork loading, and token-matched clean exit.
- Internal storage and M.2 filesystem create/rename/cleanup probes passed.
  This does not qualify every drive, failure mode, or read-only case.
- The latest frozen console candidate (`59a8868`) loaded the verified catalog
  and 12 icons, exited on its own development request, and left services healthy.
  After an initial loading spike, observed frame batches averaged 16.68 ms.

- 2026-10-03, two consoles, six scripted sessions, each ended by the app's own
  quit with the console healthy afterwards:
  - **Install**: Prospero Vibrate (2.7 MB, through GitHub's redirect) installed
    from the app page; ShadowMountPlus registered it half a minute later.
    Prospero Explorer (app files at the top of the ZIP) installed too.
  - **Update**: the same app, set one version back on the console, was found as
    an update and replaced; receipt and the console's staged `param.json` followed.
  - **Uninstall**: folder and receipt gone, nothing left in the work folders.
  - **Running check**: the store lists `/mnt/sandbox` and finds itself there.
  - **Frames at 4K**: after the first second, every 600-frame window had all
    600 frames in the 60 Hz bucket (worst 17.6 ms) while a tour moved the focus
    and opened pages, during an install, with the icon budget cut to six
    textures, and with the catalog multiplied to 1000 apps (one 30 ms frame
    when the 1000 were set).
  - Texture cost on the frame: create about 1.0 ms, new pixels 0.9 ms, delete 0.02 ms.

  - **The store's own update** (second console): a build one version up was put on
    the console as an archive; the running store swapped its own folder and kept
    running at 60 Hz; ShadowMountPlus logged the source as removed and remounted
    the new folder about a minute later; the next start finished the job
    (receipt written, old folder removed). The start right after the swap still
    ran the old store, which led to the guard described above (host-tested).
  - **Hand-over**: ProsperoRadio, installed by hand, was taken over (a receipt
    and nothing else), then the receipt was removed again.
  - The system keyboard was opened by a scripted request and closed at quit
    without trouble; typing in it has not been tried.

An update refused because the app is running, and a power cut during a
transaction, have not been exercised on a console (both are covered by host
tests). After an update ShadowMountPlus needs up to about a minute to remount
the app's folder; the notice says to give the console half a minute.

Raw build logs, screenshots, console captures, and generated packages remain
local in ignored `build/`, `results/`, and `dist/` directories. `PLAN.md` contains
the compact milestone index; those local evidence paths are not GitHub downloads.

## Known limitations and unresolved investigations

1. Automated launch works with the protocol's launch controller once the title
   is registered (six of six sessions on 2026-10-03); the controller's reply can
   still read `0x80940010` while the app starts, so the runner waits for the
   app's own first-frame line instead of trusting the reply.
2. The stalls seen on 2026-10-03 (frames over 100 ms while moving, icons loaded
   up to nine times) are gone on the console: see the frame results above. The
   cause was work on the frame around pictures and an unbuffered log on `/data`.
3. Installed scans run after catalog refresh attempts, so network failures can
   delay initial library discovery. Local-only entries currently use placeholders.
4. The home screen's title name comes from the console's database and is not
   changed by an update (icon, background and `param.json` copies are). Image
   artifacts can't be installed. USB locations are offered when ShadowMountPlus
   scans them, but only internal storage has carried a real install.
5. A first-run check, languages, a soak test and a release remain open.
6. The console limits an app's own writes: after a few hundred megabytes the
   store's process is held to about 2 MB/s and each file it creates or removes
   takes 90 to 150 ms (measured 2026-10-03 with the development request
   `fsbench`; a RetroArch unpack took 402 s whatever the number of threads). A
   process started through the payload loader is not limited (1 GB at a steady
   6 ms per file). The heavy file work is therefore done by the file worker,
   `helper/main.cpp`, built as `store-worker.elf` and sent to the loader on
   port 9021 for each job: it saves the download, unpacks, and removes folders,
   and only inside `<drive>/prosperostore`. The store keeps every decision,
   the checks, the journal and the swap, and does the work itself when no
   worker can be started. Measured on the console with Kodi (64 MB, 2,688
   files): unpack 24 to 26 s (54 s throttled), removal 19 to 20 s (309 s
   throttled), the same on back-to-back runs. An update through the worker is
   covered by the host tests only.
   Console checks of 2026-10-03 with small apps (development requests
   `updateall`, `dieat <step>`, a progress line once a second):
   - Time left: "about 20 s left", "about 10 s left", "a few seconds left"
     during a 30 MB download.
   - Update all: three apps listed, two updated, the one that looked running
     (a stand-in folder in the sandbox list; no second app can run beside the
     store) passed over; its uninstall was not offered and the installer itself
     answered "Close the app first".
   - Stop dead mid-update (`dieat moved-old`: the installed version moved
     aside, the new one not yet in place): the next start logged
     `recovery ok=1 operation=update` and the previous version was back, with
     its receipt and empty work folders.
   - Self-update guard: after the store swapped its own folder, two starts
     (at once, and 100 s later) still ran the old store; both left the journal
     and the kept folder alone. ShadowMountPlus remounted the new folder about
     four minutes after the swap, so the new store finishing the update was
     not seen again in this run (it was on the morning of the same day).
   Not checked: typing on the system keyboard (needs a hand on the
   controller), and a USB location (no drive attached to the test console).
7. Open, high priority: the test console stopped with a kernel panic at the
   store's normal exit after about a dozen sessions in one day (2026-10-03).
   Suspected cause, not proven: the elevation helper puts the system's root
   folder into the process's root and jail entries without taking a reference,
   so every exit of the elevated store gives back two references it never held.

## Remaining work under PLAN.md

| Milestone | Remaining qualification or implementation |
| --- | --- |
| M1 | Production location selection and read-only/unavailable-drive gates. |
| M2 | Real artifact verification, bounded download streaming, retry/resume qualification. |
| M3 | Native keyboard, latest artwork/cache behavior, offline browsing, and performance qualification. |
| M4 | Done on a console. |
| M5 | Update, uninstall and the running-title guard are done. Remaining: image handling, stale-receipt cleanup, recall actions. |
| M6 | Done on a console from a test archive; a real one needs the store's first release in the catalog. |
| M7 | Journal recovery, interruption simulation at each mutation, removed-drive and network-failure handling. |
| M8 | Settings, About and its notice are done. Remaining: first-run check, languages. |
| M9 | Adoption of the shared update-check kit by another Prospero app. |
| M10 | Full review, soak tests, release packaging/tagging, and catalog publication. |

The user requires store-owned files under `/data/prosperostore`. PLAN.md also
describes same-filesystem staging/backup on other installation drives. Resolve
that location constraint before implementing external-drive transactions;
cross-filesystem renames cannot provide the required atomic activation.

## Build and verification commands

Use the configured WSL environment and repository root:

```sh
make DEVELOPMENT=1 app
make test lint
make host-snapshots
python3 tools/check-foundations.py
python3 tools/store-smoke.py --self-test
```

Console tests additionally require the configured protocol and UI transport
checkouts, an idle console, an owned environment lock, an exact committed
candidate, and bounded teardown/health verification. Do not interpret a successful
TCP payload transfer as a successful launch. No release or deployment is implied
by uploading this source snapshot.
