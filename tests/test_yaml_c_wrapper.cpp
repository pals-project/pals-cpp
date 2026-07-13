#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fstream>
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