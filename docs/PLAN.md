# corvid-c — the binding's plan

corvid-c is the **canonical C consumer** of the `corvid` embedded database's
FFI. Its reason to exist is to prove, continuously and outside the engine
repo, that the *published* artifacts — the platform cdylib, `corvid.h`, and
the golden fixtures shipped in every release archive — agree with each other
and drive a real C program to the same verdicts the engine's own suite
produces.

Engine repo: `corvid-db/corvid` (read-only upstream; never a submodule, never
vendored here).

## The locked rule: golden port BEFORE ergonomic sugar

Inherited from the bindings program's master plan and non-negotiable:

> **A binding opens with the golden-suite port.** The engine's golden
> fixtures (267 executable lines across 8 files) are the contract; a
> binding that wraps the ABI before it can replay the contract is building
> on unverified ground. No ergonomic sugar ships until the port is green
> against a tagged release's published artifacts.

Concretely: corvid-c's first substantive deliverable is `test/golden.c` — a
standalone port of the engine's `c/smoke.c` harness — driven against the
**downloaded** libcorvid from a GitHub release. Only after that is green in
CI on all platforms does any wrapper-API work begin.

## Binding rules (from the master plan)

- **Pin EXACT engine tags.** One engine version at a time; today it is
  `v0.3.1`. The pin lives in exactly one variable per fetch script
  (`CORVID_VERSION`) and is stamped into `deps/version.txt`; CMake reads
  the stamp, never guesses.
- **Artifacts come from the tag's GitHub release**, not from a local build
  of the engine: `https://github.com/corvid-db/corvid/releases/download/<tag>/…`,
  verified against the release's `checksums.txt` (sha256) before anything
  is extracted or used.
- **No vendored binaries in git.** `deps/` (the extracted engine artifacts)
  is gitignored; every consumer — human, CI — runs `fetch.sh`/`fetch.ps1`
  to populate it deterministically.
- **No FetchContent / no network at build time.** CMake consumes `deps/`
  only; the build is offline-first once fetch has run.
- **Published-artifact defects are findings, not patches.** If the released
  header/dylib/fixtures disagree in a way that blocks this repo, we stop
  and report upstream (`corvid-db/corvid`). We never carry a local header
  patch or fixture edit to work around a bad artifact.

## Phase C1 (this bootstrap) — scope

1. **Plan doc** (this file) — the binding's own program, written first.
2. **Repo scaffold** — README (role, usage, requirements), MIT LICENSE
   (matching corvid's copyright line), `.gitignore` (`build/`, `deps/`).
3. **Fetch + verify** — `fetch.sh` (bash, macOS/Linux) and `fetch.ps1`
   (PowerShell, Windows): pick the host platform archive from the pinned
   release, sha256-verify it against `checksums.txt`, extract into
   `deps/`, stamp `deps/version.txt`. Deterministic and idempotent.
4. **Build system** — `CMakeLists.txt`:
   - an IMPORTED target for the downloaded cdylib (`corvid.dll.lib` import
     lib on MSVC, the dylib/so itself elsewhere; rpath baked on macOS);
   - `examples/demo.c` — a small idiomatic consumer (open, insert, query,
     print; ~15 symbols);
   - `test/golden.c` — the golden-suite port, one ctest per fixture file;
   - a `corvid.pc` pkg-config file + install rules for system installs.
5. **The golden port** — `test/golden.c` replays the engine's fixture
   grammar (`OP<TAB>args<TAB>expected`) over every executable line of the
   release's `golden/*.txt`, enforcing the same discipline as the engine
   driver: every counted line must dispatch (`executed == lines`), first
   failure names file:line + OP + expected-vs-got. **Success criterion:
   the same 256/256 green the engine-side suite reports.** This is the
   moment of truth for the published artifacts.
6. **CI** — `.github/workflows/ci.yml`: an ubuntu/macos-arm64/windows
   matrix that fetches + verifies, configures, builds, runs ctest
   (golden port) and the demo; plus a Linux sanitizer job building the
   golden port with ASan/LSan (the harness frees every handle on its
   creation path, so the expectation is zero leaks — same as upstream).
   Target wall time: under 3 minutes.

Out of scope for C1: any wrapper/ergonomic API, any additional examples
beyond the demo, any language beyond C.

## Phase C2 (follow-up task) — ergonomic API

Only after C1 is green on all CI platforms:

- a thin ergonomic layer (error-or-value helpers, string builders,
  ownership conventions) *on top of* the raw `corvid.h` ABI — never a
  replacement for it;
- the golden port stays the regression gate: every C2 change must keep
  256/256 green, and the demo is rewritten to show the ergonomic style
  alongside the raw one;
- version bumps become a one-variable change (`CORVID_VERSION`), re-pinned
  to an exact tag, with the golden port re-run as the acceptance check for
  the new artifacts.

## Verdict protocol

The port keeps the engine harness's output contract: one
`SMOKE <file> lines=<n> executed=<n>` line per fixture on stdout, exit 0
only when every expectation of every executable line passed and the
dispatch count matches the pre-scan count. Divergence from the engine-side
suite's pass/fail verdicts is a defect here; divergence of the *artifacts*
from the engine repo is a finding for the engine repo.

## Findings against published artifacts

### F1 — RESOLVED in v0.2.1 (engine commit edc1bc0): v0.2.0 macOS dylibs shipped with the CI runner's absolute path as their install name

`otool -D` on the release archives:

```
corvid-ffi-v0.2.0-aarch64-apple-darwin/libcorvid.dylib:
/Users/runner/work/corvid/corvid/target/aarch64-apple-darwin/release/deps/libcorvid.dylib
corvid-ffi-v0.2.0-x86_64-apple-darwin/libcorvid.dylib:
/Users/runner/work/corvid/corvid/target/x86_64-apple-darwin/release/deps/libcorvid.dylib
```

Any consumer that links the shipped dylib records that absolute path in
its `LC_LOAD_DYLIB`, and dyld aborts at launch (`Library not loaded:
/Users/runner/work/...`). This is exactly the moment-of-truth divergence
this repo exists to catch: the engine's own smoke suite never sees it
because (a) its CI links the *host-style* `target/release/libcorvid.dylib`
whose install name is `@rpath/libcorvid.dylib`, and (b) even a
`--target`-style build would load fine on the CI machine itself — the
recorded path exists there. External consumers have no such luck.

Evidence that the defect is *only* the install name — with the dylib made
loadable via `DYLD_FALLBACK_LIBRARY_PATH=<deps dir>`, the full golden port
runs **256/256 green** and the demo passes on aarch64-darwin (header, ABI,
and fixtures are mutually consistent).

Upstream fix (engine repo, `release.yml` "Stage the FFI artifact set"):
on the darwin legs, after copying the dylib into the staging dir, set
`install_name_tool -id @rpath/libcorvid.dylib "$DIR/libcorvid.dylib"`
(or pass `-C link-arg=-Wl,-install_name,@rpath/libcorvid.dylib` at build).

**Resolution:** the engine landed the fix (commit `edc1bc0`) and shipped
`v0.3.1`, whose darwin dylibs verify as `@rpath/libcorvid.dylib`
(`otool -D`, checked on the published aarch64-darwin archive). corvid-c
re-pinned to `v0.2.1`; the darwin CI leg is required-green again with no
`continue-on-error` and no env-var help — the golden suite now loads the
shipped dylib through the baked rpath alone. The evidence above is kept
verbatim for the record.

(For humans still holding v0.2.0 darwin artifacts: the standard
self-applied mitigation was
`install_name_tool -id @rpath/libcorvid.dylib deps/corvid-ffi-v0.2.0-*/libcorvid.dylib`
on your own downloaded copy; v0.2.1 needs nothing.)

### F2 — RESOLVED in v0.2.1: the Linux `.so` had no SONAME

`readelf -d libcorvid.so` on v0.2.0 showed no `SONAME` entry (a
long-standing rustc/cdylib trait). Consumers linking via a full path
recorded that path in `DT_NEEDED`; corvid-c's rpath handling made it
transparent for the build tree and the pkg-config file's
`-L${libdir} -lcorvid` recorded the basename, so nothing was blocked.
**Resolved:** the `v0.2.1` `.so` carries `SONAME=libcorvid.so`
(re-verified on the published x86_64-linux archive); kept here for the
record.
