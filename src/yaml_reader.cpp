#include "yaml-cpp/yaml.h"
#include <iostream>
#include <fstream>
#include <map>

// run cmake .. -DYAML_BUILD_SHARED_LIBS=ON in the pals directory
// cd into build, run make, ./my_project
void print_recur(YAML::Node node);
YAML::Node replace(YAML::Node name, std::map<std::string, YAML::Node> *seen);
YAML::Node expand(YAML::Node node, std::map<std::string, YAML::Node> *seen);
YAML::Node deep_copy(const YAML::Node& node);

int main() {
    // reads in a lattice from a yaml file
    YAML::Node config = YAML::LoadFile("ex.yaml");
    std::map<std::string, YAML::Node> seen;
    // manipulates lattice
    config = expand(config, &seen);
    
    // saves lattice to new yaml file
    std::ofstream outputFile("expand.yaml");
    YAML::Emitter out;
    out << config;
    outputFile << out.c_str();
}

// makes a deep copy of a node
YAML::Node deep_copy(const YAML::Node& node) {
    if (node.IsScalar()) {
        return YAML::Node(node.as<std::string>());
    } else if (node.IsSequence()) {
        YAML::Node seq(YAML::NodeType::Sequence);
        for (auto x : node) {
            seq.push_back(deep_copy(x));
        }
        return seq;
    } else if (node.IsMap()) {
        YAML::Node map(YAML::NodeType::Map);
        for (auto x : node) {
            map[x.first.as<std::string>()] = deep_copy(x.second);
        }
        return map;
    } else {
        return YAML::Node();
    }
}

YAML::Node replace(YAML::Node name, std::map<std::string, YAML::Node> *seen) {
    std::string str = name.as<std::string>();
    if (seen->count(str)) {
        std::cout << seen->at(str);
        return deep_copy(seen->at(str));
    } else {
        return name;
    }
}

/*/
Performs lattice expansion (not implemented correctly yet)
*/
YAML::Node expand(YAML::Node node, std::map<std::string, YAML::Node> *seen) {
    if (node.IsSequence()) {
        for (int i = 0; i < node.size(); i++) {
            node[i] = expand(node[i], seen);
        }
        return node;
    } else if (node.IsScalar()) {
        return replace(node, seen);
    } else if (node.IsMap()) {
        for (auto ele : node) {
            seen->insert({ele.first.as<std::string>(), ele.second});
            ele.second = expand(ele.second, seen);
        }
        return node;
    } else {
        return node;
    }
}

/*/
Prints out a node line by line to the terminal.
*/
void print_recur(YAML::Node node) {
    if (node.IsScalar()) {
        std::cout << node.as<std::string>() << "\n";
    } else if (node.IsSequence()) {
        for (auto x : node) {
            print_recur(x);
        }
    } else if (node.IsMap()) {
        for (auto x : node) {
            std::cout << x.first.as<std::string>() << "\n";
            print_recur(x.second);
        }
    } else {
        std::cout << "Null";
    }
}