#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

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

// Convenience: compare a node's key, then free it.
static bool key_eq(YAMLTreeHandle tree, YAMLNodeId node, const char* expected) {
    char* s = get_node_key(tree, node);
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

    YAMLNodeId node = add_scalar(tree, root, "lang", "C++", YAML_END);
    REQUIRE(node != YAML_NULL_ID);
    REQUIRE(val_eq(tree, get_child_by_key(tree, root, "lang"), "C++"));

    delete_tree(tree);
}

TEST_CASE("add_scalar appends a keyless scalar to a sequence", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq = add_sequence(tree, root, "items", YAML_END);

    add_scalar(tree, seq, nullptr, "x", YAML_END);
    add_scalar(tree, seq, nullptr, "y", YAML_END);

    REQUIRE(get_size(tree, seq) == 2);
    REQUIRE(val_eq(tree, get_child_by_index(tree, seq, 0), "x"));
    REQUIRE(val_eq(tree, get_child_by_index(tree, seq, 1), "y"));

    delete_tree(tree);
}

TEST_CASE("add_scalar inserts at a specific index", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "first",  "a", YAML_END);
    add_scalar(tree, root, "third",  "c", YAML_END);
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
    YAMLNodeId child = add_map(tree, root, "nested", YAML_END);

    REQUIRE(child != YAML_NULL_ID);
    REQUIRE(is_map(tree, child));
    REQUIRE(get_size(tree, child) == 0);

    delete_tree(tree);
}

TEST_CASE("add_sequence creates an empty sequence child", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq = add_sequence(tree, root, "list", YAML_END);

    REQUIRE(seq != YAML_NULL_ID);
    REQUIRE(is_sequence(tree, seq));
    REQUIRE(get_size(tree, seq) == 0);

    delete_tree(tree);
}

TEST_CASE("add_map inside a sequence creates an anonymous MAP element", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root = get_root(tree);
    YAMLNodeId seq  = add_sequence(tree, root, "records", YAML_END);
    // seq element has no key
    YAMLNodeId elem = add_map(tree, seq, nullptr, YAML_END);

    REQUIRE(is_map(tree, elem));
    add_scalar(tree, elem, "id", "1", YAML_END);
    REQUIRE(val_eq(tree, get_child_by_key(tree, elem, "id"), "1"));

    delete_tree(tree);
}

TEST_CASE("add_scalar returns YAML_NULL_ID for a null parent", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    REQUIRE(add_scalar(tree, YAML_NULL_ID, "k", "v", YAML_END) == YAML_NULL_ID);
    delete_tree(tree);
}

// ============================================================
// MODIFICATION — set_scalar, set_node_key
// ============================================================

TEST_CASE("set_scalar updates an existing scalar value", "[modification]") {
    YAMLTreeHandle tree = create_empty_tree();
    YAMLNodeId root  = get_root(tree);
    YAMLNodeId child = add_scalar(tree, root, "key", "initial", YAML_END);

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
    YAMLNodeId child = add_scalar(tree, root, "old", "val", YAML_END);

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
    add_scalar(tree, root, "keep",   "yes", YAML_END);
    add_scalar(tree, root, "remove", "no",  YAML_END);

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
    add_scalar(dst, dst_root, "existing", "yes", YAML_END);
    REQUIRE(get_size(dst, dst_root) == 1);

    deep_copy_children(dst, dst_root, src, get_root(src), YAML_END);

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
    add_scalar(dst, get_root(dst), "existing", "old", YAML_END);

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
    add_scalar(tree, root, "greeting", "hello", YAML_END);

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
    add_scalar(tree, root, "written", "true", YAML_END);
    add_scalar(tree, root, "count",   "7",    YAML_END);

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
    YAMLNodeId server = add_map(tree, root, "server", YAML_END);
    add_scalar(tree, server, "host", "localhost", YAML_END);
    add_scalar(tree, server, "port", "8080",      YAML_END);
    YAMLNodeId tags = add_sequence(tree, root, "tags", YAML_END);
    add_scalar(tree, tags, nullptr, "web", YAML_END);
    add_scalar(tree, tags, nullptr, "api", YAML_END);

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

// Helper: navigate root -> PALS -> facility for a given tree.
static YAMLNodeId facility_of(YAMLTreeHandle t) {
    YAMLNodeId pals = get_child_by_key(t, get_root(t), "PALS");
    return get_child_by_key(t, pals, "facility");
}

TEST_CASE("parse_and_expand_PALS returns four non-null handles", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    REQUIRE(lat.original != nullptr);
    REQUIRE(lat.combined != nullptr);
    REQUIRE(lat.expanded != nullptr);
    REQUIRE(lat.leftover != nullptr);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

// The expanded tree holds the root lattice and nothing else: no PALS/facility
// wrapper, and only the one entry.
TEST_CASE("expanded holds only the root lattice", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    YAMLNodeId root = get_root(lat.expanded);

    REQUIRE(is_map(lat.expanded, root));
    REQUIRE(get_size(lat.expanded, root) == 1);
    REQUIRE(get_child_by_key(lat.expanded, root, "PALS") == YAML_NULL_ID);

    YAMLNodeId entry = get_child_by_index(lat.expanded, root, 0);
    char* key = get_node_key(lat.expanded, entry);
    REQUIRE(std::string(key) == "lat1");
    yaml_free_string(key);
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, entry, "kind"),
                   "Lattice"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

// Everything else stays behind, under its PALS/facility scaffolding — including
// the lattice that was not expanded.
TEST_CASE("leftover keeps the rest of the document", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    YAMLNodeId fac = facility_of(lat.leftover);
    REQUIRE(fac != YAML_NULL_ID);

    bool saw_lat1 = false, saw_lat2 = false, saw_thingB = false;
    for (size_t i = 0; i < get_size(lat.leftover, fac); i++) {
        YAMLNodeId item = get_child_by_index(lat.leftover, fac, i);
        if (get_size(lat.leftover, item) != 1) continue;
        char* key = get_node_key(lat.leftover,
                                 get_child_by_index(lat.leftover, item, 0));
        std::string k(key ? key : "");
        yaml_free_string(key);
        if (k == "lat1") saw_lat1 = true;
        if (k == "lat2") saw_lat2 = true;
        if (k == "thingB") saw_thingB = true;
    }

    REQUIRE_FALSE(saw_lat1);  // moved to expanded
    REQUIRE(saw_lat2);        // a non-root Lattice is leftover like anything else
    REQUIRE(saw_thingB);      // element definitions stay put

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

// handle_fork writes the raw node id of the fork's destination element. That id
// is assigned before the lattice is cut out into its own tree, so it must be
// translated to survive the renumbering.
TEST_CASE("fork_pointer resolves inside the expanded tree", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");

    // Find the fork_pointer scalar anywhere in the expanded tree.
    YAMLNodeId fp = YAML_NULL_ID;
    std::vector<YAMLNodeId> stack{get_root(lat.expanded)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* key = get_node_key(lat.expanded, n);
        if (key && std::string(key) == "fork_pointer") fp = n;
        yaml_free_string(key);
        for (size_t i = 0; i < get_size(lat.expanded, n); i++)
            stack.push_back(get_child_by_index(lat.expanded, n, i));
    }
    REQUIRE(fp != YAML_NULL_ID);

    char* val = as_string(lat.expanded, fp);
    YAMLNodeId target = (YAMLNodeId)std::stoull(val);
    yaml_free_string(val);

    // It names the fork's destination_element in the branch expansion created.
    char* dest = as_string(lat.expanded, target);
    REQUIRE(std::string(dest) == "dump_begin");
    yaml_free_string(dest);

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

// ============================================================
// build_correspondence_map
// ============================================================

TEST_CASE("build_correspondence_map is empty for null handles", "[correspondence]") {
    struct correspondence_map m = build_correspondence_map(nullptr, nullptr, nullptr, nullptr);
    REQUIRE(m.count == 0);
    REQUIRE(m.links == nullptr);
    free_correspondence_map(m);  // must not crash
}

TEST_CASE("build_correspondence_map links the document roots", "[correspondence]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.expanded,
                                 lat.leftover);

    REQUIRE(m.count > 0);

    // leftover is what still carries the document root, so that is the node
    // corresponding to the combined root. (The expanded root is synthesised to
    // hold the lattice entry and has no counterpart — see below.)
    YAMLNodeId left_root = get_root(lat.leftover);

    bool found_root = false;
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].leftover == left_root) {
            found_root = true;
            REQUIRE(m.links[i].combined == get_root(lat.combined));
            // The original tree's first child is the top-level file's contents.
            REQUIRE(m.links[i].original ==
                    get_child_by_index(lat.original, get_root(lat.original), 0));
            REQUIRE(is_map(lat.combined, m.links[i].combined));
            REQUIRE(is_map(lat.original, m.links[i].original));
        }
        // A link names a node in exactly one of the two derived trees.
        REQUIRE((m.links[i].expanded == YAML_NULL_ID) !=
                (m.links[i].leftover == YAML_NULL_ID));
    }
    REQUIRE(found_root);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

// A definition that expansion substituted into the lattice is a copy: the
// definition still stands in leftover, so the same combined node reaches both
// trees.
TEST_CASE("build_correspondence_map ties the two trees through combined",
          "[correspondence]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.expanded,
                                 lat.leftover);

    // inj_line is defined at facility level and used by lat1's branches, so it
    // is expanded into lat1 while its definition stays in leftover.
    std::map<YAMLNodeId, int> sides;  // combined id -> bitmask of trees reached
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].combined == YAML_NULL_ID) continue;
        sides[m.links[i].combined] |=
            (m.links[i].expanded != YAML_NULL_ID) ? 1 : 2;
    }

    bool found_both = false;
    for (const auto& kv : sides)
        if (kv.second == 3) found_both = true;
    REQUIRE(found_both);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

TEST_CASE("build_correspondence_map connects a node across trees by value",
          "[correspondence]") {
    // A constant that lives outside the expanded lattice is not part of it, so
    // it lands in leftover; the map must still connect it back to combined and
    // original.
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
        build_correspondence_map(lat.original, lat.combined, lat.expanded,
                                 lat.leftover);

    // Locate a_const in the leftover tree: facility[0] -> constants -> a_const.
    YAMLNodeId l_const = get_child_by_index(lat.leftover, facility_of(lat.leftover), 0);
    YAMLNodeId l_a_const =
        get_child_by_key(lat.leftover,
                         get_child_by_key(lat.leftover, l_const, "constants"),
                         "a_const");
    REQUIRE(l_a_const != YAML_NULL_ID);
    // Expressions are evaluated across the whole document before it is split, so
    // this constant holds a number in leftover too; the combined/original copies
    // (checked below) still carry the original expression text.
    {
        char* s = as_string(lat.leftover, l_a_const);
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
        if (m.links[i].leftover != l_a_const) continue;
        found = true;
        REQUIRE(m.links[i].expanded == YAML_NULL_ID);
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
    delete_tree(lat.leftover);
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
        build_correspondence_map(lat.original, lat.combined, lat.expanded,
                                 lat.leftover);

    // Unrolling `repeat: 3` over a one-element cell produces three keyless `d1`
    // scalars in the expanded line, all copied from the same combined source.
    // Build a histogram of the combined ids that the expanded scalar `d1`
    // nodes point to; a single source must account for at least three copies.
    std::map<YAMLNodeId, int> combined_hits;
    for (size_t i = 0; i < m.count; i++) {
        // Skip the leftover half of the map: those ids index a different tree.
        if (m.links[i].expanded == YAML_NULL_ID) continue;
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
    delete_tree(lat.leftover);
    rm_tmp(path);
}

// ============================================================
// KEY ORDER
// ============================================================

// A lattice file is meant to be read by people, so a map's keys must come back
// in the order the author wrote them, never sorted. Order is preserved rather
// than imposed: the YAML backend stores map children in a sequence, and the
// expansion passes copy children in order. Nothing asserts that on its own,
// which is what these tests are for.

// The keys of `node`'s children, in order. Keyless children (sequence items)
// contribute an empty string.
static std::vector<std::string> keys_of(YAMLTreeHandle tree, YAMLNodeId node) {
    std::vector<std::string> keys;
    for (size_t i = 0; i < get_size(tree, node); i++) {
        char* k = get_node_key(tree, get_child_by_index(tree, node, i));
        keys.push_back(k ? k : "");
        yaml_free_string(k);
    }
    return keys;
}

TEST_CASE("Map keys keep their file order through a parse/emit round-trip",
          "[key_order]") {
    const char* path = "tmp_key_order.yaml";
    // Deliberately not alphabetical: sorting would give alpha, bravo, mike, zulu.
    write_tmp(path, "zulu: 1\nalpha: 2\nmike: 3\nbravo: 4\n");

    const std::vector<std::string> expected = {"zulu", "alpha", "mike", "bravo"};

    YAMLTreeHandle tree = parse_file(path);
    REQUIRE(keys_of(tree, get_root(tree)) == expected);

    // ... and again after emitting and reading the result back.
    const char* out = "tmp_key_order_out.yaml";
    REQUIRE(write_file(tree, out));
    YAMLTreeHandle reloaded = parse_file(out);
    REQUIRE(keys_of(reloaded, get_root(reloaded)) == expected);

    delete_tree(tree);
    delete_tree(reloaded);
    rm_tmp(path);
    rm_tmp(out);
}

TEST_CASE("add_scalar inserts at the requested position", "[key_order]") {
    YAMLTreeHandle tree = parse_string("zulu: 1\nalpha: 2");
    YAMLNodeId root = get_root(tree);
    add_scalar(tree, root, "omega", "3", YAML_END);  // append
    add_scalar(tree, root, "first", "0", 0);    // prepend

    const std::vector<std::string> expected = {"first", "zulu", "alpha", "omega"};
    REQUIRE(keys_of(tree, root) == expected);

    delete_tree(tree);
}

TEST_CASE("Expansion preserves the key order of the source file", "[key_order]") {
    const char* path = "tmp_key_order.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        length: 2\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        multipass: true\n"
              "        length: 37.8\n"
              "        zero_point: thingC\n"
              "        line:\n"
              "          - thingZ:\n"
              "              inherit: thingB\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: lat1\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);

    // `main_line`'s keys are not in alphabetical order — sorting would move
    // `length` above `line` and `multipass` to the end — so the order below
    // can only come from the source file.
    const std::vector<std::string> expected = {"kind", "multipass", "length",
                                               "zero_point", "line"};

    // The combined tree splices includes; `main_line` is facility item 1.
    YAMLNodeId c_line = get_child_by_index(
        lat.combined,
        get_child_by_index(lat.combined, facility_of(lat.combined), 1), 0);
    REQUIRE(key_eq(lat.combined, c_line, "main_line"));
    REQUIRE(keys_of(lat.combined, c_line) == expected);

    // The expanded tree is rooted at the lattice entry itself, and the line is
    // inlined under its `branches`.
    YAMLNodeId lat1 = get_child_by_index(lat.expanded, get_root(lat.expanded), 0);
    REQUIRE(key_eq(lat.expanded, lat1, "lat1"));
    YAMLNodeId branches = get_child_by_key(lat.expanded, lat1, "branches");
    YAMLNodeId e_line = get_child_by_index(
        lat.expanded, get_child_by_index(lat.expanded, branches, 0), 0);
    REQUIRE(key_eq(lat.expanded, e_line, "main_line"));
    REQUIRE(keys_of(lat.expanded, e_line) == expected);

    // Merging `inherit: thingB` brings the parent's keys in ahead of the
    // child's own, rather than sorting the merged result.
    YAMLNodeId line_seq = get_child_by_key(lat.expanded, e_line, "line");
    YAMLNodeId thingZ = get_child_by_index(
        lat.expanded, get_child_by_index(lat.expanded, line_seq, 0), 0);
    REQUIRE(key_eq(lat.expanded, thingZ, "thingZ"));
    const std::vector<std::string> inherited = {"kind", "length", "inherit"};
    REQUIRE(keys_of(lat.expanded, thingZ) == inherited);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

// ============================================================
// NAME MATCHING
// ============================================================

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

// True if two doubles agree to a relative/absolute tolerance. The max(1.0, ...)
// floors the tolerance at 1e-9 absolute, which keeps comparisons against zero
// workable but makes this useless for values much smaller than 1e-9 — every
// such comparison passes. Use close_rel for those.
static bool close(double got, double want) {
    return std::fabs(got - want) <= 1e-9 * std::max(1.0, std::fabs(want));
}

// Purely relative comparison, for quantities whose magnitude is far from 1.
static bool close_rel(double got, double want) {
    return std::fabs(got - want) <= 1e-9 * std::fabs(want);
}

TEST_CASE("evaluate_pals_expression: arithmetic and precedence", "[expr]") {
    REQUIRE(eval_ok("2 + 3 * 4") == 14.0);
    REQUIRE(eval_ok("(2 + 3) * 4") == 20.0);
    REQUIRE(eval_ok("2 ^ 3 ^ 2") == 512.0);   // right-associative
    REQUIRE(eval_ok("-2 ^ 2") == -4.0);        // unary minus looser than ^
    REQUIRE(eval_ok("2 ^ -2") == 0.25);
    REQUIRE(close_rel(eval_ok("3.75e7 / c_light^2"),
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
    // Value of apc::K_BOLTZMANN, in eV/K.
    REQUIRE(eval_ok("k_boltzmann") == 8.617333262e-5);

    // classical_radius_factor is derived rather than taken from APC, because
    // apc::CLASSICAL_RADIUS_FACTOR is 1e6 larger (its mass is in MeV while
    // apc::M_ELECTRON is in eV). Pin the exponent explicitly: the relation
    // below holds under either convention, so on its own it would not notice a
    // switch to the APC constant silently rescaling every lattice using it.
    REQUIRE(close_rel(eval_ok("classical_radius_factor"),
                      eval_ok("r_electron") *
                          eval_ok("mass_of(\"electron\")")));
    REQUIRE(close_rel(eval_ok("classical_radius_factor"),
                      1.4399645468825422e-9));

    // epsilon_0 and mu_0 are in the PALS standard's eV units — 1/(eV*m) and
    // eV*sec^2/m respectively, not the SI F/m and N/A^2. In these units the
    // identity eps_0 * mu_0 * c^2 == 1 holds, which pins both values at once.
    REQUIRE(close(eval_ok("epsilon_0"), 5.5263493618e7));
    REQUIRE(close_rel(eval_ok("mu_0"), 2.013354537e-25));
    REQUIRE(close(eval_ok("epsilon_0 * mu_0 * c_light^2"), 1.0));
}

TEST_CASE("evaluate_pals_expression: particle-data functions from libapc",
          "[expr]") {
    // Species names must always be quoted (single or double). Values mirror
    // AtomicAndPhysicalConstantsCLib (CODATA 2022).
    REQUIRE(close(eval_ok("mass_of(\"proton\")"), 938272089.43000007));
    REQUIRE(eval_ok("charge_of('electron')") == -1.0);
    REQUIRE(eval_ok("charge_of(\"anti-proton\")") == -1.0);
    REQUIRE(close(eval_ok("2 * mass_of(\"electron\")"), 2 * 510998.95069000003));
    // A mass number must carry a leading '#' (e.g. "#3He"). The bare atom is
    // neutral; the ionised form carries the charge.
    REQUIRE(eval_ok("charge_of(\"#3He\")") == 0.0);
    REQUIRE(eval_ok("charge_of(\"helion\")") == 2.0);
    REQUIRE(close(eval_ok("mass_of(\"#3He\")"), 2809413528.3197904));

    // An unquoted species name is an error.
    bool ok = true;
    evaluate_pals_expression("mass_of(electron)", &ok);
    REQUIRE_FALSE(ok);
    ok = true;
    evaluate_pals_expression("charge_of(#3He)", &ok);
    REQUIRE_FALSE(ok);
    // A mass number without the '#' prefix is an error, even when quoted.
    ok = true;
    evaluate_pals_expression("mass_of(\"3He\")", &ok);
    REQUIRE_FALSE(ok);
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
    evaluate_pals_expression("mass_of(\"nonsense\")", &ok);  // unknown species
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

// The first node keyed `key` anywhere in `t`, found depth-first. Used to reach
// an element that expansion inlined into the lattice, wherever it ended up.
static YAMLNodeId find_by_key(YAMLTreeHandle t, const char* key) {
    std::vector<YAMLNodeId> stack{get_root(t)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* k = get_node_key(t, n);
        bool hit = k && std::string(k) == key;
        yaml_free_string(k);
        if (hit) return n;
        for (size_t i = 0; i < get_size(t, n); i++)
            stack.push_back(get_child_by_index(t, n, i));
    }
    return YAML_NULL_ID;
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
              "        value: mass_of(\"electron\")\n"
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

    // `cleo` is referenced by main_line, so expansion inlines its definition
    // into the lattice; this is the copy inside the expanded tree.
    YAMLNodeId cleo = find_by_key(lat.expanded, "cleo");
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

    // Expressions are evaluated before the document is split, so a definition
    // that stayed behind is evaluated in leftover just the same. `m_e` is not
    // referenced by the lattice, so leftover is the only place it exists.
    YAMLNodeId m_e = facility_param(lat.leftover, "m_e");
    REQUIRE(m_e != YAML_NULL_ID);
    YAMLNodeId m_e_val = get_child_by_key(lat.leftover, m_e, "value");
    REQUIRE(close(num_val(lat.leftover, m_e_val), 510998.95069000003));
    REQUIRE(find_by_key(lat.expanded, "m_e") == YAML_NULL_ID);

    // The combined tree keeps the original expression text (evaluation happens
    // downstream of it).
    YAMLNodeId c_cleo = facility_param(lat.combined, "cleo");
    YAMLNodeId c_len = get_child_by_key(lat.combined, c_cleo, "length");
    REQUIRE(val_eq(lat.combined, c_len, "0.1*log(abs(b_var))"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS resolves map-form constants/variables",
          "[expr][lattices]") {
    // The compact `constants:`/`variables:` block may be written as a plain map
    // (`a_const: ...`) as well as the standard seq-of-single-key-maps form; a
    // later definition must be able to reference an earlier one by name.
    const char* path = "tmp_mapdefs.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        a_const: 0.3 * r_electron\n"
              "        b_const: 0.45\n"
              "    - variables:\n"
              "        a_var: a_const^2\n"
              "        b_var: 0.37 * atan2(0.1, 0.2)\n"
              "    - d1:\n"
              "        kind: Drift\n"
              "        length: a_const + b_const\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - d1\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    const double a_const = 0.3 * evaluate_pals_expression("r_electron", nullptr);

    // constants/variables blocks are not part of the lattice, so they are
    // leftover — evaluated all the same.
    YAMLNodeId consts = facility_param(lat.leftover, "constants");
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, consts, "a_const")),
                  a_const));

    // a_var references the map-form constant a_const defined above it.
    YAMLNodeId vars = facility_param(lat.leftover, "variables");
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, vars, "a_var")),
                  a_const * a_const));

    // An element parameter may reference the map-form definitions too; this is
    // d1 as inlined into the expanded lattice.
    YAMLNodeId d1 = find_by_key(lat.expanded, "d1");
    REQUIRE(d1 != YAML_NULL_ID);
    REQUIRE(close(num_val(lat.expanded, get_child_by_key(lat.expanded, d1,
                                                         "length")),
                  a_const + 0.45));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS resolves element-parameter references",
          "[expr][lattices]") {
    // An expression may reference another element's parameter with the
    // `element>group.sub. ... .param` syntax; it resolves to that parameter's
    // value (evaluated as an expression in turn).
    const char* path = "tmp_eleparamref.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        length: 0.3\n"
              "        MagneticMultipoleP:\n"
              "          Kn2L: 0.1\n"
              "    - DH1A:\n"
              "        kind: Bend\n"
              "        length: 0.2\n"
              "        BendP:\n"
              "          edge_int2: 0.02 * thingB>MagneticMultipoleP.Kn2L\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - DH1A\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    YAMLNodeId dh1a = find_by_key(lat.expanded, "DH1A");
    REQUIRE(dh1a != YAML_NULL_ID);
    YAMLNodeId bendp = get_child_by_key(lat.expanded, dh1a, "BendP");
    REQUIRE(close(
        num_val(lat.expanded, get_child_by_key(lat.expanded, bendp, "edge_int2")),
        0.02 * 0.1));

    // A clean lattice reports no problems.
    REQUIRE(lat.problems.count == 0);
    free_lattice_problems(lat.problems);  // safe on an empty list

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS resolves a species-name constant",
          "[expr][lattices]") {
    // A particle-data function may take a symbol whose value is a species name
    // (`mass_of(species)` where `species: "#3He"`), not only a quoted literal.
    const char* path = "tmp_speciesconst.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        species: \"#3He\"\n"
              "        b_const: 0.45 * mass_of(species)\n"
              "    - DH1A:\n"
              "        kind: Bend\n"
              "        ReferenceP:\n"
              "          species_ref: species\n"
              "        BendP:\n"
              "          e_tot: 1.1 * mass_of(species)\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - DH1A\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    const double m_3he = 2809413528.3197904;  // mass_of("#3He"), CODATA 2022

    YAMLNodeId consts = facility_param(lat.leftover, "constants");
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, consts, "b_const")),
                  0.45 * m_3he));

    YAMLNodeId dh1a = find_by_key(lat.expanded, "DH1A");
    REQUIRE(dh1a != YAML_NULL_ID);
    YAMLNodeId bendp = get_child_by_key(lat.expanded, dh1a, "BendP");
    REQUIRE(close(num_val(lat.expanded,
                          get_child_by_key(lat.expanded, bendp, "e_tot")),
                  1.1 * m_3he));

    // A bare identifier naming the species constant (`species_ref: species`) is
    // replaced by its species-name string in the expanded tree.
    YAMLNodeId refp = get_child_by_key(lat.expanded, dh1a, "ReferenceP");
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, refp, "species_ref"),
                   "#3He"));

    // The species constant itself stays as its (string) species name.
    REQUIRE(val_eq(lat.leftover,
                   get_child_by_key(lat.leftover, consts, "species"), "#3He"));

    // No spurious problems.
    REQUIRE(lat.problems.count == 0);
    free_lattice_problems(lat.problems);

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS reports expansion problems",
          "[expr][lattices][problems]") {
    // Every silent failure of expansion/evaluation is surfaced in the
    // `problems` list: dangling line references, undefined inherit/repeat
    // targets, and expressions that cannot be evaluated.
    const char* path = "tmp_problems.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        a_const: 0.3 * undefined_thing\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        MagneticMultipoleP:\n"
              "          Kn2L: 0.1\n"
              "    - DH1A:\n"
              "        kind: Bend\n"
              "        BendP:\n"
              "          edge_int2: 0.02 * thingB>MagneticMultipoleP.NotThere\n"
              "          e1: 3 * missing_const\n"
              "    - ghost_child:\n"
              "        kind: Bend\n"
              "        inherit: ghost_ancestor\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - DH1A\n"
              "          - ghost_child\n"
              "          - NoSuchElement\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    // Collect the messages so the assertions do not depend on their order.
    std::vector<std::string> msgs;
    for (size_t i = 0; i < lat.problems.count; ++i)
        msgs.emplace_back(lat.problems.items[i]);

    auto has = [&](const std::string& needle) {
        for (const std::string& m : msgs)
            if (m.find(needle) != std::string::npos) return true;
        return false;
    };

    REQUIRE(has("reference to undefined element or line 'NoSuchElement'"));
    REQUIRE(has("inherit: 'ghost_ancestor' is not defined"));
    REQUIRE(has("could not evaluate expression for constants.a_const"));
    REQUIRE(has("could not evaluate expression for BendP.edge_int2"));
    REQUIRE(has("could not evaluate expression for BendP.e1"));

    // Exactly those five: plain names (`kind: Bend`, the line references that
    // DO resolve) are not reported, and duplicate copies made by expansion
    // collapse to one message each.
    REQUIRE(lat.problems.count == 5);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS reports a missing lattice",
          "[lattices][problems]") {
    const char* path = "tmp_nolattice.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        length: 0.3\n");

    struct lattices lat = parse_and_expand_PALS(path, "not_here");
    REQUIRE(lat.problems.count == 1);
    REQUIRE(std::string(lat.problems.items[0]) == "lattice 'not_here' not found");

    // With no lattice to expand, expanded is an empty map and the whole document
    // is leftover — both handles are still valid.
    REQUIRE(lat.expanded != nullptr);
    REQUIRE(lat.leftover != nullptr);
    REQUIRE(get_size(lat.expanded, get_root(lat.expanded)) == 0);
    REQUIRE(facility_of(lat.leftover) != YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS evaluates controller expressions",
          "[expr][lattices][controller]") {
    const char* path = "tmp_controller.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - my_const:\n"
              "        kind: constant\n"
              "        value: 2.0\n"
              "    - ps27:\n"
              "        kind: Controller\n"
              "        control_type: ABSOLUTE\n"
              "        variables:\n"
              "          cur1: 0.023\n"
              "          cur2: cur1 / c_light\n"
              "        controls:\n"
              "          - parameter: Qa.*>MagneticMultipoleP.Ks2L\n"
              "            expression: 0.075*sin(cur1) + 0.3*cur2\n"
              "          - parameter: Qb>MagneticMultipoleP.Kn1L\n"
              "            expression: cur1 * my_const\n"
              "          - parameter: Qc>MagneticMultipoleP.Kn0\n"
              "            expression: 0.01 + random_gauss()\n"
              "    - chrom_a:\n"
              "        kind: Controller\n"
              "        control_type: RELATIVE\n"
              "        variables:\n"
              "          command: 0.4\n"
              "          derived: ps27>cur1 * 2\n"
              "        controls:\n"
              "          - parameter: S1>MagneticMultipoleP.Kn2L\n"
              "            expression: 5.62 * command + 0.02 * command^2\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - my_const\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.leftover != nullptr);

    const double cur1 = 0.023;
    const double cur2 = cur1 / 2.99792458e8;

    // Controllers are facility-level, so they are leftover rather than part
    // of the lattice; their expressions are evaluated all the same.
    // Controller variables are evaluated with the controller's own symbol
    // table: cur2 references the earlier variable cur1 and the constant c_light.
    YAMLNodeId ps27 = facility_param(lat.leftover, "ps27");
    REQUIRE(ps27 != YAML_NULL_ID);
    YAMLNodeId vars = get_child_by_key(lat.leftover, ps27, "variables");
    REQUIRE(close(num_val(lat.leftover, get_child_by_key(lat.leftover, vars,
                                                         "cur2")),
                  cur2));

    // Each control `expression` is computed and its value stored in place.
    YAMLNodeId controls = get_child_by_key(lat.leftover, ps27, "controls");
    YAMLNodeId c0 = get_child_by_index(lat.leftover, controls, 0);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, c0, "expression")),
                  0.075 * std::sin(cur1) + 0.3 * cur2));
    // Control expressions may reference lattice constants (my_const = 2).
    YAMLNodeId c1 = get_child_by_index(lat.leftover, controls, 1);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, c1, "expression")),
                  cur1 * 2.0));
    // random_gauss() stays deferred, exactly as elsewhere.
    YAMLNodeId c2 = get_child_by_index(lat.leftover, controls, 2);
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, c2, "expression"),
                   "0.01 + random_gauss()"));

    // The `parameter` target spec and `control_type` are names, left untouched.
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, c0, "parameter"),
                   "Qa.*>MagneticMultipoleP.Ks2L"));
    REQUIRE(val_eq(lat.leftover,
                   get_child_by_key(lat.leftover, ps27, "control_type"),
                   "ABSOLUTE"));

    // A second controller may reference the first's variables via `name>var`.
    YAMLNodeId chrom = facility_param(lat.leftover, "chrom_a");
    YAMLNodeId cvars = get_child_by_key(lat.leftover, chrom, "variables");
    REQUIRE(close(num_val(lat.leftover, get_child_by_key(lat.leftover, cvars,
                                                         "derived")),
                  cur1 * 2.0));
    YAMLNodeId ccontrols = get_child_by_key(lat.leftover, chrom, "controls");
    YAMLNodeId cc0 = get_child_by_index(lat.leftover, ccontrols, 0);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, cc0, "expression")),
                  5.62 * 0.4 + 0.02 * 0.4 * 0.4));

    // The combined tree keeps the original controller expression text.
    YAMLNodeId c_ps27 = facility_param(lat.combined, "ps27");
    YAMLNodeId c_controls = get_child_by_key(lat.combined, c_ps27, "controls");
    YAMLNodeId c_c0 = get_child_by_index(lat.combined, c_controls, 0);
    REQUIRE(val_eq(lat.combined,
                   get_child_by_key(lat.combined, c_c0, "expression"),
                   "0.075*sin(cur1) + 0.3*cur2"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}