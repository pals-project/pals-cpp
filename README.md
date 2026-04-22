## Introduction
`yaml_c_wrapper.cpp` wraps `YAML::Node` into C objects so they can be part of a shared object library to interface with other languages

Implemented components of lattice expansion:  
include  
inherits  
repeat  

YAML::Nodes are values that act like pointers, so editing a node will cause the tree the node is
contained in to reflect the changes.

Lattice files should be placed in the `lattice_files/` directory. 

## Usage
In pals-cpp, run  

```console
cmake -S . -B build 
cmake --build build
```

This builds `libyaml_c_wrapper.dylib`, a shared object library that can be used
by other languages. 

It also builds an executable at build/example_rw containing examples for how 
to use the library to read lattice files, perform basic manipulations, and write
to the lattice back to a file. To see the output, navigate to the build directory and run  

The program `get_lattices` will create a struct containing three lattices:
- `original` is a map containing the base lattice as well as any lattices included
in the base lattice.
- `included` is the base lattice but with all included files substituted in.
- `expanded` is the base lattice after lattice expansion has been performed.
Specify the lattice file with the first argument to the function. On default,
the lattice specified by the last `use` statement will be used, and if no `use`
statement exists, the lattice lattice in the file will be used. An optional flag
`-lat lattice_name` can be used to specify the lattice to expand, which has greatest
priority. For example, in the build directory, run
```console
./example_rw
```

The program `get_lattices` will create a struct containing three lattices:
- `original` is a map containing the base lattice as well as any lattices included
in the base lattice.
- `included` is the base lattice but with all included files substituted in.
- `expanded` is the base lattice after lattice expansion has been performed.
Specify the lattice file with the first argument to the function. On default,
the lattice specified by the last `use` statement will be used, and if no `use`
statement exists, the lattice lattice in the file will be used. An optional flag
`-lat lattice_name` can be used to specify the lattice to expand, which has greatest
priority. For example, in the build directory, run
```console
./get_lattices ex.pals.yaml -lat lat2
```

It will also build the tests.

## Testing
In the root `pals-cpp` directory, run

```console
ctest --test-dir build --output-on-failure
```

To run a specific test, run
```console
ctest --test-dir build -R "Test Name"
```

## To Do
add test cases for lattice expansion
