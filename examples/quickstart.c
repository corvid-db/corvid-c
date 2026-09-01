/* quickstart.c — the README tour as a runnable file.
 *
 * Open an in-memory database, create a collection, insert three small
 * documents carrying 2-d embeddings, run a kNN vector query under
 * cosine, and print the ranked rows. Free every handle on the path
 * that created it (the transfer rules: FFI.md §5).
 *
 * Build/run: CMake builds this as `example_quickstart`; CI runs it as
 * a ctest on every platform.
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

/* One status check that names the last recorded error on failure. */
static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "quickstart: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* docs:begin:quickstart */
static void put_doc(corvid_coll *docs, const char *key, const char *title,
                    const char *kind, const float *v, size_t dim) {
    corvid_value *doc = corvid_value_map_new();
    must("map_put title", corvid_value_map_put(
        doc, "title", 5, corvid_value_text(title, strlen(title))));
    must("map_put kind", corvid_value_map_put(
        doc, "kind", 4, corvid_value_text(kind, strlen(kind))));
    must("map_put v",
         corvid_value_map_put(doc, "v", 1, corvid_value_vector(v, dim)));
    must("insert", corvid_insert(docs, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc); /* insert CLONES the value; ours is still ours */
}

int main(void) {
    corvid_db *db = corvid_open_memory();
    if (!db) { fprintf(stderr, "quickstart: open failed\n"); return 1; }
    corvid_coll *docs = corvid_collection(db, "docs", 4);
    if (!docs) { fprintf(stderr, "quickstart: collection failed\n"); return 1; }

    put_doc(docs, "p1", "rust embedded database", "doc",
            (const float[]){1.0f, 0.0f}, 2);
    put_doc(docs, "p2", "python web frameworks", "doc",
            (const float[]){0.0f, 1.0f}, 2);
    put_doc(docs, "p3", "rust again database", "doc",
            (const float[]){0.9f, 0.1f}, 2);

    /* kNN: the 3 nearest documents to (1, 0) under cosine. */
    corvid_query *q = corvid_query_new(docs);
    if (!q) { fprintf(stderr, "quickstart: query_new failed\n"); return 1; }
    must("query_vector",
         corvid_query_vector(q, "v", 1, (const float[]){1.0f, 0.0f}, 2, 3,
                             CORVID_METRIC_COSINE));
    corvid_rows *rows = corvid_query_run(q); /* consumes q */
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "quickstart: query_run failed: %.*s\n", (int)len, msg);
        return 1;
    }

    int rank = 0;
    for (;;) {
        const uint8_t *key = NULL;
        size_t key_len = 0;
        const corvid_value *doc = NULL;
        float score = 0.0f;
        if (corvid_rows_next(rows, &key, &key_len, &doc, &score) != 1) break;
        const corvid_value *title =
            corvid_value_map_get(doc, "title", 5);
        size_t title_len = 0;
        const char *title_p = corvid_value_text_ref(title, &title_len);
        printf("%d. %-.*s score=%.6f %.*s\n", ++rank, (int)key_len, key,
               (double)score, (int)title_len, title_p ? title_p : "?");
    }
    corvid_rows_free(rows);

    corvid_collection_free(docs);
    must("close", corvid_close(db));
    return 0;
}
/* docs:end:quickstart */
