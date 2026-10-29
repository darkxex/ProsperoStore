<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoStore icon">
</p>

<h1 align="center">ProsperoStore</h1>

<p align="center">
  <strong>The native PS5 app store for <a href="https://prosperostore.net">prosperostore.net</a></strong>
</p>

<p align="center">
  Browse, install, update and uninstall PS5 homebrew from your couch, with the controller or a keyboard.
</p>

> [!TIP]
> **By default, ProsperoStore uses [prosperostore.net](https://prosperostore.net)**, the community
> catalog of PS5 homebrew. Browse it on the web at **[prosperostore.net](https://prosperostore.net)**, then
> install from the console with one button. The catalog is signed, and every download is checked
> against it before anything touches your console.

> [!IMPORTANT]
> **ProsperoStore is an aggregator.** It lists and installs apps; it doesn't build, maintain or
> support them. Each app belongs to its own developer. For a bug, a question or a feature request
> about an app, go to that app's GitHub repository (the QR code on its page in the store opens its
> page on [prosperostore.net](https://prosperostore.net), which links to the source) and contact its
> developer there. Issues here are for the store itself: browsing, installing, updating and
> uninstalling.

<p align="center">
  <img src="docs/media/home.jpg" width="900" alt="ProsperoStore's Discover screen on a PS5">
</p>

ProsperoStore is a homebrew app store that runs natively on the PS5. Title ID `PPSA99000`.
It is an alpha; the current version is the newest one on the
[releases page](https://github.com/darkxex/ProsperoStore/releases).

> [!WARNING]
> **ProsperoStore includes an exact-title one-shot helper built from upstream
> [PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon).** The PS5 jailbreak
> environment must provide [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
> ([1.7beta4](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4) or newer
> recommended) and a local ELF loader on TCP port 9021. If a resident Lapy service is already running, the store asks
> it first; otherwise it sends the packaged helper over that local connection, so no separate Lapy
> payload is needed. Installs, updates and uninstalls have run on firmware 6.02 and 12.70. Other
> firmware is experimental; the helper refuses runtime layouts it does not know instead of guessing.

## Features

- **The whole [prosperostore.net](https://prosperostore.net) catalog** - Discover shows one app big over
  shelves (New and updated, Apps, Games, Tools, Coming soon, On this console); each section has its
  own grid, with search by name or developer and sorting by name, release or update.
- **Likes and comments** - switched on in **Settings**, every tile shows the app's likes and
  comments, Discover gains a **Most liked** shelf, the grids a "Most liked" order, and an app's
  page its latest comments; see [Likes and comments](#likes-and-comments).
- **One-button installs** - Cross on an app's page downloads it, checks it against the signed
  catalog, unpacks it and puts it in place. A ring, four steps (Download, Verify, Unpack, Finish)
  and the time left show the progress; Circle cancels, and nothing changes until the end.
- **Updates** - apps installed with the store are offered their newer versions (the Updates
  section, or Update all). A running app is never touched until it is closed. An update replaces
  the app's whole folder; the previous folder, with anything you had put inside it, is kept in
  `/data/prosperostore/previous/<TITLE ID>` (or `prosperostore/previous` on the app's drive) until
  that app's next update, so files of yours are never lost. Move them back by FTP if you need them.
- **Clean uninstalls** - hold the Uninstall button: the app's folder goes and so does its
  home-screen tile. Its saved data stays.
- **Fast file work** - unpacking and removing run in a helper started through the payload loader,
  which the console does not slow down: Kodi's 2,700 files unpack in about 25 seconds.
- **Apps you installed by hand** - an app the catalog lists can be handed over to the store
  (Manage with ProsperoStore) and is updated from then on.
- **Any install location** - the folders ShadowMountPlus scans on the internal drive, an extended
  drive or a USB drive, chosen in **Settings**.
- **It updates itself** - when a newer ProsperoStore is listed, its own page offers it; see
  [Updating ProsperoStore](#updating-prosperostore).
- **Your language** - English, Spanish, French, Portuguese, Japanese, German, Dutch, Turkish and
  Simplified Chinese. The store starts in the PS5's own language (Spanish for Spain and Latin America
  alike) and falls back to English for any other; **Settings > Language** picks one. Only the store's own words change: an
  app's name, description and release notes stay as its developer wrote them.
- **Looks and feel** - each app's picture is made from its own icon's colours, with a soft
  opening animation, background music, interface sounds and springy motion. **Reduce motion** is
  in **Settings**.
- **Keyboard support** - a USB keyboard works as well as the controller; see [Controls](#controls).
- **A QR code on every app's page** - opens the app's page on prosperostore.net, with its release
  notes and source.

## Install

> [!TIP]
> **Use [ShadowMountPlus 1.7beta4](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4)
> or newer**, especially on newer firmware. It mounts ProsperoStore from the folder (or drive) you
> copy it to, and it also mounts `/data` and USB and extended storage drives into the app's
> sandbox. Older versions may not register the store or give it access to `/data`, which shows
> as "Read only" in the store.

1. Make sure the console runs ShadowMountPlus 1.7beta4 or newer and a payload loader on port 9021
   (see the warning above).
2. Download `PPSA99000.zip` from the release and unzip it. A release ZIP built by the workflow can
   be checked with `gh attestation verify PPSA99000.zip -R blackbearreloaded/ProsperoStore`
   (GitHub CLI); this covers releases built by GitHub Actions from now on, not earlier ones.
3. Copy the `PPSA99000` folder into a folder ShadowMountPlus scans, for example
   `/data/homebrew`, so that `eboot.bin` ends up at `/data/homebrew/PPSA99000/eboot.bin` (not one
   folder deeper).
4. Wait a few seconds for ShadowMountPlus to add it to the home screen, then open ProsperoStore.

> [!NOTE]
> **"Can't start the game or app" (CE-107750-0) after copying the folder?** The console only
> starts an app whose files are open to every user (permissions `777`), and what copies the files
> decides their permissions. Some FTP servers and programs upload them as `755` or `644`.
>
> - Set `PPSA99000` and everything inside it to `777` with your FTP program (often "File
>   permissions", applied to subfolders and files), then open the store again.
> - To avoid it next time: the [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) payload writes
>   uploads with the right permissions, and in WinSCP a transfer preset with "Set permissions"
>   `0777` does it for every upload.
> - Updates done from inside the store, and apps the store installs, are set to `777` for you.

> [!IMPORTANT]
> **Set the console's date and time correctly.** The store reaches the catalog and downloads apps
> over HTTPS, and a secure connection is refused when the console's clock is far off (the site's
> certificate then looks expired or not yet valid). With a wrong date the store stays offline,
> shows no apps or fails every download. Check **Settings > System > Date and Time** on the PS5;
> a console kept offline often has the wrong date. The store's debug log shows the clock it saw
> (`console clock:`).

The store keeps its own data in `/data/prosperostore` (settings, cache, install records and
logs), outside the app's folder, so updates keep it.

### Updating older ProsperoEden installations

For ProsperoEden (`PPSA99008`) installations that still keep personal game files
inside `assets/`, Store moves the known legacy folders (`roms`, `keys`,
`firmware`, `updates`, `mods`, `ryujinx`, and `save-import`) to
`/data/prosperoeden` during an update. Saves and settings are left unchanged.
The migration resumes through the update journal if it is interrupted.

Store refuses the update before replacing the app if a destination holds files,
a custom game-files folder is selected, settings cannot be read safely, or the
migration would cross drives. Resolve the reported conflict or move the files
to the chosen folder first.

An update made with ProsperoStore 1.000.050 or older left those folders in the
store's own `prosperostore/previous/PPSA99008/assets`, where ProsperoEden does
not look. The next update of ProsperoEden moves them to `/data/prosperoeden` by
the same rules, before that previous folder is replaced. If the same folder
holds files both there and in the installed app, the update is refused and
names both places: keep one of them, then update again.

See [migration validation](docs/eden-migration-validation.md) for automated checks,
controlled testing on an original/phat PS5 with firmware 4.50, and the reproducible
console test harness.

## Controls

| Action | Controller | Keyboard |
| --- | --- | --- |
| Move | D-pad or left stick | Arrows |
| Open, install, update | Cross | Enter or Space |
| Back, close | Circle | Escape or Backspace |
| Previous / next section (they go round) | L1 / R1 | Shift+Tab / Tab, or Page Up / Page Down |
| Close ProsperoStore | Hold Circle on the home page | Hold Escape |
| Search | Triangle | / or F3 |
| Downloads; hold to uninstall on an app's page | Square | Delete or F2 |
| Settings | Options | F10 or the Menu key |
| Sort | R3 | F5 |
| Like or comment, on an app's page | Touchpad | F6 |
| To an app's comments and back to the top, on its page | R2 (L2 also goes back) | F7 |

## Updating ProsperoStore

When [prosperostore.net](https://prosperostore.net) lists a newer ProsperoStore, the store asks when it
opens: **Update now**, **What's new** (the release notes) or **Skip**. The store's own page
(**Settings > ProsperoStore**) offers the update too. It downloads and unpacks like any app; then
the store closes, a small helper started through the payload loader puts the new files in place,
and a notification says when to open it again. The next start runs the new version. Versions
before 1.000.020 show a short notice at the top right instead of the question.

Updating from inside the store is the easy way. To update by hand, replace the `PPSA99000` folder
with the one from the new release; if the console then says it can't start the app, see the note
about permissions under [Install](#install).

## Likes and comments

Every app in the catalog has a discussion on GitHub, the same one its page on prosperostore.net
shows: the replies are the app's comments, and the upvotes of the discussion are its likes.

They are on from the first start; **Settings → Show likes and comments** switches them off.
While the switch is off the store asks GitHub for nothing and shows none of it. With it on:

- every tile shows the app's likes and comments, zero included;
- Discover has a **Most liked** shelf, the ten apps with the most likes in order (it is there
  when some app has a like), and each section's grid can be sorted by "Most liked";
- an app's page shows its likes and comments among its facts, and a **Comments** block after the
  release notes with the latest ten: who, when and what they wrote. R2 jumps straight to it,
  and R2 again or L2 goes back to the top;
- the Touchpad on an app's page shows a QR code for the app's discussion. Scan it with your
  phone to like the app or to write a comment there. Both happen on GitHub and need a GitHub
  account; the store itself never writes anything.

The store reads two kinds of file for this, from one address only:
`https://raw.githubusercontent.com/darkxex/ps5-homebrew-catalog/comments-data/`, the
counts of every app when the catalog is refreshed and one app's comments when its page opens.
These files are not part of the signed catalog and decide nothing about what is installed. What
they hold is shown as plain text, cut to length, and the only address taken from them is the
discussion's, which must be on github.com and is only ever shown as that code. When they can't
be read there are simply no likes and comments, and the rest of the store works as before.

## Custom background music

The store can play your own song under its screens instead of the one it ships with.

1. Get an MP3 file, mono or stereo, at any sample rate from 8 kHz to 96 kHz (44.1 kHz is fine;
   the store converts it to the console's 48 kHz).
2. Name it exactly `background.mp3`.
3. Copy it by FTP to `/data/prosperostore/`, the folder the store creates the first time it opens.
4. Close ProsperoStore and open it again. The file is read when the store starts.

The song then plays in a loop in place of the shipped one. To go back to the original music,
delete `background.mp3` and restart the store. If the file can't be played (it is damaged or isn't
an MP3), the store ignores it and keeps the shipped song.

**Settings → Background music** switches the music off and on, with your song or without it; the
music also stays quiet while **Sounds** is off.

## Reporting a problem

If the store shows no apps, stays offline or can't install something, switch on **Debug log** in
**Settings** (Options button) and repeat what failed. The log starts with what the store found
when it opened (the access it was given, the catalog), then records each step it takes: the
network requests and every install. The switch stays on the next time the store opens, wherever
the store can keep its settings.

Send one of these with your report:

- a photo of **About**, where the **Debug trace** section lists the steps (scroll for all of it);
- the file `/data/prosperostore/debug-trace.txt`, fetched by FTP;
- if the store couldn't write to `/data`: `ProsperoStore-debug-trace.txt` on a USB drive that was
  plugged in before the store was opened.

Say which console and firmware you have, your ShadowMountPlus version and your payload loader.
The log holds no account details or passwords. Switch **Debug log** off again afterwards: while
it is on, the store starts a little slower.

## Custom catalogs

**Settings → Catalogs** (its own room in the panel, also reached from the Catalogs row of
Settings) has two switches, one for each source:

- **Official catalog** - prosperostore.net, always signature-checked. On by default.
- **Custom catalogs** - your own catalogs, on GitHub or any HTTPS host. Switching it on asks for
  the first one when none is listed.

**Add a catalog** takes either of two forms:

- `owner/repository` - a GitHub repository laid out like
  [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog), whose GitHub Pages site
  publishes the store API. The repository's page address, `https://github.com/owner/repository`,
  is accepted too; the store reads it from `https://owner.github.io/repository/api/v1/`.
- The full HTTPS address of a catalog's API directory, on any host, for example
  `https://example.com/api/v1/`. A GitHub Pages address of that shape is kept as its repository.

Up to eight catalogs can be listed; each has its own row with its name (the repository, or the
address's host) and its address, and a **Remove** button, and each is read when the store opens.

With both switches on, the custom catalogs' apps are added to prosperostore.net's, in the order
the catalogs were added, and each app's page names the catalog it came from. Where two catalogs
list the same title ID, the earlier one's app is the one shown (prosperostore.net's first), and
the store says how many apps were hidden. With only the custom catalogs on, the first one
replaces the official catalog, which is what you want to test a new version of an app that is
already listed; the others still add to it. The store needs one catalog, so the official one can
only be switched off while a custom catalog is in use.

**Restore official catalog** goes back to prosperostore.net alone with signature checks on; the
catalogs stay listed, switched off. Changes are saved at once and apply when you close and reopen
ProsperoStore. A single `Custom catalog URL` saved by an earlier version is carried over as a
custom catalog.

**Verify custom catalog signatures** is on by default and uses the official catalog's built-in
public keys, so it suits a copy of the signed official feed. For an independently published
catalog you can turn it off after confirming the warning; the store then labels the catalog
**Signatures not checked**. Only do this with a publisher you trust: hashes detect changed
downloads, but an unchecked publisher controls both the download address and its expected hash.

A custom catalog must publish the [catalog API](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md)
in its API directory (`api/v1/` of a repository's GitHub Pages site): schema-3 `manifest.json`, `index.json`,
`versions.json`, and `apps/<TITLEID>.json`. A fork of ps5-homebrew-catalog builds it with its
own workflow; build it with `--base /<repository>/` so the files name the Pages address they are
served from. The manifest must list the SHA-256 of each API JSON file. `manifest.sig` is
required only while signature verification is on. API-file hashes, artifact hashes, ZIP
validation and HTTPS certificate checks remain enforced. A custom catalog's `artifact_url` may
point at GitHub releases or at that catalog's own host (the same host and port as its API
address, redirects included), never at another catalog's. That includes ProsperoStore's own
update when it comes from a custom catalog: with the official catalog on, its listing of
ProsperoStore is the one used. Plain HTTP, self-signed TLS certificates, arbitrary JSON lists,
and downloads from any other host are not supported.

Each API URL and signature mode has its own catalog cache. Re-enabling verification
never reuses an unchecked catalog; the official signed catalog retains its saved
rollback protection. Signature checks off also disables sequence rollback checks,
so development feeds can rebuild with a lower sequence. Store update notices use
the selected catalog, and app QR codes use its `page` URLs.

## Roadmap

Ideas for what a solid app store should do, in no promised order. Some need changes to the
[catalog](https://github.com/darkxex/ps5-homebrew-catalog) as well as to the store.

### Versions and updates

- **Install an older version** - pick a past release on an app's page and install it; an app held
  at a version is left alone by Update all.
- **Roll back in one press** - put back the previous version the store already keeps after an
  update, with the files you had inside it.
- **Skip or hold updates** - "don't offer this update" or "never update", for each app.
- **What's new before updating** - the release notes for every app in the Updates list, as the
  store already shows for itself.
- **Automatic updates** - optionally update apps when the store opens, all of them or only the ones
  you mark.

### Finding apps

- **Categories and tags** - emulators, media, streaming, utilities, beyond App, Game and Tool.
- **Collections** - curated shelves such as "Start here" or "Emulators".
- **Screenshots** - pictures of the app on its page.
- **Popularity** - download counts, and sorting by them.
- **Favourites** - a list kept on the console.
- **Search in descriptions** - not only names and developers.

### Trust and safety

- **What an app does outside its sandbox** - a short list on its page, from the catalog's scan of
  each release.
- **Firmware and requirements** - which firmware an app was tested on and what it needs
  (ShadowMountPlus version, Lapy, extra files), checked before installing.
- **Report a problem with an app** - a QR code to the app's issue page, with the console's firmware
  and the app's version filled in.

### Managing what is installed

- **Storage view** - the size of each app, the previous versions the store keeps and its caches,
  with a way to clear each.
- **Move an app** - between the console's storage, a USB drive and the extended drive.
- **App data** - show where an app keeps its data, and offer to remove it when uninstalling.
- **Back up and restore app data** - to a USB drive.
- **Repair** - check an installed app against the catalog and reinstall it when it differs.
- **Export the installed list** - and install everything again on another console.

### Downloads

- **Resume interrupted downloads** - large apps start again from zero today.
- **Queue controls** - reorder, pause and retry.
- **Install from a USB drive or a local ZIP** - checked against the catalog when the app is listed.
- **A notification when a download finishes** - through the PS5's own notifications.

### For developers

- **Beta channel** - opt in to an app's pre-releases.
- **Install a test build** - from a pull request or an address, in the Catalogs room.
- **Announcements** - a short message from the catalog shown when the store opens.

### Polish

- **First-run check** - a screen that tests elevation, ShadowMountPlus settings and the network,
  and says what to fix.
- **Open after installing (investigation)** - start an app from the store, if the console allows
  one title to launch another.

## Known limits of this version

- ZIP apps only; image files (ffpfsc) are listed but can't be installed.
- The store installs, updates and removes apps; it does not start them.
- The store updates itself only when it was installed as a folder (not as an image).
- An update is only offered for apps whose catalog entry lists a content version.
- The home-screen name of an app does not change after an update.
- English only.

## Source code

The store's source is in this repository: the app in `src/`, the file worker in `helper/`, the
host previews and checks in `host/` and `tests/`. Build on Linux (WSL works) with
`make DEVELOPMENT=1 app`, or `make app` for a release build without development requests.
`make test lint` runs the host checks. Every pull request gets an installable build named by its
number and commit: see [Pull-request builds](docs/PULL_REQUEST_BUILDS.md). [IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md) records
what was built and how it was verified on consoles, and [PLAN.md](PLAN.md) the design and
decisions. `FOUNDATIONS.json` pins the foundation sources, and `third_party/STORE_SOURCES.json` the
vendored libraries. The store reads the catalog API at `https://prosperostore.net/api/v1/`, specified
in [the catalog's `docs/api.md`](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md).

## Project foundation

> [!IMPORTANT]
> **The catalog is [prosperostore.net](https://prosperostore.net)**, built from
> [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog). Developers list
> their apps there, and ProsperoStore installs what it lists.

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)**,
> the native foundation also used by ProsperoEden and ProsperoLight: application structure,
> runtime, packaging, sandbox elevation and the self-update helper.

> [!IMPORTANT]
> **The interface is built with [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)**
> and drawn with [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl).

## Thanks

- [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) by drakmor puts installed apps on
  the home screen.
- [PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon) by mpereiraesaa gives
  the store access to `/data`.
- **Jones** ([X](https://x.com/Jonesskulls), [GitHub](https://github.com/AgentJonesy)), for helping
  with testing.
- szampan, for testing and feedback.

<!-- bbr-footer:start -->
<!-- Generated by ps5-homebrew-dev-protocol/scripts/readme-footer. Edit the template there, not here. -->

## Credits

Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom (ps5-payload-dev).
Third-party components, authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Copyright © 2026 BlackBearReloaded. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party components keep their own licenses.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.
<!-- bbr-footer:end -->
