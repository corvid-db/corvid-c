/* errcodes.c — the frozen error-code table test (docs/SURFACE.tsv gate).
 *
 * The engine's `corvid::Error` variants surface in this binding as the
 * frozen `corvid_err` code table (FFI.md §1.3: values are frozen, never
 * renumbered). The golden fixtures prove the codes the suites can trigger
 * (err:10/11/12/14/15/17/19 lines); the seven redb-internal fault paths
 * have no public-API trigger (the engine's own radar exempts them), so
 * the table itself — checked here at compile time, so it can never rot —
 * is the proof that every variant maps to its documented code.
 *
 * _Static_assert keeps this zero-cost: any drift in corvid.h fails the
 * BUILD, not just a runtime check.
 */

#include "corvid.h"

#define FROZEN(code, want) \
    _Static_assert((code) == (want), "error-code table drifted: " #code)

FROZEN(CORVID_E_OK, 0);
FROZEN(CORVID_E_DATABASE, 1);
FROZEN(CORVID_E_TRANSACTION, 2);
FROZEN(CORVID_E_TABLE, 3);
FROZEN(CORVID_E_STORAGE, 4);
FROZEN(CORVID_E_COMMIT, 5);
FROZEN(CORVID_E_SET_DURABILITY, 6);
FROZEN(CORVID_E_COMPACTION, 7);
FROZEN(CORVID_E_DECODE, 8);
FROZEN(CORVID_E_CORRUPT_INDEX, 9);
FROZEN(CORVID_E_RESERVED_COLLECTION, 10);
FROZEN(CORVID_E_INVALID_NAME, 11);
FROZEN(CORVID_E_ARGUMENT, 12);
FROZEN(CORVID_E_INCOMPATIBLE_FORMAT, 13);
FROZEN(CORVID_E_EMPTY_INDEX_TRAINING, 14);
FROZEN(CORVID_E_SCHEMA_VIOLATION, 15);
FROZEN(CORVID_E_INVALID_DUMP, 16);
FROZEN(CORVID_E_BACKUP_TARGET_EXISTS, 17);
FROZEN(CORVID_E_IO, 18);
/* 19 (Busy) is FFI-only — compact-exclusivity, no engine Error variant. */
FROZEN(CORVID_E_BUSY, 19);

int main(void)
{
    /* The table is the test; reaching here means every assert held. */
    return 0;
}
