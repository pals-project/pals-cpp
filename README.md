## Introduction
yaml_reader.cpp is for manipulaing lattices direcly in C++

yaml_c_wrapper.cpp wraps YAML::Node into C objects so they can be part of 
a shared object library to interface with other languages

In terminal, run
 
mkdir build; cd build

cmake .. -DYAML_BUILD_SHARED_LIBS=ON

make