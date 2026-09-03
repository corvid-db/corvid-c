/* text_search.c — BM25 ranking, English and CJK.
 *
 * Six notes (three English, three CJK) searched through a text index
 * with the query builder's BM25 source. Row scores are RRF ranks
 * (1/(60 + rank)); the *order* is the BM25 ranking.
 *
 * The CJK strings exercise the engine's dictionary-free CJK
 * segmentation: maximal runs of CJK characters are tokenized as
 * sliding BIGRAMS (「东京」… → "东京", …), so an unsegmented CJK query
 * matches by its bigrams — "城市" (city) matches both city notes,
 * "数据库" (database) matches the ML note.
 *
 * Phrase matching: engine v0.3.0 added the DIRECT positional
 * `corvid_phrase_search` to the ABI (consecutive in-order analyzed
 * tokens, stop words collapsing out of adjacency); the row score is
 * the BM25 phrase sum, not the builder's fused RRF scale.
 *
 * Build/run: CMake builds this as `example_text_search`; CI runs it as
 * a ctest on every platform (built with /utf-8 under MSVC).
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "text_search: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* docs:begin:text_search */
static void put_note(corvid_coll *notes, const char *key, const char *body) {
    corvid_value *doc = corvid_value_map_new();
    must("map_put body", corvid_value_map_put(
        doc, "body", 4, corvid_value_text(body, strlen(body))));
    must("insert", corvid_insert(notes, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc);
}

static void phrase(corvid_coll *notes, const char *q, const char *label) {
    corvid_rows *rows = corvid_phrase_search(notes, "body", 4, q, strlen(q), 3);
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "text_search: phrase_search failed: %.*s\n", (int)len,
                msg ? msg : "?");
        exit(1);
    }
    printf("%-28s ->", label);
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

static void search(corvid_coll *notes, const char *query, const char *label) {
    corvid_query *q = corvid_query_new(notes);
    if (!q) { fprintf(stderr, "text_search: query_new failed\n"); exit(1); }
    must("query_text",
         corvid_query_text(q, "body", 4, query, strlen(query), 3));
    corvid_rows *rows = corvid_query_run(q);
    if (!rows) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "text_search: query_run failed: %.*s\n", (int)len, msg);
        exit(1);
    }
    printf("%-28s ->", label);
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
    corvid_db *db = corvid_open_memory();
    if (!db) { fprintf(stderr, "text_search: open failed\n"); return 1; }
    corvid_coll *notes = corvid_collection(db, "notes", 5);
    if (!notes) { fprintf(stderr, "text_search: collection failed\n"); return 1; }

    put_note(notes, "n1", "the quick brown fox jumps over the lazy dog");
    put_note(notes, "n2", "a quick red fox leaps over a sleeping dog");
    put_note(notes, "n3", "slow green turtle crosses the road");
    put_note(notes, "n4", "东京是一座巨大的城市");   /* Tokyo is a huge city */
    put_note(notes, "n5", "大阪是关西最大的城市");   /* Osaka is Kansai's biggest city */
    put_note(notes, "n6", "机器学习正在改变数据库"); /* ML is changing databases */

    must("create_text_index", corvid_create_text_index(notes, "body", 4));

    search(notes, "quick fox", "bm25 \"quick fox\":");
    search(notes, "quick dog", "bm25 \"quick dog\":");
    search(notes, "城市", "bm25 CJK 城市 (city):");
    search(notes, "数据库", "bm25 CJK 数据库 (database):");

    phrase(notes, "fox jumps over", "phrase \"fox jumps over\":");
    phrase(notes, "over jumps fox", "phrase reversed (no match):");
    phrase(notes, "leaps over a sleeping", "phrase stop words collapsed:");

    corvid_collection_free(notes);
    must("close", corvid_close(db));
    return 0;
}
/* docs:end:text_search */
