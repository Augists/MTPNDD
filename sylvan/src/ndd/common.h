// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef NDD_COMMON_H
#define NDD_COMMON_H

#include <stdint.h>
#include <stddef.h>

// Basic type definitions
typedef uint64_t bdd_node_t;  // BDD node uses 64-bit integer
typedef uint32_t hash_t;      // Hash value type
typedef uint32_t field_id_t;  // Field ID type

// Dynamic array structure
typedef struct dynamic_array_s {
    void **data;
    size_t size;
    size_t capacity;
} dynamic_array_t;

// Hash table entry
typedef struct hash_entry_s {
    void *key;
    void *value;
    hash_t hash;
    struct hash_entry_s *next;
} hash_entry_t;

// Hash table structure
typedef struct hash_table_s {
    hash_entry_t **buckets;
    size_t bucket_count;
    size_t size;
    hash_t (*hash_func)(const void *key);
    int (*compare_func)(const void *a, const void *b);
} hash_table_t;

// Hash set structure
typedef struct hash_set_s {
    hash_table_t *table;
} hash_set_t;

// Integer hash table (specialized for BDD nodes)
typedef struct int_hash_table_s {
    struct int_hash_entry_s {
        uint64_t key;
        void *value;
        struct int_hash_entry_s *next;
    } **buckets;
    size_t bucket_count;
    size_t size;
} int_hash_table_t;

// Dynamic array functions
dynamic_array_t* dynamic_array_create(size_t initial_capacity);
void dynamic_array_destroy(dynamic_array_t *array);
void dynamic_array_push(dynamic_array_t *array, void *item);
void* dynamic_array_get(dynamic_array_t *array, size_t index);
void dynamic_array_set(dynamic_array_t *array, size_t index, void *item);
void dynamic_array_clear(dynamic_array_t *array);

// Hash table functions
hash_table_t* hash_table_create(size_t initial_bucket_count, 
                                hash_t (*hash_func)(const void *key),
                                int (*compare_func)(const void *a, const void *b));
void hash_table_destroy(hash_table_t *table);
void hash_table_put(hash_table_t *table, void *key, void *value);
void* hash_table_get(hash_table_t *table, const void *key);
int hash_table_contains(hash_table_t *table, const void *key);
void hash_table_remove(hash_table_t *table, const void *key);
void hash_table_clear(hash_table_t *table);

// Hash set functions
hash_set_t* hash_set_create(size_t initial_bucket_count,
                           hash_t (*hash_func)(const void *key),
                           int (*compare_func)(const void *a, const void *b));
void hash_set_destroy(hash_set_t *set);
void hash_set_add(hash_set_t *set, void *item);
int hash_set_contains(hash_set_t *set, const void *item);
void hash_set_remove(hash_set_t *set, const void *item);
void hash_set_clear(hash_set_t *set);

// Integer hash table functions
int_hash_table_t* int_hash_table_create(size_t initial_bucket_count);
void int_hash_table_destroy(int_hash_table_t *table);
void int_hash_table_put(int_hash_table_t *table, uint64_t key, void *value);
void* int_hash_table_get(int_hash_table_t *table, uint64_t key);
int int_hash_table_contains(int_hash_table_t *table, uint64_t key);
void int_hash_table_remove(int_hash_table_t *table, uint64_t key);
void int_hash_table_clear(int_hash_table_t *table);

// Common hash functions
hash_t ptr_hash(const void *ptr);
hash_t int_hash(uint64_t value);
int ptr_compare(const void *a, const void *b);

#endif // NDD_COMMON_H 