// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stddef.h>
#include <stdint.h>

// container_of 宏 - 从成员指针获取结构体指针
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

// 内核风格链表节点
typedef struct list_head_s {
    struct list_head_s *next;
    struct list_head_s *prev;
} list_head_t;

// 内核风格哈希链表节点
typedef struct hlist_node_s {
    struct hlist_node_s *next;
    struct hlist_node_s **pprev;
} hlist_node_t;

typedef struct hlist_head_s {
    hlist_node_t *first;
} hlist_head_t;

// 链表初始化和操作宏
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) list_head_t name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(list_head_t *list) {
    list->next = list;
    list->prev = list;
}

static inline void __list_add(list_head_t *new_node,
                              list_head_t *prev,
                              list_head_t *next) {
    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

static inline void list_add(list_head_t *new_node, list_head_t *head) {
    __list_add(new_node, head, head->next);
}

static inline void list_add_tail(list_head_t *new_node, list_head_t *head) {
    __list_add(new_node, head->prev, head);
}

static inline void __list_del(list_head_t *prev, list_head_t *next) {
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(list_head_t *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static inline int list_empty(const list_head_t *head) {
    return head->next == head;
}

// 链表遍历宏
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

// C99兼容的链表遍历宏 - 需要显式指定类型
#define list_for_each_entry(pos, head, member, type) \
    for (pos = list_entry((head)->next, type, member); \
         &pos->member != (head); \
         pos = list_entry(pos->member.next, type, member))

#define list_for_each_entry_safe(pos, n, head, member, type) \
    for (pos = list_entry((head)->next, type, member), \
         n = list_entry(pos->member.next, type, member); \
         &pos->member != (head); \
         pos = n, n = list_entry(n->member.next, type, member))

// 哈希链表操作
#define HLIST_HEAD_INIT { .first = NULL }
#define HLIST_HEAD(name) hlist_head_t name = { .first = NULL }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)

static inline void INIT_HLIST_NODE(hlist_node_t *h) {
    h->next = NULL;
    h->pprev = NULL;
}

static inline int hlist_unhashed(const hlist_node_t *h) {
    return !h->pprev;
}

static inline int hlist_empty(const hlist_head_t *h) {
    return !h->first;
}

static inline void __hlist_del(hlist_node_t *n) {
    hlist_node_t *next = n->next;
    hlist_node_t **pprev = n->pprev;
    *pprev = next;
    if (next)
        next->pprev = pprev;
}

static inline void hlist_del(hlist_node_t *n) {
    __hlist_del(n);
    n->next = NULL;
    n->pprev = NULL;
}

static inline void hlist_add_head(hlist_node_t *n, hlist_head_t *h) {
    hlist_node_t *first = h->first;
    n->next = first;
    if (first)
        first->pprev = &n->next;
    h->first = n;
    n->pprev = &h->first;
}

// 哈希链表遍历宏
#define hlist_entry(ptr, type, member) \
    container_of(ptr, type, member)

#define hlist_for_each(pos, head) \
    for (pos = (head)->first; pos; pos = pos->next)

// C99兼容的哈希链表遍历宏 - 需要显式指定类型
#define hlist_for_each_entry(tpos, pos, head, member, type) \
    for (pos = (head)->first; \
         pos && (tpos = hlist_entry(pos, type, member), 1); \
         pos = pos->next)

#define hlist_for_each_entry_safe(tpos, pos, n, head, member, type) \
    for (pos = (head)->first; \
         pos && (n = pos->next, 1) && \
         (tpos = hlist_entry(pos, type, member), 1); \
         pos = n)

// 简单哈希函数
static inline uint32_t hash_ptr(const void *ptr, uint32_t bits) {
    uintptr_t val = (uintptr_t)ptr;
    val ^= val >> 32;
    val *= 0x9e3779b9UL;
    return (uint32_t)(val >> (32 - bits));
}

static inline uint32_t hash_long(uint64_t val, uint32_t bits) {
    val ^= val >> 32;
    val *= 0x9e3779b9UL;
    return (uint32_t)(val >> (32 - bits));
}

#endif // KERNEL_UTILS_H 