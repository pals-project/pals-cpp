#include <iostream>
#include "../src/yaml_c_wrapper.h"
#include <cstring>

int main(int argc, char* argv[]) {
    const char* file_name = "../lattice_files/ex.pals.yaml";
    const char* lattice_name = "";
    if (argc >= 2) {
        file_name = argv[1];
    }
    if (argc >= 3) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-lat") == 0) {
                lattice_name = argv[i+1];
            }
        }
    }
    
    struct lattices lat = get_lattices(file_name, lattice_name);
    std::cout << "Printing original lattice information: " << std::endl;
    std::cout << yaml_to_string(lat.original) << std::endl << "\n\n"; 

    // put separating lines here
    std::cout << "Printing included lattice information: " << std::endl;
    std::cout << yaml_to_string(lat.included) << std::endl << "\n\n";

    std::cout << "Printing expanded lattice information: " << std::endl;
    std::cout << yaml_to_string(lat.expanded) << std::endl;
    return 0;
}