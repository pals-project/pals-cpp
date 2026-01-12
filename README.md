## Introduction

*pals-cpp* is a C++ package for reading, writing, and manipulating [Particle Accelerator Lattice Standard (PALS) files](https://github.com/pals-project/pals).
Note: This package is used by [*pals-julia*](https://github.com/pals-project/pals-julia) which is a Julia wrapper for *pals-cpp*.


### Install yaml-cpp
yaml_reader.cpp is for manipulaing YAML direcly in C++. yaml_c_wrapper.cpp wraps YAML::Node into C objects so they can be part of a shared object library to interface with other languages

First install yaml-cpp by running 

macOS:  
```
brew install yaml-cpp         # When using HomeBrew
sudo port install yaml-cpp    # When using MacPorts  
```

Linux:  
```
sudo apt-get install libyaml-cpp-dev
```

Windows:  
```
choco install yaml-cpp
```

Manual Install:  
```
git clone https://github.com/jbeder/yaml-cpp.git  
cd yaml-cpp/src  
mkdir build && cd build  
cmake ..  
cmake --build .  
cmake --install .  
```

### Build

Next, in pals-cpp, run  
```
mkdir build && cd build  
cmake .. -DYAML_BUILD_SHARED_LIBS=ON  
make
```

## Examples
  ... More stuff needed here...
  
