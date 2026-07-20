// Generic YAML tree wrapper: a thin C API over rapidyaml (parse, traverse,
// query, modify, and emit YAML trees). It knows nothing about PALS; the PALS
// lattice logic that builds on it lives in pals_lattice.cpp. Declarations
// shared between the two are in yaml_tree.h.

#include "yaml_c_wrapper.h"
#include "yaml_tree.h"

#include <cstring>
#include <fstream>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <sstream>
#include <string>
#include <vector>

// Grow node pool if nearly full. ryml does not auto-resize.
void ensure_capacity(ryml::Tree& t, size_t needed) {
    if (t.size() + needed >= t.capacity()) t.reserve(t.capacity() + 64);
}

// Append or insert a blank child. index=YAML_END means append.
static size_t add_child_at(ryml::Tree& t, size_t parent, size_t index) {
    ensure_capacity(t);
    if (index == YAML_END) return t.append_child(parent);
    size_t num = t.num_children(parent);
    if (index > num) index = num;
    size_t after = (index > 0) ? t.child(parent, index - 1) : ryml::NONE;
    return t.insert_child(parent, after);
}

// Cross-tree deep copy: copies type, key, val, and all descendants of src_node
// into the existing dst_node. Strings are copied into dst_t's arena. ryml only
// natively supports copying nodes within the same tree. Declared in
// yaml_tree.h; also used by the PALS lattice logic.
void deep_copy_recursive(ryml::Tree& dst_t, size_t dst_node,
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

extern "C" {

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
    if (!tree || child == YAML_NULL_ID) return;
    GET_TREE(tree).remove(child);
}

// --- TRAVERSAL ---

YAML_API YAMLNodeId get_root(YAMLTreeHandle tree) {
    if (!tree) return YAML_NULL_ID;
    return GET_TREE(tree).root_id();
}

YAML_API YAMLNodeId get_parent(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    if (node == ryml::NONE || !t.has_parent(node)) return YAML_NULL_ID;
    return t.parent(node);
}

YAML_API YAMLNodeId get_child_by_key(YAMLTreeHandle tree, YAMLNodeId parent,
                                     const char* key) {
    if (!tree || !key || parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    if (!t.is_map(parent)) return YAML_NULL_ID;
    size_t child = t.find_child(parent, ryml::to_csubstr(key));
    return (child == ryml::NONE) ? YAML_NULL_ID : child;
}

YAML_API YAMLNodeId get_child_by_index(YAMLTreeHandle tree, YAMLNodeId parent,
                                       size_t index) {
    if (!tree || parent == YAML_NULL_ID) return YAML_NULL_ID;
    ryml::Tree& t = GET_TREE(tree);
    if (!(t.is_seq(parent) || t.is_map(parent)) ||
        index >= t.num_children(parent))
        return YAML_NULL_ID;
    return t.child(parent, index);
}

YAML_API size_t get_size(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree || node == YAML_NULL_ID) return 0;
    return GET_TREE(tree).num_children(node);
}

YAML_API char* get_node_key(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree || node == YAML_NULL_ID) return nullptr;
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
    if (!tree || node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_map(node);
}

YAML_API bool is_sequence(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree || node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_seq(node);
}

YAML_API bool is_scalar(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree || node == YAML_NULL_ID) return false;
    return GET_TREE(tree).is_val(node);
}

// --- READING VALUES ---

YAML_API char* as_string(YAMLTreeHandle tree, YAMLNodeId node) {
    if (!tree || node == YAML_NULL_ID) return nullptr;
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
    if (!tree || !value || parent == YAML_NULL_ID) return YAML_NULL_ID;
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
    if (!tree || parent == YAML_NULL_ID) return YAML_NULL_ID;
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
    if (!tree || parent == YAML_NULL_ID) return YAML_NULL_ID;
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
    if (!tree || !value || node == YAML_NULL_ID) return;
    ryml::Tree& t = GET_TREE(tree);
    t.ref(node) |= ryml::VAL;
    t.set_val(node, t.to_arena(ryml::to_csubstr(value)));
}

YAML_API void set_node_key(YAMLTreeHandle tree, YAMLNodeId node,
                           const char* key) {
    if (!tree || !key || node == YAML_NULL_ID) return;
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
    if (index == YAML_END)
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
    if (!tree) return nullptr;
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
    if (!tree || !filename) return false;
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
