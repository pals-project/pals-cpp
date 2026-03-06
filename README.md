## Introduction
`yaml_c_wrapper.cpp` wraps `YAML::Node` into C objects so they can be part of a shared object library to interface with other languages

Implemented components of lattice expansion:  
include  
inherits  
repeat  

YAML::Nodes are values that act like pointers, so editing a node will cause the tree the node is
contained in to reflect the changes.

## Usage
In pals-cpp, run  

```console
cmake -S . -B build 
cmake --build build
```

This builds `libyaml_c_wrapper.dylib`, a shared object library that can be used
by other languages. 

It also builds an executable using yaml_reader.cpp containing examples for how 
to use the library to read lattice files, perform basic manipulations, and write
to other lattice files. To see the output, navigate to the build directory and run  

```console
./yaml_reader
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
