#include "../src/yaml_c_wrapper.h"
#include <iostream>

// See ex.yaml for the example lattice file and expand.yaml for the output of this file.

int main() {
    // reading a lattice from a yaml file
    YAMLNodeHandle handle = yaml_parse_file("../lattice_files/ex.pals.yaml");
    // printing to terminal
    std::cout << yaml_to_string(handle) << std::endl << std::endl;

    // type checking
    // prints "handle is of type sequence: 1", 1 meaning true
    std::cout << "handle is of type sequence: " << (yaml_is_sequence(handle)) << "\n";

    // accessing sequence
    YAMLNodeHandle node = yaml_get_index(handle, 0);
    /* prints
    the first element is: 
    thingB:
        kind: Sextupole
    */
    std::cout << "the first element is: \n" << yaml_to_string(node) << "\n";

    // accessing map
    // prints "the value at key 'thingB' is: kind: Sextupole"
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
    // give sequence a name by putting it in a map:
    YAMLNodeHandle magnets = yaml_create_map();
    yaml_set_node(magnets, "magnets", sequence);

    // adding new nodes to lattice
    yaml_push_node(handle, map);
    yaml_push_node(handle, magnets);

    yaml_expand(handle);

    // writing modified lattice file to expand.pals.yaml
    yaml_write_file(handle, "../lattice_files/expand.pals.yaml");
}
