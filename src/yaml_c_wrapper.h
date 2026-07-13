#ifndef YAML_C_WRAPPER_H
#define YAML_C_WRAPPER_H

/**
 * @file yaml_c_wrapper.h
 * @brief Public C API for parsing and manipulating PALS YAML lattices.
 */

#include <stdbool.h>
#include <stddef.h>

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
#define END ((size_t)-1)

// struct lattices uses std::map so it must be C++ only
#ifdef __cplusplus
#include <map>
struct lattices {
    YAMLTreeHandle original;  // Raw tree mapping each file (including includes)
                              // to its unparsed contents
    YAMLTreeHandle included;  // Tree with all "include" directives resolved and
                              // spliced inline
    YAMLTreeHandle expanded;  // Tree with the selected lattice fully expanded
};
extern "C" {
#endif

/**
 * Builds and returns all three representations of a lattice file.
 *
 * @param filename     Path to the top-level YAML lattice file.
 * @param root_lattice Name of the lattice to expand. If NULL or empty:
 *                       - expands the lattice named by the last "use"
 * statement, or
 *                       - expands the last lattice defined in the file if no
 * "use" is present.
 * @return A `lattices` struct containing three handles:
 *           - `original`: raw tree mapping each file (including includes) to
 * its unparsed contents.
 *           - `included`: tree with all "include" directives resolved and
 * spliced inline.
 *           - `expanded`: tree with the selected lattice fully expanded —
 * scalars substituted, repeats unrolled, inherits merged, and forks resolved.
 *         All three handles must be freed individually with delete_tree().
 */
YAML_API struct lattices parse_and_expand_PALS(const char* filename,
                                      const char* root_lattice);

// --- PARSING & MEMORY ---

/**
 * Parses a YAML file from disk into an opaque tree handle.
 *
 * The file is read into an internal buffer and parsed in-place; the buffer
 * is owned by the returned handle and freed by delete_tree().
 *
 * @param filename Path to the YAML file to parse.
 * @return A tree handle on success, NULL if the file cannot be opened or
 *         if parsing fails.
 */
YAML_API YAMLTreeHandle parse_file(const char* filename);

/**
 * Parses a YAML string into an opaque tree handle.
 *
 * The string is copied into an internal buffer and parsed in-place; the
 * buffer is owned by the returned handle and freed by delete_tree().
 *
 * @param yaml_str Null-terminated YAML string to parse.
 * @return A tree handle on success, NULL if parsing fails.
 */
YAML_API YAMLTreeHandle parse_string(const char* yaml_str);

/**
 * Creates an empty tree with a MAP root node.
 *
 * Useful as a destination for deep_copy_node() / deep_copy_children(),
 * or for building a tree programmatically via add_map(), add_sequence(),
 * and add_scalar().
 *
 * @return A tree handle representing an empty MAP. Must be freed with
 * delete_tree().
 */
YAML_API YAMLTreeHandle create_empty_tree();

/**
 * Frees all memory associated with a tree handle.
 *
 * @param tree Handle previously returned by parse_file(), parse_string(),
 *             create_empty_tree(), or parse_and_expand_PALS(). Passing NULL is safe
 *             and has no effect.
 */
YAML_API void delete_tree(YAMLTreeHandle tree);

/**
 * Removes a child node and all its descendants from the tree.
 *
 * After removal the child's node ID is invalid and must not be used again.
 * The parent parameter is accepted for API symmetry but is not used
 * internally — the parent is inferred from the tree structure.
 *
 * @param tree   Handle to the tree containing the node.
 * @param parent Node ID of the parent (unused, may be YAML_NULL_ID).
 * @param child  Node ID to remove. If YAML_NULL_ID, this call is a no-op.
 */
YAML_API void remove_node(YAMLTreeHandle tree, YAMLNodeId parent,
                          YAMLNodeId child);

// --- TRAVERSAL ---

/**
 * Returns the node ID of the tree's root node.
 *
 * @param tree Handle to a parsed or constructed tree.
 * @return Root node ID, or YAML_NULL_ID if tree is NULL.
 */
YAML_API YAMLNodeId get_root(YAMLTreeHandle tree);

/**
 * Returns the parent of a given node.
 *
 * @param tree Handle to the tree containing the node.
 * @param node Node ID whose parent is requested.
 * @return Parent node ID, or YAML_NULL_ID if node is the root or has no parent.
 */
YAML_API YAMLNodeId get_parent(YAMLTreeHandle tree, YAMLNodeId node);

/**
 * Finds a direct child of a MAP node by its key.
 *
 * @param tree   Handle to the tree.
 * @param parent Node ID of a MAP node to search.
 * @param key    Null-terminated key string to look up.
 * @return Node ID of the matching child, or YAML_NULL_ID if not found or
 *         if parent is not a MAP.
 */
YAML_API YAMLNodeId get_child_by_key(YAMLTreeHandle tree, YAMLNodeId parent,
                                     const char* key);

/**
 * Returns the nth child of a MAP or sequence node.
 *
 * @param tree   Handle to the tree.
 * @param parent Node ID of a MAP or sequence node.
 * @param index  Zero-based child index.
 * @return Node ID of the child at position index, or YAML_NULL_ID if index
 *         is out of range or parent is neither a MAP nor a sequence.
 */
YAML_API YAMLNodeId get_child_by_index(YAMLTreeHandle tree, YAMLNodeId parent,
                                       size_t index);

/**
 * Returns the number of direct children of a node.
 *
 * @param tree Handle to the tree.
 * @param node Node ID of a MAP or sequence node.
 * @return Number of children, or 0 if node is YAML_NULL_ID or is a scalar.
 */
YAML_API size_t get_size(YAMLTreeHandle tree, YAMLNodeId node);

/**
 * Returns the key of a node as a newly allocated null-terminated string.
 *
 * @param tree Handle to the tree.
 * @param node Node ID of a keyed node.
 * @return Heap-allocated string containing the key. The caller must free it
 *         with yaml_free_string(). Returns NULL if node is YAML_NULL_ID or
 *         the node has no key.
 */
YAML_API char* get_node_key(YAMLTreeHandle tree, YAMLNodeId node);

// --- TYPE CHECKS ---

/**
 * Returns true if the node is a MAP (key-value container).
 *
 * @param tree Handle to the tree.
 * @param node Node ID to test.
 * @return true if the node is a MAP, false otherwise or if node is
 * YAML_NULL_ID.
 */
YAML_API bool is_map(YAMLTreeHandle tree, YAMLNodeId node);

/**
 * Returns true if the node is a sequence (ordered list).
 *
 * @param tree Handle to the tree.
 * @param node Node ID to test.
 * @return true if the node is a sequence, false otherwise or if node is
 * YAML_NULL_ID.
 */
YAML_API bool is_sequence(YAMLTreeHandle tree, YAMLNodeId node);

/**
 * Returns true if the node is a scalar (plain value, no children).
 *
 * @param tree Handle to the tree.
 * @param node Node ID to test.
 * @return true if the node is a scalar, false otherwise or if node is
 * YAML_NULL_ID.
 */
YAML_API bool is_scalar(YAMLTreeHandle tree, YAMLNodeId node);

// --- READING VALUES ---

/**
 * Returns the scalar value of a node as a newly allocated null-terminated
 * string.
 *
 * @param tree Handle to the tree.
 * @param node Node ID of a node that has a value.
 * @return Heap-allocated string containing the value. The caller must free it
 *         with yaml_free_string(). Returns NULL if node is YAML_NULL_ID or
 *         the node has no value (e.g. is a MAP or bare sequence).
 */
YAML_API char* as_string(YAMLTreeHandle tree, YAMLNodeId node);

// --- MODIFICATION ---

/**
 * Adds a scalar key-value child to a MAP, or a plain scalar to a sequence.
 *
 * @param tree   Handle to the tree.
 * @param parent Node ID of the parent MAP or sequence.
 * @param key    Key string for MAP parents. Pass NULL for sequence elements.
 * @param value  Null-terminated scalar value string.
 * @param index  Insertion position among existing children. Pass END to append.
 * @return Node ID of the newly created child, or YAML_NULL_ID on failure.
 */
YAML_API YAMLNodeId add_scalar(YAMLTreeHandle tree, YAMLNodeId parent,
                               const char* key, const char* value,
                               size_t index);

/**
 * Adds a new empty MAP child to an existing MAP or sequence.
 *
 * @param tree   Handle to the tree.
 * @param parent Node ID of the parent MAP or sequence.
 * @param key    Key string when parent is a MAP. Pass NULL for sequence
 * elements.
 * @param index  Insertion position among existing children. Pass END to append.
 * @return Node ID of the newly created MAP child, or YAML_NULL_ID on failure.
 */
YAML_API YAMLNodeId add_map(YAMLTreeHandle tree, YAMLNodeId parent,
                            const char* key, size_t index);

/**
 * Adds a new empty sequence child to an existing MAP or sequence.
 *
 * @param tree   Handle to the tree.
 * @param parent Node ID of the parent MAP or sequence.
 * @param key    Key string when parent is a MAP. Pass NULL for sequence
 * elements.
 * @param index  Insertion position among existing children. Pass END to append.
 * @return Node ID of the newly created sequence child, or YAML_NULL_ID on
 * failure.
 */
YAML_API YAMLNodeId add_sequence(YAMLTreeHandle tree, YAMLNodeId parent,
                                 const char* key, size_t index);

/**
 * Sets or replaces the scalar value of an existing node.
 *
 * The value string is copied into the tree's internal arena.
 *
 * @param tree  Handle to the tree.
 * @param node  Node ID to update. If YAML_NULL_ID, this call is a no-op.
 * @param value Null-terminated value string to set.
 */
YAML_API void set_scalar(YAMLTreeHandle tree, YAMLNodeId node,
                         const char* value);

/**
 * Sets or replaces the key of an existing node.
 *
 * The key string is copied into the tree's internal arena.
 *
 * @param tree Handle to the tree.
 * @param node Node ID to update. If YAML_NULL_ID, this call is a no-op.
 * @param key  Null-terminated key string to set.
 */
YAML_API void set_node_key(YAMLTreeHandle tree, YAMLNodeId node,
                           const char* key);

/**
 * Deep-copies the contents of a source node into a destination node,
 * overwriting whatever was previously there. Works across different trees.
 *
 * Keys, values, type flags, and all descendants are copied. Strings are
 * duplicated into the destination tree's arena so the source tree may be
 * freed independently afterwards.
 *
 * @param dst_tree Handle to the destination tree.
 * @param dst_node Node ID in dst_tree to copy into.
 * @param src_tree Handle to the source tree (may be the same as dst_tree).
 * @param src_node Node ID in src_tree to copy from.
 *                 If either handle is NULL or either node is YAML_NULL_ID,
 *                 this call is a no-op.
 */
YAML_API void deep_copy_node(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                             YAMLTreeHandle src_tree, YAMLNodeId src_node);

/**
 * Deep-copies the children of a source node into a destination node,
 * inserting them at the given position. Works across different trees.
 *
 * Only the children are copied — the source node itself is not. Existing
 * children of dst_node are preserved. Strings are duplicated into the
 * destination tree's arena.
 *
 * @param dst_tree Handle to the destination tree.
 * @param dst_node Node ID in dst_tree to copy children into.
 * @param src_tree Handle to the source tree (may be the same as dst_tree).
 * @param src_node Node ID in src_tree whose children are copied.
 * @param index    Insertion position among dst_node's existing children.
 *                 Pass END to append after all existing children, or 0 to
 *                 prepend before all existing children.
 *                 If either handle is NULL or either node is YAML_NULL_ID,
 *                 this call is a no-op.
 */
YAML_API void deep_copy_children(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                                 YAMLTreeHandle src_tree, YAMLNodeId src_node,
                                 size_t index);

// --- EMITTING & UTILS ---

/**
 * Emits a node and its descendants as a YAML string.
 *
 * @param tree Handle to the tree.
 * @param node Node ID to emit. If YAML_NULL_ID or out of range, returns NULL.
 * @return Heap-allocated null-terminated YAML string. The caller must free it
 *         with yaml_free_string().
 */
YAML_API char* node_to_string(YAMLTreeHandle tree, YAMLNodeId node);

/**
 * Emits a tree as a YAML string. Same as `node_to_string` but defaulted to
 * the root.
 *
 * @param tree Handle to the tree.
 * @return Heap-allocated null-terminated YAML string. The caller must free it
 *         with yaml_free_string().
 */
YAML_API char* tree_to_string(YAMLTreeHandle tree);

/**
 * Writes the entire tree to a file as YAML.
 *
 * @param tree     Handle to the tree to serialize.
 * @param filename Path to the output file. Created or truncated if it already
 * exists.
 * @return true on success, false if the file could not be opened or if an
 *         error occurs during writing.
 */
YAML_API bool write_file(YAMLTreeHandle tree, const char* filename);

/**
 * Frees a string returned by get_node_key(), as_string(), or yaml_to_string().
 *
 * Passing NULL is safe and has no effect.
 *
 * @param str Pointer to the string to free.
 */
YAML_API void yaml_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif  // YAML_C_WRAPPER_H