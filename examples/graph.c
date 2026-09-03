/* graph.c — directed edges over a small corpus, and delete cascade.
 *
 * Three documents (ga, gb, gc) linked by a `parent_of` relation, plus
 * one edge pointing at `gd` which never exists as a document (dangling
 * edges are allowed), and a weighted `route` relation. Demonstrates
 * neighbors (key order), in_neighbors, weighted neighbors, BFS traverse
 * at 1 and 2 hops (cycle-safe), and the delete cascade: deleting a key
 * removes its edges in the same transaction — deleting the never-a-
 * document `gd` still drops the `gb -> gd` edge (spec §4.8/§4.11).
 *
 * Build/run: CMake builds this as `example_graph`; CI runs it as a
 * ctest on every platform.
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "graph: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

/* docs:begin:graph */
/* Print one `[a,b,c]` line from a key-set cursor (borrowed views),
 * naming the last error if the call failed. */
static void print_strs(const char *label, corvid_strs *s) {
    if (!s) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "graph: %s failed: %.*s\n", label, (int)len, msg);
        exit(1);
    }
    printf("%-36s [", label);
    for (;;) {
        const char *str = NULL;
        size_t len = 0;
        if (corvid_strs_next(s, &str, &len) != 1) break;
        printf("%.*s ", (int)len, str);
    }
    printf("]\n");
    corvid_strs_free(s);
}

int main(void) {
    corvid_db *db = corvid_open_memory();
    if (!db) { fprintf(stderr, "graph: open failed\n"); return 1; }
    corvid_coll *nodes = corvid_collection(db, "nodes", 5);
    if (!nodes) { fprintf(stderr, "graph: collection failed\n"); return 1; }

    static const char *keys[3] = {"ga", "gb", "gc"};
    for (int i = 0; i < 3; i++) {
        corvid_value *doc = corvid_value_map_new();
        must("map_put n", corvid_value_map_put(
            doc, "n", 1, corvid_value_text(keys[i], strlen(keys[i]))));
        must("insert",
             corvid_insert(nodes, (const uint8_t *)keys[i], strlen(keys[i]), doc));
        corvid_value_free(doc);
    }

    must("link ga->gb", corvid_link(nodes, (const uint8_t *)"ga", 2, "parent_of",
                                    9, (const uint8_t *)"gb", 2));
    must("link ga->gc", corvid_link(nodes, (const uint8_t *)"ga", 2, "parent_of",
                                    9, (const uint8_t *)"gc", 2));
    /* gb -> gd: gd never exists as a document; the edge dangles fine. */
    must("link gb->gd", corvid_link(nodes, (const uint8_t *)"gb", 2, "parent_of",
                                    9, (const uint8_t *)"gd", 2));
    must("link_weighted ga->gb",
         corvid_link_weighted(nodes, (const uint8_t *)"ga", 2, "route", 5,
                              (const uint8_t *)"gb", 2, 2.5));
    must("link_weighted ga->gd",
         corvid_link_weighted(nodes, (const uint8_t *)"ga", 2, "route", 5,
                              (const uint8_t *)"gd", 2, 0.75));

    print_strs("neighbors(ga)",
              corvid_neighbors(nodes, (const uint8_t *)"ga", 2,
                                                "parent_of", 9));
    print_strs("in_neighbors(gb)",
              corvid_in_neighbors(nodes, (const uint8_t *)"gb", 2, "parent_of", 9));

    corvid_geohits *routes = corvid_neighbors_weighted(
        nodes, (const uint8_t *)"ga", 2, "route", 5);
    if (!routes) { fprintf(stderr, "graph: neighbors_weighted failed\n"); return 1; }
    printf("%-34s [", "routes from ga (weighted):");
    for (;;) {
        corvid_geohit hit;
        const corvid_value *doc = NULL;
        if (corvid_geohits_next(routes, &hit, &doc) != 1) break;
        printf("%.*s=%.2f ", (int)hit.key_len, hit.key, hit.distance_km);
    }
    printf("]\n");
    corvid_geohits_free(routes);

    print_strs("traverse(ga, 1 hop)",
              corvid_traverse(nodes, (const uint8_t *)"ga", 2, "parent_of", 9, 1));
    print_strs("traverse(ga, 2 hops)",
              corvid_traverse(nodes, (const uint8_t *)"ga", 2, "parent_of", 9, 2));

    /* Delete cascade: remove gc (a document) and gd (never a document). */
    int32_t existed = 0;
    must("delete gc", corvid_delete(nodes, (const uint8_t *)"gc", 2, &existed));
    printf("delete gc: existed=%d\n", existed);
    must("delete gd", corvid_delete(nodes, (const uint8_t *)"gd", 2, &existed));
    printf("delete gd: existed=%d (never a document; its edges still cascade)\n",
           existed);

    print_strs("neighbors(ga) after deletes",
              corvid_neighbors(nodes, (const uint8_t *)"ga", 2, "parent_of", 9));
    print_strs("neighbors(gb) after deletes",
              corvid_neighbors(nodes, (const uint8_t *)"gb", 2, "parent_of", 9));
    print_strs("traverse(ga, 2 hops) after",
              corvid_traverse(nodes, (const uint8_t *)"ga", 2, "parent_of", 9, 2));

    corvid_collection_free(nodes);
    must("close", corvid_close(db));
    return 0;
}
/* docs:end:graph */
