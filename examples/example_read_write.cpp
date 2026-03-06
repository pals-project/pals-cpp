#include <iostream>
#include "../src/yaml_c_wrapper.h"
#include <yaml-cpp/yaml.h>

int main(int argc, char* argv[]) {
    // reading a lattice from a yaml file
    YAMLNodeHandle handle = parse_file("../lattice_files/ex.pals.yaml");
    std::cout << "Output of example_read_write.cpp" << std::endl;
    // printing to terminal
    std::cout << yaml_to_string(handle) << std::endl << std::endl;

    // type checking
    // prints "handle is of type sequence: 1", 1 meaning true
    std::cout << "handle is of type sequence: " << (is_sequence(handle))
              << "\n";

    // accessing sequence
    YAMLNodeHandle node = get_index(handle, 0);
    /* prints
    the first element is:
    thingB:
        kind: Sextupole
    */
    std::cout << "the first element is: \n" << yaml_to_string(node) << "\n";

    // accessing map
    // prints "the value at key 'thingB' is: kind: Sextupole"
    std::cout << "\nthe value at key 'thingB' is: "
              << yaml_to_string(get_key(node, "thingB")) << "\n";

    // creating a new node that's a map
    YAMLNodeHandle map = create_map();
    set_value_int(map, "apples", 5);

    // creating a new node that's a sequence
    YAMLNodeHandle sequence = create_sequence();
    push_string(sequence, "magnet1");
    push_string(sequence, "");
    YAMLNodeHandle scalar = create_scalar();
    set_scalar_string(scalar, "magnet2");
    set_at_index(sequence, 1, scalar);
    // give sequence a name by putting it in a map:
    YAMLNodeHandle magnets = create_map();
    set_value_node(magnets, "magnets", sequence);

    // adding new nodes to lattice
    push_node(handle, map);
    push_node(handle, magnets);

    // getting expanded lattice
    struct lattices lat = get_lattices("ex.pals.yaml", "lat1");
    YAMLNodeHandle expanded = lat.expanded;

    // writing modified lattice file to expand.pals.yaml
    write_file(handle, "../lattice_files/expand.pals.yaml");
    return 0;
}
