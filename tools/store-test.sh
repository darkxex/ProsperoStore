#!/usr/bin/env bash
# ProsperoStore - Sanitized store-specific host checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/store-tests"
mkdir -p "$build"
python3 "$root/tools/store-smoke.py" --self-test
flags=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_locations_test.cpp" "$root/src/system/locations.cpp" -o "$build/locations-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/locations-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    -DSTORE_FONTS="\"$root/assets/fonts\"" \
    "$root/tests/store_fonts_test.cpp" "$root/src/gfx/font.cpp" -o "$build/fonts-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/fonts-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_storage_test.cpp" "$root/src/system/storage_probe.cpp" \
    "$root/src/system/locations.cpp" -o "$build/storage-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/storage-test"
clang++ -std=c++20 "${flags[@]}" -DSTORE_NATIVE_PATH_WALK=1 -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_storage_test.cpp" "$root/src/system/storage_probe.cpp" \
    "$root/src/system/locations.cpp" -o "$build/storage-native-path-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/storage-native-path-test"
objects=()
for name in monocypher/monocypher monocypher/monocypher-ed25519 yyjson/yyjson qrcodegen/qrcodegen; do
    object="$build/${name//\//_}.o"
    clang -std=c11 "${flags[@]}" -c "$root/src/third_party/$name.c" -o "$object"
    objects+=("$object")
done
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_catalog_test.cpp" "$root/src/catalog/catalog.cpp" "${objects[@]}" \
    -o "$build/catalog-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/catalog-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_inventory_test.cpp" "$root/src/system/inventory.cpp" \
    "$root/src/system/locations.cpp" "$root/src/catalog/catalog.cpp" "${objects[@]}" \
    -o "$build/inventory-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/inventory-test"
miniz=()
for name in miniz miniz_tinfl miniz_tdef miniz_zip; do
    object="$build/miniz_$name.o"
    clang -std=c11 "${flags[@]}" -w -c "$root/src/third_party/miniz/$name.c" -o "$object"
    miniz+=("$object")
done
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_install_test.cpp" "$root/src/install/archive.cpp" \
    "$root/src/install/files.cpp" "$root/src/install/transaction.cpp" \
    "$root/src/system/inventory.cpp" "$root/src/system/locations.cpp" \
    "$root/src/system/storage_probe.cpp" "$root/src/system/running.cpp" \
    "$root/src/catalog/catalog.cpp" \
    "$root/src/install/worker.cpp" \
    "$root/src/core/save_file.cpp" "${objects[@]}" "${miniz[@]}" -o "$build/install-test"
# The file worker as a host program: the same sources the console payload is built from.
clang++ -std=c++20 "${flags[@]}" -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror \
    -I"$root/src" "$root/helper/main.cpp" "$root/src/install/archive.cpp" \
    "$root/src/install/files.cpp" "$root/src/install/worker.cpp" "${miniz[@]}" -pthread \
    -o "$build/store-worker"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/install-test" "$build/store-worker"
# Fuzz the ZIP and JSON readers from valid seeds. STORE_FUZZ_SECONDS=0 skips it.
fuzz_seconds=${STORE_FUZZ_SECONDS:-20}
if (( fuzz_seconds > 0 )); then
    fuzzed=()
    for name in monocypher/monocypher monocypher/monocypher-ed25519 yyjson/yyjson \
        miniz/miniz miniz/miniz_tinfl miniz/miniz_tdef miniz/miniz_zip; do
        object="$build/fuzz_${name//\//_}.o"
        clang -std=c11 "${flags[@]}" -fsanitize=fuzzer-no-link -w -c \
            "$root/src/third_party/$name.c" -o "$object"
        fuzzed+=("$object")
    done
    clang++ -std=c++20 "${flags[@]}" -fsanitize=fuzzer -Wall -Wextra -Wpedantic -Werror \
        -I"$root/src" "$root/tests/store_fuzz.cpp" "$root/src/install/archive.cpp" \
        "$root/src/install/files.cpp" "$root/src/catalog/catalog.cpp" \
        "$root/src/social/comments.cpp" "${fuzzed[@]}" -o "$build/store-fuzz"
    rm -rf "$build/fuzz-corpus"
    mkdir -p "$build/fuzz-corpus"
    python3 - "$build/fuzz-corpus" <<'PY'
import io, json, sys, zipfile
from pathlib import Path
corpus = Path(sys.argv[1])
param = json.dumps({"titleId": "PPSA99500", "contentVersion": "01.000.001"})
for level, method in enumerate((zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w", method) as archive:
        archive.writestr("PPSA99500/", "")
        archive.writestr("PPSA99500/eboot.bin", b"program" * 64)
        archive.writestr("PPSA99500/sce_sys/param.json", param)
    (corpus / f"zip-{level}").write_bytes(b"\x01" + data.getvalue())
records = [
    param,
    json.dumps({"schema": 1, "titleId": "PPSA99500", "location": "/data/homebrew",
                "contentVersion": "01.000.001", "releaseTag": "v1", "sha256": "a" * 64,
                "installedAt": "2026-10-02T00:00:00Z"}),
    json.dumps({"schema": 1, "operation": "update", "state": "swap", "titleId": "PPSA99500",
                "location": "/data/homebrew", "contentVersion": "01.000.002",
                "sha256": "b" * 64}),
    json.dumps({"schema": 3, "apps": [{"titleid": "PPSA99500", "name": "App", "kind": "app",
                                       "status": "available", "format": "zip", "size": 1}]}),
    json.dumps({"schema": 3, "apps": {"PPSA99500": {"content_version": "01.000.001"}}}),
    json.dumps({"schema": 1, "apps": {"PPSA99500": {
        "discussion": 1, "url": "https://github.com/o/r/discussions/1", "likes": 2, "comments": 1}}}),
    json.dumps({"schema": 1, "titleid": "PPSA99500", "likes": 2, "comments": 1,
                "url": "https://github.com/o/r/discussions/1",
                "latest": [{"author": "someone", "date": "2026-10-12T00:00:00Z",
                            "text": "Plain text, at most 500 characters."}]}),
]
for index, record in enumerate(records):
    (corpus / f"json-{index}").write_bytes(b"\x00" + record.encode())
PY
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/store-fuzz" \
        -max_total_time="$fuzz_seconds" -max_len=65536 -rss_limit_mb=2048 -print_final_stats=0 \
        "$build/fuzz-corpus" > "$build/fuzz.log" 2>&1 || { tail -40 "$build/fuzz.log"; exit 1; }
    printf 'Fuzzing passed: %s\n' "$(grep -c '' "$build/fuzz.log") log lines, ${fuzz_seconds}s"
fi
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_cache_test.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/catalog/client.cpp" "$root/src/core/save_file.cpp" "${objects[@]}" \
    -o "$build/cache-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/cache-test"
# Likes and comments: the address rule, the plain-text rule and the two parsers.
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    -DSTORE_TEST_FIXTURES="\"$root/tests/fixtures/comments\"" \
    "$root/tests/store_comments_test.cpp" "$root/src/social/comments.cpp" \
    "$root/src/catalog/catalog.cpp" "$root/src/net/http.cpp" "${objects[@]}" \
    -o "$build/comments-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/comments-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_http_test.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/net/http.cpp" "${objects[@]}" -o "$build/http-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/http-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_icons_test.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/catalog/icons.cpp" "$root/src/core/image.cpp" "$root/src/core/save_file.cpp" \
    "${objects[@]}" -o "$build/icons-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/icons-test"
clang -std=c11 "${flags[@]}" -Wall -Wextra -Werror -c "$root/src/system/update_check.c" \
    -o "$build/update_check.o"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    -DSTORE_TEST_FIXTURES="\"$root/tests/fixtures/comments\"" \
    "$root/tests/store_service_test.cpp" "$root/src/app/service.cpp" \
    "$root/src/social/comments.cpp" \
    "$root/src/system/inventory.cpp" "$root/src/system/locations.cpp" \
    "$root/src/core/qr.cpp" \
    "$root/src/catalog/icons.cpp" "$root/src/catalog/catalog.cpp" \
    "$root/src/core/image.cpp" "$root/src/core/save_file.cpp" "$root/host/platform_host.cpp" \
    "$root/src/install/archive.cpp" "$root/src/install/files.cpp" \
    "$root/src/install/transaction.cpp" "$root/src/system/storage_probe.cpp" \
    "$root/src/system/running.cpp" "$root/src/install/worker.cpp" \
    "${objects[@]}" "${miniz[@]}" "$build/update_check.o" -pthread -o "$build/service-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/service-test"
clang++ -std=c++20 "${flags[@]}" -Wall -Wextra -Wpedantic -Werror -I"$root/src" \
    "$root/tests/store_curl_test.cpp" "$root/host/http.cpp" "$root/src/net/curl_request.cpp" \
    "$root/src/net/http.cpp" "$root/src/catalog/catalog.cpp" "${objects[@]}" \
    -lcurl -o "$build/curl-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    python3 "$root/tests/run_curl_tls.py" "$build/curl-test"
