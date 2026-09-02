# corvid-c

The canonical C consumer of [corvid](https://github.com/corvid-db/corvid) —
an embedded database with a typed C ABI. This repo exists to prove,
continuously and outside the engine's own repository, that corvid's
**published FFI artifacts** (the platform cdylib, `corvid.h`, and the
golden fixtures shipped in each release archive) work for a plain C
consumer: it fetches a pinned release, verifies its checksums, and drives
the downloaded library through a full port of the engine's golden test
suite.

**Documentation:** the [corvid docs site](https://corvid-db.github.io/docs/)
is canonical — this binding has its own
[corvid-c page](https://corvid-db.github.io/docs/bindings/corvid-c/), and the
[C ABI section](https://corvid-db.github.io/docs/ffi/) documents every
symbol this repo links (handles, ownership, errors, threading).

Its role in the bindings program is **reference consumer**: everything
here links the release artifacts exactly the way a third-party binding
author would — no engine checkout, no vendored binaries, no FetchContent.

## What's inside

| Path | What it is |
| --- | --- |
| `fetch.sh` / `fetch.ps1` | Download the pinned release archive for the host platform, verify it against the release's `checksums.txt` (sha256), extract into gitignored `deps/` |
| `CMakeLists.txt` | Offline-first build: consumes `deps/`, builds the demo, the examples tour, and the golden-suite port, installs a `corvid.pc` |
| `examples/demo.c` | A small idiomatic consumer: open, insert, query, print (22 symbols) |
| `examples/{quickstart,hybrid,vector_index,text_search,graph,geo}.c` | The examples tour — one runnable program per concept (also ctests): the README quickstart, hybrid RRF+MMR, the three vector-index families vs exact, BM25 incl. CJK, graph + delete cascade, geo radius/bbox/nearest |
| `test/golden.c` | The golden-suite port — replays the engine's 267-line fixture suite against the downloaded libcorvid; registered as ctest tests |
| `docs/PLAN.md` | The binding's plan: golden port before ergonomic sugar, binding rules, phase scope |

## Quick start

Requirements: a C11 compiler, CMake ≥ 3.28 (the Ubuntu 24.04 LTS
system CMake — the oldest any supported platform ships), and one of
`curl` + `shasum`/`sha256sum` (macOS/Linux) or PowerShell 5+ (Windows).

```sh
./fetch.sh                     # download + verify corvid v0.3.1 into deps/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # golden suite + demo + examples
./build/bin/demo                              # open → insert → query → print
./build/bin/example_hybrid                    # the flagship hybrid query
```

Windows (PowerShell): `./fetch.ps1`, then the same CMake steps
(`ctest -C Release`; the demo lands in `build\bin\Release\`).

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

## macOS note (v0.2.0 artifact defect — resolved in v0.2.1)

The v0.2.0 darwin dylibs shipped with the release CI runner's absolute
path as their install name, so binaries linked against them aborted at
launch. corvid-c caught this (finding F1 in
[docs/PLAN.md](docs/PLAN.md)), the engine fixed its release pipeline
(commit `edc1bc0`), and **v0.3.1 — the current pin — is clean**:
`otool -D` shows `@rpath/libcorvid.dylib`, and the golden suite runs
267/267 with no workarounds (the v0.3.1 fixtures added the
VMAP_KEYS/GET_KEYS/PHRASE lines). v0.3.1's Linux `.so` also carries its
SONAME (finding F2, likewise resolved).

## Installing (system use)

`cmake --install build` installs `corvid.h`, the library, and a
`corvid.pc` pkg-config file:

```sh
pkg-config --cflags --libs corvid
```

## Surface manifest (docs/SURFACE.tsv)

Every construct of the engine's public surface (the radar-enforced list the
engine publishes as `scripts/bindings/surface.tsv` at each release tag) is
resolved in `docs/SURFACE.tsv`: the ABI symbol(s) exposing it plus the test
that proves it (golden fixture line references), or `N/A` + reason where the
v1 ABI deliberately does not expose it (FFI.md §9). `scripts/surface-gate.sh`
fails CI when a line is unresolved, a cell is empty, or the N/A count drifts
from the committed baseline — so an engine pin bump that changes the surface
lands in this gate, not in a user's bug report.

## Versioning

The engine pin lives in one variable in the fetch scripts
(`CORVID_VERSION=v0.3.1`). Artifacts are always taken from that exact
tag's GitHub release and sha256-verified; `deps/` is never committed.

## License

MIT — see [LICENSE](LICENSE).
