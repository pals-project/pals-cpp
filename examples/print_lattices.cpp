#include <iostream>
#include <string>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include "../src/yaml_c_wrapper.h"

int main(int argc, char* argv[]) {
    const char* file_name = nullptr;
    const char* root_lattice = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-lat") == 0) {
            if (i + 1 < argc) {
                root_lattice = argv[++i];
            } else {
                std::cerr << "Error: -lat requires an argument" << std::endl;
                return 1;
            }
        } else {
            file_name = argv[i];
        }
    }

    if (!file_name) {
        std::cerr << "Usage: " << argv[0] << " <file> [-lat <root_lattice>]" << std::endl;
        return 1;
    }

    std::string file_path = std::string("../lattice_files/") + file_name;

    struct lattices lat = parse_and_expand_PALS(file_path.c_str(), root_lattice);

    std::cout << "========== Printing original lattice ==========" << std::endl;
    std::cout << tree_to_string(lat.original) << std::endl << "\n\n";

    std::cout << "========== Printing included lattice ==========" << std::endl;
    std::cout << tree_to_string(lat.included) << std::endl << "\n\n";

    std::cout << "========== Printing expanded lattice ==========" << std::endl;
    std::cout << tree_to_string(lat.expanded) << std::endl;
    return 0;
}