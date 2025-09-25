// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#include "common.h"
#include <stdlib.h>
#include <string.h>

// Dynamic array implementation
dynamic_array_t* dynamic_array_create(size_t initial_capacity) {
    dynamic_array_t *array = (dynamic_array_t*)malloc(sizeof(dynamic_array_t));
    array->data = (void**)malloc(initial_capacity * sizeof(void*));
    array->size = 0;
    array->capacity = initial_capacity;
    return array;
}

void dynamic_array_destroy(dynamic_array_t *array) {
    if (array) {
        free(array->data);
        free(array);
    }
}

void dynamic_array_push(dynamic_array_t *array, void *item) {
    if (array->size >= array->capacity) {
        array->capacity *= 2;
        array->data = (void**)realloc(array->data, array->capacity * sizeof(void*));
    }
    array->data[array->size++] = item;
}

void* dynamic_array_get(dynamic_array_t *array, size_t index) {
    if (index < array->size) {
        return array->data[index];
    }
    return NULL;
}

void dynamic_array_set(dynamic_array_t *array, size_t index, void *item) {
    if (index < array->size) {
        array->data[index] = item;
    }
}

void dynamic_array_clear(dynamic_array_t *array) {
    array->size = 0;
}

// Hash table implementation
hash_table_t* hash_table_create(size_t initial_bucket_count, 
                                hash_t (*hash_func)(const void *key),
                                int (*compare_func)(const void *a, const void *b)) {
    hash_table_t *table = (hash_table_t*)malloc(sizeof(hash_table_t));
    table->buckets = (hash_entry_t**)calloc(initial_bucket_count, sizeof(hash_entry_t*));
    table->bucket_count = initial_bucket_count;
    table->size = 0;
    table->hash_func = hash_func;
    table->compare_func = compare_func;
    return table;
}

void hash_table_destroy(hash_table_t *table) {
    if (table) {
        hash_table_clear(table);
        free(table->buckets);
        free(table);
    }
}

void hash_table_put(hash_table_t *table, void *key, void *value) {
    hash_t hash = table->hash_func(key);
    size_t bucket_index = hash % table->bucket_count;
    
    hash_entry_t *entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->hash == hash && table->compare_func(entry->key, key) == 0) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    hash_entry_t *new_entry = (hash_entry_t*)malloc(sizeof(hash_entry_t));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->hash = hash;
    new_entry->next = table->buckets[bucket_index];
    table->buckets[bucket_index] = new_entry;
    table->size++;
}

void* hash_table_get(hash_table_t *table, const void *key) {
    hash_t hash = table->hash_func(key);
    size_t bucket_index = hash % table->bucket_count;
    
    hash_entry_t *entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->hash == hash && table->compare_func(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

int hash_table_contains(hash_table_t *table, const void *key) {
    return hash_table_get(table, key) != NULL;
}

void hash_table_remove(hash_table_t *table, const void *key) {
    hash_t hash = table->hash_func(key);
    size_t bucket_index = hash % table->bucket_count;
    
    hash_entry_t *entry = table->buckets[bucket_index];
    hash_entry_t *prev = NULL;
    
    while (entry) {
        if (entry->hash == hash && table->compare_func(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[bucket_index] = entry->next;
            }
            free(entry);
            table->size--;
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

void hash_table_clear(hash_table_t *table) {
    for (size_t i = 0; i < table->bucket_count; i++) {
        hash_entry_t *entry = table->buckets[i];
        while (entry) {
            hash_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }
    table->size = 0;
}

// Hash set implementation
hash_set_t* hash_set_create(size_t initial_bucket_count,
                           hash_t (*hash_func)(const void *key),
                           int (*compare_func)(const void *a, const void *b)) {
    hash_set_t *set = (hash_set_t*)malloc(sizeof(hash_set_t));
    set->table = hash_table_create(initial_bucket_count, hash_func, compare_func);
    return set;
}

void hash_set_destroy(hash_set_t *set) {
    if (set) {
        hash_table_destroy(set->table);
        free(set);
    }
}

void hash_set_add(hash_set_t *set, void *item) {
    hash_table_put(set->table, item, item);
}

int hash_set_contains(hash_set_t *set, const void *item) {
    return hash_table_contains(set->table, item);
}

void hash_set_remove(hash_set_t *set, const void *item) {
    hash_table_remove(set->table, item);
}

void hash_set_clear(hash_set_t *set) {
    hash_table_clear(set->table);
}

// Integer hash table implementation
int_hash_table_t* int_hash_table_create(size_t initial_bucket_count) {
    int_hash_table_t *table = (int_hash_table_t*)malloc(sizeof(int_hash_table_t));
    table->buckets = (struct int_hash_entry_s**)calloc(initial_bucket_count, sizeof(struct int_hash_entry_s*));
    table->bucket_count = initial_bucket_count;
    table->size = 0;
    return table;
}

void int_hash_table_destroy(int_hash_table_t *table) {
    if (table) {
        int_hash_table_clear(table);
        free(table->buckets);
        free(table);
    }
}

void int_hash_table_put(int_hash_table_t *table, uint64_t key, void *value) {
    hash_t hash = int_hash(key);
    size_t bucket_index = hash % table->bucket_count;
    
    struct int_hash_entry_s *entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    struct int_hash_entry_s *new_entry = (struct int_hash_entry_s*)malloc(sizeof(struct int_hash_entry_s));
    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = table->buckets[bucket_index];
    table->buckets[bucket_index] = new_entry;
    table->size++;
}

void* int_hash_table_get(int_hash_table_t *table, uint64_t key) {
    hash_t hash = int_hash(key);
    size_t bucket_index = hash % table->bucket_count;
    
    struct int_hash_entry_s *entry = table->buckets[bucket_index];
    while (entry) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

int int_hash_table_contains(int_hash_table_t *table, uint64_t key) {
    return int_hash_table_get(table, key) != NULL;
}

void int_hash_table_remove(int_hash_table_t *table, uint64_t key) {
    hash_t hash = int_hash(key);
    size_t bucket_index = hash % table->bucket_count;
    
    struct int_hash_entry_s *entry = table->buckets[bucket_index];
    struct int_hash_entry_s *prev = NULL;
    
    while (entry) {
        if (entry->key == key) {
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[bucket_index] = entry->next;
            }
            free(entry);
            table->size--;
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

void int_hash_table_clear(int_hash_table_t *table) {
    for (size_t i = 0; i < table->bucket_count; i++) {
        struct int_hash_entry_s *entry = table->buckets[i];
        while (entry) {
            struct int_hash_entry_s *next = entry->next;
            free(entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }
    table->size = 0;
}

// Common hash functions
hash_t ptr_hash(const void *ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    return (hash_t)(addr ^ (addr >> 32));
}

hash_t int_hash(uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return (hash_t)value;
}

int ptr_compare(const void *a, const void *b) {
    return (a == b) ? 0 : ((a < b) ? -1 : 1);
} 