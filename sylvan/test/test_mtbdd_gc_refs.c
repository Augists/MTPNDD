#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "test_assert.h"

static int
test_mtbdd_gc_refs_basic(void)
{
    MTBDD node = mtbdd_makenode(0, mtbdd_true, mtbdd_false);
    mtbdd_ref(node);
    sylvan_gc();
    test_assert(mtbdd_count_refs() == 1);
    mtbdd_deref(node);
    sylvan_gc();
    test_assert(mtbdd_count_refs() == 0);
    return 0;
}

int
main(void)
{
    lace_start(1, 0);
    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_mtbdd();

    int res = test_mtbdd_gc_refs_basic();

    sylvan_quit();
    lace_stop();
    return res;
}
