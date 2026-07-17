#include <iostream>

#include "../src/yaml_c_wrapper.h"

int main(int argc, char* argv[]) {
    std::cout << "============ Printing Developer Information ============" << std::endl;
    std::string text = "Use the function 'parse_file(filename)' to read a lattice file. For example,\n\n"
                       "YAMLTreeHandle tree = parse_file(\"../lattice_files/ex.pals.yaml\")\n"
                       "reads the file 'ex.pals.yaml' into a tree named 'tree'.\n\n";
    // reading a lattice from a yaml file
    std::cout << text;
    YAMLTreeHandle tree = parse_file("../lattice_files/ex.pals.yaml");

    // printing to terminal
    std::cout << "To print a tree to console, use the 'tree_to_string(tree_name)' function.\n";
    std::cout << tree_to_string(tree) << std::endl << std::endl;

    // type checking
    std::cout << "The root node of 'ex.pals.yaml' is the 'PALS' map, so is_map(tree, get_root(tree)) = "
              << is_map(tree, get_root(tree)) << "\n";

    // The lattice contents live under the 'facility' node of the 'PALS' root.
    YAMLNodeId pals = get_child_by_key(tree, get_root(tree), "PALS");
    YAMLNodeId facility = get_child_by_key(tree, pals, "facility");
    std::cout << "The 'facility' node is a sequence, so is_sequence(tree, facility) = "
              << is_sequence(tree, facility) << "\n";

    // accessing sequence
    std::cout << "Elements in a sequence may be accessed by their index.\n";
    YAMLNodeId seq1 = get_child_by_index(tree, facility, 0);
    std::cout << "The first element of 'facility' is: \n"
              << node_to_string(tree, seq1) << "\n";

    // accessing map
    std::cout << "Elements in a map may be accessed by their key.\n";
    YAMLNodeId map1 = get_child_by_key(tree, get_child_by_index(tree, seq1, 0), "kind");
    std::cout << "The element 'thingB' has:\n    "
              << node_to_string(tree, map1) << "\n";

    // add a new sequence element to the facility containing new_map: {apples: 5}
    std::cout << "Adding a new element '-apples: 5' to facility.\n";
    YAMLNodeId new_map_entry = add_map(tree, facility, NULL, YAML_END);
    YAMLNodeId map = add_map(tree, new_map_entry, "new_map", YAML_END);
    add_scalar(tree, map, "apples", "5", YAML_END);

    // add a new sequence element to the facility containing magnets
    std::cout << "Adding a new element\n"
              << "    - magnet_list:\n"
              << "        - magnet1\n"
              << "        - magnet2\n"
              << "to facility.\n\n";
    YAMLNodeId magnets_entry = add_map(tree, facility, NULL, YAML_END);
    YAMLNodeId sequence =
        add_sequence(tree, magnets_entry, "magnet_list", YAML_END);
    add_scalar(tree, sequence, NULL, "magnet1", YAML_END);
    add_scalar(tree, sequence, NULL, "magnet2", 1);

    // writing trees to files
    std::cout << "Use 'write_file(tree, filename)' to write the edited tree to a file.\n";
    write_file(tree, "../lattice_files/expand.pals.yaml");
    std::cout << "Wrote tree to 'expand.pals.yaml`\n\n\n\n";

    std::cout << "========== Printing Final Modified Tree ==========\n";
    std::cout << tree_to_string(tree) << std::endl;

    return 0;
}
