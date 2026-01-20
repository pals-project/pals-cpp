#include <yaml-cpp/yaml.h>
#include <string>
#include <cstring>
#include <fstream>

// ======= LATTICE EXPANSION UTILS
// === REPLACE ===
YAML::Node replace_internal(YAML::Node name, std::map<std::string, YAML::Node>* seen) {
    std::string str = name.as<std::string>();
    if (seen->count(str)) {
        return YAML::Clone(seen->at(str));
    } else {
        return name;
    }
}

// === EXPAND ===
YAML::Node expand_internal(YAML::Node node, std::map<std::string, YAML::Node>* seen) {
    if (node.IsSequence()) {
        for (int i = 0; i < node.size(); i++) {
            node[i] = expand_internal(node[i], seen);
        }
        return node;
    } else if (node.IsScalar()) {
        return replace_internal(node, seen);
    } else if (node.IsMap()) {
        for (auto ele : node) {
            seen->insert({ele.first.as<std::string>(), ele.second});
            node[ele.first.as<std::string>()] = expand_internal(ele.second, seen);
        }
        return node;
    } else {
        return node;
    }
}

extern "C" {
    typedef void* YAMLNodeHandle;
    
    // === CREATION/DELETION ===
    YAMLNodeHandle yaml_create_node() {
        return new YAML::Node();
    }
    
    YAMLNodeHandle yaml_create_map() {
        auto node = new YAML::Node();
        *node = YAML::Node(YAML::NodeType::Map);
        return node;
    }
    
    YAMLNodeHandle yaml_create_sequence() {
        auto node = new YAML::Node();
        *node = YAML::Node(YAML::NodeType::Sequence);
        return node;
    }
    
    YAMLNodeHandle yaml_create_scalar() {
        auto node = new YAML::Node();
        *node = YAML::Node(YAML::NodeType::Scalar);
        return node;
    }

    void yaml_delete_node(YAMLNodeHandle handle) {
        delete static_cast<YAML::Node*>(handle);
    }
    
    // === PARSING ===
    YAMLNodeHandle yaml_parse(const char* yaml_str) {
        try {
            return new YAML::Node(YAML::Load(yaml_str));
        } catch (...) {
            return nullptr;
        }
    }
    
    YAMLNodeHandle yaml_parse_file(const char* filename) {
        try {
            return new YAML::Node(YAML::LoadFile(filename));
        } catch (...) {
            return nullptr;
        }
    }
    
    // === TYPE CHECKS ===
    bool yaml_is_scalar(YAMLNodeHandle handle) {
        return static_cast<YAML::Node*>(handle)->IsScalar();
    }
    
    bool yaml_is_sequence(YAMLNodeHandle handle) {
        return static_cast<YAML::Node*>(handle)->IsSequence();
    }
    
    bool yaml_is_map(YAMLNodeHandle handle) {
        return static_cast<YAML::Node*>(handle)->IsMap();
    }
    
    bool yaml_is_null(YAMLNodeHandle handle) {
        return static_cast<YAML::Node*>(handle)->IsNull();
    }
    
    // === ACCESS ===
    YAMLNodeHandle yaml_get_key(YAMLNodeHandle handle, const char* key) {
        auto node = static_cast<YAML::Node*>(handle);
        auto child = (*node)[key];
        if (!child.IsDefined()) {
            return nullptr;
        }
        return new YAML::Node(child);
    }
    
    YAMLNodeHandle yaml_get_index(YAMLNodeHandle handle, int index) {
        auto node = static_cast<YAML::Node*>(handle);
        if (index < 0 || index >= node->size()) {
            return nullptr;
        }
        return new YAML::Node((*node)[index]);
    }
    
    bool yaml_has_key(YAMLNodeHandle handle, const char* key) {
        auto node = static_cast<YAML::Node*>(handle);
        return (*node)[key].IsDefined();
    }
    
    int yaml_size(YAMLNodeHandle handle) {
        return static_cast<YAML::Node*>(handle)->size();
    }

    // === ITERATION HELPERS ===
    
    // For iterating over maps - get all keys
    char** yaml_get_keys(YAMLNodeHandle handle, int* out_count) {
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
    
    void yaml_free_keys(char** keys, int count) {
        for (int i = 0; i < count; i++) {
            delete[] keys[i];
        }
        delete[] keys;
    }
    
    // Set node at index for sequences
    void yaml_set_at_index(YAMLNodeHandle handle, int index, YAMLNodeHandle value) {
        auto node = static_cast<YAML::Node*>(handle);
        auto val_node = static_cast<YAML::Node*>(value);
        (*node)[index] = *val_node;
    }
    
    // === CONVERT TO C TYPES (caller must free returned strings) ===
    char* yaml_as_string(YAMLNodeHandle handle) {
        try {
            auto str = static_cast<YAML::Node*>(handle)->as<std::string>();
            char* result = new char[str.length() + 1];
            strcpy(result, str.c_str());
            return result;
        } catch (...) {
            return nullptr;
        }
    }
    
    int yaml_as_int(YAMLNodeHandle handle) {
        try {
            return static_cast<YAML::Node*>(handle)->as<int>();
        } catch (...) {
            return 0;
        }
    }
    
    double yaml_as_float(YAMLNodeHandle handle) {
        try {
            return static_cast<YAML::Node*>(handle)->as<double>();
        } catch (...) {
            return 0.0;
        }
    }
    
    bool yaml_as_bool(YAMLNodeHandle handle) {
        try {
            return static_cast<YAML::Node*>(handle)->as<bool>();
        } catch (...) {
            return false;
        }
    }
    
    // === MODIFICATION ===
    void yaml_set_string(YAMLNodeHandle handle, const char* key, const char* value) {
        auto node = static_cast<YAML::Node*>(handle);
        (*node)[key] = value;
    }
    
    void yaml_set_int(YAMLNodeHandle handle, const char* key, int value) {
        auto node = static_cast<YAML::Node*>(handle);
        (*node)[key] = value;
    }
    
    void yaml_set_float(YAMLNodeHandle handle, const char* key, double value) {
        auto node = static_cast<YAML::Node*>(handle);
        (*node)[key] = value;
    }
    
    void yaml_set_bool(YAMLNodeHandle handle, const char* key, bool value) {
        auto node = static_cast<YAML::Node*>(handle);
        (*node)[key] = value;
    }
    
    void yaml_set_node(YAMLNodeHandle handle, const char* key, YAMLNodeHandle value) {
        auto node = static_cast<YAML::Node*>(handle);
        auto val_node = static_cast<YAML::Node*>(value);
        (*node)[key] = *val_node;
    }
    
    void yaml_push_string(YAMLNodeHandle handle, const char* value) {
        auto node = static_cast<YAML::Node*>(handle);
        node->push_back(value);
    }
    
    void yaml_push_int(YAMLNodeHandle handle, int value) {
        auto node = static_cast<YAML::Node*>(handle);
        node->push_back(value);
    }
    
    void yaml_push_float(YAMLNodeHandle handle, double value) {
        auto node = static_cast<YAML::Node*>(handle);
        node->push_back(value);
    }
    
    void yaml_push_node(YAMLNodeHandle handle, YAMLNodeHandle value) {
        auto node = static_cast<YAML::Node*>(handle);
        auto val_node = static_cast<YAML::Node*>(value);
        node->push_back(*val_node);
    }

    // === WRITE TO FILE WITH EMITTER ===
    
    bool yaml_write_file(YAMLNodeHandle handle, const char* filename) {
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
    bool yaml_write_file_formatted(YAMLNodeHandle handle, const char* filename,
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
            out.SetNullFormat(YAML::LowerNull);       // null instead of ~
            out.SetStringFormat(YAML::Auto);          // Auto-detect if quotes needed
            
            out << *node;
            
            fout << out.c_str();
            fout.close();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    // Get YAML string with emitter
    char* yaml_emit(YAMLNodeHandle handle, int indent) {
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
    bool yaml_write_file_advanced(YAMLNodeHandle handle, const char* filename,
                                  int indent, 
                                  bool flow_maps,
                                  bool flow_seqs,
                                  int bool_format,    // 0=YesNo, 1=TrueFalse, 2=OnOff
                                  int null_format,    // 0=Tilde (~), 1=Null (null), 2=NULL, 3=Null
                                  int string_format) { // 0=Auto, 1=SingleQuoted, 2=DoubleQuoted, 3=Literal
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
            switch(bool_format) {
                case 0: out.SetBoolFormat(YAML::YesNoBool); break;
                case 1: out.SetBoolFormat(YAML::TrueFalseBool); break;
                case 2: out.SetBoolFormat(YAML::OnOffBool); break;
                default: out.SetBoolFormat(YAML::TrueFalseBool);
            }
            
            // Null format
            switch(null_format) {
                case 0: out.SetNullFormat(YAML::TildeNull); break;    // ~
                case 1: out.SetNullFormat(YAML::LowerNull); break;     // null
                case 2: out.SetNullFormat(YAML::UpperNull); break;     // NULL
                case 3: out.SetNullFormat(YAML::CamelNull); break;     // Null
                default: out.SetNullFormat(YAML::LowerNull);
            }
            
            // String format
            switch(string_format) {
                case 0: out.SetStringFormat(YAML::Auto); break;
                case 1: out.SetStringFormat(YAML::SingleQuoted); break;
                case 2: out.SetStringFormat(YAML::DoubleQuoted); break;
                case 3: out.SetStringFormat(YAML::Literal); break;
                default: out.SetStringFormat(YAML::Auto);
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
    char* yaml_to_string(YAMLNodeHandle handle) {
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
    void yaml_free_string(char* str) {
        delete[] str;
    }
    
    YAMLNodeHandle yaml_clone(YAMLNodeHandle handle) {
        return new YAML::Node(YAML::Clone(*static_cast<YAML::Node*>(handle)));
    }
    
    // C interface for expand - handles the map internally
    YAMLNodeHandle yaml_expand(YAMLNodeHandle handle) {
        auto node = static_cast<YAML::Node*>(handle);
        std::map<std::string, YAML::Node> seen;
        YAML::Node result = expand_internal(*node, &seen);
        return new YAML::Node(result);
    }
}