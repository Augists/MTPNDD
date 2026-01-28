// Local Lace configuration overrides.
// Lace uses __has_include("lace_config.h") in `lace.h` to pick these up.
//
// Keep defaults disabled to avoid changing performance/behavior unless explicitly enabled
// via compiler definitions (e.g. -DLACE_COUNT_STEALS=1).

#ifndef LACE_PIE_TIMES
#define LACE_PIE_TIMES 0
#endif

#ifndef LACE_COUNT_TASKS
#define LACE_COUNT_TASKS 0
#endif

#ifndef LACE_COUNT_STEALS
#define LACE_COUNT_STEALS 0
#endif

#ifndef LACE_COUNT_SPLITS
#define LACE_COUNT_SPLITS 0
#endif

#ifndef LACE_USE_HWLOC
#define LACE_USE_HWLOC 0
#endif

// When workers are idle, Lace's default behavior is to aggressively spin and steal.
// For MTPNDD workloads this can significantly slow down the busy worker due to cache/memory contention.
// Enable a small backoff by default; override at compile time if you need pure busy-wait semantics.
#ifndef LACE_STEAL_BACKOFF
#define LACE_STEAL_BACKOFF 1
#endif

// Backoff tuning (consecutive LACE_NOWORK results in the steal loop).
// Keep the early path syscall-free, then yield, then sleep briefly if still idle.
#ifndef LACE_STEAL_BACKOFF_YIELD_ITERS
#define LACE_STEAL_BACKOFF_YIELD_ITERS 256u
#endif

#ifndef LACE_STEAL_BACKOFF_SLEEP_ITERS
#define LACE_STEAL_BACKOFF_SLEEP_ITERS 2048u
#endif

// nanoseconds for nanosleep() once we enter the sleep phase
#ifndef LACE_STEAL_BACKOFF_SLEEP_NS
#define LACE_STEAL_BACKOFF_SLEEP_NS 50000ul
#endif
