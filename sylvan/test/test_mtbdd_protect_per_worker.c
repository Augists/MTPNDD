#include <stdint.h>
#include <stdlib.h>

#include "sylvan.h"
#include "test_assert.h"

static int
test_mtbdd_protect_per_worker_basic(void)
{
    MTBDD node = mtbdd_makenode(0, mtbdd_true, mtbdd_false);
    mtbdd_protect(&node);
    test_assert(mtbdd_count_protected() == 1);

    mtbdd_unprotect(&node);
    test_assert(mtbdd_count_protected() == 0);

    mtbdd_unprotect(&node);
    test_assert(mtbdd_protect_del_only_total() == 1);
    return 0;
}

int
main(void)
{
    lace_start(1, 0);
    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_mtbdd();

    int res = test_mtbdd_protect_per_worker_basic();

    sylvan_quit();
    lace_stop();
    return res;
}
