#include "../src/yaml_c_wrapper.h"
#include <iostream>

int main() {
    // reading a lattice from a yaml file
    YAMLNodeHandle handle = yaml_parse_file("../lattice_files/ex.yaml");
    // printing to terminal
    std::cout << yaml_to_string(handle) << std::endl << std::endl;

    // type checking
    std::cout << (yaml_is_sequence(handle)) << "\n";

    // accessing sequence
    YAMLNodeHandle node = yaml_get_index(handle, 0);
    std::cout << "the first element is: \n" << yaml_to_string(node) << "\n";

    // accessing map
    std::cout << "\nthe value at key 'thingB' is: " << yaml_to_string(yaml_get_key(node, "thingB")) << "\n";

    // creating a new node that's a map 
    YAMLNodeHandle map = yaml_create_map();
    yaml_set_int(map, "first", 1);

    // creating a new node that's a sequence
    YAMLNodeHandle sequence = yaml_create_sequence();
    yaml_push_string(sequence, "magnet1");
    yaml_push_string(sequence, "");
    YAMLNodeHandle scalar = yaml_create_scalar();
    yaml_set_scalar_string(scalar, "magnet2");
    yaml_set_at_index(sequence, 1, scalar);

    // adding new nodes to lattice
    yaml_push_node(handle, map);
    yaml_push_node(handle, sequence);

    // writing modified lattice file to expand.yaml
    yaml_write_file(handle, "../lattice_files/expand.yaml");
}