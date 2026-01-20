#include "yaml_c_wrapper.h"

int main() {
    YAMLNodeHandle handle = yaml_parse_file("../lattice_files/ex.yaml");
    handle = yaml_expand(handle);
    yaml_write_file(handle, "../lattice_files/expand.yaml");
}