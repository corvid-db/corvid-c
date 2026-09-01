# corvid-c

The canonical C consumer of [corvid](https://github.com/i-rocky/corvid) —
an embedded database with a typed C ABI. This repo exists to prove,
continuously and outside the engine's own repository, that corvid's
**published FFI artifacts** (the platform cdylib, `corvid.h`, and the
golden fixtures shipped in each release archive) work for a plain C
consumer: it fetches a pinned release, verifies its checksums, and drives
the downloaded library through a full port of the engine's golden test
suite.

Its role in the bindings program is **reference consumer**: everything
here links the release artifacts exactly the way a third-party binding
author would — no engine checkout, no vendored binaries, no FetchContent.

## What's inside

| Path | What it is |
| --- | --- |
| `fetch.sh` / `fetch.ps1` | Download the pinned release archive for the host platform, verify it against the release's `checksums.txt` (sha256), extract into gitignored `deps/` |
| `CMakeLists.txt` | Offline-first build: consumes `deps/`, builds the demo and the golden-suite port, installs a `corvid.pc` |
| `examples/demo.c` | A small idiomatic consumer: open, insert, query, print (~15 symbols) |
| `test/golden.c` | The golden-suite port — replays the engine's 256-line fixture suite against the downloaded libcorvid; registered as ctest tests |
| `docs/PLAN.md` | The binding's plan: golden port before ergonomic sugar, binding rules, phase scope |

## Quick start

Requirements: a C11 compiler, CMake ≥ 3.16, and one of
`curl` + `shasum`/`sha256sum` (macOS/Linux) or PowerShell 5+ (Windows).

```sh
./fetch.sh                     # download + verify corvid v0.2.0 into deps/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # the golden suite (256 lines)
./build/examples/demo                        # open → insert → query → print
```

Windows (PowerShell): `./fetch.ps1`, then the same CMake steps
(`ctest -C Release`; the demo lands in `build\examples\Release\`).

A taste of the API (`examples/demo.c`):

```c
#include "corvid.h"

corvid_db *db = corvid_open_memory();
corvid_coll *docs = corvid_collection(db, "docs", 4);

corvid_value *doc = corvid_value_map_new();
corvid_value_map_put(doc, "name", 4, corvid_value_text("ada", 3));
corvid_value_map_put(doc, "v", 1, corvid_value_vector(v, 3));
corvid_insert(docs, (const uint8_t *)"p1", 2, doc);   /* clones the value */
corvid_value_free(doc);

corvid_query *q = corvid_query_new(docs);
corvid_query_vector(q, "v", 1, probe, 3, 2, CORVID_METRIC_COSINE);
corvid_rows *rows = corvid_query_run(q);              /* consumes q */
/* … corvid_rows_next(rows, &key, &key_len, &doc, &score) … */
corvid_rows_free(rows);
corvid_collection_free(docs);
corvid_close(db);
```

## macOS note (v0.2.0 artifact defect)

The v0.2.0 darwin dylibs ship with the release CI runner's absolute path
as their install name, so a binary linked against them aborts at launch
with `Library not loaded: /Users/runner/work/...`. This is a defect in
the published artifacts, tracked as finding F1 in
[docs/PLAN.md](docs/PLAN.md) and against the engine repo — corvid-c does
not patch around it (the macOS CI legs run it visibly red). If you need
the v0.2.0 darwin artifacts today, fix your own downloaded copy:

```sh
install_name_tool -id @rpath/libcorvid.dylib deps/corvid-ffi-v0.2.0-*/libcorvid.dylib
```

Everything else — header, ABI, golden fixtures — is verified consistent
(256/256 once the dylib is loadable). Linux and Windows artifacts are
unaffected.

## Installing (system use)

`cmake --install build` installs `corvid.h`, the library, and a
`corvid.pc` pkg-config file:

```sh
pkg-config --cflags --libs corvid
```

## Versioning

The engine pin lives in one variable in the fetch scripts
(`CORVID_VERSION=v0.2.0`). Artifacts are always taken from that exact
tag's GitHub release and sha256-verified; `deps/` is never committed.

## License

MIT — see [LICENSE](LICENSE).
