#include <iostream>
#include "../src/yaml_c_wrapper.h"

int main(int argc, char* argv[]) {
    // reading a lattice from a yaml file
    YAMLTreeHandle tree = parse_file("../lattice_files/ex.pals.yaml");
    std::cout << "Output of example_read_write.cpp" << std::endl;

    // printing to terminal
    std::cout << yaml_to_string(tree, get_root(tree)) << std::endl << std::endl;

    // type checking
    // prints "handle is of type sequence: 1", 1 meaning true
    std::cout << "handle is of type sequence: " << is_sequence(tree, get_root(tree)) << "\n";

    // accessing sequence
    YAMLNodeId node = get_child_by_index(tree, get_root(tree), 0);
    /* prints
    the first element is:
    thingB:
        kind: Sextupole
    */
    std::cout << "the first element is: \n" << yaml_to_string(tree, node) << "\n";

    // accessing map
    // prints "the value at key 'thingB' is: kind: Sextupole"
    std::cout << "\nthe value at key 'thingB' is: "
              << yaml_to_string(tree, get_child_by_key(tree, node, "thingB")) << "\n";

    // add a new sequence element to the root containing new_map: {apples: 5}
    YAMLNodeId new_map_entry = add_map(tree, get_root(tree), NULL, END);
    YAMLNodeId map = add_map(tree, new_map_entry, "new_map", END);
    add_scalar(tree, map, "apples", "5", END);

    // add a new sequence element to the root containing magnets: {magnet_list: [...]}
    YAMLNodeId magnets_entry = add_map(tree, get_root(tree), NULL, END);
    YAMLNodeId magnets = add_map(tree, magnets_entry, "magnets", END);
    YAMLNodeId sequence = add_sequence(tree, magnets, "magnet_list", END);
    add_scalar(tree, sequence, NULL, "magnet1", END);
    add_scalar(tree, sequence, NULL, "magnet2", 1);

    // print tree after modifications — if new nodes don't appear here the issue is in add_*,
    // if they do appear but not in the file the issue is in write_file
    std::cout << "\nTree after modifications:\n";
    std::cout << yaml_to_string(tree, get_root(tree)) << std::endl;

    bool ok = write_file(tree, "../lattice_files/expand.pals.yaml");
    std::cout << "write_file returned: " << ok << std::endl;
    return 0;
}