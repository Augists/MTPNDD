#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "sylvan_refs.h"
#include "test_assert.h"

static int
find_key_and_count(refs_table_t *tbl, uint64_t key, int32_t *out_count)
{
    uint64_t *it = refs_iter(tbl, 0, tbl->refs_size);
    while (it != NULL) {
        int32_t count = 0;
        uint64_t found = refs_next_full(tbl, &it, tbl->refs_size, &count);
        if (found == key) {
            if (out_count) *out_count = count;
            return 1;
        }
    }
    return 0;
}

static int
test_signed_refs(void)
{
    refs_table_t tbl;
    refs_create(&tbl, 128);

    const uint64_t key = 0x123;

    refs_down(&tbl, key);
    test_assert(refs_count(&tbl) == 1);
    int32_t count = 0;
    test_assert(find_key_and_count(&tbl, key, &count));
    test_assert(count == -1);

    refs_up(&tbl, key); /* -1 + 1 => 0, should delete */
    test_assert(refs_count(&tbl) == 0);
    test_assert(!find_key_and_count(&tbl, key, &count));

    refs_up(&tbl, key);
    test_assert(refs_count(&tbl) == 1);
    test_assert(find_key_and_count(&tbl, key, &count));
    test_assert(count == 1);

    refs_down(&tbl, key); /* 1 - 1 => 0 */
    test_assert(refs_count(&tbl) == 0);
    test_assert(!find_key_and_count(&tbl, key, &count));

    refs_free(&tbl);
    return 0;
}

int
main(void)
{
    return test_signed_refs();
}
