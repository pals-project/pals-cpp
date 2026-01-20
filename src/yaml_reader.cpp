#include "yaml_c_wrapper.h"
#include <iostream>

int main() {
    YAMLNodeHandle handle = yaml_parse_file("../lattice_files/ex.yaml");
    handle = yaml_expand(handle);
    yaml_write_file(handle, "../lattice_files/expand.yaml");

    // type checking:
    std::cout << (yaml_is_sequence(handle)) << "\n";

    // access:
    std::cout << yaml_to_string(yaml_get_index(handle, 0)) << "\n";
}