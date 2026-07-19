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

// Pass as the index argument to add_* functions to append instead of inserting.
// Spelled YAML_END rather than END because this is a public header: an
// unprefixed END would clobber any enumerator, variable or macro of that
// name in every translation unit that includes us.
#define YAML_END YAML_NULL_ID

// --- STRING LIST ---
//
// A flat, owning list of C strings. Used to return the human-readable problems
// encountered while building the `expanded` tree (undefined lattice, dangling
// element/line references, undefined `inherit`/`repeat`/`Fork` targets,
// expressions that could not be evaluated, ...). Free with
// free_lattice_problems().
struct string_list {
    char** items;
    size_t count;
};

// --- LATTICES ---
//
// The four trees parse_and_expand_PALS() produces, plus the problems found on
// the way. Plain C: the handles are opaque pointers, so this is usable from any
// language that can read the rest of this header.
struct lattices {
    YAMLTreeHandle original;  // Raw tree mapping each file (including includes)
                              // to its unparsed contents
    YAMLTreeHandle combined;  // Tree with all "include" directives resolved and
                              // spliced inline
    YAMLTreeHandle expanded;  // The expanded root lattice, and nothing else: a
                              // map holding the single `name: {kind: Lattice,
                              // ...}` entry, without the PALS/facility
                              // scaffolding it was defined under
    YAMLTreeHandle leftover;  // Everything the expanded tree does not carry —
                              // the whole PALS/facility document minus the root
                              // lattice, so element/beamline definitions, `use`
                              // statements, constants and any non-root Lattice
    struct string_list problems;  // Problems found while building `expanded`;
                                  // free with free_lattice_problems()
};

// --- CORRESPONDENCE MAP ---
//
// Links a node across the representations produced by parse_and_expand_PALS().
// The trees are built as a derivation chain (original -> combined -> expanded
// and leftover), and provenance is recorded at every copy, so each link records
// which node in each tree a single logical entity maps to.
//
// The mapping is functional per link: one expanded (or leftover) node maps to at
// most one combined node, which maps to at most one original node. Because
// expansion can duplicate nodes (scalar substitution, `repeat`, `inherit`,
// forks), a single combined/original node may appear in several links — one per
// copy. A field is YAML_NULL_ID when no corresponding node exists (e.g. the
// `fork_pointer` scalars synthesised during expansion have no original source).
//
// Expansion splits the document, so a link carries either an `expanded` id or a
// `leftover` id, never both; the two are tied together through the `combined`
// id they share. A definition that was substituted into the lattice therefore
// yields links on both sides: one for the copy in `expanded`, one for the
// definition still standing in `leftover`.
struct node_link {
    YAMLNodeId original;
    YAMLNodeId combined;
    YAMLNodeId expanded;
    YAMLNodeId leftover;
};

// A flat list of node_links. One link is emitted per node of the expanded tree
// and one per node of the leftover tree. Free with free_correspondence_map().
struct correspondence_map {
    struct node_link* links;
    size_t count;
};

// --- NAME MATCHING ---
//
// A flat list of node ids identifying every named construct that matched a
// query string. Each id is a node within the single tree that was passed to
// match_names(). Free with free_name_matches().
struct name_matches {
    YAMLNodeId* nodes;
    size_t count;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Builds and returns all four representations of a lattice file.
 *
 * @param filename     Path to the top-level YAML lattice file.
 * @param root_lattice Name of the lattice to expand. If NULL or empty:
 *                       - expands the lattice named by the last "use"
 * statement, or
 *                       - expands the last lattice defined in the file if no
 * "use" is present.
 * @return A `lattices` struct containing four handles:
 *           - `original`: raw tree mapping each file (including includes) to
 * its unparsed contents.
 *           - `combined`: tree with all "include" directives resolved and
 * spliced inline.
 *           - `expanded`: the selected lattice fully expanded — scalars
 * substituted, repeats unrolled, inherits merged, and forks resolved — and
 * nothing else. Rooted at a map holding the single `name: {kind: Lattice, ...}`
 * entry, stripped of the PALS/facility scaffolding it was defined under. Its
 * `branches` entries are branches, not the BeamLines they were built from, and
 * so carry no `kind`; a BeamLine nested in a `line:` is a sub-line and keeps
 * its own.
 *           - `leftover`: the rest of the document, keeping its PALS/facility
 * scaffolding: element and beamline definitions, `use` statements, constants,
 * and any Lattice that was not the one expanded. Definitions substituted into
 * the lattice are copies, so they appear in both trees.
 *         All four handles must be freed individually with delete_tree().
 *
 *         The returned struct also carries `problems`: an owning list of
 *         human-readable messages for every issue met while building the
 *         `expanded` tree (undefined lattice, dangling element/line references,
 *         undefined `inherit`/`repeat`/`Fork` targets, and expressions that
 *         could not be evaluated). It is empty when expansion was clean. The
 *         caller owns it and must release it with free_lattice_problems(). The
 *         library does not print — the caller decides whether to report.
 */
YAML_API struct lattices parse_and_expand_PALS(const char* filename,
                                      const char* root_lattice);

/**
 * Frees the string array owned by the `problems` list of a `lattices` value.
 * Passing a list with a NULL `items` pointer (e.g. when there were no problems)
 * is safe and has no effect.
 *
 * @param problems The `lattices::problems` list to free (passed by value).
 */
YAML_API void free_lattice_problems(struct string_list problems);

/**
 * Evaluates a single PALS mathematical expression to a double.
 *
 * Supports the full PALS expression grammar: arithmetic (+ - * / ^), unary
 * signs, parentheses, the built-in constants (pi, c_light, r_electron, ...),
 * the math functions (sqrt, log, sin, floor, modulo, ...), and the
 * particle-data functions mass_of / charge_of / anomalous_moment_of (backed by
 * AtomicAndPhysicalConstantsCLib), whose species-name argument must be quoted,
 * e.g. mass_of("#3He"). A leading `expr(...)` wrapper is accepted and unwrapped.
 *
 * This entry point evaluates a standalone string: user-defined constants and
 * variables are NOT in scope (use parse_and_expand_PALS() for whole-lattice
 * evaluation, which resolves them). Expressions containing random() /
 * random_gauss() are treated as non-evaluable here.
 *
 * @param expr Null-terminated expression string.
 * @param ok   Optional out-param. Set to true on success, false on a parse
 *             error, unknown identifier/species, deferred random(), or a
 *             non-finite result. May be NULL.
 * @return The evaluated value on success, or 0.0 on failure.
 */
YAML_API double evaluate_pals_expression(const char* expr, bool* ok);

/**
 * Builds the node correspondence between the four trees of a `lattices` value.
 *
 * Returns a flat list containing one `node_link` per node of the `expanded`
 * tree and one per node of the `leftover` tree. Each link gives the
 * corresponding `combined` and `original` node ids (or YAML_NULL_ID where none
 * exists). Grouping the links by shared combined / original ids recovers, for
 * any node in any of the four trees, the set of nodes it corresponds to in the
 * others.
 *
 * The handles must come from the same parse_and_expand_PALS() call — the
 * provenance recorded during that call is what makes the mapping exact. The
 * `original` handle is accepted for API symmetry; the mapping is derived from
 * the provenance stored in `combined`, `expanded` and `leftover`.
 *
 * @param original Handle to the `original` tree.
 * @param combined Handle to the `combined` tree.
 * @param expanded Handle to the `expanded` tree.
 * @param leftover Handle to the `leftover` tree. May be NULL, in which case only
 *                 the expanded tree is walked.
 * @return A correspondence_map. The caller must free it with
 *         free_correspondence_map(). `links` is NULL and `count` is 0 if
 *         `combined` is NULL, or if both `expanded` and `leftover` are NULL.
 */
YAML_API struct correspondence_map build_correspondence_map(
    YAMLTreeHandle original, YAMLTreeHandle combined, YAMLTreeHandle expanded,
    YAMLTreeHandle leftover);

/**
 * Frees the link array owned by a correspondence_map. Passing a map with a NULL
 * `links` pointer is safe and has no effect.
 *
 * @param map The map to free (passed by value).
 */
YAML_API void free_correspondence_map(struct correspondence_map map);

/**
 * Finds every named construct matched by a PALS name-matching string.
 *
 * The string follows the "Name Matching" / "Element Name Matching" syntax:
 *
 *   [{lattice}>>>][{branch}>>][{kind}::]{name}[>{group}.{sub}. ... .{param}]
 *
 * `{lattice}`, `{branch}`, and `{name}` are PCRE2 patterns matched against the
 * whole name (anchored at both ends); `{kind}` is matched exactly; the
 * parameter path after the single `>` is matched exactly, key by key. An
 * omitted or empty pattern component matches any name at that level. `{branch}`
 * matches an element if any enclosing BeamLine/Branch name matches, so elements
 * in sub-lines are included.
 *
 * The node returned for each match is whatever the string resolves to: the
 * element node (no parameter path), the parameter-group or parameter node (with
 * a path), or, for a bare name — no lattice/branch/kind qualifier and no
 * parameter path — additionally each matching constant and variable defined
 * directly under the `PALS` or `facility` node (both the full
 * `kind: constant`/`kind: variable` and the compact `constants:`/`variables:`
 * forms).
 *
 * Not yet implemented from Element Name Matching: `#N` instance selection,
 * `{e1}:{e2}` ranges, `,` unions, and `&` intersections.
 *
 * Because beamlines and elements are only fully realised after expansion, this
 * is normally run on the `expanded` tree of a parse_and_expand_PALS() result,
 * but it works on any tree. Results are de-duplicated and returned in
 * document order.
 *
 * @param tree         Handle to the tree to search.
 * @param match_string Null-terminated name-matching string.
 * @return A name_matches listing the matched node ids. `nodes` is NULL and
 *         `count` is 0 when there are no matches or on a malformed pattern. The
 *         caller must free it with free_name_matches().
 */
YAML_API struct name_matches match_names(YAMLTreeHandle tree,
                                         const char* match_string);

/**
 * Frees the node array owned by a name_matches. Passing a value with a NULL
 * `nodes` pointer is safe and has no effect.
 *
 * @param matches The value to free (passed by value).
 */
YAML_API void free_name_matches(struct name_matches matches);

// --- PARAMETER VALUES ---
//
// The three outcomes of get_parameter_value(). PARAM_VALUE_NUMBER also covers
// the "parameter identified but not set" case, which returns the parameter's
// default value (0 for every parameter, for now — real per-parameter defaults
// come later).
enum param_value_kind {
    PARAM_VALUE_MISSING = 0,  // the match string did not identify a parameter
    PARAM_VALUE_NUMBER = 1,   // numeric value, in `number`
    PARAM_VALUE_STRING = 2,   // non-numeric string value, in `string`
};

// The result of get_parameter_value(). When `kind` is PARAM_VALUE_STRING,
// `string` is a heap-allocated null-terminated copy the caller must release
// with yaml_free_string(); it is NULL for the other two kinds, which need no
// freeing.
struct param_value {
    int kind;       // one of enum param_value_kind
    double number;  // valid when kind == PARAM_VALUE_NUMBER
    char* string;   // valid when kind == PARAM_VALUE_STRING; NULL otherwise
};

/**
 * Looks up the value of a single lattice parameter named by a PALS
 * name-matching string.
 *
 * `match_string` uses the same syntax as match_names(). It names either an
 * element parameter (with a `>{group}.{sub}. ... .{param}` path) or, as a bare
 * name (no lattice/branch/kind qualifier and no path), a constant or variable —
 * the same constructs match_names() resolves. Run an element-parameter query on
 * the `expanded` tree of a parse_and_expand_PALS() result, where constants and
 * variables referenced by a value have already been substituted; constants and
 * variables themselves live in the facility scaffolding, so look them up in any
 * view that keeps it — `original`, `combined`, or `leftover` — but not
 * `expanded`, which drops it (exactly as with match_names()).
 *
 * The value is returned as stored; it is NOT evaluated. A plain numeric literal
 * comes back as a number (PARAM_VALUE_NUMBER); anything else — an expression such
 * as "0.3 * 5", a species name like "#3He", or any other text — comes back
 * verbatim as a string (PARAM_VALUE_STRING). Evaluation is the job of lattice
 * expansion (the `expanded` tree already holds numbers) or
 * evaluate_pals_expression, not of this accessor.
 *
 * Resolution, and the resulting `kind`:
 *   - Element parameter, set: its stored value — a number, or a string for an
 *     expression or other non-numeric text.
 *   - Element parameter, not set: the default value is returned
 *     (PARAM_VALUE_NUMBER with number 0, for now).
 *   - Constant or variable (bare name): its stored value, the same way.
 *   - Nothing is identified: PARAM_VALUE_MISSING. That covers no matching
 *     element, no matching constant/variable, a bare element name (an element
 *     has no single scalar value), a path that stops on a whole parameter group,
 *     and several matches that carry conflicting values. (Matches that agree —
 *     the same element reused, or several that all take the default — collapse
 *     to the one shared value.)
 *
 * @param tree         Handle to the tree to search.
 * @param match_string Null-terminated name-matching string.
 * @return A param_value. If `kind` is PARAM_VALUE_STRING the caller must free
 *         `string` with yaml_free_string().
 */
YAML_API struct param_value get_parameter_value(YAMLTreeHandle tree,
                                                const char* match_string);

/**
 * Looks up a parameter value across the two trees of an expanded lattice that
 * carry live values: the `expanded` lattice (element parameters, already
 * evaluated) and the `leftover` facility scaffolding (constants, variables, and
 * any element/beamline definitions not spliced into the lattice).
 *
 * This is the whole-lattice form of get_parameter_value(): the caller passes the
 * two relevant handles from a parse_and_expand_PALS() result instead of picking a
 * tree by hand. `expanded` is consulted first; only if it does not identify the
 * parameter is `leftover` tried. The `original` and `combined` trees are
 * deliberately not consulted — they hold raw, pre-expansion text, so a value read
 * from them would be unevaluated and, for reused definitions, duplicated.
 *
 * Either handle may be NULL (treated as "no match" for that tree). The value's
 * `kind`, string ownership, and the number/string/missing rules are exactly
 * those of get_parameter_value().
 *
 * @param expanded     Handle to the `expanded` tree (element parameters).
 * @param leftover     Handle to the `leftover` tree (constants/variables).
 * @param match_string Null-terminated name-matching string.
 * @return A param_value. If `kind` is PARAM_VALUE_STRING the caller must free
 *         `string` with yaml_free_string().
 */
YAML_API struct param_value get_lattice_parameter_value(
    YAMLTreeHandle expanded, YAMLTreeHandle leftover, const char* match_string);

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
 * @param index  Insertion position among existing children. Pass YAML_END
 * to append.
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
 * @param index  Insertion position among existing children. Pass YAML_END
 * to append.
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
 * @param index  Insertion position among existing children. Pass YAML_END
 * to append.
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
 *                 Pass YAML_END to append after all existing children, or 0 to
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
 * Frees a string returned by get_node_key(), as_string(), node_to_string() or
 * tree_to_string(). These are every char*-returning function in this header.
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