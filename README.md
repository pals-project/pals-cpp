## Introduction
PALSParserCpp is a parser library for [PALS](https://github.com/pals-project/pals) accelerator lattice files. 
It uses rapidyaml https://github.com/biojppm/rapidyaml to read lattices into memory and provides 
additional capabilities like printing to console, writing to files, and searching for elements. 
One major component is performing lattice expansion following the PALS specifications.
The lattice to be expanded can be specified by the user as an argument to the `parse_and_expand_PALS`
function. If none is specified, the lattice is chosen using the prescription given by PALS.

## Usage
First, to build, run the following in the root directory:  

```console
cmake -S . -B build 
cmake --build build
```

This builds `libPALSParserCpp.dylib`, a shared object library that can interface with other languages. Make sure to rebuild after making any changes to files, and before running tests. All lattice files should go in `lattice_files/`.

### Example 1
`examples/example_rw.cpp` contains examples for how to use the library API to read lattice files, perform basic manipulations (adding and removing elements, renaming, etc.), print to console, and write the lattice back to a file. The example uses `ex.pals.yaml` and writes to `expand.pals.yaml`. To see the consle output, navigate to the build directory and run  

```console
./example_rw
```

### Example 2
The program `examples/print_lattices` performs lattice expansion on a user-specified lattice. The first argument is the file name where the lattice is defined. It also takes an option argument using `-lat root_lattice` to specify a specific lattice to expand, otherwise it will choose a default (the lattice in the last `use` statement, or the last lattice in the file if none is present). The program will create and print a struct containing the lattice views:
- `original` is a map containing the base lattice as well as any file it reaches
by `include` or `load`.
- `combined` is the base lattice but with all included files substituted in and
all loaded files merged in.
- `expanded` is the base lattice after lattice expansion has been performed, holding
the parameters the author wrote and nothing derived from them. Values are the
finished ones, so a parameter in both trees has the same value in both.
- `full_expanded` is the same lattice with every dependent parameter computed:
reference parameters, floor placement, s-positions, and the derived members of each
parameter family.
To see the console output, in the build directory, run

```console
./print_lattices ex.pals.yaml -lat lat2
```

## Extensions

The [PALSParserJ.jl](https://github.com/pals-project/PALSParserJ.jl) package is an extension of `PALSParserCpp` to:

- Provide a Julia interface for `PALSParserCpp` C++ functions.
- Provide a translator from `PALS` files to [`Bmad`](https://github.com/bmad-sim/bmad-ecosystem) lattice files.
- Provide a translator from `PALS` files to [`SciBmad`](https://github.com/bmad-sim/SciBmad.jl) lattice files.

## Documentation

The full documentation — the user guide, the five-tree model, the expression
evaluation rules, and the generated C API reference — is at
<https://pals-project.github.io/PALSParserCpp/>.

Notes for working on the library itself (source layout, memory model, running
the tests, building the docs) are on the
[Working on PALSParserCpp](https://pals-project.github.io/PALSParserCpp/develop.html)
page.
