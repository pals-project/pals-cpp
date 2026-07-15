## Introduction
Pals-cpp is the parser library for PALS accelerator lattice files. It uses rapidyaml https://github.com/biojppm/rapidyaml to read lattices into memory and provides additional capabilities like printing to console, writing to files, and searching for elements. One major component is performing lattice expansion following the PALS specifications:
1. The lattice to be expanded can be specified by the user. If none is specified, the one in the last `use: root_lattice` statement will be expanded. If none exists, the last lattice in the file is expanded. 
2. Content from other files can be brought into scope using `include: filename`
3. Elements that inherit parameters from other elements will have those properties brought into scope.
4. Beamlines in a lattice with a `repeat: count` will have their contents repeated `count` times in the lattice.
5. Elements in a lattice defined outside of it will have their definitions brought it.
6. Fork elements will create new branches in the lattice to their destination branch. 
7. Mathematical expressions are evaluated to numbers (see below).

## Expression Evaluation
As a final step of expansion, every scalar value in the `expanded` tree that is a
PALS mathematical expression is replaced with its numeric value. This covers the
full PALS expression grammar — arithmetic (`+ - * / ^`), parentheses, the
[built-in constants](https://github.com/pals-project/pals) (`pi`, `c_light`,
`r_electron`, …), the math functions (`sqrt`, `log`, `sin`, `floor`, `modulo`, …),
and user-defined constants and variables (both the `kind: constant`/`value:` and
the compact `constants:`/`variables:` forms, resolved in dependency order). Both
immediate expressions and `expr(...)`-delayed expressions are evaluated to a
number in the expanded tree. Values that are not expressions — element/line name
references, `kind:` names, booleans — are left untouched, and expressions
containing `random()`/`random_gauss()` are left as text so the expanded tree stays
reproducible. The `combined` and `original` trees always retain the original
expression text.

**Controllers.** A `kind: Controller` element bundles expressions that drive
lattice parameters. Its `variables:` form a controller-scoped symbol table
(variables may reference earlier variables of the same controller and, via the
`controller>variable` syntax, variables of another controller), and each entry
in `controls:` pairs a `parameter` target with an `expression`. Controller
`variables` and control `expression`s are evaluated against that scoped table
(rather than the global one), and each control `expression` is computed and its
value written into the control entry. The `parameter` target specs and
`control_type` are names and are left untouched.

The particle-data functions `mass_of`, `charge_of`, and `anomalous_moment_of`, and
the physical values behind the named constants, are provided by
[AtomicAndPhysicalConstantsCLib](https://github.com/pals-project/AtomicAndPhysicalConstantsCLib)
(a C++ mirror of [AtomicAndPhysicalConstants.jl](https://github.com/bmad-sim/AtomicAndPhysicalConstants.jl)),
fetched automatically by CMake. A single expression can also be evaluated on its
own via `evaluate_pals_expression()`.

## Usage
First, to build, run the following in the root directory:  

```console
cmake -S . -B build 
cmake --build build
```

This builds `libyaml_c_wrapper.dylib`, a shared object library that can interface with other languages. Make sure to rebuild after making any changes to files, and before running tests. All lattice files should go in `lattice_files/`.

### Example 1
`examples/example_rw.cpp` contains examples for how to use the library API to read lattice files, perform basic manipulations (adding and removing elements, renaming, etc.), print to console, and write the lattice back to a file. The example uses `ex.pals.yaml` and writes to `expand.pals.yaml`. To see the consle output, navigate to the build directory and run  

```console
./example_rw
```

### Example 2
The program `examples/print_lattices` performs lattice expansion on a user-specified lattice. The first argument is the file name where the lattice is defined. It also takes an option argument using `-lat root_lattice` to specify a specific lattice to expand, otherwise it will choose a default (the lattice in the last `use` statement, or the last lattice in the file if none is present). The program will create and print a struct containing three lattices:
- `original` is a map containing the base lattice as well as any lattices included
in the base lattice.
- `combined` is the base lattice but with all included files substituted in.
- `expanded` is the base lattice after lattice expansion has been performed.
To see the console output, in the build directory, run

```console
./print_lattices ex.pals.yaml -lat lat2
```

## Extensions

The [PALSJulia.jl](https://github.com/pals-project/PALSJulia.jl) package is an extension of `pals-cpp` to:

- Provide a Julia interface for `pals-cpp` C++ functions.
- Provide a translator from `PALS` files to [`Bmad`](https://github.com/bmad-sim/bmad-ecosystem) lattice files.
- Provide a translator from `PALS` files to [`SciBmad`](https://github.com/bmad-sim/SciBmad.jl) lattice files.

## Developer Notes
`YAMLTreeHandle` wraps `ryml::Tree` into C objects so they can be part of a shared object library to interface with other languages. `ryml::Tree`s are stored in memory simply as arrays. `ryml::NodeRef` acts as a simple wrapper around nodes, which are just indices in the tree array. Trees are obtained by parsing C++ std::string, and values are simply pointers to locations in the string. Therefore, the string must be kept in memory as long as the tree is in use. 

Most of the relevant ryml code for reference is contained in `/build/_deps/rapidyaml-src/src/c4/yml/tree.hpp`.

Documentation generated by Doxygen. Run in the root directory

```console
doxygen Doxyfile
```

To build the test, in the root directory run

```console
ctest --test-dir build --output-on-failure
```

To run a specific test, run
```console
ctest --test-dir build -R "Test Name"
```
