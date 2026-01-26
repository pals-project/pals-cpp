## Introduction
yaml_c_wrapper.cpp wraps YAML::Node into C objects so they can be part of a shared object library to interface with other languages

First install yaml-cpp by running 

macOS:  
brew install yaml-cpp  
or  
port install yaml-cpp

Linux:  
sudo apt-get install libyaml-cpp-dev

Windows:  
choco install yaml-cpp

Manual Install:  
git clone https://github.com/jbeder/yaml-cpp.git  
cd yaml-cpp/src  
mkdir build && cd build  
cmake ..  
cmake --build .  
cmake --install .  

## Usage
In pals-cpp, run  

mkdir build 
cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -S . -B build 
cmake --build build
ctest --test-dir build

This builds libyaml_c_wrapper.dylib, a shared object library that can be used
by other languages. 

It also builds an executable using yaml_reader.cpp containing examples for how 
to use the library to read lattice files, perform basic manipulations, and write
to other lattice files. To see the output, navigate to the build directory and run  
./yaml_reader

It will also build the tests.

## Testing
In the root pals-cpp directory, run  
ctest --test-dir build --output-on-failure  

To run a specific test, run 
ctest --test-dir build -R "Test Name"

## Issue
yaml-cpp's cmake only requires cmake version 3.4, which is deprecated. Warnings must
be suppressed to run properly