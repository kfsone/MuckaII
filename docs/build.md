# Build

FACT This file pins the toolchains, dependencies, presets, targets and CI jobs. Nothing is built yet.
Layer and thread structure is in `docs/spec/architecture.md`. Versions were checked on 2026-10-01.
source: director

## CMake

DECISION `cmake_minimum_required(VERSION 3.28)` and `CMakePresets.json` schema version 8. 3.28 is
Ubuntu 24.04's packaged CMake; the windows-2025 runner ships 3.31.6; Slint needs 3.21. Rejected:
4.x as the minimum (schema 11-12), which no runner image ships by default.
source: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
source: https://docs.slint.dev/latest/docs/cpp/cmake
source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md

## Language standard

DECISION C++20: `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`
(`/std:c++20`, `-std=c++20`). It is the highest standard usable on all three toolchains today, and
Slint's C++ API requires it. libc++'s remaining C++20 gaps (parts of chrono time zones,
`atomic<shared_ptr>`, `<syncstream>` manipulators, parallel `shift`) are not used. Rejected: C++23. MSVC has only `/std:c++23preview`, documented as
not ABI-stable; the stable `/std:c++23` arrives with MSVC Build Tools 14.52 in November 2026. libc++
(Linux gate and NDK) still lists `std::generator`, `<stacktrace>` and several ranges papers as not
started.
source: https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version
source: https://devblogs.microsoft.com/cppblog/c-in-the-age-of-ai-visual-studio-at-cppcon-2026/
source: https://libcxx.llvm.org/Status/Cxx23.html
source: https://libcxx.llvm.org/Status/Cxx20.html
open: move to C++23 once MSVC 14.52 is on the CI runner image and NDK libc++ covers what we use.
default: stay on C++20.

## Toolchains

| Target | Compiler | Standard library | Pin |
|--------|----------|------------------|-----|
| Windows x64 | MSVC `cl` | MSVC STL | VS 2022 17.14 or VS 2026 |
| Linux x64 (gate) | clang 22 | libc++ | LLVM 22, latest 22.1.x from apt.llvm.org |
| Android arm64 | NDK clang | libc++ | NDK r30 (30.0.16248370, LTS) |

source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md
source: https://github.com/llvm/llvm-project/releases
source: https://github.com/android/ndk/wiki

DECISION Windows builds with MSVC `cl`. Slint's prebuilt Windows binaries are MSVC builds, and MSVC
ASan covers the Windows test run. UBSan and libFuzzer need clang, so the Linux clang job carries
them. Rejected: `clang-cl`, which adds no gate the Linux job does not already carry.
source: director

DECISION The Linux gate uses libc++ from LLVM 22, the release line nearest NDK r30's clang
(`clang-r574158c`, a rolling snapshot of LLVM main from October 2025, after LLVM 21 branched). This
narrows, but does not close, the gap between what Linux and the NDK accept; the android CI job is
the final check. Rejected: libstdc++, and LLVM 23.1.2 (latest), which is further from the NDK.
source: https://github.com/android/ndk/wiki/Changelog-r30
source: https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/mirror-goog-main-llvm-toolchain-source/README.md
source: https://github.com/llvm/llvm-project/releases

open: the exact LLVM commit of NDK r30's clang.
default: read `toolchains/llvm/prebuilt/*/clang_source_info.md` in the NDK during spike step 1.

open: the exact LLVM 22.1.x patch release on apt.llvm.org.
default: the latest 22.1.x at the time the CI job is written.

open: whether the windows-2025 runner gains VS 2026.
default: CI uses the image's VS 2022 17.14; the director's machine may use VS 2026.

## Dependencies

DECISION Every third-party dependency is fetched by `FetchContent` from a pinned release URL with
`URL_HASH`. Nothing third-party is committed, which keeps the repo's text files under the 7-bit rule.
CI caches `FETCHCONTENT_BASE_DIR`. Rejected: vendoring into `third_party/`; vcpkg and Conan, a second
build system for five packages.
source: director

| Dependency | Pin | Obtained | Used by |
|------------|-----|----------|---------|
| Slint | 1.18.1 | Windows: prebuilt MSVC x86-64 C++ package, `find_package(Slint)`. Android: source tag `v1.18.1` via `FetchContent` (Corrosion inside Slint's CMake) | `gui` |
| miniaudio | 0.11.25 | tag tarball | `audio` |
| SQLite | 3.53.4 | `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip` | `persistence` |
| FTXUI | 7.0.3 | tag tarball, target `ftxui::ftxui` | `tui` |
| GoogleTest | 1.18.0 | tag tarball | every `*_tests` target |

FACT Tag tarballs are `https://github.com/<repo>/archive/refs/tags/<tag>.tar.gz`. Tags were
confirmed with `git ls-remote` and tarballs hashed with `sha256sum` on 2026-10-01:

| Dependency | Tag | Tag commit | `URL_HASH` |
|------------|-----|------------|------------|
| Slint source | `v1.18.1` | `372cf0ee5577c3dfec309a45e7b778ba4e81b734` | `SHA256=fe485305ed303215e76c04918ee9aefbffbe229f18f979098ec36c7fa1dab28b` |
| miniaudio | `0.11.25` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | `SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274` |
| FTXUI | `v7.0.3` | `f921fad208912747c17d129a8ef75ec7624b6eec` | `SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88` |
| GoogleTest | `v1.18.0` | `063de7e9578f82b369302001269680b4b1553359` | `SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5` |
| SQLite | (zip) | n/a | `SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` (from the download page; the URL returned 200) |

source: https://www.sqlite.org/download.html

open: the hash of Slint's prebuilt Windows package.
default: hash the release asset when the Windows preset is written.

source: https://github.com/slint-ui/slint/releases
source: https://github.com/mackron/miniaudio/releases
source: https://www.sqlite.org/download.html
source: https://github.com/ArthurSonzogni/FTXUI/releases
source: https://github.com/google/googletest/releases

FACT Slint's runtime is Rust. Prebuilt binaries need no Rust toolchain. Building from source needs
Rust 1.92 or newer on the BUILD machine only. MuckaII's code contains no Rust.
source: https://docs.slint.dev/latest/docs/cpp/cmake

FACT Slint for Android builds from source, Skia renderer included, with API 26 as the minimum. Slint's
CMake maps the NDK ABI to the Rust target.
source: https://docs.slint.dev/latest/docs/slint/guide/platforms/mobile/android/

open: Android needs `rustup target add aarch64-linux-android` and a Gradle wrapper for packaging.
default: spike step 1 verifies both before any other Android work.

## Presets

| Preset | Toolchain | Builds | Global flags |
|--------|-----------|--------|--------------|
| `windows-msvc-debug` | MSVC | everything | `/fsanitize=address`; `/RTC1` and `/ZI` removed (MSVC rejects them with ASan); `/Zi` |
| `windows-msvc-release` | MSVC | everything | `/O2` |
| `linux-clang-asan-ubsan` | clang 22, libc++ | core libraries, `tui`, `bot`, tests; no Slint | `-stdlib=libc++ -fsanitize=address,undefined -fno-sanitize-recover=all` |
| `linux-clang-fuzz` | clang 22, libc++ | `mud` and its fuzz target only | `-stdlib=libc++ -fsanitize=fuzzer-no-link,address,undefined`; `-fsanitize=fuzzer` on the `muckaii_mud_fuzz` link only |
| `android-arm64` | NDK r30 | core libraries, `gui` | ABI `arm64-v8a`, API 26 |

RULE Warnings-as-errors is set per target on MuckaII's own targets only: `/W4 /WX` (MSVC),
`-Wall -Wextra -Werror` (clang). Third-party include directories are `SYSTEM`. Sanitizer flags and
`-stdlib=libc++` stay global, so dependencies are instrumented too.
enforced_by: none: judgement
source: director

RULE The Linux gate presets never build Slint, so no Rust and no uninstrumented library enters the
sanitizer runs.
enforced_by: none: judgement
source: director

## Targets

| Library | Test target | Extra |
|---------|-------------|-------|
| `muckaii_transport` | `muckaii_transport_tests` | loopback fake server |
| `muckaii_mud` | `muckaii_mud_tests` | `muckaii_mud_golden` (golden serialiser), `muckaii_mud_fuzz` (seeded from `docs/evidence/captures/`) |
| `muckaii_persistence` | `muckaii_persistence_tests` | |
| `muckaii_session` | `muckaii_session_tests` | injectable clock |
| `muckaii_client` | `muckaii_client_tests` | |
| `muckaii_audio` | `muckaii_audio_tests` | book-end frame positions |
| `muckaii_core` | `muckaii_core_tests` | ordering test at the fake server's received bytes |

| Executable | Frontend |
|------------|----------|
| `muckaii` | `gui` (Slint) |
| `muckaii_tui` | `tui` (FTXUI) |
| `muckaii_bot` | `bot` |

RULE `ctest` runs every `*_tests` target and `python scripts/check-goldens.py`.
enforced_by: none: judgement
source: director

## CI

| Job | Runner | Presets | Gates |
|-----|--------|---------|-------|
| windows | windows-2025 | `windows-msvc-debug`, `windows-msvc-release` | build, `ctest` under ASan |
| linux-gate | ubuntu-24.04 | `linux-clang-asan-ubsan`, `linux-clang-fuzz` | `ctest` under ASan+UBSan, fuzz run, clang-tidy, firewall tripwire |
| android | ubuntu-24.04 | `android-arm64` | build only; the job installs NDK r30, Rust >= 1.92 and `aarch64-linux-android` itself |

FACT The windows-2025 image lists NDK 27-29 only, so no runner image is assumed to carry r30.
source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md

DECISION Linux and macOS release artifacts are later, not launch items. Release builds for launch
are Windows and Android only.
source: director
