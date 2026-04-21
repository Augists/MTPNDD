/*
 * Copyright 2023 Tom van Dijk, Formal Methods and Tools, University of Twente
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sylvan_config.h>
#include <stdlib.h>
#include <string.h>

#if SYLVAN_USE_MMAP
#include <sys/mman.h> // for mmap
#endif

#ifndef SYLVAN_ALIGN_H
#define SYLVAN_ALIGN_H

// 2 MiB hugepage size on x86_64; rounding is used consistently in alloc/free/clear
// so munmap and MAP_FIXED remaps see the same size the kernel mapped.
#define SYLVAN_HUGEPAGE_SIZE ((size_t)2 * 1024 * 1024)

#ifdef __cplusplus
namespace sylvan {
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static inline size_t
sylvan_align_round(size_t size)
{
#if SYLVAN_USE_MMAP
    return (size + SYLVAN_HUGEPAGE_SIZE - 1) & ~(SYLVAN_HUGEPAGE_SIZE - 1);
#else
    return (size + LINE_SIZE - 1) & ~((size_t)(LINE_SIZE - 1));
#endif
}

static inline void*
alloc_aligned(size_t size)
{
    size = sylvan_align_round(size);
    void* res;
#if SYLVAN_USE_MMAP
    res = mmap(0, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (res == MAP_FAILED) {
        res = mmap(0, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    if (res == MAP_FAILED) return 0;
#else
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    res = _aligned_malloc(size, LINE_SIZE);
#elif defined(__MINGW32__)
    res = __mingw_aligned_malloc(size, LINE_SIZE);
#else
    res = aligned_alloc(LINE_SIZE, size);
#endif
    if (res != 0) memset(res, 0, size);
#endif
    return res;
}

static inline void
free_aligned(void* ptr, size_t size)
{
    size = sylvan_align_round(size);
#if SYLVAN_USE_MMAP
    munmap(ptr, size);
#elif defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
    _aligned_free(ptr);
#elif defined(__MINGW32__)
    __mingw_aligned_free(ptr);
#else
    free(ptr);
#endif
    (void)size; // suppress unused parameter
}

static inline void
clear_aligned(void* ptr, size_t size)
{
    size = sylvan_align_round(size);
#if SYLVAN_USE_MMAP
    // Try reassigning fresh zero'd hugepages first, fall back through small pages to memset.
    void* res = mmap(ptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_HUGETLB, -1, 0);
    if (res == MAP_FAILED) {
        res = mmap(ptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    }
    if (res == MAP_FAILED) memset(ptr, 0, size);
#else
    memset(ptr, 0, size);
#endif
}

#ifdef __cplusplus
} /* namespace */
#endif

#endif
