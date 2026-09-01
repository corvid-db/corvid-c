/* hybrid.c — the flagship: filter + vector + BM25, RRF fusion, MMR
 * rerank, limit.
 *
 * Hybrid retrieval over a 4-document corpus: a pre-ranking `kind`
 * filter, a vector (ANN) source and a BM25 text source, both
 * contributing top-2 candidate lists, fused with Reciprocal Rank
 * Fusion (k = 60) and reranked for diversity with MMR (lambda = 1.0),
 * capped at 2 rows. The printed scores are RRF rank sums: s1 is rank 1
 * of both sources (1/61 + 1/61 = 2/61), s3 rank 2 of both (2/62).
 *
 * Build/run: CMake builds this as `example_hybrid`; CI runs it as a
 * ctest on every platform.
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "hybrid: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* docs:begin:hybrid */
static void put_doc(corvid_coll *docs, const char *key, const char *kind,
                    const char *body, const float *v) {
    corvid_value *doc = corvid_value_map_new();
    must("map_put kind", corvid_value_map_put(
        doc, "kind", 4, corvid_value_text(kind, strlen(kind))));
    if (body)
        must("map_put body", corvid_value_map_put(
            doc, "body", 4, corvid_value_text(body, strlen(body))));
    if (v)
        must("map_put v",
             corvid_value_map_put(doc, "v", 1, corvid_value_vector(v, 2)));
    must("insert", corvid_insert(docs, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc);
}

static void print_rows(corvid_rows *rows) {
    int rank = 0;
    for (;;) {
        const uint8_t *key = NULL;
        size_t key_len = 0;
        const corvid_value *doc = NULL;
        float score = 0.0f;
        if (corvid_rows_next(rows, &key, &key_len, &doc, &score) != 1) break;
        const corvid_value *body = corvid_value_map_get(doc, "body", 4);
        size_t body_len = 0;
        const char *body_p = corvid_value_text_ref(body, &body_len);
        printf("%d. %-.*s score=%.6f %.*s\n", ++rank, (int)key_len, key,
               (double)score, (int)body_len, body_p ? body_p : "?");
    }
    corvid_rows_free(rows);
}

int main(void) {
    corvid_db *db = corvid_open_memory();
    if (!db) { fprintf(stderr, "hybrid: open failed\n"); return 1; }
    corvid_coll *docs = corvid_collection(db, "docs", 4);
    if (!docs) { fprintf(stderr, "hybrid: collection failed\n"); return 1; }

    put_doc(docs, "s1", "doc", "rust embedded database",
            (const float[]){1.0f, 0.0f});
    put_doc(docs, "s2", "doc", "python web frameworks",
            (const float[]){0.0f, 1.0f});
    put_doc(docs, "s3", "doc", "rust again database",
            (const float[]){0.9f, 0.1f});
    put_doc(docs, "m1", "meta", NULL, NULL); /* filtered out below */

    /* The flagship query: filter + vector + text, RRF + MMR + limit. */
    corvid_query *q = corvid_query_new(docs);
    if (!q) { fprintf(stderr, "hybrid: query_new failed\n"); return 1; }

    corvid_value *doc_kind = corvid_value_text("doc", 3);
    corvid_pred *only_docs =
        corvid_pred_compare("kind", 4, CORVID_CMP_EQ, doc_kind);
    corvid_value_free(doc_kind); /* CLONED into the tree (§5 rule 3) */
    if (!only_docs) { fprintf(stderr, "hybrid: pred_compare failed\n"); return 1; }
    must("query_filter", corvid_query_filter(q, only_docs)); /* consumes pred */

    must("query_vector",
         corvid_query_vector(q, "v", 1, (const float[]){1.0f, 0.0f}, 2, 2,
                             CORVID_METRIC_COSINE));
    must("query_text",
         corvid_query_text(q, "body", 4, "rust database", 13, 2));
    must("query_fuse_rrf", corvid_query_fuse_rrf(q, 60.0f));
    must("query_rerank_mmr", corvid_query_rerank_mmr(q, 1.0f));
    must("query_limit", corvid_query_limit(q, 2));

    corvid_rows *rows = corvid_query_run(q); /* consumes q */
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "hybrid: query_run failed: %.*s\n", (int)len, msg);
        return 1;
    }
    print_rows(rows);

    corvid_collection_free(docs);
    must("close", corvid_close(db));
    return 0;
}
/* docs:end:hybrid */
