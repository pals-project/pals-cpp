## Introduction
yaml_reader.cpp is for manipulaing lattices direcly in C++. yaml_c_wrapper.cpp wraps YAML::Node into C objects so they can be part of a shared object library to interface with other languages

First install yaml-cpp by running 

macOS:  
brew install yaml-cpp

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