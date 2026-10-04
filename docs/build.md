# Build

FACT Versions below were checked on 2026-10-01 unless a line says otherwise. What exists is under
"Scaffold".
source: agent

## CMake

DECISION `cmake_minimum_required(VERSION 3.28)` and `CMakePresets.json` schema version 8. 3.28 is
Ubuntu 24.04's packaged CMake; Slint needs 3.21. Rejected: 4.x as the minimum (schema 11-12), which
Ubuntu 24.04 does not package.
source: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
source: https://docs.slint.dev/latest/docs/cpp/cmake

open: the lowest CMake that knows the generator `Visual Studio 18 2026` (the Windows presets need it;
the 3.28 minimum is for the Linux gate).
default: not pinned; CMake 4.3.1 (the director's machine) and 4.4.3 (the `windows-2025-vs2026` image)
both have it.

## Language standard

DECISION C++20: `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`
(`/std:c++20`, `-std=c++20`): the highest standard all three toolchains support (the director's
rule, `INTERNAL.md`), and Slint's C++ API requires it. libc++'s remaining C++20 gaps (parts of
chrono time zones, `atomic<shared_ptr>`, `<syncstream>` manipulators, parallel `shift`) are not
used. Rejected: C++23. MSVC has only `/std:c++23preview`, not ABI-stable; stable `/std:c++23`
arrives with MSVC Build Tools 14.52 in November 2026. libc++ (Linux gate and NDK) still lists
`std::generator`, `<stacktrace>` and several ranges papers as not started.
source: https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version
source: https://devblogs.microsoft.com/cppblog/c-in-the-age-of-ai-visual-studio-at-cppcon-2026/
source: https://libcxx.llvm.org/Status/Cxx23.html
source: https://libcxx.llvm.org/Status/Cxx20.html
open: move to C++23 once MSVC 14.52 is on the CI runner image and NDK libc++ covers what we use.
default: stay on C++20.

open: `std::expected` is C++23. Which expected type the code uses.
default: a small in-house header-only `Expected<T, E>`, not yet written; until then, write no code
that needs an expected type.

## Toolchains

| Target | Compiler | Standard library | Pin |
|--------|----------|------------------|-----|
| Windows x64 | MSVC `cl` | MSVC STL | Visual Studio 2026 (VS 18) only |
| Linux x64 (gate) | clang 22 | libc++ | LLVM 22, latest 22.1.x from apt.llvm.org |
| Android arm64 | NDK clang | libc++ | NDK r30 (30.0.16248370, LTS) |

source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md
source: https://github.com/llvm/llvm-project/releases
source: https://github.com/android/ndk/wiki

DECISION Windows builds with MSVC `cl`. Slint's prebuilt Windows binaries are MSVC builds, and MSVC
ASan covers the Windows test run. UBSan and libFuzzer need clang, so the Linux clang job carries
them. Rejected: `clang-cl`, which adds no gate the Linux job does not already carry.
source: agent

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

DECISION Visual Studio 2026 (VS 18) is the only supported Visual Studio. The Windows presets use the
generator `Visual Studio 18 2026`.
source: director

EVIDENCE GitHub's runner-images lists the label `windows-2025-vs2026` (Windows Server 2025 with
Visual Studio 2026). Its image 20260925.250.1 carries Visual Studio Enterprise 2026 18.10.12217.157
at `C:\Program Files\Microsoft Visual Studio\18\Enterprise`, MSVC toolset 14.51.36247, CMake 4.4.3
and LLVM 20.1.8. The plain `windows-2025` image does not carry Visual Studio 2026. Checked
2026-10-03. The CI `windows` job therefore uses `windows-2025-vs2026`.
source: https://github.com/actions/runner-images
source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-VS2026-Readme.md
source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md

open: whether Visual Studio 18 on the runner bundles clang-format and clang-tidy 22.x under
`VC\Tools\Llvm\x64\bin`. The image's own LLVM is 20.1.8, which the style gates refuse.
default: the workflow checks the bundled version with vswhere and installs LLVM 22 from Chocolatey
only if it is not 22.x. Not yet run on GitHub.

## Dependencies

DECISION Every third-party dependency is fetched by `FetchContent` from a pinned release URL with
`URL_HASH`. Nothing third-party is committed, which keeps the repo's text files under the 7-bit rule.
CI caches `FETCHCONTENT_BASE_DIR`. Rejected: vendoring into `third_party/`; vcpkg and Conan, a second
build system for five packages.
source: agent

| Dependency | Pin | Obtained | Used by |
|------------|-----|----------|---------|
| Slint | 1.18.1 | Windows: prebuilt MSVC x86-64 C++ package, `find_package(Slint)`. Android: source tag `v1.18.1` via `FetchContent` (Corrosion inside Slint's CMake) | `gui` |
| miniaudio | 0.11.25 | tag tarball | `audio` |
| SQLite | 3.53.4 | `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip` | `persistence` |
| FTXUI | 7.0.3 | tag tarball, target `ftxui::ftxui` | `tui` |
| GoogleTest | 1.18.0 | tag tarball | every `*Tests` target |

FACT Tag tarballs are `https://github.com/<repo>/archive/refs/tags/<tag>.tar.gz`. Tags were
confirmed with `git ls-remote` and tarballs hashed with `sha256sum` on 2026-10-01:

| Dependency | Tag | Tag commit | `URL_HASH` |
|------------|-----|------------|------------|
| Slint source | `v1.18.1` | `372cf0ee5577c3dfec309a45e7b778ba4e81b734` | `SHA256=fe485305ed303215e76c04918ee9aefbffbe229f18f979098ec36c7fa1dab28b` |
| miniaudio | `0.11.25` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | `SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274` |
| FTXUI | `v7.0.3` | `f921fad208912747c17d129a8ef75ec7624b6eec` | `SHA256=e7c62ffe19009759821b4f0f8df7f2a6fb83784c3a9f1477d81f56d3ee723c88` |
| GoogleTest | `v1.18.0` | `063de7e9578f82b369302001269680b4b1553359` | `SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5` |
| SQLite | (zip) | n/a | `SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e` (from the download page; the URL returned 200) |

source: https://github.com/slint-ui/slint/releases
source: https://github.com/mackron/miniaudio/releases
source: https://www.sqlite.org/download.html
source: https://github.com/ArthurSonzogni/FTXUI/releases
source: https://github.com/google/googletest/releases

open: the hash of Slint's prebuilt Windows package.
default: hash the release asset when the Windows preset is written.

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
| `windows-msvc-debug` | MSVC | everything | `/fsanitize=address`; `/RTC1`, `/ZI` and `/INCREMENTAL` removed (`/INCREMENTAL:NO`; MSVC rejects them with ASan); `/Zi` |
| `windows-msvc-release` | MSVC | everything | `/O2` |
| `linux-clang-asan-ubsan` | clang 22, libc++ | core libraries, `tui`, `bot`, tests; no Slint | `-stdlib=libc++ -fsanitize=address,undefined -fno-sanitize-recover=all` |
| `linux-clang-fuzz` | clang 22, libc++ | `mud` and its fuzz target only | `-stdlib=libc++ -fsanitize=fuzzer-no-link,address,undefined`; `-fsanitize=fuzzer` on the `muckaiiMudFuzz` link only |
| `android-arm64` | NDK r30 | core libraries, `gui` | ABI `arm64-v8a`, API 26 |

RULE Warnings-as-errors is set per target on MuckaII's own targets only: `/W4 /WX` (MSVC),
`-Wall -Wextra -Werror` (clang). Third-party include directories are `SYSTEM`. Sanitizer flags and
`-stdlib=libc++` stay global, so dependencies are instrumented too.
enforced_by: none: judgement
source: agent

RULE The Linux gate presets never build Slint, so no Rust and no uninstrumented library enters the
sanitizer runs.
enforced_by: none: judgement
source: agent

## Targets

| Library | Test target | Extra |
|---------|-------------|-------|
| `muckaiiTransport` | `muckaiiTransportTests` | loopback fake server |
| `muckaiiMud` | `muckaiiMudTests` | `muckaiiMudGolden` (golden serialiser), `muckaiiMudFuzz` (seeded from `docs/evidence/captures/`) |
| `muckaiiPersistence` | `muckaiiPersistenceTests` | |
| `muckaiiSession` | `muckaiiSessionTests` | injectable clock |
| `muckaiiClient` | `muckaiiClientTests` | |
| `muckaiiAudio` | `muckaiiAudioTests` | book-end frame positions |
| `muckaiiCore` | `muckaiiCoreTests` | ordering test at the fake server's received bytes |

| Executable | Frontend |
|------------|----------|
| `muckaii` | `gui` (Slint) |
| `muckaiiTui` | `tui` (FTXUI) |
| `muckaiiBot` | `bot` |

Targets, functions and variables that MuckaII defines in CMake are camelCase (`docs/style.md`,
Names). A library's output file is named after its target (`muckaiiMud.lib`).

RULE `ctest` runs every `*Tests` target and `python scripts/check-all.py`, which runs the goldens
check with the rest of the repo gates.
enforced_by: CMakeLists.txt (`repoGates`)
source: agent

## CI

| Job | Runner | Presets | Gates |
|-----|--------|---------|-------|
| windows | windows-2025-vs2026 | `windows-msvc-debug`, `windows-msvc-release` | build, `ctest` under ASan |
| linux-gate | ubuntu-24.04 | `linux-clang-asan-ubsan`, `linux-clang-fuzz` | `ctest` under ASan+UBSan, fuzz run, clang-tidy, firewall tripwire |
| android | ubuntu-24.04 | `android-arm64` | build only; the job installs NDK r30, Rust >= 1.92 and `aarch64-linux-android` itself |

FACT The windows-2025 image lists NDK 27-29 only, so no runner image is assumed to carry r30.
source: https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md

DECISION `scripts/check-tidy.py`, which runs clang-tidy outside CMake, loads `vcvars64.bat` from the
Visual Studio 18 install that `vswhere -all -prerelease -version [18.0,19.0)` reports. No edition
path (Insiders, Preview, Professional) is hard-coded.
source: agent proposal, accepted by director 2026-10-03

Release builds for launch are Windows and Android only (`INTERNAL.md`, Platforms).

## Scaffold

EVIDENCE Built and run on the director's machine: `windows-msvc-debug` and `windows-msvc-release`,
with `cmake --preset <name>`, `cmake --build --preset <name>` and `ctest --preset <name>`. The
Debug preset compiles with `/fsanitize=address`, and each executable gets a copy of MSVC's ASan
runtime DLLs beside it, because those sit on `PATH` only inside a developer prompt.
source: measured on the director's machine, 2026-10-02

EVIDENCE Not run: the `linux-clang-asan-ubsan` and `linux-clang-fuzz` presets, and the `linux-gate`
and `windows` jobs in `.github/workflows/ci.yml`. `linux-clang-fuzz` has no fuzz target yet. No
Android preset exists here; that work lives on the `android` branch.
source: measured on the director's machine, 2026-10-02

EVIDENCE `muckaii<Layer>` and `muckaii<Layer>Tests` exist for transport, mud, persistence, session,
client, audio and core, and the placeholder executable `muckaiiTui` links `muckaiiCore`. Each
library holds one small function. Only GoogleTest is fetched; Slint, miniaudio, SQLite and FTXUI
are not yet.
source: measured on the director's machine, 2026-10-02

EVIDENCE `cmake/MuckaiiLayers.cmake` holds the layer table of `docs/spec/architecture.md`. It fails
the configure on an upward or sideways link between targets, and runs as the ctest test
`repoLayerIncludes` for the `#include "muckaii/<layer>/..."` lines. `ctest` also runs `repoGates`
(`python scripts/check-all.py`) and `repoTidy` (`scripts/check-tidy.py`).
source: measured on the director's machine, 2026-10-02

EVIDENCE The style gates need clang-format and clang-tidy 22.x (`docs/style.md`). Visual Studio 18
(Insiders 18.11 here) bundles 22.1.3 under `VC\Tools\Llvm\x64\bin`.
source: measured on the director's machine, 2026-10-02

DECISION The style gates find clang-format and clang-tidy through `CLANG_FORMAT` / `CLANG_TIDY`,
then `PATH`, then the Visual Studio 18 install's bundled LLVM (`VC\Tools\Llvm\x64\bin`, located with
`vswhere`).
source: agent proposal, accepted by director 2026-10-03
