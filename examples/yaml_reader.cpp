#include <iostream>
#include "../src/yaml_c_wrapper.h"

// If file name is provided as a command line argument, this will print out the
// expanded contents of the file to the terminal, as well as to expand.pals.yaml. 
// Otherwise, it will use the example file ex.pals.yaml

int main(int argc, char* argv[]) {
    struct lattices lat = get_lattices("ex.pals.yaml");
    std::cout << yaml_to_string(lat.original);
    return 0;
    if (argc != 1) {
        char* filename = argv[1];
        std::string path = "../lattice_files/";
        path += filename;
        YAMLNodeHandle handle = parse_file(path.c_str());

        lattice_expand(handle);
        std::cout << "Printing out contents of file: " << filename << std::endl;
        std::cout << yaml_to_string(handle) << std::endl;
        write_file(handle, "../lattice_files/expand.pals.yaml");
        return 0;
    }
    // reading a lattice from a yaml file
    YAMLNodeHandle handle = parse_file("../lattice_files/ex.pals.yaml");
    std::cout << "Printing out contents of file: " << "ex.pals.yaml" << std::endl;
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

    // performing lattice expansion
    lattice_expand(handle);

    // writing modified lattice file to expand.pals.yaml
    write_file(handle, "../lattice_files/expand.pals.yaml");
    return 0;
}
