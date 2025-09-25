// Copyright (C) Augists
// Copyright (C) XJTU ANTS Netverify Lab
#ifndef MTPNDD_H
#define MTPNDD_H

#include "ndd.h"
#include <string.h>
#include <math.h>

// Terminal value type enumeration
typedef enum mtpndd_terminal_type_e {
    MTPNDD_TERMINAL_BOOLEAN = 0,    // Boolean value (compatible with existing NDD)
    MTPNDD_TERMINAL_INTEGER,        // 64-bit integer  
    MTPNDD_TERMINAL_DOUBLE,         // Double precision floating point
    MTPNDD_TERMINAL_STRING,         // String (null-terminated)
    MTPNDD_TERMINAL_BYTES,          // Binary data
    MTPNDD_TERMINAL_CUSTOM          // Custom type
} mtpndd_terminal_type_t;

// String terminal value (with length to avoid frequent strlen)
typedef struct mtpndd_string_s {
    char *data;
    size_t length;
    size_t capacity;
} mtpndd_string_t;

// Binary data terminal value
typedef struct mtpndd_bytes_s {
    void *data;
    size_t length;
    size_t capacity;
} mtpndd_bytes_t;

// Custom type terminal value
typedef struct mtpndd_custom_s {
    void *data;
    size_t size;
    // Function pointers for custom operations
    int (*compare)(const void *a, const void *b);
    void* (*copy)(const void *data);
    void (*destroy)(void *data);
    char* (*to_string)(const void *data);
} mtpndd_custom_t;

// Terminal value union (good taste: unify all types, avoid branches)
typedef union mtpndd_terminal_value_u {
    bool boolean;                   // Boolean value
    int64_t integer;               // Integer
    double floating;               // Floating point
    mtpndd_string_t string;        // String
    mtpndd_bytes_t bytes;          // Binary data
    mtpndd_custom_t custom;        // Custom type
} mtpndd_terminal_value_t;

// Complete terminal value structure (type + value + reference count)
typedef struct mtpndd_terminal_s {
    mtpndd_terminal_type_t type;
    mtpndd_terminal_value_t value;
    uint32_t ref_count;            // Reference count, thread-safe
} mtpndd_terminal_t;

// MTPNDD node structure (extends ndd_t)
typedef struct mtpndd_node_s {
    // Inherit all ndd_t fields for compatibility
    uint32_t field;
    ndd_bdd_t *edges;              // BDD edge label array
    uint32_t edge_count;
    uint32_t edge_capacity;
    uint32_t ref_count;            // Node reference count
    bool is_terminal;              // Whether it's a terminal node
    
    // MTPNDD extension fields
    mtpndd_terminal_t *terminal;   // Terminal value (valid only when is_terminal=true)
    bool is_multiterminal;         // Whether it's a multi-terminal node
} mtpndd_node_t;

// MTPNDD handle type (extends ndd_t)
typedef struct mtpndd_s {
    mtpndd_node_t *node;           // Pointer to MTPNDD node
    bool is_terminal;              // Inherited from ndd_t for compatibility
    bool is_multiterminal;         // Whether it's multi-terminal
} mtpndd_t;

// ===========================================
// Backward compatibility: NDD <-> MTPNDD seamless conversion
// ===========================================

// Convert NDD to MTPNDD (zero-cost conversion)
static inline mtpndd_t ndd_to_mtpndd(ndd_t ndd) {
    mtpndd_t result;
    result.node = (mtpndd_node_t*)&ndd;  // Safe conversion
    result.is_terminal = ndd_is_terminal(ndd);
    result.is_multiterminal = false;         // NDD is always single-terminal
    return result;
}

// Convert MTPNDD to NDD (compatibility check)
static inline ndd_t mtpndd_to_ndd(mtpndd_t mtpndd) {
    ndd_t result = *(ndd_t*)mtpndd.node;  // Direct copy
    // is_terminal field removed, determined by field==0
    return result;
}

// ===========================================
// Terminal value creation and management
// ===========================================

// Create boolean terminal value (compatible with existing NDD)
mtpndd_terminal_t* mtpndd_terminal_boolean(bool value);

// Create integer terminal value
mtpndd_terminal_t* mtpndd_terminal_integer(int64_t value);

// Create floating point terminal value
mtpndd_terminal_t* mtpndd_terminal_double(double value);

// Create string terminal value (auto-calculate length)
mtpndd_terminal_t* mtpndd_terminal_string(const char *str);

// Create string terminal value (specified length)
mtpndd_terminal_t* mtpndd_terminal_string_n(const char *str, size_t len);

// Create binary data terminal value
mtpndd_terminal_t* mtpndd_terminal_bytes(const void *data, size_t len);

// Create custom type terminal value
mtpndd_terminal_t* mtpndd_terminal_custom(
    void *data, 
    size_t size,
    int (*compare)(const void *a, const void *b),
    void* (*copy)(const void *data),
    void (*destroy)(void *data),
    char* (*to_string)(const void *data)
);

// Terminal value reference counting management (thread-safe)
mtpndd_terminal_t* mtpndd_terminal_ref(mtpndd_terminal_t *terminal);
void mtpndd_terminal_deref(mtpndd_terminal_t *terminal);

// Terminal value comparison
int mtpndd_terminal_compare(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);

// Terminal value copying
mtpndd_terminal_t* mtpndd_terminal_copy(const mtpndd_terminal_t *terminal);

// ===========================================
// MTPNDD node creation and management
// ===========================================

// Create multi-terminal node
mtpndd_t mtpndd_create_terminal(mtpndd_terminal_t *terminal);

// Create regular MTPNDD node (compatible with NDD)
mtpndd_t mtpndd_create_node(uint32_t field);

// Add edge to MTPNDD node
int mtpndd_add_edge_ex(mtpndd_t *mtpndd, mtpndd_t descendant, ndd_bdd_t label_bdd);

// ===========================================
// Backward compatible convenience functions
// ===========================================

// Create MTPNDD from existing NDD boolean value
mtpndd_t mtpndd_from_boolean(bool value);

// Create MTPNDD from other types
mtpndd_t mtpndd_from_integer(int64_t value);
mtpndd_t mtpndd_from_double(double value);
mtpndd_t mtpndd_from_string(const char *str);
mtpndd_t mtpndd_from_bytes(const void *data, size_t len);

// Compatible with existing ndd_true() and ndd_false()
#define mtpndd_true() mtpndd_from_boolean(true)
#define mtpndd_false() mtpndd_from_boolean(false)

// Type checking
bool mtpndd_is_terminal(mtpndd_t mtpndd);
bool mtpndd_is_multiterminal(mtpndd_t mtpndd);
bool mtpndd_is_boolean_terminal(mtpndd_t mtpndd);
bool mtpndd_is_integer_terminal(mtpndd_t mtpndd);
bool mtpndd_is_double_terminal(mtpndd_t mtpndd);
bool mtpndd_is_string_terminal(mtpndd_t mtpndd);

// Value retrieval (with type checking)
bool mtpndd_get_boolean(mtpndd_t mtpndd);
int64_t mtpndd_get_integer(mtpndd_t mtpndd);
double mtpndd_get_double(mtpndd_t mtpndd);
const char* mtpndd_get_string(mtpndd_t mtpndd);
const void* mtpndd_get_bytes(mtpndd_t mtpndd, size_t *length);
const void* mtpndd_get_custom(mtpndd_t mtpndd, size_t *size);

// Node attribute access
mtpndd_terminal_type_t mtpndd_get_terminal_type(mtpndd_t mtpndd);
uint32_t mtpndd_get_field(mtpndd_t mtpndd);
uint32_t mtpndd_get_edge_count(mtpndd_t mtpndd);
uint32_t mtpndd_get_ref_count(mtpndd_t mtpndd);
uint32_t mtpndd_get_depth(mtpndd_t mtpndd);
uint32_t mtpndd_count_nodes(mtpndd_t mtpndd);
uint32_t mtpndd_count_terminals(mtpndd_t mtpndd);

// Node modification operations
int mtpndd_set_terminal_integer(mtpndd_t *mtpndd, int64_t value);
int mtpndd_set_terminal_string(mtpndd_t *mtpndd, const char *str);
int mtpndd_set_terminal_bytes(mtpndd_t *mtpndd, const void *data, size_t len);
int mtpndd_replace_terminal(mtpndd_t *mtpndd, mtpndd_terminal_t *new_terminal);
int mtpndd_remove_edge(mtpndd_t *mtpndd, uint32_t edge_index);
int mtpndd_clear_edges(mtpndd_t *mtpndd);
int mtpndd_set_terminal_integer(mtpndd_t *mtpndd, int64_t value);
int mtpndd_set_terminal_string(mtpndd_t *mtpndd, const char *str);
int mtpndd_set_terminal_bytes(mtpndd_t *mtpndd, const void *data, size_t len);
int mtpndd_replace_terminal(mtpndd_t *mtpndd, mtpndd_terminal_t *new_terminal);
int mtpndd_remove_edge(mtpndd_t *mtpndd, uint32_t edge_index);
int mtpndd_clear_edges(mtpndd_t *mtpndd);

// ===========================================
// Advanced node creation operations
// ===========================================

// Convenient node creation functions (type inference)
mtpndd_t mtpndd_create_integer_node(int64_t value);
mtpndd_t mtpndd_create_double_node(double value);
mtpndd_t mtpndd_create_string_node(const char *str);
mtpndd_t mtpndd_create_bytes_node(const void *data, size_t len);

// Node copying and cloning
mtpndd_t mtpndd_copy(mtpndd_t source);
mtpndd_t mtpndd_clone(mtpndd_t source);

// Reference counting and memory management
mtpndd_t mtpndd_ref(mtpndd_t mtpndd);
void mtpndd_deref(mtpndd_t mtpndd);

// ===========================================
// MTPNDD edge and value set structures
// ===========================================

// MTPNDD edge structure
typedef struct mtpndd_edge_s {
    ndd_bdd_t label_bdd;     // Edge label BDD
    mtpndd_t target;         // Target node
    bool is_valid;           // Whether valid
} mtpndd_edge_t;

// Value set union
typedef union mtpndd_value_set_union_u {
    bool *booleans;
    int64_t *integers;
    double *doubles;
    char **strings;
    mtpndd_bytes_t *bytes;
    mtpndd_custom_t *customs;
} mtpndd_value_set_union_t;

// Value set structure
typedef struct mtpndd_value_set_s {
    mtpndd_terminal_type_t type;
    uint32_t count;
    mtpndd_value_set_union_t values;
} mtpndd_value_set_t;

// ===========================================
// MTPNDD multi-terminal logic operations
// ===========================================

// Multi-terminal value logic operation types
typedef enum mtpndd_logic_operation_e {
    MTPNDD_OP_AND,         // Logical AND
    MTPNDD_OP_OR,          // Logical OR
    MTPNDD_OP_NOT,         // Logical NOT
    MTPNDD_OP_XOR,         // Logical XOR
    MTPNDD_OP_IMP,         // Logical implication
    MTPNDD_OP_DIFF,        // Logical difference
    MTPNDD_OP_ITE          // if-then-else
} mtpndd_logic_operation_t;

// Terminal value operation strategies
typedef enum mtpndd_terminal_operation_strategy_e {
    MTPNDD_STRATEGY_BOOLEAN,     // Boolean logic (true/false)
    MTPNDD_STRATEGY_NUMERIC_MIN, // Numeric minimum
    MTPNDD_STRATEGY_NUMERIC_MAX, // Numeric maximum
    MTPNDD_STRATEGY_NUMERIC_ADD, // Numeric addition
    MTPNDD_STRATEGY_NUMERIC_MUL, // Numeric multiplication
    MTPNDD_STRATEGY_BITWISE,     // Bitwise operations
    MTPNDD_STRATEGY_STRING_CONCAT, // String concatenation
    MTPNDD_STRATEGY_STRING_CHOICE, // String choice (prefer first)
    MTPNDD_STRATEGY_CUSTOM        // Custom operations
} mtpndd_terminal_operation_strategy_t;

// Logic operation configuration
typedef struct mtpndd_logic_config_s {
    mtpndd_terminal_operation_strategy_t strategy;
    void *user_data;                               // User data
    bool auto_type_promotion;                      // Auto type promotion
    bool preserve_type_safety;                     // Preserve type safety
} mtpndd_logic_config_t;

// Basic multi-terminal logic operations
mtpndd_t mtpndd_and(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_or(mtpndd_t a, mtpndd_t b);
mtpndd_t mtpndd_not(mtpndd_t a);

// Configurable multi-terminal logic operations
mtpndd_t mtpndd_and_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);
mtpndd_t mtpndd_or_with_config(mtpndd_t a, mtpndd_t b, const mtpndd_logic_config_t *config);
mtpndd_t mtpndd_not_with_config(mtpndd_t a, const mtpndd_logic_config_t *config);

// Conditional branch operation (if-then-else)
mtpndd_t mtpndd_ite(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch);
mtpndd_t mtpndd_ite_with_config(mtpndd_t condition, mtpndd_t then_branch, mtpndd_t else_branch,
                               const mtpndd_logic_config_t *config);

// Logic operation helper functions
bool mtpndd_terminals_compatible(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_logic_config_t mtpndd_create_default_logic_config(mtpndd_terminal_operation_strategy_t strategy);

// Logic operation statistics and performance monitoring
typedef struct mtpndd_logic_stats_s {
    uint64_t and_operations;
    uint64_t or_operations;
    uint64_t not_operations;
    uint64_t ite_operations;
    uint64_t terminal_conversions;
    uint64_t type_promotions;
    double average_operation_time;
    uint64_t cache_hits;
    uint64_t cache_misses;
} mtpndd_logic_stats_t;

// Get logic operation statistics
mtpndd_logic_stats_t mtpndd_get_logic_stats();
void mtpndd_reset_logic_stats();
void mtpndd_print_logic_stats();

// ===========================================
// MTPNDD serialization and deserialization operations (simplified version)
// ===========================================

// Serialization format enumeration
typedef enum mtpndd_serialization_format_e {
    MTPNDD_FORMAT_BINARY,     // Binary format (efficient)
    MTPNDD_FORMAT_JSON,       // JSON format (readable)
    MTPNDD_FORMAT_XML,        // XML format (compatible)
    MTPNDD_FORMAT_CUSTOM      // Custom format
} mtpndd_serialization_format_t;

// Serialization options
typedef struct mtpndd_serialization_options_s {
    mtpndd_serialization_format_t format;
    bool compress_data;               // Whether to compress data
    bool include_metadata;            // Whether to include metadata
    bool validate_integrity;          // Whether to validate integrity
    uint32_t version;                 // Serialization version number
    const char *encoding;             // Encoding format (e.g., UTF-8)
} mtpndd_serialization_options_t;

// Serialization result
typedef struct mtpndd_serialized_data_s {
    void *data;                       // Serialized data
    size_t size;                      // Data size
    mtpndd_serialization_format_t format; // Data format
    uint32_t checksum;                // Data checksum
    char *error_message;              // Error message (if any)
} mtpndd_serialized_data_t;

// Deserialization result
typedef struct mtpndd_deserialized_result_s {
    mtpndd_t *nodes;                  // Deserialized node array
    uint32_t node_count;              // Node count
    mtpndd_serialization_options_t options; // Original serialization options
    bool integrity_verified;          // Integrity verification result
    char *warning_message;            // Warning message (if any)
} mtpndd_deserialized_result_t;

// Serialization statistics
typedef struct mtpndd_serialization_stats_s {
    size_t total_nodes;               // Total node count
    size_t terminal_nodes;            // Terminal node count
    size_t internal_nodes;            // Internal node count
    size_t unique_terminals;          // Unique terminal value count
    size_t compressed_size;           // Compressed size
    size_t uncompressed_size;         // Uncompressed size
    double compression_ratio;         // Compression ratio
    uint64_t serialization_time_ms;   // Serialization time (milliseconds)
} mtpndd_serialization_stats_t;

// Batch creation structure
typedef struct mtpndd_batch_create_s {
    mtpndd_terminal_type_t type;
    mtpndd_terminal_value_t value;
} mtpndd_batch_create_t;

// Serialization operations
mtpndd_serialized_data_t* mtpndd_serialize_node(mtpndd_t mtpndd, 
                                               const mtpndd_serialization_options_t *options);
mtpndd_deserialized_result_t* mtpndd_deserialize(const mtpndd_serialized_data_t *data);
int mtpndd_serialize_to_file(mtpndd_t mtpndd, const char *filename,
                           const mtpndd_serialization_options_t *options);
mtpndd_deserialized_result_t* mtpndd_deserialize_from_file(const char *filename);
bool mtpndd_verify_serialized_data(const mtpndd_serialized_data_t *data);
mtpndd_serialization_stats_t mtpndd_get_serialization_stats(const mtpndd_serialized_data_t *data);
void mtpndd_destroy_serialized_data(mtpndd_serialized_data_t *data);
void mtpndd_destroy_deserialized_result(mtpndd_deserialized_result_t *result);

// Batch creation
int mtpndd_create_terminals_batch(const mtpndd_batch_create_t *specs, size_t count, mtpndd_t *results);

// ===========================================
// MTPNDD custom terminal value operation system
// ===========================================

// Custom operation function type definition
typedef mtpndd_terminal_t* (*mtpndd_custom_operation_fn)(
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2,
    void *user_data
);

// Unary operation function type
typedef mtpndd_terminal_t* (*mtpndd_unary_operation_fn)(
    const mtpndd_terminal_t *operand,
    void *user_data
);

// Operation function metadata
typedef struct mtpndd_operation_metadata_s {
    const char *name;                    // Operation name
    const char *description;             // Operation description
    mtpndd_terminal_type_t input_type1;  // First operand type
    mtpndd_terminal_type_t input_type2;  // Second operand type (ignored for unary operations)
    mtpndd_terminal_type_t output_type;  // Output type
    bool is_commutative;                 // Whether it satisfies commutativity
    bool is_associative;                 // Whether it satisfies associativity
    bool is_unary;                       // Whether it's a unary operation
    uint32_t operation_id;               // Operation ID (for caching)
} mtpndd_operation_metadata_t;

// Operation registry entry
typedef struct mtpndd_operation_registry_s {
    mtpndd_operation_metadata_t metadata;
    mtpndd_custom_operation_fn binary_fn;  // Binary operation function
    mtpndd_unary_operation_fn unary_fn;    // Unary operation function
    void *user_data;                       // User data
    bool is_active;                        // Whether active
} mtpndd_operation_registry_t;

// Operation manager
typedef struct mtpndd_operation_manager_s {
    mtpndd_operation_registry_t *operations;  // Registered operations array
    uint32_t operation_count;                 // Operation count
    uint32_t operation_capacity;              // Operation capacity
    uint32_t next_operation_id;               // Next operation ID
} mtpndd_operation_manager_t;

// Operation result cache entry
typedef struct mtpndd_operation_cache_entry_s {
    uint32_t operation_id;                // Operation ID
    const mtpndd_terminal_t *operand1;   // First operand
    const mtpndd_terminal_t *operand2;   // Second operand (NULL for unary operations)
    mtpndd_terminal_t *result;            // Operation result
    uint64_t access_count;                // Access count
    uint64_t last_access_time;            // Last access time
    bool is_valid;                        // Whether valid
} mtpndd_operation_cache_entry_t;

// Operation cache manager
typedef struct mtpndd_operation_cache_manager_s {
    mtpndd_operation_cache_entry_t *cache; // Cache array
    uint32_t cache_size;                   // Cache size
    uint32_t cache_used;                   // Used cache
    uint64_t cache_hits;                   // Cache hit count
    uint64_t cache_misses;                 // Cache miss count
    bool enable_cache;                     // Whether cache is enabled
} mtpndd_operation_cache_manager_t;

// Predefined operation types
typedef enum mtpndd_predefined_operation_e {
    MTPNDD_OP_ADD,           // Addition
    MTPNDD_OP_SUB,           // Subtraction
    MTPNDD_OP_MUL,           // Multiplication
    MTPNDD_OP_DIV,           // Division
    MTPNDD_OP_MOD,           // Modulo
    MTPNDD_OP_POW,           // Power
    MTPNDD_OP_ABS,           // Absolute value (unary)
    MTPNDD_OP_NEG,           // Negation (unary)
    MTPNDD_OP_SQRT,          // Square root (unary)
    MTPNDD_OP_LOG,           // Logarithm (unary)
    MTPNDD_OP_SIN,           // Sine (unary)
    MTPNDD_OP_COS,           // Cosine (unary)
    MTPNDD_OP_CONCAT,        // String concatenation
    MTPNDD_OP_SUBSTRING,     // Substring
    MTPNDD_OP_LENGTH,        // String length (unary)
    MTPNDD_OP_UPPERCASE,     // To uppercase (unary)
    MTPNDD_OP_LOWERCASE,     // To lowercase (unary)
    MTPNDD_OP_TRIM,          // Trim whitespace (unary)
    MTPNDD_OP_EQUALS,        // Equality comparison
    MTPNDD_OP_COMPARE,       // Size comparison
    MTPNDD_OP_CONTAINS,      // Contains check
    MTPNDD_OP_STARTS_WITH,   // Prefix check
    MTPNDD_OP_ENDS_WITH,     // Suffix check
    MTPNDD_OP_COUNT          // Total number of predefined operations
} mtpndd_predefined_operation_t;

// ===========================================
// Custom operation management API
// ===========================================

// Initialize operation manager
int mtpndd_init_operation_manager();
void mtpndd_cleanup_operation_manager();

// Register custom operations
uint32_t mtpndd_register_binary_operation(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t input_type1,
    mtpndd_terminal_type_t input_type2,
    mtpndd_terminal_type_t output_type,
    mtpndd_custom_operation_fn operation_fn,
    void *user_data,
    bool is_commutative,
    bool is_associative
);

uint32_t mtpndd_register_unary_operation(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t input_type,
    mtpndd_terminal_type_t output_type,
    mtpndd_unary_operation_fn operation_fn,
    void *user_data
);

// Unregister operations
int mtpndd_unregister_operation(uint32_t operation_id);

// Find operations
uint32_t mtpndd_find_operation_by_name(const char *name);
uint32_t mtpndd_find_operation_by_types(
    mtpndd_terminal_type_t input_type1,
    mtpndd_terminal_type_t input_type2,
    mtpndd_terminal_type_t output_type,
    bool is_unary
);

// Get operation information
const mtpndd_operation_metadata_t* mtpndd_get_operation_metadata(uint32_t operation_id);
int mtpndd_list_operations(mtpndd_operation_metadata_t *operations, uint32_t max_count);

// ===========================================
// Custom operation execution API
// ===========================================

// Execute custom operations
mtpndd_terminal_t* mtpndd_execute_custom_operation(
    uint32_t operation_id,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2  // Pass NULL for unary operations
);

// Execute operations by name
mtpndd_terminal_t* mtpndd_execute_operation_by_name(
    const char *operation_name,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// Auto-match and execute operations
mtpndd_terminal_t* mtpndd_execute_auto_operation(
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2,
    mtpndd_terminal_type_t desired_output_type
);

// Execute predefined operations
mtpndd_terminal_t* mtpndd_execute_predefined_operation(
    mtpndd_predefined_operation_t op_type,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// ===========================================
// Operation optimization and caching API
// ===========================================

// Initialize operation cache
int mtpndd_init_operation_cache(uint32_t cache_size);
void mtpndd_cleanup_operation_cache();

// Cache control
void mtpndd_enable_operation_cache(bool enable);
void mtpndd_clear_operation_cache();
void mtpndd_optimize_operation_cache();  // Cache optimization (clean unused entries)

// Batch operations
typedef struct mtpndd_batch_operation_s {
    uint32_t operation_id;
    const mtpndd_terminal_t *operand1;
    const mtpndd_terminal_t *operand2;
    mtpndd_terminal_t *result;  // Output result
} mtpndd_batch_operation_t;

// Batch operation execution
int mtpndd_execute_batch_operations(
    mtpndd_batch_operation_t *operations,
    uint32_t operation_count
);

// Parallel batch operations
int mtpndd_execute_parallel_batch_operations(
    mtpndd_batch_operation_t *operations,
    uint32_t operation_count,
    uint32_t thread_count
);

// ===========================================
// Advanced custom operation functionality
// ===========================================

// Operation chain calling
typedef struct mtpndd_operation_chain_s {
    uint32_t *operation_ids;     // Operation ID sequence
    uint32_t operation_count;    // Operation count
    void **intermediate_data;    // Intermediate data (optional)
} mtpndd_operation_chain_t;

// Create and execute operation chain
mtpndd_operation_chain_t* mtpndd_create_operation_chain();
int mtpndd_add_operation_to_chain(mtpndd_operation_chain_t *chain, uint32_t operation_id);
mtpndd_terminal_t* mtpndd_execute_operation_chain(
    mtpndd_operation_chain_t *chain,
    const mtpndd_terminal_t *initial_operand
);
void mtpndd_destroy_operation_chain(mtpndd_operation_chain_t *chain);

// Conditional operations
typedef struct mtpndd_conditional_operation_s {
    bool (*condition_fn)(const mtpndd_terminal_t *operand, void *user_data);
    uint32_t true_operation_id;
    uint32_t false_operation_id;
    void *condition_data;
} mtpndd_conditional_operation_t;

// Execute conditional operation
mtpndd_terminal_t* mtpndd_execute_conditional_operation(
    const mtpndd_conditional_operation_t *cond_op,
    const mtpndd_terminal_t *operand1,
    const mtpndd_terminal_t *operand2
);

// Operation statistics information
typedef struct mtpndd_operation_stats_s {
    uint64_t total_operations;           // Total operation count
    uint64_t cached_operations;          // Cached operation count
    uint64_t failed_operations;          // Failed operation count
    uint64_t registered_operations;      // Registered operation count
    uint64_t active_operations;          // Active operation count
    double average_operation_time;       // Average operation time
    double cache_hit_ratio;              // Cache hit ratio
    uint64_t memory_usage;               // Memory usage
} mtpndd_operation_stats_t;

// Get operation statistics
mtpndd_operation_stats_t mtpndd_get_operation_stats();
void mtpndd_reset_operation_stats();
void mtpndd_print_operation_stats();

// ===========================================
// Predefined operation shortcut functions
// ===========================================

// Numeric operations
mtpndd_terminal_t* mtpndd_add(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_subtract(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_multiply(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_divide(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_modulo(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_power(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);

// Unary numeric operations
mtpndd_terminal_t* mtpndd_abs(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_negate(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_sqrt(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_log(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_sin(const mtpndd_terminal_t *a);
mtpndd_terminal_t* mtpndd_cos(const mtpndd_terminal_t *a);

// String operations
mtpndd_terminal_t* mtpndd_concat(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_substring(const mtpndd_terminal_t *str, const mtpndd_terminal_t *pos);
mtpndd_terminal_t* mtpndd_length(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_uppercase(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_lowercase(const mtpndd_terminal_t *str);
mtpndd_terminal_t* mtpndd_trim(const mtpndd_terminal_t *str);

// Comparison operations
mtpndd_terminal_t* mtpndd_equals(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_compare(const mtpndd_terminal_t *a, const mtpndd_terminal_t *b);
mtpndd_terminal_t* mtpndd_contains(const mtpndd_terminal_t *str, const mtpndd_terminal_t *substr);
mtpndd_terminal_t* mtpndd_starts_with(const mtpndd_terminal_t *str, const mtpndd_terminal_t *prefix);
mtpndd_terminal_t* mtpndd_ends_with(const mtpndd_terminal_t *str, const mtpndd_terminal_t *suffix);

// Encoding/decoding operations
mtpndd_t mtpndd_encode_prefix(uint32_t* prefix_binary, uint32_t len, uint32_t field);
mtpndd_t mtpndd_encode_prefix_with_terminal(uint32_t* prefix_binary, uint32_t len, uint32_t field, mtpndd_terminal_t *terminal);
mtpndd_t mtpndd_encode_integer_range(uint32_t field, int64_t min_value, int64_t max_value);
mtpndd_t mtpndd_encode_string_set(uint32_t field, const char **strings, uint32_t string_count);
mtpndd_t mtpndd_from_bdd(ndd_bdd_t bdd, uint32_t field);
ndd_bdd_t mtpndd_to_bdd(mtpndd_t mtpndd);

// Value set operations
mtpndd_value_set_t* mtpndd_extract_values(mtpndd_t mtpndd, mtpndd_terminal_type_t type);
void mtpndd_value_set_destroy(mtpndd_value_set_t *value_set);

// Internal function declarations
static void mtpndd_register_predefined_operations();

// ===========================================
// Terminal value constraint and validation mechanism
// ===========================================

// Constraint type enumeration
typedef enum mtpndd_constraint_type_e {
    MTPNDD_CONSTRAINT_RANGE,          // Numeric range constraint
    MTPNDD_CONSTRAINT_SET,            // Value set constraint (enumeration values)
    MTPNDD_CONSTRAINT_PATTERN,        // String pattern constraint (regular expression)
    MTPNDD_CONSTRAINT_LENGTH,         // Length constraint
    MTPNDD_CONSTRAINT_CUSTOM,         // Custom validation function
    MTPNDD_CONSTRAINT_TYPE_SAFE,      // Type safety constraint
    MTPNDD_CONSTRAINT_NOT_NULL        // Non-null constraint
} mtpndd_constraint_type_t;

// Constraint condition union
typedef union mtpndd_constraint_value_u {
    struct {
        double min;
        double max;
        bool min_inclusive;
        bool max_inclusive;
    } range;
    
    struct {
        void **values;
        uint32_t count;
        mtpndd_terminal_type_t value_type;
    } set;
    
    struct {
        char *pattern;
        bool case_sensitive;
    } pattern;
    
    struct {
        uint32_t min_length;
        uint32_t max_length;
    } length;
    
    struct {
        bool (*validate_fn)(const mtpndd_terminal_t *terminal, void *user_data);
        void *user_data;
    } custom;
} mtpndd_constraint_value_t;

// Constraint structure definition
typedef struct mtpndd_constraint_s {
    uint32_t constraint_id;
    const char *name;
    const char *description;
    mtpndd_constraint_type_t type;
    mtpndd_terminal_type_t target_type;
    mtpndd_constraint_value_t value;
    bool is_active;
    uint64_t validation_count;
    uint64_t violation_count;
} mtpndd_constraint_t;

// Validation result structure
typedef struct mtpndd_validation_result_s {
    bool is_valid;
    uint32_t violated_constraint_count;
    uint32_t *violated_constraint_ids;
    char **violation_messages;
    ndd_error_t error_code;
    const char *error_message;
} mtpndd_validation_result_t;

// Constraint manager structure
typedef struct mtpndd_constraint_manager_s {
    mtpndd_constraint_t *constraints;
    uint32_t constraint_count;
    uint32_t constraint_capacity;
    uint32_t next_constraint_id;
    bool global_validation_enabled;
    bool strict_mode;  // Strict mode: any constraint violation causes operation failure
} mtpndd_constraint_manager_t;

// Constraint statistics
typedef struct mtpndd_constraint_stats_s {
    uint64_t total_validations;
    uint64_t successful_validations;
    uint64_t failed_validations;
    uint64_t constraint_violations;
    uint64_t type_violations;
    double average_validation_time;
    uint32_t active_constraints;
    uint32_t total_constraints;
} mtpndd_constraint_stats_t;

// ===========================================
// Constraint management API
// ===========================================

// Initialize constraint manager
int mtpndd_init_constraint_manager();
void mtpndd_cleanup_constraint_manager();

// Enable/disable global validation
void mtpndd_enable_global_validation(bool enable);
void mtpndd_set_strict_mode(bool strict);

// Constraint registration and management
uint32_t mtpndd_register_range_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    double min_value,
    double max_value,
    bool min_inclusive,
    bool max_inclusive
);

uint32_t mtpndd_register_set_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    void **allowed_values,
    uint32_t value_count
);

uint32_t mtpndd_register_pattern_constraint(
    const char *name,
    const char *description,
    const char *pattern,
    bool case_sensitive
);

uint32_t mtpndd_register_length_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    uint32_t min_length,
    uint32_t max_length
);

uint32_t mtpndd_register_custom_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    bool (*validate_fn)(const mtpndd_terminal_t *terminal, void *user_data),
    void *user_data
);

// Constraint query and control
int mtpndd_activate_constraint(uint32_t constraint_id);
int mtpndd_deactivate_constraint(uint32_t constraint_id);
int mtpndd_remove_constraint(uint32_t constraint_id);
const mtpndd_constraint_t* mtpndd_get_constraint(uint32_t constraint_id);
uint32_t mtpndd_find_constraint_by_name(const char *name);
int mtpndd_list_constraints(uint32_t *constraint_ids, uint32_t max_count);

// ===========================================
// Validation API
// ===========================================

// Single terminal value validation
mtpndd_validation_result_t* mtpndd_validate_terminal(const mtpndd_terminal_t *terminal);
mtpndd_validation_result_t* mtpndd_validate_terminal_with_constraints(
    const mtpndd_terminal_t *terminal,
    const uint32_t *constraint_ids,
    uint32_t constraint_count
);

// Batch validation
int mtpndd_validate_terminals_batch(
    const mtpndd_terminal_t **terminals,
    uint32_t terminal_count,
    mtpndd_validation_result_t **results
);

// MTPNDD node validation
mtpndd_validation_result_t* mtpndd_validate_node(mtpndd_t mtpndd);

// Validation result management
void mtpndd_destroy_validation_result(mtpndd_validation_result_t *result);
bool mtpndd_is_validation_successful(const mtpndd_validation_result_t *result);

// ===========================================
// Predefined constraints
// ===========================================

// Common numeric constraints
uint32_t mtpndd_register_positive_constraint(const char *name);     // > 0
uint32_t mtpndd_register_non_negative_constraint(const char *name);  // >= 0
uint32_t mtpndd_register_percentage_constraint(const char *name);    // [0, 100]
uint32_t mtpndd_register_probability_constraint(const char *name);   // [0.0, 1.0]

// Common string constraints
uint32_t mtpndd_register_non_empty_string_constraint(const char *name);
uint32_t mtpndd_register_email_constraint(const char *name);
uint32_t mtpndd_register_phone_constraint(const char *name);
uint32_t mtpndd_register_alphanumeric_constraint(const char *name);

// Type safety constraints
uint32_t mtpndd_register_type_constraint(
    const char *name,
    mtpndd_terminal_type_t required_type
);

// ===========================================
// Constraint statistics and monitoring
// ===========================================

// Get constraint statistics
mtpndd_constraint_stats_t mtpndd_get_constraint_stats();
void mtpndd_reset_constraint_stats();
void mtpndd_print_constraint_stats();

// Constraint performance monitoring
void mtpndd_start_constraint_profiling();
void mtpndd_stop_constraint_profiling();
double mtpndd_get_constraint_validation_time(uint32_t constraint_id);

// ===========================================
// Advanced validation functionality
// ===========================================

// Conditional constraints: apply constraints based on other field values
typedef struct mtpndd_conditional_constraint_s {
    uint32_t base_constraint_id;
    bool (*condition_fn)(const mtpndd_terminal_t *terminal, void *context);
    void *condition_context;
} mtpndd_conditional_constraint_t;

uint32_t mtpndd_register_conditional_constraint(
    const char *name,
    const char *description,
    uint32_t base_constraint_id,
    bool (*condition_fn)(const mtpndd_terminal_t *terminal, void *context),
    void *condition_context
);

// Compound constraints: logical combination of multiple constraints
typedef enum mtpndd_compound_operator_e {
    MTPNDD_COMPOUND_AND,    // All constraints must be satisfied
    MTPNDD_COMPOUND_OR,     // At least one constraint must be satisfied
    MTPNDD_COMPOUND_XOR,    // Exactly one constraint must be satisfied
    MTPNDD_COMPOUND_NOT     // Constraint must not be satisfied
} mtpndd_compound_operator_t;

uint32_t mtpndd_register_compound_constraint(
    const char *name,
    const char *description,
    const uint32_t *constraint_ids,
    uint32_t constraint_count,
    mtpndd_compound_operator_t operator
);

// Dynamic constraints: constraints that can be modified at runtime
typedef struct mtpndd_dynamic_constraint_s {
    uint32_t constraint_id;
    void *dynamic_data;
    int (*update_fn)(mtpndd_constraint_t *constraint, void *dynamic_data);
} mtpndd_dynamic_constraint_t;

uint32_t mtpndd_register_dynamic_constraint(
    const char *name,
    const char *description,
    mtpndd_terminal_type_t target_type,
    void *initial_data,
    int (*update_fn)(mtpndd_constraint_t *constraint, void *dynamic_data)
);

int mtpndd_update_dynamic_constraint(uint32_t constraint_id, void *new_data);

// ===========================================
// Error handling
// ===========================================

// MTPNDD specific error types
typedef enum mtpndd_error_e {
    MTPNDD_SUCCESS = 0,
    MTPNDD_ERROR_TYPE_MISMATCH,    // Type mismatch
    MTPNDD_ERROR_INVALID_TERMINAL, // Invalid terminal value
    MTPNDD_ERROR_STRING_TOO_LONG,  // String too long
    MTPNDD_ERROR_UNSUPPORTED_TYPE, // Unsupported type
    MTPNDD_ERROR_CONVERSION_FAILED // Type conversion failed
} mtpndd_error_t;

// Error handling functions
const char* mtpndd_error_string(mtpndd_error_t error);
void mtpndd_set_error(mtpndd_error_t error, const char *function, int line);
mtpndd_error_t mtpndd_get_last_error();

// ===========================================
// Configuration and statistics
// ===========================================

// MTPNDD configuration structure (extends ndd_config_t)
typedef struct mtpndd_config_s {
    ndd_config_t base;             // Base NDD configuration
    size_t max_string_length;      // Maximum string length
    size_t max_bytes_length;       // Maximum binary data length
    bool enable_string_interning;  // Enable string interning (save memory)
    bool enable_terminal_cache;    // Enable terminal value cache
} mtpndd_config_t;

// MTPNDD statistics
typedef struct mtpndd_stats_s {
    ndd_stats_t base;              // Base NDD statistics
    uint64_t terminal_count;       // Terminal value count
    uint64_t multiterminal_count;  // Multi-terminal node count
    uint64_t string_count;         // String terminal count
    uint64_t integer_count;        // Integer terminal count
    uint64_t double_count;         // Floating point terminal count
    uint64_t bytes_count;          // Binary data terminal count
    uint64_t custom_count;         // Custom type terminal count
} mtpndd_stats_t;

mtpndd_stats_t mtpndd_get_stats();
void mtpndd_print_stats();

// ===========================================
// Initialization and cleanup
// ===========================================

// MTPNDD initialization (internally calls ndd_init)
int mtpndd_init(mtpndd_config_t *config);

// MTPNDD cleanup (internally calls ndd_quit)
void mtpndd_quit();

// Check if MTPNDD is initialized
bool mtpndd_is_initialized();

#endif // MTPNDD_H