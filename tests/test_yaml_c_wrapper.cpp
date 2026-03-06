#define CATCH_CONFIG_MAIN
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fstream>
#include <string>

#include "../src/yaml_c_wrapper.h"

using Catch::Approx;

// Helper to create test YAML files
void create_test_file(const std::string& filename, const std::string& content) {
    std::ofstream file(filename);
    file << content;
    file.close();
}

// Helper to read file content
std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

// Helper to clean up test files
void cleanup_file(const std::string& filename) {
    std::remove(filename.c_str());
}

// ===========================================
// TEST SUITE: Creation and Deletion
// ===========================================

TEST_CASE("YAML nodes can be created and deleted", "[creation]") {
    SECTION("Create empty node") {
        YAMLNodeHandle node = create_node();
        REQUIRE(node != nullptr);
        delete_node(node);
    }

    SECTION("Create map node") {
        YAMLNodeHandle map = create_map();
        REQUIRE(map != nullptr);
        REQUIRE(is_map(map));
        delete_node(map);
    }

    SECTION("Create sequence node") {
        YAMLNodeHandle seq = create_sequence();
        REQUIRE(seq != nullptr);
        REQUIRE(is_sequence(seq));
        delete_node(seq);
    }

    SECTION("Create scalar node") {
        YAMLNodeHandle scalar = create_scalar();
        REQUIRE(scalar != nullptr);
        REQUIRE(is_scalar(scalar));
        delete_node(scalar);
    }
}

// ===========================================
// TEST SUITE: Parsing
// ===========================================

TEST_CASE("YAML can be parsed from strings", "[parsing]") {
    SECTION("Parse simple map") {
        const char* yaml = "key: value";
        YAMLNodeHandle node = parse_string(yaml);

        REQUIRE(node != nullptr);
        REQUIRE(is_map(node));
        REQUIRE(has_key(node, "key"));

        delete_node(node);
    }

    SECTION("Parse sequence") {
        const char* yaml = "[a, b, c]";
        YAMLNodeHandle node = parse_string(yaml);

        REQUIRE(node != nullptr);
        REQUIRE(is_sequence(node));
        REQUIRE(size(node) == 3);

        delete_node(node);
    }

    SECTION("Parse invalid YAML returns nullptr") {
        const char* invalid = "invalid: yaml: :";
        YAMLNodeHandle node = parse_string(invalid);

        REQUIRE(node == nullptr);
    }
}

TEST_CASE("YAML can be parsed from files", "[parsing][file]") {
    SECTION("Parse valid file") {
        YAMLNodeHandle node = parse_file("../lattice_files/ex.pals.yaml");

        REQUIRE(node != nullptr);
        REQUIRE(is_sequence(node));
        REQUIRE(size(node) >= 2);

        delete_node(node);
    }

    SECTION("Parse non-existent file returns nullptr") {
        YAMLNodeHandle node = parse_file("nonexistent.pals.yaml");
        REQUIRE(node == nullptr);
    }
}

// ===========================================
// TEST SUITE: Type Checking
// ===========================================

TEST_CASE("Node types can be checked", "[types]") {
    SECTION("Check scalar type") {
        YAMLNodeHandle node = parse_string("value");
        REQUIRE(is_scalar(node));
        REQUIRE_FALSE(is_map(node));
        REQUIRE_FALSE(is_sequence(node));
        delete_node(node);
    }

    SECTION("Check map type") {
        YAMLNodeHandle node = parse_string("key: value");
        REQUIRE(is_map(node));
        REQUIRE_FALSE(is_scalar(node));
        REQUIRE_FALSE(is_sequence(node));
        delete_node(node);
    }

    SECTION("Check sequence type") {
        YAMLNodeHandle node = parse_string("[a, b, c]");
        REQUIRE(is_sequence(node));
        REQUIRE_FALSE(is_map(node));
        REQUIRE_FALSE(is_scalar(node));
        delete_node(node);
    }

    SECTION("Check null type") {
        YAMLNodeHandle node = parse_string("null");
        REQUIRE(is_null(node));
        delete_node(node);
    }
}

// ===========================================
// TEST SUITE: Access Operations
// ===========================================

TEST_CASE("Map keys can be accessed", "[access][map]") {
    const char* yaml = "name: test\nvalue: 42";
    YAMLNodeHandle node = parse_string(yaml);

    SECTION("Check key existence") {
        REQUIRE(has_key(node, "name"));
        REQUIRE(has_key(node, "value"));
        REQUIRE_FALSE(has_key(node, "nonexistent"));
    }

    SECTION("Get key value") {
        YAMLNodeHandle name = get_key(node, "name");
        REQUIRE(name != nullptr);

        char* str = as_string(name);
        REQUIRE(std::string(str) == "test");

        yaml_free_string(str);
        delete_node(name);
    }

    SECTION("Get non-existent key returns nullptr") {
        YAMLNodeHandle missing = get_key(node, "missing");
        REQUIRE(missing == nullptr);
    }

    SECTION("Get all keys") {
        int count;
        char** keys = get_keys(node, &count);

        REQUIRE(count == 2);
        REQUIRE(keys != nullptr);

        bool has_name = false, has_value = false;
        for (int i = 0; i < count; i++) {
            if (std::string(keys[i]) == "name") has_name = true;
            if (std::string(keys[i]) == "value") has_value = true;
        }

        REQUIRE(has_name);
        REQUIRE(has_value);

        yaml_free_keys(keys, count);
    }

    delete_node(node);
}

TEST_CASE("Sequence indices can be accessed", "[access][sequence]") {
    const char* yaml = "[apple, banana, cherry]";
    YAMLNodeHandle node = parse_string(yaml);

    SECTION("Check size") { REQUIRE(size(node) == 3); }

    SECTION("Access valid index") {
        YAMLNodeHandle item = get_index(node, 1);
        REQUIRE(item != nullptr);

        char* str = as_string(item);
        REQUIRE(std::string(str) == "banana");

        yaml_free_string(str);
        delete_node(item);
    }

    SECTION("Access out of bounds index returns nullptr") {
        YAMLNodeHandle item = get_index(node, 999);
        REQUIRE(item == nullptr);
    }

    SECTION("Access negative index returns nullptr") {
        YAMLNodeHandle item = get_index(node, -1);
        REQUIRE(item == nullptr);
    }

    delete_node(node);
}

// ===========================================
// TEST SUITE: Type Conversions
// ===========================================

TEST_CASE("Values can be converted to C types", "[conversion]") {
    SECTION("Convert to string") {
        YAMLNodeHandle node = parse_string("test_value");
        char* str = as_string(node);

        REQUIRE(str != nullptr);
        REQUIRE(std::string(str) == "test_value");

        yaml_free_string(str);
        delete_node(node);
    }

    SECTION("Convert to int") {
        YAMLNodeHandle node = parse_string("42");
        int val = as_int(node);

        REQUIRE(val == 42);

        delete_node(node);
    }

    SECTION("Convert to float") {
        YAMLNodeHandle node = parse_string("3.14");
        double val = as_float(node);

        REQUIRE(val == Approx(3.14));

        delete_node(node);
    }

    SECTION("Convert to bool") {
        YAMLNodeHandle node_true = parse_string("true");
        YAMLNodeHandle node_false = parse_string("false");

        REQUIRE(as_bool(node_true) == true);
        REQUIRE(as_bool(node_false) == false);

        delete_node(node_true);
        delete_node(node_false);
    }

    SECTION("Invalid conversion returns default") {
        YAMLNodeHandle node = parse_string("[a, b, c]");

        // Can't convert sequence to string
        char* str = as_string(node);
        REQUIRE(str == nullptr);

        delete_node(node);
    }
}

// ===========================================
// TEST SUITE: Modification - Maps
// ===========================================

TEST_CASE("Map values can be set", "[modification][map]") {
    YAMLNodeHandle map = create_map();

    SECTION("Set string value") {
        set_value_string(map, "name", "test");
        REQUIRE(has_key(map, "name"));

        YAMLNodeHandle value = get_key(map, "name");
        char* str = as_string(value);
        REQUIRE(std::string(str) == "test");

        yaml_free_string(str);
        delete_node(value);
    }

    SECTION("Set int value") {
        set_value_int(map, "count", 42);

        YAMLNodeHandle value = get_key(map, "count");
        REQUIRE(as_int(value) == 42);

        delete_node(value);
    }

    SECTION("Set float value") {
        set_value_float(map, "pi", 3.14);

        YAMLNodeHandle value = get_key(map, "pi");
        REQUIRE(as_float(value) == Catch::Approx(3.14));

        delete_node(value);
    }

    SECTION("Set bool value") {
        set_value_bool(map, "enabled", true);

        YAMLNodeHandle value = get_key(map, "enabled");
        REQUIRE(as_bool(value) == true);

        delete_node(value);
    }

    SECTION("Set nested node") {
        YAMLNodeHandle nested = create_map();
        set_value_string(nested, "inner", "value");

        set_value_node(map, "nested", nested);

        REQUIRE(has_key(map, "nested"));
        YAMLNodeHandle retrieved = get_key(map, "nested");
        REQUIRE(is_map(retrieved));

        delete_node(retrieved);
        delete_node(nested);
    }

    delete_node(map);
}

// ===========================================
// TEST SUITE: Modification - Sequences
// ===========================================

TEST_CASE("Sequence values can be pushed", "[modification][sequence]") {
    YAMLNodeHandle seq = create_sequence();

    SECTION("Push string values") {
        push_string(seq, "first");
        push_string(seq, "second");

        REQUIRE(size(seq) == 2);

        YAMLNodeHandle item = get_index(seq, 1);
        char* str = as_string(item);
        REQUIRE(std::string(str) == "second");

        yaml_free_string(str);
        delete_node(item);
    }

    SECTION("Push int values") {
        push_int(seq, 10);
        push_int(seq, 20);

        REQUIRE(size(seq) == 2);

        YAMLNodeHandle item = get_index(seq, 0);
        REQUIRE(as_int(item) == 10);

        delete_node(item);
    }

    SECTION("Push float values") {
        push_float(seq, 1.1);
        push_float(seq, 2.2);

        REQUIRE(size(seq) == 2);

        YAMLNodeHandle item = get_index(seq, 1);
        REQUIRE(as_float(item) == Catch::Approx(2.2));

        delete_node(item);
    }

    SECTION("Push node") {
        YAMLNodeHandle node = create_map();
        set_value_string(node, "key", "value");

        push_node(seq, node);

        REQUIRE(size(seq) == 1);

        YAMLNodeHandle retrieved = get_index(seq, 0);
        REQUIRE(is_map(retrieved));

        delete_node(retrieved);
        delete_node(node);
    }

    delete_node(seq);
}

TEST_CASE("Sequence values can be set at index", "[modification][sequence]") {
    YAMLNodeHandle seq = parse_string("[a, b, c]");

    SECTION("Set string at index") {
        YAMLNodeHandle replacement = parse_string("replaced");
        set_at_index(seq, 1, replacement);

        YAMLNodeHandle item = get_index(seq, 1);
        char* str = as_string(item);
        REQUIRE(std::string(str) == "replaced");

        yaml_free_string(str);
        delete_node(item);
        delete_node(replacement);
    }

    delete_node(seq);
}

// ===========================================
// TEST SUITE: Scalar Editing
// ===========================================

TEST_CASE("Scalar values can be edited directly", "[modification][scalar]") {
    SECTION("Set scalar string") {
        YAMLNodeHandle scalar = create_scalar();
        set_scalar_string(scalar, "new_value");

        char* str = as_string(scalar);
        REQUIRE(std::string(str) == "new_value");

        yaml_free_string(str);
        delete_node(scalar);
    }

    SECTION("Set scalar int") {
        YAMLNodeHandle scalar = create_scalar();
        set_scalar_int(scalar, 99);

        REQUIRE(as_int(scalar) == 99);

        delete_node(scalar);
    }

    SECTION("Set scalar float") {
        YAMLNodeHandle scalar = create_scalar();
        set_scalar_float(scalar, 2.718);

        REQUIRE(as_float(scalar) == Catch::Approx(2.718));

        delete_node(scalar);
    }

    SECTION("Set scalar bool") {
        YAMLNodeHandle scalar = create_scalar();
        set_scalar_bool(scalar, false);

        REQUIRE(as_bool(scalar) == false);

        delete_node(scalar);
    }
}

// ===========================================
// TEST SUITE: File I/O
// ===========================================

TEST_CASE("YAML can be written to files", "[io][file]") {
    const char* test_file = "test_output.pals.yaml";

    SECTION("Write simple map") {
        YAMLNodeHandle map = create_map();
        set_value_string(map, "test", "value");
        set_value_int(map, "count", 5);

        REQUIRE(write_file(map, test_file));

        // Read back and verify
        YAMLNodeHandle loaded = parse_file(test_file);
        REQUIRE(loaded != nullptr);
        REQUIRE(has_key(loaded, "test"));
        REQUIRE(has_key(loaded, "count"));

        delete_node(map);
        delete_node(loaded);
        cleanup_file(test_file);
    }

    SECTION("Write with formatting") {
        YAMLNodeHandle seq = parse_string("[a, b, c]");

        REQUIRE(write_file_formatted(seq, test_file, 4, false, true));

        std::string content = read_file(test_file);
        REQUIRE(content.find("[a, b, c]") != std::string::npos);  // Flow style

        delete_node(seq);
        cleanup_file(test_file);
    }
}

// ===========================================
// TEST SUITE: String Conversion
// ===========================================

TEST_CASE("YAML nodes can be converted to strings", "[conversion][string]") {
    SECTION("Convert map to string") {
        YAMLNodeHandle map = create_map();
        set_value_string(map, "key", "value");

        char* str = yaml_to_string(map);
        REQUIRE(str != nullptr);
        REQUIRE(std::string(str).find("key") != std::string::npos);
        REQUIRE(std::string(str).find("value") != std::string::npos);

        yaml_free_string(str);
        delete_node(map);
    }

    SECTION("Emit with custom indent") {
        YAMLNodeHandle map = create_map();
        set_value_string(map, "test", "val");

        char* str2 = yaml_emit(map, 2);
        char* str4 = yaml_emit(map, 4);

        REQUIRE(str2 != nullptr);
        REQUIRE(str4 != nullptr);

        yaml_free_string(str2);
        yaml_free_string(str4);
        delete_node(map);
    }
}

// ===========================================
// TEST SUITE: Cloning
// ===========================================

TEST_CASE("YAML nodes can be cloned", "[clone]") {
    SECTION("Clone simple map") {
        YAMLNodeHandle original = create_map();
        set_value_string(original, "name", "original");

        YAMLNodeHandle clone = yaml_clone(original);
        REQUIRE(clone != nullptr);

        // Modify clone
        set_value_string(clone, "name", "modified");

        // Verify original unchanged
        YAMLNodeHandle orig_val = get_key(original, "name");
        char* orig_str = as_string(orig_val);
        REQUIRE(std::string(orig_str) == "original");

        // Verify clone changed
        YAMLNodeHandle clone_val = get_key(clone, "name");
        char* clone_str = as_string(clone_val);
        REQUIRE(std::string(clone_str) == "modified");

        yaml_free_string(orig_str);
        yaml_free_string(clone_str);
        delete_node(orig_val);
        delete_node(clone_val);
        delete_node(original);
        delete_node(clone);
    }

    SECTION("Clone nested structure") {
        YAMLNodeHandle original = parse_string("outer: {inner: value}");
        YAMLNodeHandle clone = yaml_clone(original);

        REQUIRE(clone != nullptr);
        REQUIRE(is_map(clone));

        YAMLNodeHandle outer = get_key(clone, "outer");
        REQUIRE(has_key(outer, "inner"));

        delete_node(outer);
        delete_node(original);
        delete_node(clone);
    }
}

// ===========================================
// TEST SUITE: Memory Safety
// ===========================================

TEST_CASE("Memory is properly managed", "[memory]") {
    SECTION("Can safely delete nullptr") {
        delete_node(nullptr);  // Should not crash
    }

    SECTION("Multiple operations don't leak") {
        for (int i = 0; i < 100; i++) {
            YAMLNodeHandle node = create_map();
            set_value_int(node, "test", i);

            char* str = yaml_to_string(node);
            yaml_free_string(str);

            delete_node(node);
        }
        // No assertion - just shouldn't crash or leak
    }

    SECTION("Complex structure cleanup") {
        YAMLNodeHandle root = create_sequence();

        for (int i = 0; i < 10; i++) {
            YAMLNodeHandle map = create_map();
            set_value_int(map, "id", i);
            push_node(root, map);
            delete_node(map);  // Safe after push_node copies
        }

        delete_node(root);
    }
}

// ===========================================
// TEST SUITE: Edge Cases
// ===========================================

TEST_CASE("Edge cases are handled correctly", "[edge_cases]") {
    SECTION("Empty map") {
        YAMLNodeHandle map = create_map();
        REQUIRE(size(map) == 0);

        int count;
        char** keys = get_keys(map, &count);
        REQUIRE(count == 0);

        delete_node(map);
    }

    SECTION("Empty sequence") {
        YAMLNodeHandle seq = create_sequence();
        REQUIRE(size(seq) == 0);

        YAMLNodeHandle item = get_index(seq, 0);
        REQUIRE(item == nullptr);

        delete_node(seq);
    }

    SECTION("Set scalar to nullptr is safe") {
        YAMLNodeHandle scalar = create_scalar();
        set_scalar_string(nullptr, "test");  // Should not crash
        set_scalar_string(scalar, nullptr);  // Should not crash
        delete_node(scalar);
    }
}
