#include <yaml-cpp/yaml.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define YAML_API extern "C" __declspec(dllexport)
#else
#define YAML_API extern "C" __attribute__((visibility("default")))
#endif

// TODO: add absolute file paths
//  for every element, add a field that points to the parent
/*
`original` is a map containing the base lattice as well as any lattices included
in the base lattice. They can be accessed using original[filename].
`included` is the base lattice but with all included files substituted in.
`expanded` is the base lattice after lattice expansion has been performed.
*/
struct lattices_int {
    YAML::Node original;
    YAML::Node included;
    YAML::Node expanded;
};

template <typename Condition>
std::vector<YAML::Node> search(YAML::Node root_node, Condition condition) {
    std::vector<YAML::Node> matches;

    if (condition(root_node)) matches.push_back(root_node);

    std::vector<YAML::Node> child_matches;
    if (root_node.IsMap()) {
        for (auto ele : root_node) {
            child_matches = search(ele.second, condition);
            matches.insert(matches.end(), child_matches.begin(),
                           child_matches.end());
        }
    } else if (root_node.IsSequence()) {
        for (int i = 0; i < root_node.size(); i++) {
            child_matches = search(root_node[i], condition);
            matches.insert(matches.end(), child_matches.begin(),
                           child_matches.end());
        }
    }
    return matches;
}

auto is_kind = [](YAML::Node node, std::string kind_type_string) {
    if (node.IsMap()) {
        for (auto ele : node) {
            if (ele.second.IsMap() &&
                ele.second["kind"].as<std::string>("") == kind_type_string) {
                return true;
            }
        }
    }
    return false;
};

std::vector<YAML::Node> search_kind(YAML::Node root,
                                    std::string kind_type_string) {
    auto condition_wrapper = [kind_type_string](const YAML::Node& node) {
        return is_kind(node, kind_type_string);
    };
    return search(root, condition_wrapper);
}

/*
Recursively loops through the node to record all the lattices and beamlines and
their corresponding parameters.
*/
void get_dict_helper(YAML::Node node,
                     std::map<std::string, YAML::Node>* element_map) {
    if (node.IsSequence()) {
        for (size_t i = 0; i < node.size(); i++) {
            YAML::Node child = node[i];
            get_dict_helper(child, element_map);
        }
    } else if (node.IsMap()) {
        for (auto ele : node) {
            if (ele.second.IsMap() && ele.second["kind"]) {
                element_map->insert({ele.first.as<std::string>(), ele.second});
            } else {
                get_dict_helper(ele.second, element_map);
            }
        }
    }
}

/*
Constructs the map from names to lattice elements. The values in the map
are references to lattice elements with a given name, and modifying them
will directly modifty the lattice. Elements in values are stored in the order
that they appear in the lattice file. Use YAML::Clone if only the information
is required.
*/
std::map<std::string, YAML::Node>* get_dict(YAML::Node root) {
    std::map<std::string, YAML::Node>* element_map =
        new std::map<std::string, YAML::Node>();
    get_dict_helper(root, element_map);
    return element_map;
}

/*
Adds the file contained in `filename` to `original`, which should be
lat.original. The key is the filename and the value is the contents of the file.
*/
void add_to_original(YAML::Node original, std::string filename) {
    if (!original[filename]) {
        YAML::Node node = YAML::Node(YAML::NodeType::Map);
        node["path"] = "";
        node["info"] = YAML::LoadFile("../lattice_files/" + filename);
        original[filename] = node;
    }
}

/*
Constructs the original lattice.
*/
YAML::Node original_lattice(std::string filename) {
    YAML::Node original = YAML::Node(YAML::NodeType::Map);

    std::vector<std::string> files_to_process;
    files_to_process.push_back(filename);

    auto find_includes = [](const YAML::Node& node) {
        return node.IsMap() && node["include"];
    };

    while (!files_to_process.empty()) {
        std::string current_file = files_to_process.back();
        files_to_process.pop_back();

        if (original[current_file]) {
            continue;
        }

        add_to_original(original, current_file);
        YAML::Node loaded_content = original[current_file]["info"];
        std::vector<YAML::Node> includes_found =
            search(loaded_content, find_includes);
        for (const auto& inc_node : includes_found) {
            std::string inc_fn = inc_node["include"].as<std::string>();
            if (inc_fn.size() >= 5 &&
                inc_fn.substr(inc_fn.size() - 5) == ".yaml") {
                files_to_process.push_back(inc_fn);
            }
        }
    }
    return original;
}

/*
Constructs the included lattice.
*/
YAML::Node included_lattice(std::string filename) {
    YAML::Node included = YAML::LoadFile("../lattice_files/" + filename);
    std::vector<YAML::Node> include_files =
        search(included,
               [](YAML::Node node) { return node.IsMap() && node["include"]; });
    for (int i = 0; i < include_files.size(); i++) {
        std::string inc_fn = include_files[i]["include"].as<std::string>();
        if (inc_fn.size() >= 5 && inc_fn.substr(inc_fn.size() - 5) == ".yaml") {
            YAML::Node node(YAML::NodeType::Sequence);
            YAML::Node content = included_lattice(inc_fn);
            YAML::Node file(YAML::NodeType::Map);
            file["file"] = inc_fn;

            node.push_back(file);
            node.push_back(content[0]);

            include_files[i].remove("include");
            include_files[i]["included"] = node;
        }
    }
    return included;
}

/*
Returns a new node with the elements in `line` inserted into `seq` at `index`
a `repeat` number of times. The value at index will be rewritten by the first
inserted element. For example, if seq = [1,2,3,4,5], line = [a,b], index = 2,
repeat = 3, calling the function will output [1,2,a,b,a,b,a,b,4,5].
*/
YAML::Node repeat(YAML::Node target_element, YAML::Node seq, int index,
                  int repeat_count) {
    YAML::Node exp = YAML::Node(YAML::NodeType::Sequence);
    for (int i = 0; i < index; i++) {
        exp.push_back(YAML::Clone(seq[i]));
    }

    YAML::Node inner_content = target_element;
    if (inner_content.IsMap() && inner_content["line"]) {
        inner_content = inner_content["line"];
    }

    for (int i = 0; i < repeat_count; i++) {
        if (inner_content.IsSequence()) {
            for (std::size_t j = 0; j < inner_content.size(); j++) {
                exp.push_back(YAML::Clone(inner_content[j]));
            }
        } else {
            exp.push_back(YAML::Clone(inner_content));
        }
    }

    for (std::size_t i = index + 1; i < seq.size(); i++) {
        exp.push_back(YAML::Clone(seq[i]));
    }
    return exp;
}

/*
Performs lattice expansion on the provided `node`.
*/
YAML::Node expand_internal(YAML::Node node,
                           std::map<std::string, YAML::Node>* elements_map) {
    if (node.IsScalar() && elements_map->count(node.as<std::string>())) {
        std::string element_name = node.as<std::string>();
        YAML::Node wrapped_node = YAML::Node(YAML::NodeType::Map);

        wrapped_node[element_name] = expand_internal(
            YAML::Clone(elements_map->at(element_name)), elements_map);
        return wrapped_node;
    } else if (node.IsSequence()) {
        for (std::size_t i = 0; i < node.size(); i++) {
            if (node[i].IsMap()) {
                for (auto ele : node[i]) {
                    if (ele.second.IsMap() && ele.second["repeat"]) {
                        std::string target_name = ele.first.as<std::string>();
                        if (elements_map->count(target_name)) {
                            YAML::Node target_node =
                                YAML::Clone(elements_map->at(target_name));
                            YAML::Node repeated_seq =
                                repeat(target_node, node, i,
                                       ele.second["repeat"].as<int>());
                            return expand_internal(repeated_seq, elements_map);
                        }
                    }
                }
            }
        }
        YAML::Node new_seq = YAML::Node(YAML::NodeType::Sequence);
        for (std::size_t i = 0; i < node.size(); i++) {
            new_seq.push_back(expand_internal(node[i], elements_map));
        }
        return new_seq;
    } else if (node.IsMap()) {
        YAML::Node new_map = YAML::Clone(node);

        if (new_map["inherit"]) {
            std::string parent_name = new_map["inherit"].as<std::string>();
            new_map.remove("inherit");
            new_map["inherited"] = parent_name;

            if (elements_map->count(parent_name)) {
                YAML::Node parent = elements_map->at(parent_name);
                for (auto elep : parent) {
                    std::string key = elep.first.as<std::string>();
                    if (!new_map[key]) {
                        new_map[key] = YAML::Clone(elep.second);
                    }
                }
            }
        }

        YAML::Node final_map = YAML::Node(YAML::NodeType::Map);
        for (auto ele : new_map) {
            final_map[ele.first.as<std::string>()] =
                expand_internal(ele.second, elements_map);
        }
        return final_map;
    }

    return YAML::Clone(node);
}

/*
Performs lattice expansion with the following rules:
1. If name is specified, the lattice in `root` with the given name will be
expanded.
2. If no name is specified, the lattice that appears latest in `root` will be
expanded.
*/
void find_and_replace(std::string name, YAML::Node root,
                      std::map<std::string, YAML::Node>* elements_map) {
    if (name != "") {
        for (std::size_t i = 0; i < root.size(); i++) {
            if (root[i].IsMap() && root[i][name]) {
                root[i][name] = expand_internal(root[i][name], elements_map);
                return;
            }
        }
    } else {
        for (int i = root.size() - 1; i >= 0; i--) {
            if (root[i].IsMap() && root[i]["use"]) {
                std::string target = root[i]["use"].as<std::string>();

                for (std::size_t j = 0; j < root.size(); j++) {
                    if (root[j].IsMap() && root[j][target]) {
                        root[j][target] =
                            expand_internal(root[j][target], elements_map);
                        return;
                    }
                }
            }
        }
        for (int i = root.size() - 1; i >= 0; i--) {
            if (is_kind(root[i], "Lattice")) {
                root[i] = expand_internal(root[i], elements_map);
                return;
            }
        }
    }
}

/*
Constructs the expanded lattice. Expanding has the following priority:
1. If a lattice name is supplied through lattice_name, that lattice will be
expanded.
2. If no lattice_name is supplied, the lattice specified by the last use
statement will be expanded.
3. If no lattice_name is supplied and no use statements are present, the lattice
that occurs latest in the file will be expanded.
*/
YAML::Node expanded_lattice(std::string filename, std::string lattice_name) {
    YAML::Node root = YAML::LoadFile("../lattice_files/" + filename);
    root = included_lattice(filename);
    std::map<std::string, YAML::Node>* elements_map = get_dict(root);

    find_and_replace(lattice_name, root, elements_map);
    return root;
}

struct lattices_int get_lattices_int(const std::string& filename,
                                     const std::string& lattice_name = "") {
    struct lattices_int lat;
    lat.original = original_lattice(filename);
    lat.included = included_lattice(filename);
    lat.expanded = expanded_lattice(filename, lattice_name);
    return lat;
}

extern "C" {
typedef void* YAMLNodeHandle;

struct lattices {
    YAMLNodeHandle original;
    YAMLNodeHandle included;
    YAMLNodeHandle expanded;
};

YAML_API struct lattices get_lattices(const char* filename,
                                      const char* lattice_name) {
    struct lattices lat;
    lat.original = static_cast<void*>(
        new YAML::Node(original_lattice(std::string(filename))));
    lat.included = static_cast<void*>(
        new YAML::Node(included_lattice(std::string(filename))));
    lat.expanded = static_cast<void*>(new YAML::Node(
        expanded_lattice(std::string(filename), std::string(lattice_name))));
    return lat;
}

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
}