#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/yaml_c_wrapper.h"
#include <fstream>
#include <string>
#include <cstring>

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
        YAMLNodeHandle node = yaml_create_node();
        REQUIRE(node != nullptr);
        yaml_delete_node(node);
    }
    
    SECTION("Create map node") {
        YAMLNodeHandle map = yaml_create_map();
        REQUIRE(map != nullptr);
        REQUIRE(yaml_is_map(map));
        yaml_delete_node(map);
    }
    
    SECTION("Create sequence node") {
        YAMLNodeHandle seq = yaml_create_sequence();
        REQUIRE(seq != nullptr);
        REQUIRE(yaml_is_sequence(seq));
        yaml_delete_node(seq);
    }
    
    SECTION("Create scalar node") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        REQUIRE(scalar != nullptr);
        REQUIRE(yaml_is_scalar(scalar));
        yaml_delete_node(scalar);
    }
}

// ===========================================
// TEST SUITE: Parsing
// ===========================================

TEST_CASE("YAML can be parsed from strings", "[parsing]") {
    SECTION("Parse simple map") {
        const char* yaml = "key: value";
        YAMLNodeHandle node = yaml_parse(yaml);
        
        REQUIRE(node != nullptr);
        REQUIRE(yaml_is_map(node));
        REQUIRE(yaml_has_key(node, "key"));
        
        yaml_delete_node(node);
    }
    
    SECTION("Parse sequence") {
        const char* yaml = "[a, b, c]";
        YAMLNodeHandle node = yaml_parse(yaml);
        
        REQUIRE(node != nullptr);
        REQUIRE(yaml_is_sequence(node));
        REQUIRE(yaml_size(node) == 3);
        
        yaml_delete_node(node);
    }
    
    SECTION("Parse invalid YAML returns nullptr") {
        const char* invalid = "invalid: yaml: :";
        YAMLNodeHandle node = yaml_parse(invalid);
        
        REQUIRE(node == nullptr);
    }
}

TEST_CASE("YAML can be parsed from files", "[parsing][file]") {
    SECTION("Parse valid file") {
        YAMLNodeHandle node = yaml_parse_file("../lattice_files/ex.yaml");
        
        REQUIRE(node != nullptr);
        REQUIRE(yaml_is_sequence(node));
        REQUIRE(yaml_size(node) >= 2);
        
        yaml_delete_node(node);
    }
    
    SECTION("Parse non-existent file returns nullptr") {
        YAMLNodeHandle node = yaml_parse_file("nonexistent.yaml");
        REQUIRE(node == nullptr);
    }
}

// ===========================================
// TEST SUITE: Type Checking
// ===========================================

TEST_CASE("Node types can be checked", "[types]") {
    SECTION("Check scalar type") {
        YAMLNodeHandle node = yaml_parse("value");
        REQUIRE(yaml_is_scalar(node));
        REQUIRE_FALSE(yaml_is_map(node));
        REQUIRE_FALSE(yaml_is_sequence(node));
        yaml_delete_node(node);
    }
    
    SECTION("Check map type") {
        YAMLNodeHandle node = yaml_parse("key: value");
        REQUIRE(yaml_is_map(node));
        REQUIRE_FALSE(yaml_is_scalar(node));
        REQUIRE_FALSE(yaml_is_sequence(node));
        yaml_delete_node(node);
    }
    
    SECTION("Check sequence type") {
        YAMLNodeHandle node = yaml_parse("[a, b, c]");
        REQUIRE(yaml_is_sequence(node));
        REQUIRE_FALSE(yaml_is_map(node));
        REQUIRE_FALSE(yaml_is_scalar(node));
        yaml_delete_node(node);
    }
    
    SECTION("Check null type") {
        YAMLNodeHandle node = yaml_parse("null");
        REQUIRE(yaml_is_null(node));
        yaml_delete_node(node);
    }
}

// ===========================================
// TEST SUITE: Access Operations
// ===========================================

TEST_CASE("Map keys can be accessed", "[access][map]") {
    const char* yaml = "name: test\nvalue: 42";
    YAMLNodeHandle node = yaml_parse(yaml);
    
    SECTION("Check key existence") {
        REQUIRE(yaml_has_key(node, "name"));
        REQUIRE(yaml_has_key(node, "value"));
        REQUIRE_FALSE(yaml_has_key(node, "nonexistent"));
    }
    
    SECTION("Get key value") {
        YAMLNodeHandle name = yaml_get_key(node, "name");
        REQUIRE(name != nullptr);
        
        char* str = yaml_as_string(name);
        REQUIRE(std::string(str) == "test");
        
        yaml_free_string(str);
        yaml_delete_node(name);
    }
    
    SECTION("Get non-existent key returns nullptr") {
        YAMLNodeHandle missing = yaml_get_key(node, "missing");
        REQUIRE(missing == nullptr);
    }
    
    SECTION("Get all keys") {
        int count;
        char** keys = yaml_get_keys(node, &count);
        
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
    
    yaml_delete_node(node);
}

TEST_CASE("Sequence indices can be accessed", "[access][sequence]") {
    const char* yaml = "[apple, banana, cherry]";
    YAMLNodeHandle node = yaml_parse(yaml);
    
    SECTION("Check size") {
        REQUIRE(yaml_size(node) == 3);
    }
    
    SECTION("Access valid index") {
        YAMLNodeHandle item = yaml_get_index(node, 1);
        REQUIRE(item != nullptr);
        
        char* str = yaml_as_string(item);
        REQUIRE(std::string(str) == "banana");
        
        yaml_free_string(str);
        yaml_delete_node(item);
    }
    
    SECTION("Access out of bounds index returns nullptr") {
        YAMLNodeHandle item = yaml_get_index(node, 999);
        REQUIRE(item == nullptr);
    }
    
    SECTION("Access negative index returns nullptr") {
        YAMLNodeHandle item = yaml_get_index(node, -1);
        REQUIRE(item == nullptr);
    }
    
    yaml_delete_node(node);
}

// ===========================================
// TEST SUITE: Type Conversions
// ===========================================

TEST_CASE("Values can be converted to C types", "[conversion]") {
    SECTION("Convert to string") {
        YAMLNodeHandle node = yaml_parse("test_value");
        char* str = yaml_as_string(node);
        
        REQUIRE(str != nullptr);
        REQUIRE(std::string(str) == "test_value");
        
        yaml_free_string(str);
        yaml_delete_node(node);
    }
    
    SECTION("Convert to int") {
        YAMLNodeHandle node = yaml_parse("42");
        int val = yaml_as_int(node);
        
        REQUIRE(val == 42);
        
        yaml_delete_node(node);
    }
    
    SECTION("Convert to float") {
        YAMLNodeHandle node = yaml_parse("3.14");
        double val = yaml_as_float(node);
        
        REQUIRE(val == Approx(3.14));
        
        yaml_delete_node(node);
    }
    
    SECTION("Convert to bool") {
        YAMLNodeHandle node_true = yaml_parse("true");
        YAMLNodeHandle node_false = yaml_parse("false");
        
        REQUIRE(yaml_as_bool(node_true) == true);
        REQUIRE(yaml_as_bool(node_false) == false);
        
        yaml_delete_node(node_true);
        yaml_delete_node(node_false);
    }
    
    SECTION("Invalid conversion returns default") {
        YAMLNodeHandle node = yaml_parse("[a, b, c]");
        
        // Can't convert sequence to string
        char* str = yaml_as_string(node);
        REQUIRE(str == nullptr);
        
        yaml_delete_node(node);
    }
}

// ===========================================
// TEST SUITE: Modification - Maps
// ===========================================

TEST_CASE("Map values can be set", "[modification][map]") {
    YAMLNodeHandle map = yaml_create_map();
    
    SECTION("Set string value") {
        yaml_set_string(map, "name", "test");
        REQUIRE(yaml_has_key(map, "name"));
        
        YAMLNodeHandle value = yaml_get_key(map, "name");
        char* str = yaml_as_string(value);
        REQUIRE(std::string(str) == "test");
        
        yaml_free_string(str);
        yaml_delete_node(value);
    }
    
    SECTION("Set int value") {
        yaml_set_int(map, "count", 42);
        
        YAMLNodeHandle value = yaml_get_key(map, "count");
        REQUIRE(yaml_as_int(value) == 42);
        
        yaml_delete_node(value);
    }
    
    SECTION("Set float value") {
        yaml_set_float(map, "pi", 3.14);
        
        YAMLNodeHandle value = yaml_get_key(map, "pi");
        REQUIRE(yaml_as_float(value) == Catch::Approx(3.14));
        
        yaml_delete_node(value);
    }
    
    SECTION("Set bool value") {
        yaml_set_bool(map, "enabled", true);
        
        YAMLNodeHandle value = yaml_get_key(map, "enabled");
        REQUIRE(yaml_as_bool(value) == true);
        
        yaml_delete_node(value);
    }
    
    SECTION("Set nested node") {
        YAMLNodeHandle nested = yaml_create_map();
        yaml_set_string(nested, "inner", "value");
        
        yaml_set_node(map, "nested", nested);
        
        REQUIRE(yaml_has_key(map, "nested"));
        YAMLNodeHandle retrieved = yaml_get_key(map, "nested");
        REQUIRE(yaml_is_map(retrieved));
        
        yaml_delete_node(retrieved);
        yaml_delete_node(nested);
    }
    
    yaml_delete_node(map);
}

// ===========================================
// TEST SUITE: Modification - Sequences
// ===========================================

TEST_CASE("Sequence values can be pushed", "[modification][sequence]") {
    YAMLNodeHandle seq = yaml_create_sequence();
    
    SECTION("Push string values") {
        yaml_push_string(seq, "first");
        yaml_push_string(seq, "second");
        
        REQUIRE(yaml_size(seq) == 2);
        
        YAMLNodeHandle item = yaml_get_index(seq, 1);
        char* str = yaml_as_string(item);
        REQUIRE(std::string(str) == "second");
        
        yaml_free_string(str);
        yaml_delete_node(item);
    }
    
    SECTION("Push int values") {
        yaml_push_int(seq, 10);
        yaml_push_int(seq, 20);
        
        REQUIRE(yaml_size(seq) == 2);
        
        YAMLNodeHandle item = yaml_get_index(seq, 0);
        REQUIRE(yaml_as_int(item) == 10);
        
        yaml_delete_node(item);
    }
    
    SECTION("Push float values") {
        yaml_push_float(seq, 1.1);
        yaml_push_float(seq, 2.2);
        
        REQUIRE(yaml_size(seq) == 2);
        
        YAMLNodeHandle item = yaml_get_index(seq, 1);
        REQUIRE(yaml_as_float(item) == Catch::Approx(2.2));
        
        yaml_delete_node(item);
    }
    
    SECTION("Push node") {
        YAMLNodeHandle node = yaml_create_map();
        yaml_set_string(node, "key", "value");
        
        yaml_push_node(seq, node);
        
        REQUIRE(yaml_size(seq) == 1);
        
        YAMLNodeHandle retrieved = yaml_get_index(seq, 0);
        REQUIRE(yaml_is_map(retrieved));
        
        yaml_delete_node(retrieved);
        yaml_delete_node(node);
    }
    
    yaml_delete_node(seq);
}

TEST_CASE("Sequence values can be set at index", "[modification][sequence]") {
    YAMLNodeHandle seq = yaml_parse("[a, b, c]");
    
    SECTION("Set string at index") {
        YAMLNodeHandle replacement = yaml_parse("replaced");
        yaml_set_at_index(seq, 1, replacement);
        
        YAMLNodeHandle item = yaml_get_index(seq, 1);
        char* str = yaml_as_string(item);
        REQUIRE(std::string(str) == "replaced");
        
        yaml_free_string(str);
        yaml_delete_node(item);
        yaml_delete_node(replacement);
    }
    
    yaml_delete_node(seq);
}

// ===========================================
// TEST SUITE: Scalar Editing
// ===========================================

TEST_CASE("Scalar values can be edited directly", "[modification][scalar]") {
    SECTION("Set scalar string") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        yaml_set_scalar_string(scalar, "new_value");
        
        char* str = yaml_as_string(scalar);
        REQUIRE(std::string(str) == "new_value");
        
        yaml_free_string(str);
        yaml_delete_node(scalar);
    }
    
    SECTION("Set scalar int") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        yaml_set_scalar_int(scalar, 99);
        
        REQUIRE(yaml_as_int(scalar) == 99);
        
        yaml_delete_node(scalar);
    }
    
    SECTION("Set scalar float") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        yaml_set_scalar_float(scalar, 2.718);
        
        REQUIRE(yaml_as_float(scalar) == Catch::Approx(2.718));
        
        yaml_delete_node(scalar);
    }
    
    SECTION("Set scalar bool") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        yaml_set_scalar_bool(scalar, false);
        
        REQUIRE(yaml_as_bool(scalar) == false);
        
        yaml_delete_node(scalar);
    }
}

// ===========================================
// TEST SUITE: File I/O
// ===========================================

TEST_CASE("YAML can be written to files", "[io][file]") {
    const char* test_file = "test_output.yaml";
    
    SECTION("Write simple map") {
        YAMLNodeHandle map = yaml_create_map();
        yaml_set_string(map, "test", "value");
        yaml_set_int(map, "count", 5);
        
        REQUIRE(yaml_write_file(map, test_file));
        
        // Read back and verify
        YAMLNodeHandle loaded = yaml_parse_file(test_file);
        REQUIRE(loaded != nullptr);
        REQUIRE(yaml_has_key(loaded, "test"));
        REQUIRE(yaml_has_key(loaded, "count"));
        
        yaml_delete_node(map);
        yaml_delete_node(loaded);
        cleanup_file(test_file);
    }
    
    SECTION("Write with formatting") {
        YAMLNodeHandle seq = yaml_parse("[a, b, c]");
        
        REQUIRE(yaml_write_file_formatted(seq, test_file, 4, false, true));
        
        std::string content = read_file(test_file);
        REQUIRE(content.find("[a, b, c]") != std::string::npos); // Flow style
        
        yaml_delete_node(seq);
        cleanup_file(test_file);
    }
}

// ===========================================
// TEST SUITE: String Conversion
// ===========================================

TEST_CASE("YAML nodes can be converted to strings", "[conversion][string]") {
    SECTION("Convert map to string") {
        YAMLNodeHandle map = yaml_create_map();
        yaml_set_string(map, "key", "value");
        
        char* str = yaml_to_string(map);
        REQUIRE(str != nullptr);
        REQUIRE(std::string(str).find("key") != std::string::npos);
        REQUIRE(std::string(str).find("value") != std::string::npos);
        
        yaml_free_string(str);
        yaml_delete_node(map);
    }
    
    SECTION("Emit with custom indent") {
        YAMLNodeHandle map = yaml_create_map();
        yaml_set_string(map, "test", "val");
        
        char* str2 = yaml_emit(map, 2);
        char* str4 = yaml_emit(map, 4);
        
        REQUIRE(str2 != nullptr);
        REQUIRE(str4 != nullptr);
        
        yaml_free_string(str2);
        yaml_free_string(str4);
        yaml_delete_node(map);
    }
}

// ===========================================
// TEST SUITE: Cloning
// ===========================================

TEST_CASE("YAML nodes can be cloned", "[clone]") {
    SECTION("Clone simple map") {
        YAMLNodeHandle original = yaml_create_map();
        yaml_set_string(original, "name", "original");
        
        YAMLNodeHandle clone = yaml_clone(original);
        REQUIRE(clone != nullptr);
        
        // Modify clone
        yaml_set_string(clone, "name", "modified");
        
        // Verify original unchanged
        YAMLNodeHandle orig_val = yaml_get_key(original, "name");
        char* orig_str = yaml_as_string(orig_val);
        REQUIRE(std::string(orig_str) == "original");
        
        // Verify clone changed
        YAMLNodeHandle clone_val = yaml_get_key(clone, "name");
        char* clone_str = yaml_as_string(clone_val);
        REQUIRE(std::string(clone_str) == "modified");
        
        yaml_free_string(orig_str);
        yaml_free_string(clone_str);
        yaml_delete_node(orig_val);
        yaml_delete_node(clone_val);
        yaml_delete_node(original);
        yaml_delete_node(clone);
    }
    
    SECTION("Clone nested structure") {
        YAMLNodeHandle original = yaml_parse("outer: {inner: value}");
        YAMLNodeHandle clone = yaml_clone(original);
        
        REQUIRE(clone != nullptr);
        REQUIRE(yaml_is_map(clone));
        
        YAMLNodeHandle outer = yaml_get_key(clone, "outer");
        REQUIRE(yaml_has_key(outer, "inner"));
        
        yaml_delete_node(outer);
        yaml_delete_node(original);
        yaml_delete_node(clone);
    }
}

// ===========================================
// TEST SUITE: ex.yaml Structure Tests
// ===========================================

TEST_CASE("ex.yaml has expected structure", "[ex.yaml][structure]") {
    YAMLNodeHandle root = yaml_parse_file("../lattice_files/ex.yaml");
    REQUIRE(root != nullptr);
    
    SECTION("Root is a sequence") {
        REQUIRE(yaml_is_sequence(root));
        REQUIRE(yaml_size(root) >= 2);
    }
    
    SECTION("First element has thingB") {
        YAMLNodeHandle first = yaml_get_index(root, 0);
        REQUIRE(first != nullptr);
        REQUIRE(yaml_is_map(first));
        REQUIRE(yaml_has_key(first, "thingB"));
        
        YAMLNodeHandle thingB = yaml_get_key(first, "thingB");
        REQUIRE(yaml_is_map(thingB));
        REQUIRE(yaml_has_key(thingB, "kind"));
        
        YAMLNodeHandle kind = yaml_get_key(thingB, "kind");
        char* kind_str = yaml_as_string(kind);
        REQUIRE(std::string(kind_str) == "Sextupole");
        
        yaml_free_string(kind_str);
        yaml_delete_node(kind);
        yaml_delete_node(thingB);
        yaml_delete_node(first);
    }
    
    SECTION("Second element has inj_line") {
        YAMLNodeHandle second = yaml_get_index(root, 1);
        REQUIRE(second != nullptr);
        REQUIRE(yaml_is_map(second));
        REQUIRE(yaml_has_key(second, "inj_line"));
        
        YAMLNodeHandle inj_line = yaml_get_key(second, "inj_line");
        REQUIRE(yaml_is_map(inj_line));
        REQUIRE(yaml_has_key(inj_line, "kind"));
        REQUIRE(yaml_has_key(inj_line, "multipass"));
        REQUIRE(yaml_has_key(inj_line, "line"));
        
        YAMLNodeHandle kind = yaml_get_key(inj_line, "kind");
        char* kind_str = yaml_as_string(kind);
        REQUIRE(std::string(kind_str) == "BeamLine");
        
        YAMLNodeHandle multipass = yaml_get_key(inj_line, "multipass");
        REQUIRE(yaml_as_bool(multipass) == true);
        
        yaml_free_string(kind_str);
        yaml_delete_node(kind);
        yaml_delete_node(multipass);
        yaml_delete_node(inj_line);
        yaml_delete_node(second);
    }
    
    SECTION("inj_line has line sequence") {
        YAMLNodeHandle second = yaml_get_index(root, 1);
        YAMLNodeHandle inj_line = yaml_get_key(second, "inj_line");
        YAMLNodeHandle line = yaml_get_key(inj_line, "line");
        
        REQUIRE(yaml_is_sequence(line));
        REQUIRE(yaml_size(line) >= 3);
        
        yaml_delete_node(line);
        yaml_delete_node(inj_line);
        yaml_delete_node(second);
    }
    
    yaml_delete_node(root);
}

// ===========================================
// TEST SUITE: Memory Safety
// ===========================================

TEST_CASE("Memory is properly managed", "[memory]") {
    SECTION("Can safely delete nullptr") {
        yaml_delete_node(nullptr); // Should not crash
    }
    
    SECTION("Multiple operations don't leak") {
        for (int i = 0; i < 100; i++) {
            YAMLNodeHandle node = yaml_create_map();
            yaml_set_int(node, "test", i);
            
            char* str = yaml_to_string(node);
            yaml_free_string(str);
            
            yaml_delete_node(node);
        }
        // No assertion - just shouldn't crash or leak
    }
    
    SECTION("Complex structure cleanup") {
        YAMLNodeHandle root = yaml_create_sequence();
        
        for (int i = 0; i < 10; i++) {
            YAMLNodeHandle map = yaml_create_map();
            yaml_set_int(map, "id", i);
            yaml_push_node(root, map);
            yaml_delete_node(map); // Safe after push_node copies
        }
        
        yaml_delete_node(root);
    }
}

// ===========================================
// TEST SUITE: Edge Cases
// ===========================================

TEST_CASE("Edge cases are handled correctly", "[edge_cases]") {
    SECTION("Empty map") {
        YAMLNodeHandle map = yaml_create_map();
        REQUIRE(yaml_size(map) == 0);
        
        int count;
        char** keys = yaml_get_keys(map, &count);
        REQUIRE(count == 0);
        
        yaml_delete_node(map);
    }
    
    SECTION("Empty sequence") {
        YAMLNodeHandle seq = yaml_create_sequence();
        REQUIRE(yaml_size(seq) == 0);
        
        YAMLNodeHandle item = yaml_get_index(seq, 0);
        REQUIRE(item == nullptr);
        
        yaml_delete_node(seq);
    }
    
    SECTION("Set scalar to nullptr is safe") {
        YAMLNodeHandle scalar = yaml_create_scalar();
        yaml_set_scalar_string(nullptr, "test"); // Should not crash
        yaml_set_scalar_string(scalar, nullptr); // Should not crash
        yaml_delete_node(scalar);
    }
}