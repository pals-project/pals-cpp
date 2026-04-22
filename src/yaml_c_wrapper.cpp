#include "yaml_c_wrapper.h"
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <fstream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <map>
#include <vector>
#include <iostream>

// ============================================================
// INTERNALS
// ============================================================

struct ParsedData {
    ryml::Tree tree;
    std::string buffer;
};

#define GET_TREE(handle) (static_cast<ParsedData*>(handle)->tree)

// Shared helper: append or insert a blank child depending on index.
// Grows the node pool if needed since ryml does not auto-resize.
// index=END means append.
static size_t add_child_at(ryml::Tree& t, size_t parent, size_t index) {
    if (t.size() + 1 >= t.capacity())
        t.reserve(t.capacity() + 64);

    if (index == END) {
        return t.append_child(parent);
    }
    size_t num = t.num_children(parent);
    if (index > num) index = num;
    size_t after_id = (index > 0) ? t.child(parent, index - 1) : ryml::NONE;
    return t.insert_child(parent, after_id);
}

// Shared helper: optionally stamp a key onto a node.
static void maybe_set_key(ryml::Tree& t, size_t node, const char* key) {
    if (key != nullptr) {
        t.ref(node) |= ryml::KEY;
        t.set_key(node, t.to_arena(ryml::to_csubstr(key)));
    }
}

// Recursive deep copy used by deep_copy_node and internal helpers.
static void deep_copy_recursive(ryml::Tree& dst_t, size_t dst_node,
                                 const ryml::Tree& src_t, size_t src_node) {
    ryml::NodeType safe_flags = ryml::MAP | ryml::SEQ | ryml::VAL | ryml::KEY;
    dst_t.ref(dst_node) |= (src_t.type(src_node) & safe_flags);

    // Capture key/val as std::string BEFORE any append_child calls because
    // append_child can reallocate the arena, dangling any csubstr views.
    std::string key_str, val_str;
    bool has_key = src_t.has_key(src_node);
    bool has_val = src_t.has_val(src_node);
    if (has_key) key_str = std::string(src_t.key(src_node).str, src_t.key(src_node).len);
    if (has_val) val_str = std::string(src_t.val(src_node).str, src_t.val(src_node).len);

    // Collect child IDs before mutating the tree.
    std::vector<size_t> src_children;
    for (size_t c = src_t.first_child(src_node); c != ryml::NONE; c = src_t.next_sibling(c))
        src_children.push_back(c);

    // Append children first (may reallocate arena).
    std::vector<size_t> dst_children;
    for (size_t i = 0; i < src_children.size(); i++)
        dst_children.push_back(dst_t.append_child(dst_node));

    // Now safe to set key/val — arena is stable.
    if (has_key) dst_t.set_key(dst_node, dst_t.to_arena(ryml::to_csubstr(key_str)));
    if (has_val) dst_t.set_val(dst_node, dst_t.to_arena(ryml::to_csubstr(val_str)));

    for (size_t i = 0; i < src_children.size(); i++)
        deep_copy_recursive(dst_t, dst_children[i], src_t, src_children[i]);
}

// ============================================================
// INTERNAL LATTICE LOGIC
// ============================================================

static void make_ele_map(std::map<std::string, YAMLNodeId>& map,
                          YAMLTreeHandle tree, YAMLNodeId node) {
    if (node == YAML_NULL_ID) return;
    if (is_scalar(tree, node)) return;

    if (is_map(tree, node)) {
        YAMLNodeId params = get_child_by_index(tree, node, 0);
        YAMLNodeId kind   = get_child_by_key(tree, params, "kind");
        char* key         = get_node_key(tree, params);
        if (kind != YAML_NULL_ID && key != nullptr)
            map.emplace(std::string(key), params);
        yaml_free_string(key);
    }

    for (size_t i = 0; i < get_size(tree, node); i++)
        make_ele_map(map, tree, get_child_by_index(tree, node, i));
}

static void expand(YAMLTreeHandle tree, YAMLNodeId node,
                   std::map<std::string, YAMLNodeId>& map) {
    if (node == YAML_NULL_ID) return;

    if (is_scalar(tree, node)) {
        char* name = as_string(tree, node);
        if (name && map.count(name)) {
            YAMLNodeId def = map.at(std::string(name));
            GET_TREE(tree).ref(node).clear_val(); 
            // Promote to map and deep-copy the definition under a keyed child.
            GET_TREE(tree).ref(node) |= ryml::MAP;
            size_t kv_child = GET_TREE(tree).append_child(node);
            deep_copy_recursive(GET_TREE(tree), kv_child, GET_TREE(tree), def);

            // Stamp the key after the copy (arena may have moved).
            GET_TREE(tree).ref(kv_child) |= ryml::KEY;
            GET_TREE(tree).set_key(kv_child,
                GET_TREE(tree).to_arena(ryml::to_csubstr(name)));

            expand(tree, node, map);
        }
        yaml_free_string(name);
    } else if (is_map(tree, node)) {
        YAMLNodeId to_inherit = get_child_by_key(tree, node, "inherit");
        if (to_inherit != YAML_NULL_ID) {
            YAMLNodeId ancestor = map.at(as_string(tree, to_inherit));
            deep_copy_children(tree, node, tree, ancestor, END);
            set_node_key(tree, to_inherit, "inherited");
            expand(tree, node, map);
        }

    } else if (is_sequence(tree, node)) {
        for (size_t i = 0; i < get_size(tree, node); i++) {
            YAMLNodeId child = get_child_by_index(tree, node, i);
            if (is_map(tree, child)) {
                for (size_t j = 0; j < get_size(tree, child); j++) {
                    YAMLNodeId ele = get_child_by_index(tree, child, j);
                    if (ele != YAML_NULL_ID) {
                        YAMLNodeId repeat = get_child_by_key(tree, ele, "repeat");
                        if (repeat != YAML_NULL_ID) {
                            char* ntimes_str = as_string(tree, repeat);
                            int ntimes = std::stoi(ntimes_str);
                            yaml_free_string(ntimes_str); // Free the string allocation
                            char* key_str = get_node_key(tree, ele); 
                            remove_node(tree, node, child);
                            if (key_str) {
                                YAMLNodeId repeated_subline = map.at(key_str);
                                YAMLNodeId line = get_child_by_key(tree, repeated_subline, "line");
                                for (int n = 0; n < ntimes; n++) {
                                    deep_copy_children(tree, node, tree, line, i);
                                }
                                std::cout << yaml_to_string(tree, line);
                            }
                            yaml_free_string(key_str); // Free the key allocation
                        }
                    }
                }
            }
            expand(tree, child, map);
        }
    }

    for (size_t i = 0; i < get_size(tree, node); i++)
        expand(tree, get_child_by_index(tree, node, i), map);
}

// static YAMLNodeId find_lattice_to_expand(YAMLTreeHandle tree, char* lat_name = "", 
//     std::map<std::string, YAMLNodeId>& map) {
//         if (lat_name != "") {
//             return map.at(lat_name);
//         } else {

//         }
// }
static YAMLTreeHandle make_expanded(const char* filename) {
    YAMLTreeHandle expanded = parse_file(filename);
    if (!expanded) return nullptr;

    std::map<std::string, YAMLNodeId> map;
    make_ele_map(map, expanded, get_root(expanded));
    ryml::Tree& t = GET_TREE(expanded);
    t.reserve_arena(t.arena_capacity() + 100000);
    t.reserve(t.capacity() + 10000);

    expand(expanded, get_root(expanded), map);
    return expanded;
}

static void add_to_master_tree(YAMLTreeHandle master, YAMLTreeHandle tree,
                                YAMLNodeId tree_node) {
    if (tree_node == YAML_NULL_ID) return;
    YAMLNodeId master_root = get_root(master);
    if (is_scalar(tree, tree_node)) return;

    for (size_t i = 0; i < get_size(tree, tree_node); i++) {
        YAMLNodeId ele = get_child_by_index(tree, tree_node, i);
        add_to_master_tree(master, tree, ele);

        char* key = get_node_key(tree, ele);
        if (key && strcmp(key, "include") == 0) {
            char* child_filename = as_string(tree, ele);
            if (child_filename) {
                YAMLTreeHandle child_file = parse_file(child_filename);
                if (child_file) {
                    YAMLNodeId child_root = get_root(child_file);
                    size_t dest = GET_TREE(master).append_child(master_root);
                    deep_copy_recursive(GET_TREE(master), dest,
                                        GET_TREE(child_file), child_root);
                    set_node_key(master, dest, child_filename);
                    add_to_master_tree(master, child_file, child_root);
                    delete_tree(child_file);
                }
                yaml_free_string(child_filename);
            }
        }
        yaml_free_string(key);
    }
}

static YAMLTreeHandle make_original(const char* filename) {
    YAMLTreeHandle master = create_empty_tree();
    YAMLTreeHandle tree   = parse_file(filename);

    if (tree) {
        size_t dest = GET_TREE(master).append_child(get_root(master));
        deep_copy_recursive(GET_TREE(master), dest,
                            GET_TREE(tree), get_root(tree));
        set_node_key(master, dest, filename);
        add_to_master_tree(master, tree, get_root(tree));
    }
    return master;
}

// ============================================================
// PUBLIC API
// ============================================================

extern "C" {

YAML_API struct lattices get_lattices(const char* filename, const char* lattice_name) {
    struct lattices lat = {};
    lat.original = make_original(filename);
    lat.expanded = make_expanded(filename);
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

YAML_API void remove_node(YAMLTreeHandle tree, YAMLNodeId parent, YAMLNodeId child) {
    if (parent == YAML_NULL_ID || child == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    size_t index = t.child_pos(parent, child);
    if (index != ryml::NONE)
        t.ref(parent).remove_child(index);
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

YAML_API YAMLNodeId get_child_by_key(YAMLTreeHandle tree, YAMLNodeId parent, const char* key) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    if (!t.is_map(parent)) return YAML_NULL_ID;
    YAMLNodeId child = t.find_child(parent, ryml::to_csubstr(key));
    return (child == ryml::NONE) ? YAML_NULL_ID : child;
}

YAML_API YAMLNodeId get_child_by_index(YAMLTreeHandle tree, YAMLNodeId parent, size_t index) {
    ryml::Tree& t = GET_TREE(tree);
    if (!(t.is_seq(parent) || t.is_map(parent)) || index >= t.num_children(parent))
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
                                const char* key, const char* value, size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t new_id = add_child_at(t, parent, index);
    if (t.is_map(parent)) maybe_set_key(t, new_id, key);
    t.ref(new_id) |= ryml::VAL;
    t.set_val(new_id, t.to_arena(ryml::to_csubstr(value)));
    return new_id;
}

YAML_API YAMLNodeId add_map(YAMLTreeHandle tree, YAMLNodeId parent,
                             const char* key, size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t new_id = add_child_at(t, parent, index);
    if (t.is_map(parent)) maybe_set_key(t, new_id, key);
    t.ref(new_id) |= ryml::MAP;
    return new_id;
}

YAML_API YAMLNodeId add_sequence(YAMLTreeHandle tree, YAMLNodeId parent,
                                  const char* key, size_t index) {
    if (parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    size_t new_id = add_child_at(t, parent, index);
    if (t.is_map(parent)) maybe_set_key(t, new_id, key);
    t.ref(new_id) |= ryml::SEQ;
    return new_id;
}

YAML_API void set_scalar(YAMLTreeHandle tree, YAMLNodeId node, const char* value) {
    if (node == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    t.ref(node) |= ryml::VAL;
    t.set_val(node, t.to_arena(ryml::to_csubstr(value)));
}

YAML_API void set_node_key(YAMLTreeHandle tree, YAMLNodeId node, const char* key) {
    if (node == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    t.ref(node) |= ryml::KEY;
    t.set_key(node, t.to_arena(ryml::to_csubstr(key)));
}

YAML_API void deep_copy_node(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                              YAMLTreeHandle src_tree, YAMLNodeId src_node) {
    if (!dst_tree || !src_tree) return;
    if (dst_node == YAML_NULL_ID || src_node == YAML_NULL_ID) return;
    deep_copy_recursive(GET_TREE(dst_tree), dst_node, GET_TREE(src_tree), src_node);
}

YAML_API void deep_copy_children(YAMLTreeHandle dst_tree, YAMLNodeId dst_node,
                                  YAMLTreeHandle src_tree, YAMLNodeId src_node, size_t index) {
    if (!dst_tree || !src_tree) return;
    if (dst_node == YAML_NULL_ID || src_node == YAML_NULL_ID) return;
    ryml::Tree& dt = GET_TREE(dst_tree);
    ryml::Tree& st = GET_TREE(src_tree);
    size_t insert_pos = index;
    for (size_t c = st.first_child(src_node); c != ryml::NONE; c = st.next_sibling(c)) {
        if (dt.size() + 1 >= dt.capacity())
            dt.reserve(dt.capacity() + 64);
        size_t new_child = add_child_at(dt, dst_node, insert_pos);
        deep_copy_recursive(dt, new_child, st, c);
        if (insert_pos != END) insert_pos++;
    }
}

// --- EMITTING & UTILS ---

YAML_API char* yaml_to_string(YAMLTreeHandle tree, YAMLNodeId node) {
    ryml::Tree& t = GET_TREE(tree);
    if (node == ryml::NONE || node >= t.capacity()) return nullptr;
    std::stringstream ss;
    ss << t.ref(node);
    std::string str = ss.str();
    char* result = new char[str.length() + 1];
    strcpy(result, str.c_str());
    return result;
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

YAML_API void yaml_free_string(char* str) {
    delete[] str;
}

} // extern "C"