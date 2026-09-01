/* geo.c — points, radius, bbox, nearest-k with real coordinates.
 *
 * Four German cities stored with their real lat/lon (the `[lat, lon]`
 * array encoding; a `{lat=…, lon=…}` map encodes the same point).
 * Distances are haversine kilometres:
 *
 *   radius 600 km from central Berlin (52.52, 13.40):
 *     berlin 0.000000, potsdam 26.621424, hamburg 255.120591,
 *     munchen 503.833264 — nearest first, inclusive boundary.
 *   bbox (47..55, 5..15): all four, key order, the 0.0 sentinel
 *     (a box has no center to measure from).
 *   nearest 2: berlin, potsdam — exact haversine order.
 *
 * These are the same points and tolerances the engine's golden geo
 * fixture asserts (~1e-6 km).
 *
 * Build/run: CMake builds this as `example_geo`; CI runs it as a ctest
 * on every platform.
 */

#include <stdio.h>
#include <string.h>

#include "corvid.h"

static void must(const char *what, corvid_status st) {
    if (st != CORVID_OK) {
        size_t len = 0;
        const char *msg = corvid_last_error_message(&len);
        fprintf(stderr, "geo: %s failed (code %u): %.*s\n", what,
                (unsigned)corvid_last_error_code(), (int)len, msg ? msg : "?");
        exit(1);
    }
}

static void put_place(corvid_coll *places, const char *key, double lat,
                      double lon) {
    corvid_value *doc = corvid_value_map_new();
    must("map_put name", corvid_value_map_put(
        doc, "name", 4, corvid_value_text(key, strlen(key))));
    /* map_put / array_push CONSUME the child value (spec §5/§8) — the
     * [lat, lon] point in the engine's array encoding. */
    corvid_value *loc = corvid_value_array_new();
    must("array_push lat", corvid_value_array_push(loc, corvid_value_float(lat)));
    must("array_push lon", corvid_value_array_push(loc, corvid_value_float(lon)));
    must("map_put loc", corvid_value_map_put(doc, "loc", 3, loc));
    must("insert",
         corvid_insert(places, (const uint8_t *)key, strlen(key), doc));
    corvid_value_free(doc); /* insert CLONES the value; ours is still ours */
}

static void print_geohits(const char *label, corvid_geohits *h) {
    printf("%-34s [", label);
    for (;;) {
        corvid_geohit hit;
        const corvid_value *doc = NULL;
        if (corvid_geohits_next(h, &hit, &doc) != 1) break;
        printf("%.*s %.6fkm ", (int)hit.key_len, hit.key, hit.distance_km);
    }
    printf("]\n");
    corvid_geohits_free(h);
}

int main(void) {
    corvid_db *db = corvid_open_memory();
    if (!db) { fprintf(stderr, "geo: open failed\n"); return 1; }
    corvid_coll *places = corvid_collection(db, "places", 6);
    if (!places) { fprintf(stderr, "geo: collection failed\n"); return 1; }

    put_place(places, "berlin", 52.52, 13.40);
    put_place(places, "potsdam", 52.40, 13.06);
    put_place(places, "hamburg", 53.55, 9.99);
    put_place(places, "munchen", 48.14, 11.58);

    must("create_geo_index", corvid_create_geo_index(places, "loc", 3));

    corvid_geohits *hits = corvid_geo_within_radius(places, "loc", 3,
                                                    52.52, 13.40, 600.0);
    if (!hits) { fprintf(stderr, "geo: radius failed\n"); return 1; }
    print_geohits("within 600km of Berlin:", hits);

    hits = corvid_geo_within_bbox(places, "loc", 3, 47, 5, 55, 15);
    if (!hits) { fprintf(stderr, "geo: bbox failed\n"); return 1; }
    print_geohits("bbox 47..55N, 5..15E:", hits);

    hits = corvid_geo_nearest(places, "loc", 3, 52.52, 13.40, 2);
    if (!hits) { fprintf(stderr, "geo: nearest failed\n"); return 1; }
    print_geohits("nearest 2 to Berlin:", hits);

    corvid_collection_free(places);
    must("close", corvid_close(db));
    return 0;
}
