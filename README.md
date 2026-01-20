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

Next, in pals-cpp, run  

mkdir build && cd build  
cmake .. -DYAML_BUILD_SHARED_LIBS=ON  
make

## Example
See yaml_reader.cpp for an example of how to use the library to read a lattice file, 
perform a basic manipulation, and write to another lattice file. 
Navigate to the build directly and run  
./yaml_reader