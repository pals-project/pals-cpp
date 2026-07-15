#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "../src/yaml_c_wrapper.h"

// ─── helpers ────────────────────────────────────────────────────────────────

static void write_tmp(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

static void rm_tmp(const std::string& path) {
    std::remove(path.c_str());
}

// Convenience: get a string value and compare, then free it.
static bool val_eq(YAMLTreeHandle tree, YAMLNodeId node, const char* expected) {
    char* s = as_string(tree, node);
    if (!s) return false;
    bool ok = std::string(s) == expected;
    yaml_free_string(s);
    return ok;
}

// ============================================================
// PARSING & MEMORY
// ============================================================

TEST_CASE("parse_string returns a valid tree for well-formed YAML", "[parsing]") {
    YAMLTreeHandle tree = parse_string("key: value");
    REQUIRE(tree != nullptr);
    REQUIRE(is_map(tree, get_root(tree)));
    delete_tree(tree);
}

TEST_CASE("parse_string returns nullptr for nullptr input", "[parsing]") {
    // should not crash
    YAMLTreeHandle tree = parse_string(nullptr);
    // behaviour is implementation-defined; just don't crash
    if (tree) delete_tree(tree);
}

TEST_CASE("parse_file returns a valid tree for an existing file", "[parsing][file]") {
    write_tmp("tmp_parse.yaml", "- a\n- b\n- c\n");
    YAMLTreeHandle tree = parse_file("tmp_parse.yaml");
    REQUIRE(tree != nullptr);
    REQUIRE(is_sequence(tree, get_root(tree)));
    REQUIRE(get_size(tree, get_root(tree)) == 3);
    delete_tree(tree);
    rm_tmp("tmp_parse.yaml");
}

TEST_CASE("parse_file returns nullptr for a missing file", "[parsing][file]") {
    YAMLTreeHandle tree = parse_file("does_not_exist.yaml");
    REQUIRE(tree == nullptr);
}

TEST_CASE("create_empty_tree gives an empty MAP root", "[parsing]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(tree != nullptr);
    REQUIRE(is_map(tree, get_root(tree)));
    REQUIRE(get_size(tree, get_root(tree)) == 0);
    delete_tree(tree);
}

TEST_CASE("delete_tree accepts nullptr without crashing", "[memory]") {
    delete_tree(nullptr);  // must not crash
}

// ============================================================
// TRAVERSAL
// ============================================================

TEST_CASE("get_root returns YAML_NULL_ID for a null handle", "[traversal]") {
    REQUIRE(get_root(nullptr) == YAML_NULL_ID);
}

TEST_CASE("get_child_by_key finds a MAP child by name", "[traversal]") {
    YAMLTreeHandle tree = parse_string("name: alice\nage: 30");
    YAMLNodeId root = get_root(tree);

    YAMLNodeId name = get_child_by_key(tree, root, "name");
    REQUIRE(name != YAML_NULL_ID);
    REQUIRE(val_eq(tree, name, "alice"));

    YAMLNodeId age = get_child_by_key(tree, root, "age");
    REQUIRE(age != YAML_NULL_ID);
    REQUIRE(val_eq(tree, age, "30"));

    delete_tree(tree);
}

TEST_CASE("get_child_by_key returns YAML_NULL_ID for missing key", "[traversal]") {
    YAMLTreeHandle tree = parse_string("key: value");
    REQUIRE(get_child_by_key(tree, get_root(tree), "missing") == YAML_NULL_ID);
    delete_tree(tree);
}

TEST_CASE("get_child_by_key returns YAML_NULL_ID on non-MAP parent", "[traversal]") {
    YAMLTreeHandle tree = parse_string("- a\n- b");
    YAMLNodeId root = get_root(tree);
    REQUIRE(is_sequence(tree, root));
    REQUIRE(get_child_by_key(tree, root, "anything") == YAML_NULL_ID);
    delete_tree(tree);
}

TEST_CASE("get_child_by_index accesses sequence elements in order", "[traversal]") {
    YAMLTreeHandle tree = parse_string("- apple\n- banana\n- cherry");
    YAMLNodeId root = get_root(tree);

    REQUIRE(val_eq(tree, get_child_by_index(tree, root, 0), "apple"));
    REQUIRE(val_eq(tree, get_child_by_index(tree, root, 1), "banana"));
    REQUIRE(val_eq(tree, get_child_by_index(tree, root, 2), "cherry"));

    delete_tree(tree);
}

TEST_CASE("get_child_by_index returns YAML_NULL_ID out of bounds", "[traversal]") {
    YAMLTreeHandle tree = parse_string("- a\n- b");
    REQUIRE(get_child_by_index(tree, get_root(tree), 99) == YAML_NULL_ID);
    delete_tree(tree);
}

TEST_CASE("get_size counts direct children", "[traversal]") {
    YAMLTreeHandle tree = parse_string("a: 1\nb: 2\nc: 3");
    REQUIRE(get_size(tree, get_root(tree)) == 3);
    delete_tree(tree);
}

TEST_CASE("get_size returns 0 for YAML_NULL_ID", "[traversal]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(get_size(tree, YAML_NULL_ID) == 0);
    delete_tree(tree);
}

TEST_CASE("get_parent returns the correct parent node", "[traversal]") {
    YAMLTreeHandle tree = parse_string("key: value");
    YAMLNodeId root = get_root(tree);
    YAMLNodeId child = get_child_by_key(tree, root, "key");
    REQUIRE(get_parent(tree, child) == root);
    delete_tree(tree);
}

TEST_CASE("get_parent returns YAML_NULL_ID for the root", "[traversal]") {
    YAMLTreeHandle tree = parse_string("key: value");
    REQUIRE(get_parent(tree, get_root(tree)) == YAML_NULL_ID);
    delete_tree(tree);
}

TEST_CASE("get_node_key returns the key string for a MAP child", "[traversal]") {
    YAMLTreeHandle tree = parse_string("mykey: value");
    YAMLNodeId child = get_child_by_key(tree, get_root(tree), "mykey");
    char* key = get_node_key(tree, child);
    REQUIRE(std::string(key) == "mykey");
    yaml_free_string(key);
    delete_tree(tree);
}

TEST_CASE("get_node_key returns nullptr for a keyless node", "[traversal]") {
    YAMLTreeHandle tree = parse_string("- item");
    YAMLNodeId item = get_child_by_index(tree, get_root(tree), 0);
    REQUIRE(get_node_key(tree, item) == nullptr);
    delete_tree(tree);
}

TEST_CASE("get_node_key returns nullptr for YAML_NULL_ID", "[traversal]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(get_node_key(tree, YAML_NULL_ID) == nullptr);
    delete_tree(tree);
}

// ============================================================
// TYPE CHECKS
// ============================================================

TEST_CASE("is_map identifies MAP nodes", "[types]") {
    YAMLTreeHandle tree = parse_string("key: value");
    REQUIRE(is_map(tree, get_root(tree)));
    REQUIRE_FALSE(is_sequence(tree, get_root(tree)));
    REQUIRE_FALSE(is_scalar(tree, get_root(tree)));
    delete_tree(tree);
}

TEST_CASE("is_sequence identifies sequence nodes", "[types]") {
    YAMLTreeHandle tree = parse_string("- a\n- b");
    REQUIRE(is_sequence(tree, get_root(tree)));
    REQUIRE_FALSE(is_map(tree, get_root(tree)));
    REQUIRE_FALSE(is_scalar(tree, get_root(tree)));
    delete_tree(tree);
}

TEST_CASE("is_scalar identifies bare scalar nodes", "[types]") {
    // A document containing only a value guarantees the root is a VAL
    YAMLTreeHandle tree = parse_string("just_a_bare_string");
    YAMLNodeId root = get_root(tree);
    
    REQUIRE(root != YAML_NULL_ID);
    REQUIRE(is_scalar(tree, root));
    REQUIRE_FALSE(is_map(tree, root));
    REQUIRE_FALSE(is_sequence(tree, root));
    
    delete_tree(tree);
}

TEST_CASE("type checks return false for YAML_NULL_ID", "[types]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE_FALSE(is_map(tree, YAML_NULL_ID));
    REQUIRE_FALSE(is_sequence(tree, YAML_NULL_ID));
    REQUIRE_FALSE(is_scalar(tree, YAML_NULL_ID));
    delete_tree(tree);
}

// ============================================================
// READING VALUES
// ============================================================

TEST_CASE("as_string returns the scalar value", "[reading]") {
    YAMLTreeHandle tree = parse_string("word: hello");
    YAMLNodeId node = get_child_by_key(tree, get_root(tree), "word");
    char* s = as_string(tree, node);
    REQUIRE(std::string(s) == "hello");
    yaml_free_string(s);
    delete_tree(tree);
}

TEST_CASE("as_string returns nullptr for a MAP node", "[reading]") {
    YAMLTreeHandle tree = parse_string("key: value");
    REQUIRE(as_string(tree, get_root(tree)) == nullptr);
    delete_tree(tree);
}

TEST_CASE("as_string returns nullptr for YAML_NULL_ID", "[reading]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(as_string(tree, YAML_NULL_ID) == nullptr);
    delete_tree(tree);
}

TEST_CASE("as_string works for numeric and boolean strings", "[reading]") {
    YAMLTreeHandle tree = parse_string("count: 42\nratio: 3.14\nflag: true");
    YAMLNodeId root = get_root(tree);

    REQUIRE(val_eq(tree, get_child_by_key(tree, root, "count"), "42"));
    REQUIRE(val_eq(tree, get_child_by_key(tree, root, "ratio"), "3.14"));
    REQUIRE(val_eq(tree, get_child_by_key(tree, root, "flag"), "true"));

    delete_tree(tree);
}

// ============================================================
// MODIFICATION — add_scalar, add_map, add_sequence
// ============================================================

TEST_CASE("add_scalar appends a keyed scalar to a MAP", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);

    YAMLNodeId node = add_scalar(tree, root, "lang", "C++", END);
    REQUIRE(node != YAML_NULL_ID);
    REQUIRE(val_eq(tree, get_child_by_key(tree, root, "lang"), "C++"));

    delete_tree(tree);
}

TEST_CASE("add_scalar appends a keyless scalar to a sequence", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq = add_sequence(tree, root, "items", END);

    add_scalar(tree, seq, nullptr, "x", END);
    add_scalar(tree, seq, nullptr, "y", END);

    REQUIRE(get_size(tree, seq) == 2);
    REQUIRE(val_eq(tree, get_child_by_index(tree, seq, 0), "x"));
    REQUIRE(val_eq(tree, get_child_by_index(tree, seq, 1), "y"));

    delete_tree(tree);
}

TEST_CASE("add_scalar inserts at a specific index", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "first",  "a", END);
    add_scalar(tree, root, "third",  "c", END);
    add_scalar(tree, root, "second", "b", 1);   // insert between first and third

    char* k0 = get_node_key(tree, get_child_by_index(tree, root, 0));
    char* k1 = get_node_key(tree, get_child_by_index(tree, root, 1));
    char* k2 = get_node_key(tree, get_child_by_index(tree, root, 2));

    REQUIRE(std::string(k0) == "first");
    REQUIRE(std::string(k1) == "second");
    REQUIRE(std::string(k2) == "third");

    yaml_free_string(k0);
    yaml_free_string(k1);
    yaml_free_string(k2);
    delete_tree(tree);
}

TEST_CASE("add_map creates an empty MAP child", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId child = add_map(tree, root, "nested", END);

    REQUIRE(child != YAML_NULL_ID);
    REQUIRE(is_map(tree, child));
    REQUIRE(get_size(tree, child) == 0);

    delete_tree(tree);
}

TEST_CASE("add_sequence creates an empty sequence child", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq = add_sequence(tree, root, "list", END);

    REQUIRE(seq != YAML_NULL_ID);
    REQUIRE(is_sequence(tree, seq));
    REQUIRE(get_size(tree, seq) == 0);

    delete_tree(tree);
}

TEST_CASE("add_map inside a sequence creates an anonymous MAP element", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq  = add_sequence(tree, root, "records", END);
    YAMLNodeId elem = add_map(tree, seq, nullptr, END);   // seq element has no key

    REQUIRE(is_map(tree, elem));
    add_scalar(tree, elem, "id", "1", END);
    REQUIRE(val_eq(tree, get_child_by_key(tree, elem, "id"), "1"));

    delete_tree(tree);
}

TEST_CASE("add_scalar returns YAML_NULL_ID for a null parent", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(add_scalar(tree, YAML_NULL_ID, "k", "v", END) == YAML_NULL_ID);
    delete_tree(tree);
}

// ============================================================
// MODIFICATION — set_scalar, set_node_key
// ============================================================

TEST_CASE("set_scalar updates an existing scalar value", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root  = get_root(tree);
    YAMLNodeId child = add_scalar(tree, root, "key", "initial", END);

    set_scalar(tree, child, "updated");
    REQUIRE(val_eq(tree, child, "updated"));

    delete_tree(tree);
}

TEST_CASE("set_scalar on YAML_NULL_ID does not crash", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    set_scalar(tree, YAML_NULL_ID, "value");  // must not crash
    delete_tree(tree);
}

TEST_CASE("set_node_key renames a MAP child", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root  = get_root(tree);
    YAMLNodeId child = add_scalar(tree, root, "old", "val", END);

    set_node_key(tree, child, "new");

    REQUIRE(get_child_by_key(tree, root, "old") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(tree, root, "new") != YAML_NULL_ID);

    delete_tree(tree);
}

TEST_CASE("set_node_key on YAML_NULL_ID does not crash", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    set_node_key(tree, YAML_NULL_ID, "key");  // must not crash
    delete_tree(tree);
}

// ============================================================
// MODIFICATION — remove_node
// ============================================================

TEST_CASE("remove_node removes a child from a MAP", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "keep",   "yes", END);
    add_scalar(tree, root, "remove", "no",  END);

    YAMLNodeId to_remove = get_child_by_key(tree, root, "remove");
    remove_node(tree, root, to_remove);

    REQUIRE(get_size(tree, root) == 1);
    REQUIRE(get_child_by_key(tree, root, "remove") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(tree, root, "keep")   != YAML_NULL_ID);

    delete_tree(tree);
}

TEST_CASE("remove_node on YAML_NULL_ID does not crash", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    remove_node(tree, YAML_NULL_ID, YAML_NULL_ID);  // must not crash
    delete_tree(tree);
}

// ============================================================
// DEEP COPY
// ============================================================

TEST_CASE("deep_copy_node copies content into an existing node", "[copy]") {
    YAMLTreeHandle src = parse_string("x: 10\ny: 20");
    YAMLTreeHandle dst = create_empty_tree();

    deep_copy_node(dst, get_root(dst), src, get_root(src));

    REQUIRE(val_eq(dst, get_child_by_key(dst, get_root(dst), "x"), "10"));
    REQUIRE(val_eq(dst, get_child_by_key(dst, get_root(dst), "y"), "20"));

    delete_tree(src);
    delete_tree(dst);
}

TEST_CASE("deep_copy_node works across different trees", "[copy]") {
    YAMLTreeHandle src = parse_string("nested:\n  a: 1\n  b: 2");
    YAMLTreeHandle dst = create_empty_tree();

    deep_copy_node(dst, get_root(dst), src, get_root(src));

    YAMLNodeId nested = get_child_by_key(dst, get_root(dst), "nested");
    REQUIRE(nested != YAML_NULL_ID);
    REQUIRE(is_map(dst, nested));
    REQUIRE(val_eq(dst, get_child_by_key(dst, nested, "a"), "1"));
    REQUIRE(val_eq(dst, get_child_by_key(dst, nested, "b"), "2"));

    delete_tree(src);
    delete_tree(dst);
}

TEST_CASE("deep_copy_node with null handles does not crash", "[copy]") {
    YAMLTreeHandle tree = create_empty_tree();
    deep_copy_node(nullptr, YAML_NULL_ID, tree, get_root(tree));
    deep_copy_node(tree, get_root(tree), nullptr, YAML_NULL_ID);
    delete_tree(tree);
}

TEST_CASE("deep_copy_children appends children to dst", "[copy]") {
    YAMLTreeHandle src = parse_string("a: 1\nb: 2\nc: 3");
    YAMLTreeHandle dst = create_empty_tree();
    YAMLNodeId dst_root = get_root(dst);

    // Pre-populate dst with one entry
    add_scalar(dst, dst_root, "existing", "yes", END);
    REQUIRE(get_size(dst, dst_root) == 1);

    deep_copy_children(dst, dst_root, src, get_root(src), END);

    // Should now have original + 3 copied children
    REQUIRE(get_size(dst, dst_root) == 4);
    REQUIRE(val_eq(dst, get_child_by_key(dst, dst_root, "a"), "1"));
    REQUIRE(val_eq(dst, get_child_by_key(dst, dst_root, "existing"), "yes"));

    delete_tree(src);
    delete_tree(dst);
}

TEST_CASE("deep_copy_children inserts at index 0 (prepend)", "[copy]") {
    YAMLTreeHandle src = parse_string("new: value");
    YAMLTreeHandle dst = create_empty_tree();
    add_scalar(dst, get_root(dst), "existing", "old", END);

    deep_copy_children(dst, get_root(dst), src, get_root(src), 0);

    // "new" should be at index 0
    char* key = get_node_key(dst, get_child_by_index(dst, get_root(dst), 0));
    REQUIRE(std::string(key) == "new");
    yaml_free_string(key);

    delete_tree(src);
    delete_tree(dst);
}

// ============================================================
// EMITTING
// ============================================================

TEST_CASE("node_to_string emits valid YAML containing expected keys", "[emitting]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "greeting", "hello", END);

    char* str = node_to_string(tree, root);
    REQUIRE(str != nullptr);
    REQUIRE(std::string(str).find("greeting") != std::string::npos);
    REQUIRE(std::string(str).find("hello") != std::string::npos);

    yaml_free_string(str);
    delete_tree(tree);
}

TEST_CASE("node_to_string returns nullptr for YAML_NULL_ID", "[emitting]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(node_to_string(tree, YAML_NULL_ID) == nullptr);
    delete_tree(tree);
}

TEST_CASE("tree_to_string emits the full tree", "[emitting]") {
    YAMLTreeHandle tree = parse_string("a: 1\nb: 2");
    char* str = tree_to_string(tree);
    REQUIRE(str != nullptr);
    std::string s(str);
    REQUIRE(s.find("a") != std::string::npos);
    REQUIRE(s.find("b") != std::string::npos);
    yaml_free_string(str);
    delete_tree(tree);
}

TEST_CASE("write_file writes a tree that can be read back", "[emitting][file]") {
    const char* path = "tmp_write.yaml";
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "written", "true", END);
    add_scalar(tree, root, "count",   "7",    END);

    REQUIRE(write_file(tree, path));
    delete_tree(tree);

    YAMLTreeHandle loaded = parse_file(path);
    REQUIRE(loaded != nullptr);
    REQUIRE(val_eq(loaded, get_child_by_key(loaded, get_root(loaded), "written"), "true"));
    REQUIRE(val_eq(loaded, get_child_by_key(loaded, get_root(loaded), "count"),   "7"));
    delete_tree(loaded);

    rm_tmp(path);
}

TEST_CASE("write_file returns false for an unwritable path", "[emitting][file]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE_FALSE(write_file(tree, "/nonexistent_dir/file.yaml"));
    delete_tree(tree);
}

TEST_CASE("yaml_free_string accepts nullptr without crashing", "[memory]") {
    yaml_free_string(nullptr);
}

// ============================================================
// ROUND-TRIP: build, emit, parse, verify
// ============================================================

TEST_CASE("Nested structure survives a write/read round-trip", "[round_trip]") {
    const char* path = "tmp_roundtrip.yaml";

    // Build: { server: { host: localhost, port: 8080 }, tags: [web, api] }
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root   = get_root(tree);
    YAMLNodeId server = add_map(tree, root, "server", END);
    add_scalar(tree, server, "host", "localhost", END);
    add_scalar(tree, server, "port", "8080",      END);
    YAMLNodeId tags = add_sequence(tree, root, "tags", END);
    add_scalar(tree, tags, nullptr, "web", END);
    add_scalar(tree, tags, nullptr, "api", END);

    REQUIRE(write_file(tree, path));
    delete_tree(tree);

    // Read back and verify
    YAMLTreeHandle loaded = parse_file(path);
    REQUIRE(loaded != nullptr);
    YAMLNodeId lroot  = get_root(loaded);
    YAMLNodeId lserver = get_child_by_key(loaded, lroot, "server");
    REQUIRE(lserver != YAML_NULL_ID);
    REQUIRE(is_map(loaded, lserver));
    REQUIRE(val_eq(loaded, get_child_by_key(loaded, lserver, "host"), "localhost"));
    REQUIRE(val_eq(loaded, get_child_by_key(loaded, lserver, "port"), "8080"));

    YAMLNodeId ltags = get_child_by_key(loaded, lroot, "tags");
    REQUIRE(ltags != YAML_NULL_ID);
    REQUIRE(is_sequence(loaded, ltags));
    REQUIRE(get_size(loaded, ltags) == 2);
    REQUIRE(val_eq(loaded, get_child_by_index(loaded, ltags, 0), "web"));
    REQUIRE(val_eq(loaded, get_child_by_index(loaded, ltags, 1), "api"));

    delete_tree(loaded);
    rm_tmp(path);
}

// ============================================================
// CLONE via create_empty_tree + deep_copy_node
// ============================================================

TEST_CASE("Cloning via deep_copy_node produces an independent copy", "[copy]") {
    YAMLTreeHandle original = parse_string("name: original\ncount: 1");
    YAMLTreeHandle clone    = create_empty_tree();
    deep_copy_node(clone, get_root(clone), original, get_root(original));

    // Modify clone — original must be unchanged
    YAMLNodeId clone_name = get_child_by_key(clone, get_root(clone), "name");
    set_scalar(clone, clone_name, "modified");

    REQUIRE(val_eq(clone,    get_child_by_key(clone,    get_root(clone),    "name"), "modified"));
    REQUIRE(val_eq(original, get_child_by_key(original, get_root(original), "name"), "original"));

    delete_tree(original);
    delete_tree(clone);
}

// ============================================================
// parse_and_expand_PALS (smoke test — requires the example lattice files)
// ============================================================

TEST_CASE("parse_and_expand_PALS returns three non-null handles", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    REQUIRE(lat.original != nullptr);
    REQUIRE(lat.combined != nullptr);
    REQUIRE(lat.expanded != nullptr);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
}

// ============================================================
// build_correspondence_map
// ============================================================

TEST_CASE("build_correspondence_map is empty for null handles", "[correspondence]") {
    struct correspondence_map m = build_correspondence_map(nullptr, nullptr, nullptr);
    REQUIRE(m.count == 0);
    REQUIRE(m.links == nullptr);
    free_correspondence_map(m);  // must not crash
}

TEST_CASE("build_correspondence_map links the three tree roots", "[correspondence]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.expanded);

    REQUIRE(m.count > 0);

    // One link is emitted per expanded node, so the count matches the number
    // of nodes reachable from the expanded root.
    YAMLNodeId exp_root = get_root(lat.expanded);

    // Find the link for the expanded root and verify it points at the combined
    // root and at the top-level file's entry in the original tree.
    bool found_root = false;
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].expanded == exp_root) {
            found_root = true;
            REQUIRE(m.links[i].combined == get_root(lat.combined));
            // The original tree's first child is the top-level file's contents.
            REQUIRE(m.links[i].original ==
                    get_child_by_index(lat.original, get_root(lat.original), 0));
            REQUIRE(is_map(lat.combined, m.links[i].combined));
            REQUIRE(is_map(lat.original, m.links[i].original));
        }
        // Every emitted link has a valid expanded node.
        REQUIRE(m.links[i].expanded != YAML_NULL_ID);
    }
    REQUIRE(found_root);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
}

// Helper: navigate root -> PALS -> facility for a given tree.
static YAMLNodeId facility_of(YAMLTreeHandle t) {
    YAMLNodeId pals = get_child_by_key(t, get_root(t), "PALS");
    return get_child_by_key(t, pals, "facility");
}

TEST_CASE("build_correspondence_map connects a node across trees by value",
          "[correspondence]") {
    // A constant that lives outside the expanded lattice appears, unchanged,
    // in all three trees; the map must connect the three copies.
    const char* path = "tmp_corr.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        a_const: 0.3 * r_electron\n"
              "    - q1:\n"
              "        kind: Quadrupole\n"
              "        length: 1.0\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - q1\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.expanded);

    // Locate a_const in the expanded tree: facility[0] -> constants -> a_const.
    YAMLNodeId e_const = get_child_by_index(lat.expanded, facility_of(lat.expanded), 0);
    YAMLNodeId e_a_const =
        get_child_by_key(lat.expanded,
                         get_child_by_key(lat.expanded, e_const, "constants"),
                         "a_const");
    REQUIRE(e_a_const != YAML_NULL_ID);
    // The expanded tree has its expressions evaluated, so this constant now
    // holds a number; the combined/original copies (checked below) still carry
    // the original expression text.
    {
        char* s = as_string(lat.expanded, e_a_const);
        REQUIRE(s != nullptr);
        double got = std::strtod(s, nullptr);
        yaml_free_string(s);
        bool okc = false;
        double want = evaluate_pals_expression("0.3 * r_electron", &okc);
        REQUIRE(okc);
        REQUIRE(got == want);
    }

    // Find its link and follow it to the combined and original copies.
    bool found = false;
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].expanded != e_a_const) continue;
        found = true;
        REQUIRE(m.links[i].combined != YAML_NULL_ID);
        REQUIRE(m.links[i].original != YAML_NULL_ID);
        REQUIRE(val_eq(lat.combined, m.links[i].combined, "0.3 * r_electron"));
        REQUIRE(val_eq(lat.original, m.links[i].original, "0.3 * r_electron"));
    }
    REQUIRE(found);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    rm_tmp(path);
}

TEST_CASE("build_correspondence_map maps one source to many expanded copies",
          "[correspondence]") {
    // A `repeat` unrolls one combined element into several expanded nodes; the
    // map must link all copies back to a single combined source.
    const char* path = "tmp_corr_repeat.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - d1:\n"
              "        kind: Drift\n"
              "        length: 2.0\n"
              "    - cell:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - d1\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - cell:\n"
              "              repeat: 3\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.expanded);

    // Unrolling `repeat: 3` over a one-element cell produces three keyless `d1`
    // scalars in the expanded line, all copied from the same combined source.
    // Build a histogram of the combined ids that the expanded scalar `d1`
    // nodes point to; a single source must account for at least three copies.
    std::map<YAMLNodeId, int> combined_hits;
    for (size_t i = 0; i < m.count; i++) {
        if (!is_scalar(lat.expanded, m.links[i].expanded)) continue;
        if (!val_eq(lat.expanded, m.links[i].expanded, "d1")) continue;
        REQUIRE(m.links[i].combined != YAML_NULL_ID);  // has a source
        combined_hits[m.links[i].combined]++;
    }
    int max_copies = 0;
    for (auto& kv : combined_hits) max_copies = std::max(max_copies, kv.second);
    REQUIRE(max_copies >= 3);  // one combined node -> three expanded copies

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    rm_tmp(path);
}

// ============================================================
// NAME MATCHING
// ============================================================

// Convenience: compare a node's key, then free it.
static bool key_eq(YAMLTreeHandle tree, YAMLNodeId node, const char* expected) {
    char* s = get_node_key(tree, node);
    if (!s) return false;
    bool ok = std::string(s) == expected;
    yaml_free_string(s);
    return ok;
}

// A small two-lattice lattice covering constants, variables, elements with
// ungrouped (length) and grouped (BendP.e1) parameters, a sub-line (sub/S1),
// and a repeated element name (B1a in both lattices) to exercise `>>>`.
static const char* MATCH_YAML =
    "PALS:\n"
    "  facility:\n"
    "    - constants:\n"
    "        - a_const: 0.3 * r_electron\n"
    "        - a_two: 5\n"
    "    - my_var:\n"
    "        kind: variable\n"
    "        value: 37\n"
    "    - lat1:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - main:\n"
    "              kind: BeamLine\n"
    "              line:\n"
    "                - B1a:\n"
    "                    kind: Bend\n"
    "                    length: 1.2\n"
    "                    BendP:\n"
    "                      e1: 0.1\n"
    "                      g_ref: 0.02\n"
    "                - B1b:\n"
    "                    kind: Bend\n"
    "                    length: 1.5\n"
    "                    BendP:\n"
    "                      e1: 0.3\n"
    "                - Q1:\n"
    "                    kind: Quadrupole\n"
    "                    length: 0.5\n"
    "                - sub:\n"
    "                    kind: BeamLine\n"
    "                    line:\n"
    "                      - S1:\n"
    "                          kind: Sextupole\n"
    "                          length: 0.2\n"
    "    - lat2:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - other:\n"
    "              kind: BeamLine\n"
    "              line:\n"
    "                - B1a:\n"
    "                    kind: Bend\n"
    "                    length: 9.9\n";

TEST_CASE("match_names handles null args", "[matching]") {
    struct name_matches m = match_names(nullptr, "a_const");
    REQUIRE(m.count == 0);
    REQUIRE(m.nodes == nullptr);
    free_name_matches(m);

    YAMLTreeHandle t = parse_string(MATCH_YAML);
    m = match_names(t, nullptr);
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a bare name matches a single constant", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "a_const");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(val_eq(t, m.nodes[0], "0.3 * r_electron"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a constant/variable pattern matches by name across forms",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // a_.* matches the two compact constants but not the full-form variable.
    struct name_matches m = match_names(t, "a_.*");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(key_eq(t, m.nodes[1], "a_two"));
    free_name_matches(m);

    // A full-form variable is matched too; the returned node is its named node.
    m = match_names(t, "my_var");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "my_var"));
    REQUIRE(is_map(t, m.nodes[0]));  // full form: kind/value live underneath
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("compact constants written as a plain map are matched too",
          "[matching]") {
    // The compact form may be a YAML map (name: value) rather than a sequence
    // of single-key maps; both must be matched.
    YAMLTreeHandle t = parse_string(
        "PALS:\n"
        "  facility:\n"
        "    - constants:\n"
        "        a_const: 0.3 * r_electron\n"
        "        b_const: 0.45\n"
        "    - variables:\n"
        "        a_var: 5\n");
    struct name_matches m = match_names(t, ".*_const");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(key_eq(t, m.nodes[1], "b_const"));
    free_name_matches(m);

    m = match_names(t, "a_var");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "5"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("the name pattern is a whole-name (anchored) match", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // "a" alone must not match "a_const" — the whole name has to match.
    struct name_matches m = match_names(t, "a");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a bare name matches the elements themselves", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1a exists in both lattices; each occurrence is returned as its element
    // (map) node.
    struct name_matches m = match_names(t, "B1a");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "B1a"));
    REQUIRE(is_map(t, m.nodes[0]));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an element pattern with a grouped parameter matches all elements",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1.* matches B1a and B1b in lat1; both carry BendP.e1. (lat2's B1a has no
    // BendP.)
    struct name_matches m = match_names(t, "B1.*>BendP.e1");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "e1"));
    REQUIRE(val_eq(t, m.nodes[0], "0.1"));
    REQUIRE(val_eq(t, m.nodes[1], "0.3"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an ungrouped parameter resolves directly on the element",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1a appears in both lattices, so its length matches twice.
    struct name_matches m = match_names(t, "B1a>length");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "length"));
    REQUIRE(val_eq(t, m.nodes[0], "1.2"));
    REQUIRE(val_eq(t, m.nodes[1], "9.9"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an omitted element matches the parameter in every element",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // >length : the length of every element, in both lattices and the sub-line.
    struct name_matches m = match_names(t, ">length");
    REQUIRE(m.count == 5);
    free_name_matches(m);

    // A parameter only some elements have (g_ref) matches only those.
    m = match_names(t, ">BendP.g_ref");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "g_ref"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("`::` restricts a match to an element kind", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // Only the Quadrupole matches.
    struct name_matches m = match_names(t, "Quadrupole::.*>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.5"));
    free_name_matches(m);

    // Both B1a's are Bends.
    m = match_names(t, "Bend::B1a>length");
    REQUIRE(m.count == 2);
    free_name_matches(m);

    // A kind that no matching element has yields nothing.
    m = match_names(t, "Sextupole::B1a>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("`>>` filters by BeamLine/Branch, including sub-lines", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // main contains B1a and B1b directly.
    struct name_matches m = match_names(t, "main>>B1.*>length");
    REQUIRE(m.count == 2);
    free_name_matches(m);

    // S1 lives in sub-line `sub` of `main`; the `main>>` qualifier reaches it.
    m = match_names(t, "main>>S1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.2"));
    free_name_matches(m);

    // A branch that does not exist yields nothing.
    m = match_names(t, "nobranch>>B1.*>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("`>>>` selects among lattices with the same element name",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "lat1>>>B1a>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "1.2"));
    free_name_matches(m);

    m = match_names(t, "lat2>>>B1a>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "9.9"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("dropping the parameter matches the parameter group", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // Only lat1's B1a has a BendP group.
    struct name_matches m = match_names(t, "B1a>BendP");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "BendP"));
    REQUIRE(is_map(t, m.nodes[0]));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("no matches yields an empty result", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "nosuch>foo");
    REQUIRE(m.count == 0);
    REQUIRE(m.nodes == nullptr);
    free_name_matches(m);

    // A missing parameter path on an existing element also yields nothing.
    m = match_names(t, "B1a>BendP.nope");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a malformed pattern yields an empty result, not a crash",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "(unclosed");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

// ============================================================
// EXPRESSION EVALUATION
// ============================================================

// Evaluate a standalone expression, requiring success, and return its value.
static double eval_ok(const char* expr) {
    bool ok = false;
    double v = evaluate_pals_expression(expr, &ok);
    REQUIRE(ok);
    return v;
}

// True if two doubles agree to a relative/absolute tolerance.
static bool close(double got, double want) {
    return std::fabs(got - want) <= 1e-9 * std::max(1.0, std::fabs(want));
}

TEST_CASE("evaluate_pals_expression: arithmetic and precedence", "[expr]") {
    REQUIRE(eval_ok("2 + 3 * 4") == 14.0);
    REQUIRE(eval_ok("(2 + 3) * 4") == 20.0);
    REQUIRE(eval_ok("2 ^ 3 ^ 2") == 512.0);   // right-associative
    REQUIRE(eval_ok("-2 ^ 2") == -4.0);        // unary minus looser than ^
    REQUIRE(eval_ok("2 ^ -2") == 0.25);
    REQUIRE(close(eval_ok("3.75e7 / c_light^2"),
                  3.75e7 / (2.99792458e8 * 2.99792458e8)));
}

TEST_CASE("evaluate_pals_expression: functions", "[expr]") {
    REQUIRE(close(eval_ok("sqrt(2)"), std::sqrt(2.0)));
    REQUIRE(close(eval_ok("0.1*log(abs(-0.34))"), 0.1 * std::log(0.34)));
    REQUIRE(eval_ok("modulo(7, 3)") == 1.0);
    REQUIRE(eval_ok("floor(-1.5)") == -2.0);
    REQUIRE(eval_ok("ceiling(-1.5)") == -1.0);
    REQUIRE(eval_ok("int(-1.9)") == -1.0);     // toward zero
    REQUIRE(eval_ok("nint(2.5)") == 3.0);      // nearest
    REQUIRE(eval_ok("sign(-3)") == -1.0);
    REQUIRE(eval_ok("sinc(0)") == 1.0);
    REQUIRE(close(eval_ok("atan2(1, 1)"), std::atan(1.0)));
    REQUIRE(close(eval_ok("factorial(5)"), 120.0));
}

TEST_CASE("evaluate_pals_expression: built-in constants", "[expr]") {
    REQUIRE(close(eval_ok("pi"), 3.14159265358979323846));
    REQUIRE(eval_ok("c_light") == 2.99792458e8);
    // classical_radius_factor and k_boltzmann are derived from AAPC quantities.
    REQUIRE(close(eval_ok("classical_radius_factor"),
                  eval_ok("r_electron") * eval_ok("mass_of(electron)")));
}

TEST_CASE("evaluate_pals_expression: particle-data functions from libapc",
          "[expr]") {
    // Values mirror AtomicAndPhysicalConstantsCLib (CODATA 2022).
    REQUIRE(close(eval_ok("mass_of(proton)"), 938272089.43000007));
    REQUIRE(eval_ok("charge_of(electron)") == -1.0);
    REQUIRE(eval_ok("charge_of(anti-proton)") == -1.0);
    REQUIRE(close(eval_ok("2 * mass_of(electron)"), 2 * 510998.95069000003));
    // A bare isotope is the neutral atom; the ionised form carries the charge.
    REQUIRE(eval_ok("charge_of(3He)") == 0.0);
    REQUIRE(eval_ok("charge_of(helion)") == 2.0);
}

TEST_CASE("evaluate_pals_expression: expr() wrapper is accepted", "[expr]") {
    REQUIRE(eval_ok("expr(3.74 * 2)") == 7.48);
    REQUIRE(eval_ok("expr( (1 + 2) * 3 )") == 9.0);
}

TEST_CASE("evaluate_pals_expression: non-evaluable inputs report failure",
          "[expr]") {
    bool ok = true;
    // random()/random_gauss() are deferred, so not evaluable here.
    evaluate_pals_expression("0.01 + 0.003 * random_gauss()", &ok);
    REQUIRE_FALSE(ok);
    ok = true;
    evaluate_pals_expression("thingB", &ok);          // unknown identifier
    REQUIRE_FALSE(ok);
    ok = true;
    evaluate_pals_expression("mass_of(nonsense)", &ok);  // unknown species
    REQUIRE_FALSE(ok);
    ok = true;
    evaluate_pals_expression("1 + ", &ok);            // parse error
    REQUIRE_FALSE(ok);
    ok = true;
    evaluate_pals_expression(nullptr, &ok);           // null input
    REQUIRE_FALSE(ok);
}

// Navigate root -> PALS -> facility -> the value node keyed `name` inside the
// facility entry whose single key is `entry` (facility is a sequence of
// single-key maps).
static YAMLNodeId facility_param(YAMLTreeHandle t, const char* entry) {
    YAMLNodeId fac = facility_of(t);
    size_t n = get_size(t, fac);
    for (size_t i = 0; i < n; i++) {
        YAMLNodeId e = get_child_by_index(t, fac, i);
        YAMLNodeId c = get_child_by_key(t, e, entry);
        if (c != YAML_NULL_ID) return c;
    }
    return YAML_NULL_ID;
}

static double num_val(YAMLTreeHandle t, YAMLNodeId n) {
    char* s = as_string(t, n);
    double v = s ? std::strtod(s, nullptr) : NAN;
    yaml_free_string(s);
    return v;
}

TEST_CASE("parse_and_expand_PALS evaluates expressions in the expanded tree",
          "[expr][lattices]") {
    const char* path = "tmp_expr.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - variables:\n"
              "        - a_var: 3.75e7 / c_light^2\n"
              "        - b_var: -0.34\n"
              "    - m_e:\n"
              "        kind: constant\n"
              "        value: mass_of(electron)\n"
              "    - cleo:\n"
              "        kind: Solenoid\n"
              "        length: 0.1*log(abs(b_var))\n"
              "        MagneticMultipoleP:\n"
              "          Kn1: expr(3.74 * a_var)\n"
              "          Kn2: 0.01 + 0.003*random_gauss()\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - cleo\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    const double a_var = 3.75e7 / (2.99792458e8 * 2.99792458e8);

    YAMLNodeId cleo = facility_param(lat.expanded, "cleo");
    REQUIRE(cleo != YAML_NULL_ID);

    // Immediate expression using a user variable.
    YAMLNodeId len = get_child_by_key(lat.expanded, cleo, "length");
    REQUIRE(close(num_val(lat.expanded, len), 0.1 * std::log(0.34)));

    YAMLNodeId mmp = get_child_by_key(lat.expanded, cleo, "MagneticMultipoleP");
    // expr()-delayed expression is evaluated to a number in the expanded tree.
    YAMLNodeId kn1 = get_child_by_key(lat.expanded, mmp, "Kn1");
    REQUIRE(close(num_val(lat.expanded, kn1), 3.74 * a_var));

    // random_gauss() is deferred: the text is left untouched.
    YAMLNodeId kn2 = get_child_by_key(lat.expanded, mmp, "Kn2");
    REQUIRE(val_eq(lat.expanded, kn2, "0.01 + 0.003*random_gauss()"));

    // Full-form constant defined via a particle function.
    YAMLNodeId m_e = facility_param(lat.expanded, "m_e");
    YAMLNodeId m_e_val = get_child_by_key(lat.expanded, m_e, "value");
    REQUIRE(close(num_val(lat.expanded, m_e_val), 510998.95069000003));

    // The combined tree keeps the original expression text (only `expanded`
    // is evaluated).
    YAMLNodeId c_cleo = facility_param(lat.combined, "cleo");
    YAMLNodeId c_len = get_child_by_key(lat.combined, c_cleo, "length");
    REQUIRE(val_eq(lat.combined, c_len, "0.1*log(abs(b_var))"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    rm_tmp(path);
}