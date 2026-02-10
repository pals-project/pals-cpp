#include <yaml-cpp/yaml.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#define YAML_API extern "C" __declspec(dllexport)
#else
#define YAML_API extern "C" __attribute__((visibility("default")))
#endif

// ======= LATTICE EXPANSION UTILS
/*
Inserts the elements in `line` into `seq` at `index` a `repeat` number of times.
The value at index will be rewritten by the first inserted element. For example,
if seq = [1,2,3,4,5], line = [a,b], index = 2, repeat = 3, calling the function
will output [1,2,a,b,a,b,a,b,4,5].
*/
YAML::Node repeat(YAML::Node line, YAML::Node seq, int index, int repeat) {
    YAML::Node exp = YAML::Node(YAML::NodeType::Sequence);
    for (int i = 0; i < index; i++) {
        exp.push_back(YAML::Clone(seq[i]));
    }
    for (int i = 0; i < repeat; i++) {
        for (int j = 0; j < line.size(); j++) {
            exp.push_back(YAML::Clone(line[j]));
        }
    }
    for (int i = index + 1; i < seq.size(); i++) {
        exp.push_back(YAML::Clone(seq[i]));
    }
    return exp;
}

// recursively loops through the node to record all the elements and their
// corresponding parameters
void get_dict_helper(YAML::Node node, std::map<std::string, YAML::Node>* seen) {
    if (node.IsSequence()) {
        for (int i = 0; i < node.size(); i++) {
            get_dict_helper(node[i], seen);
        }
    } else if (node.IsMap()) {
        for (auto ele : node) {
            // exclude key words from being stored
            if (ele.first.as<std::string>() == "include" ||
                ele.first.as<std::string>() == "inherit" ||
                (ele.second.IsMap() && ele.second["repeat"])) {
                continue;
            }
            seen->insert(
                {ele.first.as<std::string>(), YAML::Clone(ele.second)});
            get_dict_helper(ele.second, seen);
        }
    }
}

std::map<std::string, YAML::Node>* get_dict(YAML::Node root) {
    std::map<std::string, YAML::Node>* seen =
        new std::map<std::string, YAML::Node>();
    ;
    get_dict_helper(root, seen);
    return seen;
}

// === EXPAND ===
YAML::Node expand_internal(YAML::Node node,
                           std::map<std::string, YAML::Node>* seen) {
    if (node.IsSequence()) {
        bool modified = true;
        while (modified) {
            modified = false;
            for (int i = 0; i < node.size(); i++) {
                if (node[i].IsMap()) {
                    for (auto ele : node[i]) {
                        if (ele.second.IsMap() && ele.second["repeat"]) {
                            YAML::Node line =
                                (*seen)[ele.first.as<std::string>()]["line"];
                            node = repeat(line, node, i,
                                          ele.second["repeat"].as<int>());
                            modified = true;
                            break;
                        }
                    }
                    if (modified) break;
                }
            }
        }
        for (int i = 0; i < node.size(); i++) {
            node[i] = expand_internal(node[i], seen);
        }
        return node;
    } else if (node.IsMap()) {
        for (auto ele : node) {
            // will probably need to add better method of finding files
            if (ele.first.as<std::string>() == "include") {
                std::string filename = ele.second.as<std::string>();
                YAML::Node seq = YAML::Node(YAML::NodeType::Sequence);
                YAML::Node file = YAML::Node(YAML::NodeType::Map);
                file["file"] = filename;
                seq.push_back(file);
                seq.push_back(
                    YAML::LoadFile("../lattice_files/" + filename)[0]);

                ele.first = "included";
                ele.second = seq;
                break;
            } else if (ele.first.as<std::string>() == "inherit") {
                YAML::Node parent = (*seen)[ele.second.as<std::string>()];
                for (auto ele : parent) {
                    node[ele.first.as<std::string>()] = ele.second;
                }
                ele.first = "inherited";
            }
            node[ele.first.as<std::string>()] =
                expand_internal(ele.second, seen);
        }
        return node;
    } else {
        return node;
    }
}

extern "C" {
typedef void* YAMLNodeHandle;

// === CREATION/DELETION ===
YAML_API YAMLNodeHandle create_node() { return new YAML::Node(); }

YAML_API YAMLNodeHandle create_map() {
    auto node = new YAML::Node();
    *node = YAML::Node(YAML::NodeType::Map);
    return node;
}

YAML_API YAMLNodeHandle create_sequence() {
    auto node = new YAML::Node();
    *node = YAML::Node(YAML::NodeType::Sequence);
    return node;
}

YAML_API YAMLNodeHandle create_scalar() {
    auto node = new YAML::Node();
    *node = YAML::Node(YAML::NodeType::Scalar);
    return node;
}

YAML_API void delete_node(YAMLNodeHandle handle) {
    delete static_cast<YAML::Node*>(handle);
}

// === PARSING ===
YAML_API YAMLNodeHandle parse_string(const char* yaml_str) {
    try {
        return new YAML::Node(YAML::Load(yaml_str));
    } catch (...) {
        return nullptr;
    }
}

YAML_API YAMLNodeHandle parse_file(const char* filename) {
    try {
        return new YAML::Node(YAML::LoadFile(filename));
    } catch (...) {
        return nullptr;
    }
}

// === TYPE CHECKS ===
YAML_API bool is_scalar(YAMLNodeHandle handle) {
    return static_cast<YAML::Node*>(handle)->IsScalar();
}

YAML_API bool is_sequence(YAMLNodeHandle handle) {
    return static_cast<YAML::Node*>(handle)->IsSequence();
}

YAML_API bool is_map(YAMLNodeHandle handle) {
    return static_cast<YAML::Node*>(handle)->IsMap();
}

YAML_API bool is_null(YAMLNodeHandle handle) {
    return static_cast<YAML::Node*>(handle)->IsNull();
}

// === ACCESS ===
// equivalent to map[key]
YAML_API YAMLNodeHandle get_key(YAMLNodeHandle handle, const char* key) {
    auto node = static_cast<YAML::Node*>(handle);
    auto child = (*node)[key];
    if (!child.IsDefined()) {
        return nullptr;
    }
    return new YAML::Node(child);
}

YAML_API YAMLNodeHandle get_index(YAMLNodeHandle handle, int index) {
    auto node = static_cast<YAML::Node*>(handle);
    if (index < 0 || index >= node->size()) {
        return nullptr;
    }
    return new YAML::Node((*node)[index]);
}

YAML_API bool has_key(YAMLNodeHandle handle, const char* key) {
    auto node = static_cast<YAML::Node*>(handle);
    return (*node)[key].IsDefined();
}

YAML_API int size(YAMLNodeHandle handle) {
    return static_cast<YAML::Node*>(handle)->size();
}

YAML_API char** get_keys(YAMLNodeHandle handle, int* out_count) {
    auto node = static_cast<YAML::Node*>(handle);
    if (!node->IsMap()) {
        *out_count = 0;
        return nullptr;
    }

    std::vector<std::string> keys;
    for (auto it = node->begin(); it != node->end(); ++it) {
        keys.push_back(it->first.as<std::string>());
    }

    *out_count = keys.size();
    char** result = new char*[keys.size()];
    for (size_t i = 0; i < keys.size(); i++) {
        result[i] = new char[keys[i].length() + 1];
        strcpy(result[i], keys[i].c_str());
    }
    return result;
}

// === CONVERT TO C TYPES (caller must free returned strings) ===
YAML_API char* as_string(YAMLNodeHandle handle) {
    try {
        auto str = static_cast<YAML::Node*>(handle)->as<std::string>();
        char* result = new char[str.length() + 1];
        strcpy(result, str.c_str());
        return result;
    } catch (...) {
        return nullptr;
    }
}

YAML_API int as_int(YAMLNodeHandle handle) {
    try {
        return static_cast<YAML::Node*>(handle)->as<int>();
    } catch (...) {
        return 0;
    }
}

YAML_API double as_float(YAMLNodeHandle handle) {
    try {
        return static_cast<YAML::Node*>(handle)->as<double>();
    } catch (...) {
        return 0.0;
    }
}

YAML_API bool as_bool(YAMLNodeHandle handle) {
    try {
        return static_cast<YAML::Node*>(handle)->as<bool>();
    } catch (...) {
        return false;
    }
}

// === MODIFICATION ===
YAML_API void set_value_string(YAMLNodeHandle handle, const char* key,
                               const char* value) {
    auto node = static_cast<YAML::Node*>(handle);
    (*node)[key] = value;
}

YAML_API void set_value_int(YAMLNodeHandle handle, const char* key, int value) {
    auto node = static_cast<YAML::Node*>(handle);
    (*node)[key] = value;
}

YAML_API void set_value_float(YAMLNodeHandle handle, const char* key,
                              double value) {
    auto node = static_cast<YAML::Node*>(handle);
    (*node)[key] = value;
}

YAML_API void set_value_bool(YAMLNodeHandle handle, const char* key,
                             bool value) {
    auto node = static_cast<YAML::Node*>(handle);
    (*node)[key] = value;
}

YAML_API void set_value_node(YAMLNodeHandle handle, const char* key,
                             YAMLNodeHandle value) {
    auto node = static_cast<YAML::Node*>(handle);
    auto val_node = static_cast<YAML::Node*>(value);
    (*node)[key] = *val_node;
}

YAML_API void set_scalar_string(YAMLNodeHandle handle, const char* value) {
    if (handle == nullptr || value == nullptr) return;
    auto node = static_cast<YAML::Node*>(handle);
    *node = value;
}

YAML_API void set_scalar_int(YAMLNodeHandle handle, int value) {
    if (handle == nullptr) return;
    auto node = static_cast<YAML::Node*>(handle);
    *node = value;
}

YAML_API void set_scalar_float(YAMLNodeHandle handle, double value) {
    if (handle == nullptr) return;
    auto node = static_cast<YAML::Node*>(handle);
    *node = value;
}

YAML_API void set_scalar_bool(YAMLNodeHandle handle, bool value) {
    if (handle == nullptr) return;
    auto node = static_cast<YAML::Node*>(handle);
    *node = value;
}

// Set node at index for sequences
YAML_API void set_at_index(YAMLNodeHandle handle, int index,
                           YAMLNodeHandle value) {
    auto node = static_cast<YAML::Node*>(handle);
    auto val_node = static_cast<YAML::Node*>(value);
    (*node)[index] = *val_node;
}

YAML_API void push_string(YAMLNodeHandle handle, const char* value) {
    auto node = static_cast<YAML::Node*>(handle);
    node->push_back(value);
}

YAML_API void push_int(YAMLNodeHandle handle, int value) {
    auto node = static_cast<YAML::Node*>(handle);
    node->push_back(value);
}

YAML_API void push_float(YAMLNodeHandle handle, double value) {
    auto node = static_cast<YAML::Node*>(handle);
    node->push_back(value);
}

YAML_API void push_node(YAMLNodeHandle handle, YAMLNodeHandle value) {
    auto node = static_cast<YAML::Node*>(handle);
    auto val_node = static_cast<YAML::Node*>(value);
    node->push_back(*val_node);
}

// === WRITE TO FILE WITH EMITTER ===

YAML_API bool write_file(YAMLNodeHandle handle, const char* filename) {
    try {
        auto node = static_cast<YAML::Node*>(handle);

        std::ofstream fout(filename);
        if (!fout.is_open()) {
            return false;
        }

        YAML::Emitter out;
        out << *node;

        fout << out.c_str();
        fout.close();
        return true;
    } catch (...) {
        return false;
    }
}

// Write with emitter control (removed SetWrap)
YAML_API bool write_file_formatted(YAMLNodeHandle handle, const char* filename,
                                   int indent, bool flow_maps, bool flow_seqs) {
    try {
        auto node = static_cast<YAML::Node*>(handle);

        std::ofstream fout(filename);
        if (!fout.is_open()) {
            return false;
        }

        YAML::Emitter out;

        // Set formatting options (only valid ones)
        out.SetIndent(indent);
        out.SetMapFormat(flow_maps ? YAML::Flow : YAML::Block);
        out.SetSeqFormat(flow_seqs ? YAML::Flow : YAML::Block);
        out.SetBoolFormat(YAML::TrueFalseBool);  // true/false instead of yes/no
        out.SetNullFormat(YAML::LowerNull);      // null instead of ~
        out.SetStringFormat(YAML::Auto);         // Auto-detect if quotes needed

        out << *node;

        fout << out.c_str();
        fout.close();
        return true;
    } catch (...) {
        return false;
    }
}

// Get YAML string with emitter
YAML_API char* yaml_emit(YAMLNodeHandle handle, int indent) {
    try {
        auto node = static_cast<YAML::Node*>(handle);

        YAML::Emitter out;
        out.SetIndent(indent);
        out.SetBoolFormat(YAML::TrueFalseBool);
        out.SetNullFormat(YAML::LowerNull);
        out << *node;

        std::string str = out.c_str();
        char* result = new char[str.length() + 1];
        strcpy(result, str.c_str());
        return result;
    } catch (...) {
        return nullptr;
    }
}

// Advanced version with all available options
YAML_API bool write_file_advanced(
    YAMLNodeHandle handle, const char* filename, int indent, bool flow_maps,
    bool flow_seqs,
    int bool_format,      // 0=YesNo, 1=TrueFalse, 2=OnOff
    int null_format,      // 0=Tilde (~), 1=Null (null), 2=NULL, 3=Null
    int string_format) {  // 0=Auto, 1=SingleQuoted, 2=DoubleQuoted, 3=Literal
    try {
        auto node = static_cast<YAML::Node*>(handle);

        std::ofstream fout(filename);
        if (!fout.is_open()) {
            return false;
        }

        YAML::Emitter out;
        out.SetIndent(indent);
        out.SetMapFormat(flow_maps ? YAML::Flow : YAML::Block);
        out.SetSeqFormat(flow_seqs ? YAML::Flow : YAML::Block);

        // Boolean format
        switch (bool_format) {
            case 0:
                out.SetBoolFormat(YAML::YesNoBool);
                break;
            case 1:
                out.SetBoolFormat(YAML::TrueFalseBool);
                break;
            case 2:
                out.SetBoolFormat(YAML::OnOffBool);
                break;
            default:
                out.SetBoolFormat(YAML::TrueFalseBool);
        }

        // Null format
        switch (null_format) {
            case 0:
                out.SetNullFormat(YAML::TildeNull);
                break;  // ~
            case 1:
                out.SetNullFormat(YAML::LowerNull);
                break;  // null
            case 2:
                out.SetNullFormat(YAML::UpperNull);
                break;  // NULL
            case 3:
                out.SetNullFormat(YAML::CamelNull);
                break;  // Null
            default:
                out.SetNullFormat(YAML::LowerNull);
        }

        // String format
        switch (string_format) {
            case 0:
                out.SetStringFormat(YAML::Auto);
                break;
            case 1:
                out.SetStringFormat(YAML::SingleQuoted);
                break;
            case 2:
                out.SetStringFormat(YAML::DoubleQuoted);
                break;
            case 3:
                out.SetStringFormat(YAML::Literal);
                break;
            default:
                out.SetStringFormat(YAML::Auto);
        }

        out << *node;

        fout << out.c_str();
        fout.close();
        return true;
    } catch (...) {
        return false;
    }
}

// === UTILITY ===
YAML_API char* yaml_to_string(YAMLNodeHandle handle) {
    try {
        YAML::Emitter out;
        out << *static_cast<YAML::Node*>(handle);
        std::string str = out.c_str();
        char* result = new char[str.length() + 1];
        strcpy(result, str.c_str());
        return result;
    } catch (...) {
        return nullptr;
    }
}

// Helper to free strings allocated by this library
YAML_API void yaml_free_string(char* str) { delete[] str; }

YAML_API void yaml_free_keys(char** keys, int count) {
    for (int i = 0; i < count; i++) {
        delete[] keys[i];
    }
    delete[] keys;
}

YAML_API YAMLNodeHandle yaml_clone(YAMLNodeHandle handle) {
    return new YAML::Node(YAML::Clone(*static_cast<YAML::Node*>(handle)));
}

// C interface for expand - handles the map internally
YAML_API YAMLNodeHandle lattice_expand(YAMLNodeHandle handle) {
    auto node = static_cast<YAML::Node*>(handle);
    std::map<std::string, YAML::Node>* seen = get_dict(*node);
    YAML::Node result = expand_internal(*node, seen);
    return new YAML::Node(result);
}
}