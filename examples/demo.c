/* demo.c — the canonical-consumer tour of the corvid C ABI.
 *
 * A deliberately small program doing the ordinary thing: open a database,
 * build a few documents with the value constructors, insert them, run a
 * vector (kNN) query, print the ranked rows, and free every handle on the
 * path that created it. It exercises ~20 symbols of the published
 * corvid.h exactly the way a binding author would.
 *
 * Build/run: see README.md (the demo is built by CMake against the
 * downloaded, checksum-verified cdylib in deps/).
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

/* One status check that names the last recorded error on failure. */
static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "demo: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* Build {name=…, year=…, v=[…]} and hand it to the engine (insert CLONES
 * the value; the local one still belongs to us and must be freed). */
static void put_doc(corvid_coll *docs, const char *key, const char *name,
                    int64_t year, const float *v, size_t dim) {
    corvid_value *doc = corvid_value_map_new();
    must("value_map_new-put name",
         corvid_value_map_put(doc, "name", 4, corvid_value_text(name, strlen(name))));
    must("value_map_put year",
         corvid_value_map_put(doc, "year", 4, corvid_value_int(year)));
    must("value_map_put v",
         corvid_value_map_put(doc, "v", 1, corvid_value_vector(v, dim)));
    must("insert", corvid_insert(docs, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc);
}

int main(void) {
    printf("corvid FFI version %u\n", (unsigned)corvid_ffi_version());

    /* open (in-memory) + the primary collection */
    corvid_db *db = corvid_open_memory();
    if (!db) {
        fprintf(stderr, "demo: corvid_open_memory failed\n");
        return 1;
    }
    corvid_coll *docs = corvid_collection(db, "docs", 4);
    if (!docs) {
        fprintf(stderr, "demo: corvid_collection failed\n");
        return 1;
    }

    /* insert three small documents with 3-d embeddings */
    put_doc(docs, "p1", "corvid", 2026, (const float[]){0.9f, 0.1f, 0.0f}, 3);
    put_doc(docs, "p2", "raven", 2025, (const float[]){0.8f, 0.2f, 0.1f}, 3);
    put_doc(docs, "p3", "kestrel", 2024, (const float[]){0.1f, 0.9f, 0.4f}, 3);

    /* kNN vector query: nearest to (1, 0, 0) under cosine distance */
    const float probe[3] = {1.0f, 0.0f, 0.0f};
    corvid_query *q = corvid_query_new(docs);
    if (!q) {
        fprintf(stderr, "demo: corvid_query_new failed\n");
        return 1;
    }
    must("query_vector", corvid_query_vector(q, "v", 1, probe, 3, 3,
                                             CORVID_METRIC_COSINE));
    corvid_rows *rows = corvid_query_run(q); /* consumes q */
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "demo: query_run failed: %.*s\n", (int)len, msg);
        return 1;
    }

    printf("%-6s %-8s %-6s %s\n", "rank", "key", "year", "name");
    int rank = 0;
    for (;;) {
        const uint8_t *key = NULL;
        size_t key_len = 0;
        const corvid_value *doc = NULL;
        float score = 0.0f;
        if (corvid_rows_next(rows, &key, &key_len, &doc, &score) != 1) break;
        const corvid_value *name = corvid_value_map_get(doc, "name", 4);
        const corvid_value *year = corvid_value_map_get(doc, "year", 4);
        size_t name_len = 0;
        const char *name_p = corvid_value_text_ref(name, &name_len);
        int ok = 0;
        printf("%-6d %-.*s %-6lld %.*s\n", ++rank, (int)key_len, key,
               (long long)corvid_value_as_int(year, &ok), (int)name_len,
               name_p ? name_p : "?");
    }
    corvid_rows_free(rows);

    /* every handle freed on the path that created it */
    corvid_collection_free(docs);
    must("close", corvid_close(db));
    printf("demo: ok\n");
    return 0;
}
