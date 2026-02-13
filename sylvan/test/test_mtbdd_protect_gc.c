#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sylvan.h"
#include "test_assert.h"

static int
run_child(void)
{
    lace_start(1, 0);
    sylvan_set_sizes(1LL<<20, 1LL<<20, 1LL<<16, 1LL<<16);
    sylvan_init_package();
    sylvan_init_mtbdd();

    MTBDD node = mtbdd_makenode(0, mtbdd_true, mtbdd_false);
    mtbdd_unprotect(&node);
    sylvan_gc();

    sylvan_quit();
    lace_stop();
    return 0;
}

static int
test_mtbdd_protect_gc_abort_on_unmatched_del(void)
{
    pid_t pid = fork();
    test_assert(pid >= 0);
    if (pid == 0) {
        int res = run_child();
        _exit(res == 0 ? 0 : 1);
    }

    int status = 0;
    test_assert(waitpid(pid, &status, 0) == pid);
    test_assert(WIFSIGNALED(status));
    test_assert(WTERMSIG(status) == SIGABRT);
    return 0;
}

int
main(void)
{
    return test_mtbdd_protect_gc_abort_on_unmatched_del();
}
