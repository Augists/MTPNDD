#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "sylvan_refs.h"
#include "test_assert.h"

static int
count_ptr(refs_table_t *tbl, uint64_t ptr)
{
    int count = 0;
    uint64_t *it = protect_iter(tbl, 0, tbl->refs_size);
    while (it != NULL) {
        uint64_t v = protect_next(tbl, &it, tbl->refs_size);
        if (v == ptr) count++;
    }
    return count;
}

static int
test_protect_multiset(void)
{
    refs_table_t tbl;
    protect_create(&tbl, 128);

    uint64_t ptr = 0x12345678abcdefULL;

    protect_add_insert(&tbl, ptr);
    protect_add_insert(&tbl, ptr);
    test_assert(count_ptr(&tbl, ptr) == 2);

    test_assert(protect_add_remove_one(&tbl, ptr));
    test_assert(count_ptr(&tbl, ptr) == 1);

    test_assert(protect_add_remove_one(&tbl, ptr));
    test_assert(count_ptr(&tbl, ptr) == 0);

    protect_free(&tbl);
    return 0;
}

int
main(void)
{
    return test_protect_multiset();
}
