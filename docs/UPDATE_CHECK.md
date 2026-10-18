# Update check

An app listed on [prosperostore.net](https://prosperostore.net) can tell its user that
a newer release exists. This optional example is that check: two files you
copy into your project, and a small title that exercises them on a console.

It asks the catalog one question, over HTTPS, about the running app only. It
downloads nothing else, installs nothing, and works inside the normal app
sandbox: no elevation and no loader service.

## Use it in your app

Copy `examples/update-check/update_check.h` and `update_check.c` into your
sources. They depend on nothing else in this repository and compile as C11 or
as C++.

In an app built from this template that is one command, because every C and
C++ file under `src/` is compiled automatically:

```bash
cp examples/update-check/update_check.h examples/update-check/update_check.c src/
```

The copies build with `make` and pass `make lint` unchanged. In another
project, add the two files to your build; they need only the console's own
`libSceHttp`, `libSceSsl`, `libSceNet` and `libkernel`.

```cpp
#include "update_check.h"

// On a worker thread, once per launch:
update_check_result result;
update_check_run_self(&result);
if (result.state == UPDATE_CHECK_AVAILABLE)
    show_notice("Update available: %s", result.version);
```

`update_check_run_self` reads the app's own title ID and `contentVersion` from
`/app0/sce_sys/param.json`, so there is nothing to configure and nothing to
keep in sync with a release. `update_check_run(title_id, installed, &result)`
does the same for values you pass.

| `result.state` | Meaning | What to do |
| --- | --- | --- |
| `UPDATE_CHECK_AVAILABLE` | The catalog lists a higher content version | Tell the user. `result.version` is the release's name, `result.page` its page on prosperostore.net. |
| `UPDATE_CHECK_UP_TO_DATE` | The catalog has nothing newer | Nothing. |
| `UPDATE_CHECK_UNKNOWN` | No answer, or one that can't be used | Nothing. `result.reason` says why, for your log. |

The check is for apps that are **listed in the catalog**. Before your app is
listed, its title ID isn't known there and every check answers "not listed",
which is `UPDATE_CHECK_UNKNOWN`. To get listed, see the catalog's
[submission guide](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/submitting.md).

Rules that keep it harmless:

- **Never on the main thread.** The call blocks for up to five seconds per
  network phase. Run it on a worker and let the app start without it.
- **Once per launch** is enough; the catalog changes a few times a day.
- **Unknown means silence.** No network, an app that isn't listed, a catalog
  that doesn't know the app's content version: all give
  `UPDATE_CHECK_UNKNOWN`, and none is an error worth showing.
- **Only notify.** The check never downloads or installs. Point the user to
  ProsperoStore or to the app's page.
- **One check at a time.** The function isn't reentrant.

## What makes an update visible

The catalog compares **content versions**: the `contentVersion` in your
`sce_sys/param.json`, in the PlayStation format `NN.NNN.NNN` (`01.000.070`). An
update exists when the catalog's value for your newest release is higher than
the one in the running app. So:

- raise `contentVersion` in every release, and build from that commit;
- keep `sce_sys/param.json` in your repository, so the catalog can read it at
  your release tag.

The catalog's
[App versions](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/versioning.md)
page has the full rules, and its
[Store API](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/api.md)
page specifies the file this check reads
(`https://prosperostore.net/api/v1/apps/<TITLEID>.json`).

## How it works

1. Build the address from the title ID. Anything that isn't four capital
   letters and five digits is refused before a request is made.
2. `GET` it through the console's own `sceHttp`, with certificate verification
   on (server, name, validity dates, known authority, SNI), no redirects, and a
   `User-Agent` naming the app: `homebrew-update-check/1 (<TITLEID>)`.
3. Refuse an answer larger than 64 KiB, a status other than 200, and anything
   that isn't one complete JSON object.
4. Read `status` and `content_version`. A reservation (`coming_soon`) and a
   `null` version are "unknown".
5. Compare the two content versions as three numbers.

Everything read from the network is treated as hostile: lengths are checked,
values are copied only into buffers that hold them, nesting is bounded, and an
answer cut short in transit is refused as a whole. The `sceHttp` contexts are
created for the request and destroyed after it, so the check leaves nothing
running. It uses about 5 MiB while the request is in flight.

`update_check_run_with` takes a transport of your own, for an app that already
has an HTTP client, and is what the tests use.

## The example title

`make update-check-example` builds `dist/PPSA99780/`, a title with no
interface that checks itself and the titles in
[`examples/update-check/assets/targets.txt`](../examples/update-check/assets/targets.txt),
then reports each answer three ways:

- a line in the kernel log, starting `UPDATE-CHECK:`;
- the same line in `/download0/update-check.txt`;
- one notification with the number of requests answered.

```text
UPDATE-CHECK: PPSA99002 installed=01.000.000 state=update-available reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://prosperostore.net/app/PPSA99002/ ms=412
```

The example's own title ID isn't in the catalog, so its self check answers
`not-listed`: that is the expected result, and it shows the 404 path working.
Edit `targets.txt` to check other titles.

Two build definitions are for scripted console runs:

| Definition | Effect |
| --- | --- |
| `UPDATE_CHECK_RUN_TAG=<word>` | Printed in the first line, to tell runs apart |
| `UPDATE_CHECK_EXIT_AFTER=<seconds>` | The title ends itself that long after reporting, through `sceSystemServiceLoadExec("exit")`. Without it the title stays up until it is closed from the home screen. |

```bash
APP_DEFINITIONS="UPDATE_CHECK_RUN_TAG=run1 UPDATE_CHECK_EXIT_AFTER=30" make update-check-example
```

## Tests

```bash
make test-update-check
```

Builds the two files into a host test with the address and undefined-behaviour
sanitizers and checks version parsing and ordering, the address builder, the
JSON reader (escapes, nesting, wrong types, oversized and malformed input), the
decision for every kind of answer, the mapping of HTTP statuses and transport
failures, and every truncation and thousands of single-byte mutations of a real
API answer. Host tests can't establish that the console's HTTPS reaches the
catalog; the example title does that.

## Console validation

Run once on a PS5 on 2026-10-02 (the example title built with
`UPDATE_CHECK_RUN_TAG=c2 UPDATE_CHECK_EXIT_AFTER=60`, `eboot.bin` SHA-256
`ab1dde8bec0fde046bcd26ec0a0dc02d2ecccbde6eaa8fe00c1017594ae6a1fe`), installed as a folder under `/data/homebrew` and registered by
ShadowMountPlus. The console's firmware version wasn't recorded in this run.

All five requests were answered over HTTPS with certificate verification on,
through the title's own sandbox with no elevation:

```text
UPDATE-CHECK: start tag=c2 host=prosperostore.net
UPDATE-CHECK: self installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=154
UPDATE-CHECK: PPSA99002 installed=01.000.000 state=update-available reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://prosperostore.net/app/PPSA99002/ ms=109
UPDATE-CHECK: PPSA99002 installed=99.999.999 state=up-to-date reason=ok http=200 error=0x00000000 available=01.000.070 version=01.000.070 page=https://prosperostore.net/app/PPSA99002/ ms=94
UPDATE-CHECK: PPSA99009 installed=01.000.000 state=unknown reason=not-available http=200 error=0x00000000 available=- version=- page=- ms=71
UPDATE-CHECK: PPSA00000 installed=01.000.000 state=unknown reason=not-listed http=404 error=0x00000000 available=- version=- page=- ms=90
UPDATE-CHECK: done requests=5 answered=5
```

| Checked | Result |
| --- | --- |
| The catalog is reachable with the console's own HTTPS and certificate store | Yes: five answers, 71 to 154 ms each, including creating and destroying the `sceHttp` contexts |
| The title's own `param.json` is readable at `/app0/sce_sys/param.json` | Yes: the self check used its title ID and content version |
| Each kind of answer is decided correctly | Update available, up to date, coming soon, and not listed all matched what the catalog held |
| The report file in `/download0` | Written, and identical to the kernel log lines |
| The title ends itself | Yes, 60 seconds after reporting, through `sceSystemServiceLoadExec("exit")`: the kernel log shows `Kill for LoadExec ... => 0` and a normal unload, with no core dump and no forced kill |
| The console afterwards | Services answering, installed files unchanged two minutes later |

Not exercised on hardware: a failing network (the timeout and error paths are
covered by the host tests only), an oversized answer, and calling the check
from a worker thread beside a running renderer.
