#ifndef YAML_C_WRAPPER_H
#define YAML_C_WRAPPER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef _WIN32
  #define YAML_API __declspec(dllexport)
#else
  #define YAML_API __attribute__((visibility("default")))
#endif

// --- CORE TYPES ---
typedef void* YAMLTreeHandle;
typedef size_t YAMLNodeId;

// ryml uses (size_t)-1 to represent "not found" or "invalid"
#define YAML_NULL_ID ((size_t)-1)

// Pass as the index argument to add_* functions to append instead of inserting
#define END  ((size_t)-1)

// struct lattices uses std::map so it must be C++ only
#ifdef __cplusplus
#include <map>
struct lattices {
    YAMLTreeHandle original;
    YAMLTreeHandle included;
    YAMLTreeHandle expanded;
};
extern "C" {
#endif

YAML_API struct lattices get_lattices(const char* filename, const char* lattice_name);

// --- PARSING & MEMORY ---
YAML_API YAMLTreeHandle parse_file(const char* filename);
YAML_API YAMLTreeHandle parse_string(const char* yaml_str);
YAML_API YAMLTreeHandle create_empty_tree();
YAML_API void delete_tree(YAMLTreeHandle tree);
YAML_API void remove_node(YAMLTreeHandle tree, YAMLNodeId parent, YAMLNodeId child);

// --- TRAVERSAL ---
YAML_API YAMLNodeId get_root(YAMLTreeHandle tree);
YAML_API YAMLNodeId get_parent(YAMLTreeHandle tree, YAMLNodeId node);
YAML_API YAMLNodeId get_child_by_key(YAMLTreeHandle tree, YAMLNodeId parent, const char* key);
YAML_API YAMLNodeId get_child_by_index(YAMLTreeHandle tree, YAMLNodeId parent, size_t index);
YAML_API size_t get_size(YAMLTreeHandle tree, YAMLNodeId node);
YAML_API char* get_node_key(YAMLTreeHandle tree, YAMLNodeId node);

// --- TYPE CHECKS ---
YAML_API bool is_map(YAMLTreeHandle tree, YAMLNodeId node);
YAML_API bool is_sequence(YAMLTreeHandle tree, YAMLNodeId node);
YAML_API bool is_scalar(YAMLTreeHandle tree, YAMLNodeId node);

// --- READING VALUES (Caller must free returned strings with yaml_free_string) ---
YAML_API char* as_string(YAMLTreeHandle tree, YAMLNodeId node);

// --- MODIFICATION ---
// For all three add_* functions:
//   key=NULL    -> no key set (use for sequence elements)
//   index=SIZE_MAX -> append at end; any other value inserts at that position

// Adds a new scalar child. If parent is a map, key is used; if a sequence, key is ignored.
YAML_API YAMLNodeId add_scalar(YAMLTreeHandle tree, YAMLNodeId parent, const char* key, const char* value, size_t index);

// Adds a new empty map child. If parent is a map, key is used; if a sequence, key is ignored.
YAML_API YAMLNodeId add_map(YAMLTreeHandle tree, YAMLNodeId parent, const char* key, size_t index);

// Adds a new empty sequence child. If parent is a map, key is used; if a sequence, key is ignored.
YAML_API YAMLNodeId add_sequence(YAMLTreeHandle tree, YAMLNodeId parent, const char* key, size_t index);

// Changes the scalar value of an existing node.
YAML_API void set_scalar(YAMLTreeHandle tree, YAMLNodeId node, const char* value);

// Changes or sets the key of an existing node.
YAML_API void set_node_key(YAMLTreeHandle tree, YAMLNodeId node, const char* key);

// Deep-copies the type, value, and all children of src into the existing dst node.
YAML_API void deep_copy_node(YAMLTreeHandle dst_tree, YAMLNodeId dst_node, YAMLTreeHandle src_tree, YAMLNodeId src_node);
// Copies all children of src_node into dst_node. Use END to append, or an index to insert at that position.
YAML_API void deep_copy_children(YAMLTreeHandle dst_tree, YAMLNodeId dst_node, YAMLTreeHandle src_tree, YAMLNodeId src_node, size_t index);
// --- EMITTING & UTILS ---
YAML_API char* yaml_to_string(YAMLTreeHandle tree, YAMLNodeId node);
YAML_API bool write_file(YAMLTreeHandle tree, const char* filename);
YAML_API void yaml_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif // YAML_C_WRAPPER_H