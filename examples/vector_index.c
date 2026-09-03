/* vector_index.c — three vector-index families, ANN vs exact.
 *
 * A file-backed database (the on-disk index is a disk-resident HNSW
 * graph persisted inside the db file) with eight 4-d documents. The
 * same embedding is stored under three fields so each index family can
 * be demonstrated side by side:
 *
 *   v_mem  — in-memory HNSW              (create_vector_index)
 *   v_disk — on-disk HNSW                (create_vector_index_ondisk)
 *   v_q    — in-memory binary-quantized   (create_vector_index_quantized)
 *
 * The exact (streaming-scan) ranking is printed first, then the ANN
 * (approx) ranking served by each index. The unquantized indexes
 * answer identically to the scan on this corpus; the binary-quantized
 * one genuinely diverges — the recall/footprint trade-off quantization
 * makes (binary packs each float32 to one sign bit, ~32x smaller).
 * Finally the db is closed and reopened: the on-disk graph reloads and
 * serves the same ANN answer without a rebuild.
 *
 * Scores are RRF ranks (1/(60 + rank)) — the lone vector source's row
 * score — so they reflect each lane's own ranking.
 *
 * Build/run: CMake builds this as `example_vector_index`; CI runs it
 * as a ctest on every platform.
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

#define DB_FILE "example-vector-index.redb"

static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "vector_index: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* docs:begin:vector_index */
static void put_doc(corvid_coll *items, const char *key, const float *v) {
    corvid_value *doc = corvid_value_map_new();
    must("map_put v_mem", corvid_value_map_put(
        doc, "v_mem", 5, corvid_value_vector(v, 4)));
    must("map_put v_disk", corvid_value_map_put(
        doc, "v_disk", 6, corvid_value_vector(v, 4)));
    must("map_put v_q",
         corvid_value_map_put(doc, "v_q", 3, corvid_value_vector(v, 4)));
    must("insert", corvid_insert(items, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc);
}

/* Run a top-4 vector query over `field`, print its ranked keys. */
static void run_query(corvid_coll *items, const char *field, int approx,
                      const char *label) {
    corvid_query *q = corvid_query_new(items);
    if (!q) { fprintf(stderr, "vector_index: query_new failed\n"); exit(1); }
    static const float probe[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    must("query_vector",
         corvid_query_vector(q, field, strlen(field), probe, 4, 4,
                             CORVID_METRIC_COSINE));
    if (approx) must("query_approx", corvid_query_approx(q));
    corvid_rows *rows = corvid_query_run(q);
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "vector_index: query_run failed: %.*s\n", (int)len,
                msg);
        exit(1);
    }
    printf("%-38s", label);
    for (;;) {
        const uint8_t *key = NULL;
        size_t key_len = 0;
        const corvid_value *doc = NULL;
        float score = 0.0f;
        if (corvid_rows_next(rows, &key, &key_len, &doc, &score) != 1) break;
        printf(" %.*s(%.6f)", (int)key_len, key, (double)score);
    }
    printf("\n");
    corvid_rows_free(rows);
}

int main(void) {
    remove(DB_FILE); /* reruns start clean (single-file db) */

    static const float vecs[8][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},   /* k0 — nearest */
        {0.95f, 0.05f, 0.0f, 0.0f}, /* k1 */
        {0.0f, 1.0f, 0.0f, 0.0f},   /* k2 */
        {0.0f, 0.9f, 0.1f, 0.0f},   /* k3 */
        {0.0f, 0.0f, 1.0f, 0.0f},   /* k4 */
        {0.7f, 0.7f, 0.0f, 0.0f},   /* k5 */
        {0.0f, 0.0f, 0.0f, 1.0f},   /* k6 */
        {0.98f, 0.02f, 0.0f, 0.0f}, /* k7 */
    };
    static const char *keys[8] = {"k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7"};

    corvid_db *db = corvid_open(DB_FILE, strlen(DB_FILE));
    if (!db) { fprintf(stderr, "vector_index: open failed\n"); return 1; }
    corvid_coll *items = corvid_collection(db, "items", 5);
    if (!items) { fprintf(stderr, "vector_index: collection failed\n"); return 1; }

    for (int i = 0; i < 8; i++) put_doc(items, keys[i], vecs[i]);

    must("create_vector_index (in-memory)",
         corvid_create_vector_index(items, "v_mem", 5, CORVID_METRIC_COSINE));
    must("create_vector_index_ondisk",
         corvid_create_vector_index_ondisk(items, "v_disk", 6,
                                           CORVID_METRIC_COSINE));
    must("create_vector_index_quantized (binary)",
         corvid_create_vector_index_quantized(items, "v_q", 3,
                                              CORVID_METRIC_COSINE,
                                              CORVID_QUANT_BINARY));

    printf("top-4 nearest to (1,0,0,0) under cosine:\n");
    run_query(items, "v_mem", 0, "exact (scan):");
    run_query(items, "v_mem", 1, "ann in-memory HNSW:");
    run_query(items, "v_disk", 1, "ann on-disk HNSW:");
    run_query(items, "v_q", 1, "ann binary-quantized:");
    printf("(the quantized lane trades recall for a ~32x smaller index)\n");

    corvid_collection_free(items);
    must("close", corvid_close(db));

    /* Reopen: the on-disk graph reloads (no rebuild) and answers again. */
    db = corvid_open(DB_FILE, strlen(DB_FILE));
    if (!db) { fprintf(stderr, "vector_index: reopen failed\n"); return 1; }
    items = corvid_collection(db, "items", 5);
    if (!items) { fprintf(stderr, "vector_index: recollection failed\n"); return 1; }
    run_query(items, "v_disk", 1, "ann on-disk after reopen:");
    corvid_collection_free(items);
    must("close", corvid_close(db));

    remove(DB_FILE);
    return 0;
}
/* docs:end:vector_index */
