#include "yaml_c_wrapper.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <sstream>
#include <string>

// Underlying object for YAMLTreeHandle. Contains the parsed YAML tree and the
// string buffer it was parsed from. Nodes are identied by their index (of type
// size_t within its tree.
struct ParsedData {
    ryml::Tree tree;
    std::string buffer;
};

#define GET_TREE(handle) (static_cast<ParsedData*>(handle)->tree)

// Grow node pool if nearly full. ryml does not auto-resize.
static void ensure_capacity(ryml::Tree& t, size_t needed = 1) {
    if (t.size() + needed >= t.capacity()) t.reserve(t.capacity() + 64);
}

// Append or insert a blank child. index=END means append.
static size_t add_child_at(ryml::Tree& t, size_t parent, size_t index) {
    ensure_capacity(t);
    if (index == END) return t.append_child(parent);
    size_t num = t.num_children(parent);
    if (index > num) index = num;
    size_t after = (index > 0) ? t.child(parent, index - 1) : ryml::NONE;
    return t.insert_child(parent, after);
}

// ============================================================
// INTERNAL LATTICE LOGIC
// ============================================================

// Cross-tree deep copy: copies type, key, val, and all descendants of src_node
// into the existing dst_node. Strings are copied into dst_t's arena. ryml only
// natively supports copying nodes within the same tree.
static void deep_copy_recursive(ryml::Tree& dst_t, size_t dst_node,
                                const ryml::Tree& src_t, size_t src_node) {
    std::vector<size_t> src_children;
    for (size_t c = src_t.first_child(src_node); c != ryml::NONE;
         c = src_t.next_sibling(c))
        src_children.push_back(c);

    // Copy type flags (MAP, SEQ, VAL, KEY)
    dst_t.ref(dst_node) |= (src_t.type(src_node) &
                            (ryml::MAP | ryml::SEQ | ryml::VAL | ryml::KEY));

    // Copy key and val
    if (src_t.has_key(src_node)) {
        ryml::csubstr k = src_t.key(src_node);
        dst_t.set_key(dst_node, dst_t.to_arena(k));
    }
    if (src_t.has_val(src_node)) {
        ryml::csubstr v = src_t.val(src_node);
        dst_t.set_val(dst_node, dst_t.to_arena(v));
    }

    // Pre-allocate dst children, then recurse
    std::vector<size_t> dst_children;
    for (size_t i = 0; i < src_children.size(); i++) {
        ensure_capacity(dst_t);
        dst_children.push_back(dst_t.append_child(dst_node));
    }
    for (size_t i = 0; i < src_children.size(); i++)
        deep_copy_recursive(dst_t, dst_children[i], src_t, src_children[i]);
}

// Build a name->node map for all elements that have a "kind" key.
static void make_ele_map(std::map<std::string, size_t>& emap,
                         const ryml::Tree& t, size_t node) {
    // do nothing if node is a scalar
    if (node == ryml::NONE || t.is_val(node)) return;

    // if node is a map from a name to it's properties and if the properties
    // contain "kind", add the node to the element map
    if (t.is_map(node)) {
        size_t first = t.first_child(node);
        if (first != ryml::NONE && t.has_key(first) && t.is_map(first) &&
            t.has_child(first, ryml::to_csubstr("kind"))) {
            emap.emplace(std::string(t.key(first).str, t.key(first).len),
                         first);
        }
    }

    // if node is a map or sequence, recurse into children
    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        make_ele_map(emap, t, c);
}

// Forward declaration — handle_fork calls expand, expand calls handle_fork
static void expand(ryml::Tree& t, size_t node,
                   std::map<std::string, size_t>& emap,
                   size_t branches = ryml::NONE);

// Helper: get a string value of a keyed child, returns "" if not found
static std::string child_val_str(const ryml::Tree& t, size_t parent,
                                 const char* key) {
    size_t id = t.find_child(parent, ryml::to_csubstr(key));
    if (id == ryml::NONE || !t.has_val(id)) return "";
    return std::string(t.val(id).str, t.val(id).len);
}

// Helper: find an element named 'name' within a line sequence. The element can
// be just the scalar name or defined as a map.
static size_t find_in_line(const ryml::Tree& t, size_t line,
                           const std::string& name) {
    for (size_t c = t.first_child(line); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (t.is_val(c) && std::string(t.val(c).str, t.val(c).len) == name)
            return c;
        if (t.is_map(c)) {
            size_t entry = t.first_child(c);
            if (entry != ryml::NONE && t.has_key(entry) &&
                std::string(t.key(entry).str, t.key(entry).len) == name)
                return entry;
        }
    }
    return ryml::NONE;
}

// Handle a Fork element:
//  1. Reads the ForkP
//  2. If the beamline ForkP["to_line"] doesn't exist in the lattice,
//     create the target branch in branches
//  3. Rename the forked beamline to ForkP["new_branch"]
//  4. Create a fork_pointer in the element with pointing to "destination_element" in the
//     new bracnh.
static void handle_fork(ryml::Tree& t, size_t fork_node, size_t branches,
                        std::map<std::string, size_t>& emap) {
    if (branches == ryml::NONE) return;

    size_t forkp = t.find_child(fork_node, ryml::to_csubstr("ForkP"));
    if (forkp == ryml::NONE || !t.is_map(forkp)) return;

    std::string to_line = child_val_str(t, forkp, "to_line");
    std::string to_element = child_val_str(t, forkp, "destination_element");
    std::string branch_name = child_val_str(t, forkp, "new_branch");
    if (to_line.empty() || to_element.empty() || branch_name.empty()) return;

    // Check whether to_line already exists as a branch (by its original element
    // name)
    size_t existing_branch = ryml::NONE;
    for (size_t c = t.first_child(branches); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (!t.is_map(c)) continue;
        size_t entry = t.first_child(c);
        if (entry != ryml::NONE && t.has_key(entry) &&
            std::string(t.key(entry).str, t.key(entry).len) == to_line) {
            existing_branch = entry;
            break;
        }
    }

    size_t branch_node = existing_branch;
    if (existing_branch == ryml::NONE && emap.count(to_line)) {
        size_t def = emap[to_line];
        // Append a new wrapper map to branches, then duplicate the definition
        // into it
        ensure_capacity(t, t.num_children(def) + 2);
        size_t wrapper = t.append_child(branches);
        t.to_map(wrapper);
        branch_node = t.duplicate(def, wrapper, ryml::NONE);
        // Rename from original element name to branch_name
        t.set_key(branch_node, t.to_arena(ryml::to_csubstr(branch_name)));
        // Expand the new branch so its scalars and inherits are resolved
        expand(t, branch_node, emap, branches);
    }

    if (branch_node == ryml::NONE) return;

    // Find to_element within the new branch's line
    size_t line = t.find_child(branch_node, ryml::to_csubstr("line"));
    if (line == ryml::NONE || !t.is_seq(line)) return;
    size_t target = find_in_line(t, line, to_element);
    if (target == ryml::NONE) return;

    // Add fork_pointer: <node id of target as string>
    ensure_capacity(t);
    std::string id_str = std::to_string(target);
    size_t fp_child = t.append_child(fork_node);
    t.ref(fp_child) |= ryml::KEY | ryml::VAL;
    t.set_key(fp_child, t.to_arena(ryml::to_csubstr("fork_pointer")));
    t.set_val(fp_child, t.to_arena(ryml::to_csubstr(id_str)));
}

/**
 * Perform lattice expansion on the element `node`.
 * 1. Substitute scalar elements with their full definition taken from emap.
 * 2. Beamlines that contain "repeat: n" have their contents repeated n times.
 * 3. Elements that contain "inherit: ancestor" have the contents of ancestor
 * copied into element.
 */
static void expand(ryml::Tree& t, size_t node,
                   std::map<std::string, size_t>& emap, size_t branches) {
    if (node == ryml::NONE) return;

    // Sequence — handle 'repeat'
    if (t.is_seq(node)) {
        size_t child = t.first_child(node);
        // loop over all children
        while (child != ryml::NONE) {
            size_t next = t.next_sibling(child);
            if (t.is_map(child) && t.has_children(child)) {
                // first_child because elements are defined as maps with only
                // one key
                size_t entry = t.first_child(child);
                if (t.has_key(entry) && t.is_map(entry)) {
                    size_t repeat_id =
                        t.find_child(entry, ryml::to_csubstr("repeat"));
                    if (repeat_id != ryml::NONE && t.has_val(repeat_id)) {
                        int count = 0;
                        try {
                            count = std::stoi(std::string(
                                t.val(repeat_id).str, t.val(repeat_id).len));
                        } catch (...) {
                        }
                        std::string target(t.key(entry).str, t.key(entry).len);
                        // check if the beamline to be repeated has been defined
                        // in the file
                        if (emap.count(target)) {
                            size_t def = emap[target];
                            size_t line_id =
                                t.find_child(def, ryml::to_csubstr("line"));
                            size_t after = t.prev_sibling(child);
                            // if beamline to be repeated has a line, duplicate
                            // it `count` times, else just duplicate the name of
                            // the beamline `count` times
                            if (line_id != ryml::NONE && t.is_seq(line_id)) {
                                for (int r = 0; r < count; r++)
                                    for (size_t c2 = t.first_child(line_id);
                                         c2 != ryml::NONE;
                                         c2 = t.next_sibling(c2)) {
                                        ensure_capacity(t, 2);
                                        after = t.duplicate(c2, node, after);
                                    }
                            } else {
                                for (int r = 0; r < count; r++) {
                                    ensure_capacity(t, 2);
                                    size_t wrapper =
                                        t.insert_child(node, after);
                                    t.ref(wrapper) |= ryml::MAP;
                                    t.duplicate(def, wrapper, ryml::NONE);
                                    after = wrapper;
                                }
                            }
                            t.remove(child);
                            child = next;
                            continue;
                        }
                    }
                }
            }
            expand(t, child, emap, branches);
            child = next;
        }
        return;
    }

    // Standalone scalar value
    if (t.is_val(node) && !t.has_key(node)) {
        std::string name(t.val(node).str, t.val(node).len);
        // replace with definition in element map
        if (emap.count(name)) {
            size_t def = emap[name];
            ensure_capacity(t, 2);
            t.change_type(node, ryml::MAP);
            t.duplicate(def, node, ryml::NONE);
            expand(t, node, emap, branches);
        }
        return;
    }

    // Map — handle inherit, detect Lattice/Fork, recurse
    if (t.is_map(node)) {
        // Inherit: merge fields from named element (existing keys win)
        size_t inherit_id = t.find_child(node, ryml::to_csubstr("inherit"));
        if (inherit_id != ryml::NONE) {
            std::string name(t.val(inherit_id).str, t.val(inherit_id).len);
            if (emap.count(name)) {
                ensure_capacity(t, t.num_children(emap[name]) + 1);
                t.duplicate_children_no_rep(emap[name], node, ryml::NONE);
            }
        }

        // Detect kind to set context or trigger fork handling
        std::string kind = child_val_str(t, node, "kind");
        size_t node_branches = branches;
        if (kind == "Lattice") {
            node_branches = t.find_child(node, ryml::to_csubstr("branches"));
        } else if (kind == "Fork") {
            handle_fork(t, node, branches, emap);
        }

        size_t original_size = t.num_children(node);
        size_t c = t.first_child(node);
        for (size_t i = 0; i < original_size && c != ryml::NONE;
             i++, c = t.next_sibling(c))
            expand(t, c, emap, node_branches);
    }
}

/**
 * Recursive helper for make_included. Starting from `node`, replace all
 * instances of "include: filename" with the contents of `filename`. Also
 * recurses into `filename` to handle nested include statements.
 */
static void make_included_recursive(ryml::Tree& t, size_t node) {
    if (node == ryml::NONE) return;

    if (t.is_seq(node)) {
        size_t child = t.first_child(node);
        while (child != ryml::NONE) {
            size_t next = t.next_sibling(child);

            // Seq elements are anonymous MAP wrappers, so - include: "file"
            // looks like:
            //   anon wrapper (MAP, no key) -> KEYVAL(key="include", val="file")
            std::string filename;
            bool is_include = false;
            if (!t.has_key(child) && t.is_map(child) &&
                t.num_children(child) == 1) {
                size_t inner = t.first_child(child);
                if (inner != ryml::NONE && t.has_key(inner) &&
                    t.key(inner) == ryml::to_csubstr("include") &&
                    t.has_val(inner)) {
                    is_include = true;
                    filename = std::string(t.val(inner).str, t.val(inner).len);
                }
            }

            if (is_include) {
                ParsedData* inc =
                    static_cast<ParsedData*>(parse_file(filename.c_str()));
                if (inc) {
                    ryml::Tree& inc_t = inc->tree;
                    size_t after = child;
                    std::vector<size_t> inserted;
                    // Splice each child of the included file's root into this
                    // sequence
                    if (inc_t.is_seq(inc_t.root_id())) {
                        for (size_t c = inc_t.first_child(inc_t.root_id());
                             c != ryml::NONE; c = inc_t.next_sibling(c)) {
                            ensure_capacity(t);
                            size_t n = t.insert_child(node, after);
                            deep_copy_recursive(t, n, inc_t, c);
                            inserted.push_back(n);
                            after = n;
                        }
                    } else {
                        // Included root is not a sequence — insert as a single
                        // element
                        ensure_capacity(t);
                        size_t n = t.insert_child(node, after);
                        deep_copy_recursive(t, n, inc_t, inc_t.root_id());
                        inserted.push_back(n);
                    }
                    delete inc;
                    // Recurse into inserted nodes to handle nested includes
                    for (size_t n : inserted) make_included_recursive(t, n);
                }
                t.remove(child);
                child = next;
                continue;
            }

            make_included_recursive(t, child);
            child = next;
        }
        return;
    }

    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        make_included_recursive(t, c);
}

/**
 * Makes the included lattice file. Takes all instances of include statements in
 * filename, as well as nested include statements within included files, and
 * adds them all to the master tree.
 */
static YAMLTreeHandle make_included(const char* filename) {
    ParsedData* data = static_cast<ParsedData*>(parse_file(filename));
    if (!data) return nullptr;
    ryml::Tree& t = data->tree;
    t.reserve(t.capacity() + 64);
    t.reserve_arena(t.arena_capacity() + 65536);
    make_included_recursive(t, t.root_id());
    return data;
}

// helper function for find_lattice
static size_t find_lattice_recursive(const ryml::Tree& t, size_t node,
                                     const std::string& name,
                                     size_t& last_lattice,
                                     std::string& use_name) {
    if (!t.is_map(node) && !t.is_seq(node)) return ryml::NONE;

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c)) {
        // Track "use" statements — last one wins
        if (t.has_key(c) && t.key(c) == ryml::to_csubstr("use") &&
            t.has_val(c)) {
            use_name = std::string(t.val(c).str, t.val(c).len);
            continue;
        }

        // Check if this node is a lattice (map with kind: Lattice)
        bool is_lattice = false;
        if (t.is_map(c)) {
            for (size_t child = t.first_child(c); child != ryml::NONE;
                 child = t.next_sibling(child)) {
                if (t.has_key(child) &&
                    t.key(child) == ryml::to_csubstr("kind") &&
                    t.has_val(child) &&
                    t.val(child) == ryml::to_csubstr("Lattice")) {
                    is_lattice = true;
                    break;
                }
            }
        }

        if (is_lattice) {
            if (!name.empty()) {
                if (t.has_key(c) &&
                    t.key(c) == ryml::csubstr(name.data(), name.size()))
                    return c;
            } else {
                last_lattice = c;
            }
            continue;
        }

        size_t found =
            find_lattice_recursive(t, c, name, last_lattice, use_name);
        if (found != ryml::NONE) return found;
    }

    return ryml::NONE;
}

/**
 * Finds the lattice to be expanded as specified in make_expanded.
 */
static size_t find_lattice(ryml::Tree& t, const std::string& name) {
    size_t last_lattice = ryml::NONE;
    std::string use_name;

    size_t found =
        find_lattice_recursive(t, t.root_id(), name, last_lattice, use_name);
    if (found != ryml::NONE) return found;
    if (!name.empty()) return ryml::NONE;

    if (!use_name.empty()) return find_lattice(t, use_name);

    return last_lattice;
}

/**
 * Create the expanded lattice. Starts with the included lattice, and expands
 * the lattice with the following priorities:
 *  1. If `lat_name` != null, then expand the lattice called `lat_name`
 *  2. If `lat_name` == null, expand the lattice specified in the last `use`
 * statement.
 *  3. If no use statement is present, expand the lattice that occurs last in
 * the file. Last expansion performs the following:
 * 1. Substitute scalar elements with their full definition, if defined in the
 * file outside the lattice.
 * 2. Beamlines that contain "repeat: n" have their contents repeated n times.
 * 3. Elements that contain "inherit: ancestor" have the contents of ancestor
 * copied into element.
 */
static YAMLTreeHandle make_expanded(const char* filename,
                                    const char* lat_name) {
    YAMLTreeHandle data = make_included(filename);
    if (!data) return nullptr;
    ryml::Tree& t = GET_TREE(data);
    t.reserve_arena(t.arena_capacity() + 100000);
    t.reserve(t.capacity() + 10000);

    std::map<std::string, size_t> emap;
    make_ele_map(emap, t, t.root_id());

    std::string name_str = lat_name ? lat_name : "";
    size_t lat_node = find_lattice(t, name_str);
    if (lat_node == ryml::NONE) return data;
    size_t branches = t.find_child(lat_node, ryml::to_csubstr("branches"));

    expand(t, lat_node, emap, branches);
    return data;
}

/**
 * Recursive helper for make_original. For each included file in `src`:
 *  1. Load the file into a new temporary tree.
 *  2. Make a new key-value pair in `master` with key = file name and value =
 * file contents
 *  3. Delete the temporary tree.
 */
static void add_to_master_tree(ryml::Tree& master, const ryml::Tree& src,
                               size_t node) {
    if (node == ryml::NONE || src.is_val(node)) return;
    for (size_t c = src.first_child(node); c != ryml::NONE;
         c = src.next_sibling(c)) {
        add_to_master_tree(master, src, c);
        if (src.has_key(c) && src.key(c) == "include" && src.has_val(c)) {
            std::string filename(src.val(c).str, src.val(c).len);
            ParsedData* child =
                static_cast<ParsedData*>(parse_file(filename.c_str()));
            if (child) {
                ensure_capacity(master, 2);
                size_t dest = master.append_child(master.root_id());
                deep_copy_recursive(master, dest, child->tree,
                                    child->tree.root_id());
                // Root has no key, so stamp the filename on after the copy
                master.ref(dest) |= ryml::KEY;
                master.set_key(dest,
                               master.to_arena(ryml::to_csubstr(filename)));
                add_to_master_tree(master, child->tree, child->tree.root_id());
                delete child;
            }
        }
    }
}

/**
 * Makes the original lattice. Creates a tree that maps included files to their
 * contents.
 */
static YAMLTreeHandle make_original(const char* filename) {
    ParsedData* master = new ParsedData();
    master->tree.rootref() |= ryml::MAP;
    ParsedData* src = static_cast<ParsedData*>(parse_file(filename));
    if (src) {
        ensure_capacity(master->tree, 2);
        size_t dest = master->tree.append_child(master->tree.root_id());
        deep_copy_recursive(master->tree, dest, src->tree, src->tree.root_id());
        // Root has no key, so stamp the filename on after the copy
        master->tree.ref(dest) |= ryml::KEY;
        master->tree.set_key(dest,
                             master->tree.to_arena(ryml::to_csubstr(filename)));
        add_to_master_tree(master->tree, src->tree, src->tree.root_id());
        delete src;
    }
    return master;
}

// ============================================================
// PUBLIC API
// ============================================================

extern "C" {

YAML_API struct lattices parse_and_expand_PALS(const char* filename,
                                      const char* lattice_name) {
    struct lattices lat = {};
    lat.original = make_original(filename);
    lat.included = make_included(filename);
    lat.expanded = make_expanded(filename, lattice_name);
    return lat;
}

// --- PARSING & MEMORY ---

YAML_API YAMLTreeHandle parse_file(const char* filename) {
    try {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) return nullptr;
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        ParsedData* data = new ParsedData();
        data->buffer.resize(size);
        file.read(&data->buffer[0], size);
        data->tree = ryml::parse_in_place(ryml::to_substr(data->buffer));
        return data;
    } catch (...) {
        return nullptr;
    }
}

YAML_API YAMLTreeHandle parse_string(const char* yaml_str) {
    // Explicit null check prevents the segfault
    if (yaml_str == nullptr) {
        return nullptr;
    }

    try {
        ParsedData* data = new ParsedData();
        data->buffer = yaml_str;
        data->tree = ryml::parse_in_place(ryml::to_substr(data->buffer));
        return data;
    } catch (...) {
        return nullptr;
    }
}

YAML_API YAMLTreeHandle create_empty_tree() {
    ParsedData* data = new ParsedData();
    data->tree.rootref() |= ryml::MAP;
    return data;
}

YAML_API void delete_tree(YAMLTreeHandle tree) {
    delete static_cast<ParsedData*>(tree);
}

YAML_API void remove_node(YAMLTreeHandle tree, YAMLNodeId parent,
                          YAMLNodeId child) {
    if (child == YAML_NULL_ID) return;
    GET_TREE(tree).remove(child);
}

// --- TRAVERSAL ---

YAML_API YAMLNodeId get_root(YAMLTreeHandle tree) {
    if (!tree) return YAML_NULL_ID;
    return GET_TREE(tree).root_id();
}

YAML_API YAMLNodeId get_parent(YAMLTreeHandle tree, YAMLNodeId node) {
    ryml::Tree& t = GET_TREE(tree);
    if (node == ryml::NONE || !t.has_parent(node)) return YAML_NULL_ID;
    return t.parent(node);
}

YAML_API YAMLNodeId get_child_by_key(YAMLTreeHandle tree, YAMLNodeId parent,
                                     const char* key) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    if (!t.is_map(parent)) return YAML_NULL_ID;
    size_t child = t.find_child(parent, ryml::to_csubstr(key));
    return (child == ryml::NONE) ? YAML_NULL_ID : child;
}

YAML_API YAMLNodeId get_child_by_index(YAMLTreeHandle tree, YAMLNodeId parent,
                                       size_t index) {
    ryml::Tree& t = GET_TREE(tree);
    if (!(t.is_seq(parent) || t.is_map(parent)) ||
        index >= t.num_children(parent))
        return YAML_NULL_ID;
    return t.child(parent, index);
}

YAML_API size_t get_size(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return 0;
    return GET_TREE(tree).num_children(node);
}

YAML_API char* get_node_key(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return nullptr;
    ryml::Tree& t = GET_TREE(tree);
    if (!t.has_key(node)) return nullptr;
    ryml::csubstr k = t.key(node);
    char* result = new char[k.len + 1];
    memcpy(result, k.str, k.len);
    result[k.len] = '\0';
    return result;
}

// --- TYPE CHECKS ---

YAML_API bool is_map(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_map(node);
}

YAML_API bool is_sequence(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_seq(node);
}

YAML_API bool is_scalar(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_val(node);
}

// --- READING VALUES ---

YAML_API char* as_string(YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return nullptr;
    ryml::Tree& t = GET_TREE(tree);
    if (!t.has_val(node)) return nullptr;
    ryml::csubstr v = t.val(node);
    char* result = new char[v.len + 1];
    memcpy(result, v.str, v.len);
    result[v.len] = '\0';
    return result;
}

// --- MODIFICATION ---

YAML_API YAMLNodeId add_scalar(YAMLTreeHandle tree, YAMLNodeId parent,
                               const char* key, const char* value,
                               size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t id = add_child_at(t, parent, index);
    if (t.is_map(parent) && key) {
        t.ref(id) |= ryml::KEY | ryml::VAL;
        t.set_key(id, t.to_arena(ryml::to_csubstr(key)));
        t.set_val(id, t.to_arena(ryml::to_csubstr(value)));
    } else {
        t.to_val(id, t.to_arena(ryml::to_csubstr(value)));
    }
    return id;
}

YAML_API YAMLNodeId add_map(YAMLTreeHandle tree, YAMLNodeId parent,
                            const char* key, size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t id = add_child_at(t, parent, index);
    if (t.is_map(parent) && key)
        t.to_map(id, t.to_arena(ryml::to_csubstr(key)));
    else
        t.to_map(id);
    return id;
}

YAML_API YAMLNodeId add_sequence(YAMLTreeHandle tree, YAMLNodeId parent,
                                 const char* key, size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t id = add_child_at(t, parent, index);
    if (t.is_map(parent) && key)
        t.to_seq(id, t.to_arena(ryml::to_csubstr(key)));
    else
        t.to_seq(id);
    return id;
}

YAML_API void set_scalar(YAMLTreeHandle tree, YAMLNodeId node,
                         const char* value) {
    if (node == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    t.ref(node) |= ryml::VAL;
    t.set_val(node, t.to_arena(ryml::to_csubstr(value)));
}

YAML_API void set_node_key(YAMLTreeHandle tree, YAMLNodeId node,
                           const char* key) {
    if (node == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    t.ref(node) |= ryml::KEY;
    t.set_key(node, t.to_arena(ryml::to_csubstr(key)));
}

YAML_API void deep_copy_node(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                             YAMLTreeHandle src_tree, YAMLNodeId src_node) {
    if (!dst_tree || !src_tree) return;
    if (dst_node == YAML_NULL_ID || src_node == YAML_NULL_ID) return;
    ryml::Tree& dt = GET_TREE(dst_tree);
    const ryml::Tree& st = GET_TREE(src_tree);
    ensure_capacity(dt, st.num_children(src_node) + 1);
    dt.duplicate_contents(&st, src_node, dst_node);
}

YAML_API void deep_copy_children(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                                 YAMLTreeHandle src_tree, YAMLNodeId src_node,
                                 size_t index) {
    if (!dst_tree || !src_tree) return;
    if (dst_node == YAML_NULL_ID || src_node == YAML_NULL_ID) return;
    ryml::Tree& dt = GET_TREE(dst_tree);
    const ryml::Tree& st = GET_TREE(src_tree);
    size_t after;
    if (index == END)
        after = dt.last_child(dst_node);
    else if (index == 0)
        after = ryml::NONE;
    else
        after = dt.child(dst_node, index - 1);
    ensure_capacity(dt, st.num_children(src_node) + 1);
    dt.duplicate_children(&st, src_node, dst_node, after);
}

// --- EMITTING & UTILS ---

YAML_API char* node_to_string(YAMLTreeHandle tree, YAMLNodeId node) {
    ryml::Tree& t = GET_TREE(tree);
    if (node == ryml::NONE || node >= t.capacity()) return nullptr;
    std::stringstream ss;
    ss << t.ref(node);
    std::string str = ss.str();
    char* result = new char[str.length() + 1];
    strcpy(result, str.c_str());
    return result;
}

YAML_API char* tree_to_string(YAMLTreeHandle tree) {
    return node_to_string(tree, get_root(tree));
}

YAML_API bool write_file(YAMLTreeHandle tree, const char* filename) {
    try {
        std::ofstream fout(filename);
        if (!fout) return false;
        fout << GET_TREE(tree);
        return true;
    } catch (...) {
        return false;
    }
}

YAML_API void yaml_free_string(char* str) { delete[] str; }

}  // extern "C"