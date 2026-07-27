## Introduction
Pals-cpp is a parser library for [PALS](https://github.com/pals-project/pals) accelerator lattice files. 
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

This builds `libyaml_c_wrapper.dylib`, a shared object library that can interface with other languages. Make sure to rebuild after making any changes to files, and before running tests. All lattice files should go in `lattice_files/`.

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

The [PALSJulia.jl](https://github.com/pals-project/PALSJulia.jl) package is an extension of `pals-cpp` to:

- Provide a Julia interface for `pals-cpp` C++ functions.
- Provide a translator from `PALS` files to [`Bmad`](https://github.com/bmad-sim/bmad-ecosystem) lattice files.
- Provide a translator from `PALS` files to [`SciBmad`](https://github.com/bmad-sim/SciBmad.jl) lattice files.

## Developer Notes

### Source layout
The library sources live in `src/`, split by concern:

- `yaml_c_wrapper.h` — the public C API: the opaque handle types and every
  exported function. This is the only header consumers include.
- `yaml_c_wrapper.cpp` — the generic YAML tree wrapper over rapidyaml (parse,
  traverse, query, modify, emit). Knows nothing about PALS.
- `yaml_tree.h` — internal declarations shared between the wrapper and the PALS
  code: the tree representation behind `YAMLTreeHandle` (`ParsedData`) plus the
  low-level tree helpers (`ensure_capacity`, `deep_copy_recursive`).
- `pals_expand.cpp` — the lattice expansion pipeline that builds the five-tree
  representation (`original` / `combined` / `expanded` / `full_expanded` /
  `leftover`): include
  splicing, `load` merging, structural expansion (repeats, inherits, forks), expression,
  controller and `set` evaluation, and the element bookkeeper that walks each
  branch
  filling in reference parameters, floor placement, s-positions and dependent
  parameters.
- `pals_check.{h,cpp}` — spelling checks against the fixed PALS vocabulary
  (element kinds and parameter group names). A `FlorP` group parses as valid
  YAML and would otherwise go unrecognised in silence, so it is reported here,
  with a "did you mean" suggestion where a near match exists.
- `pals_floor.{h,cpp}` — floor (global) coordinate geometry. A placement is a
  position plus an orientation, carried as a unit quaternion rather than the
  standard's W matrix; `floor_propagate` advances one along the reference
  curve, and straight_LS / bend_LS / patch_LS build the (L, S) pair for the
  three geometries PALS defines.
- `pals_match.cpp` — PALS name matching and parameter lookup (the PCRE2-based
  `match_names` / `get_parameter_value` family).
- `pals_util.{h,cpp}` — small helpers shared across the PALS split
  (`child_val_str`, `split_dots`, `resolve_param_path`, `strip_expr_wrapper`).
- `pals_expression.{h,cpp}` — the standalone PALS expression grammar and
  evaluator (arithmetic, functions, built-in constants, particle-data lookups).

Everything builds into `libyaml_c_wrapper.dylib`; see `CMakeLists.txt`.

### Memory model
`YAMLTreeHandle` wraps `ryml::Tree` into C objects so they can be part of a shared object library to interface with other languages. `ryml::Tree`s are stored in memory simply as arrays. `ryml::NodeRef` acts as a simple wrapper around nodes, which are just indices in the tree array. Trees are obtained by parsing C++ std::string, and values are simply pointers to locations in the string. Therefore, the string must be kept in memory as long as the tree is in use. 

Most of the relevant ryml code for reference is contained in `/build/_deps/rapidyaml-src/src/c4/yml/tree.hpp`.

The documentation site is built with Sphinx (MyST Markdown + the Furo theme),
with the C/C++ API pulled in from Doxygen via [Breathe](https://breathe.readthedocs.io).
The published site is at <https://pals-project.github.io/pals-cpp/>. To build and
preview it locally (requires `doxygen` and `python3`):

```console
docs/build_local.sh
```

This runs `docs/build.py` — Doxygen (API → XML) then Sphinx → `docs/build/html` —
and serves it at <http://localhost:8000/>. Narrative pages live in `docs/src/`.

To build the test, in the root directory run

```console
ctest --test-dir build --output-on-failure
```

To run a specific test, run
```console
ctest --test-dir build -R "Test Name"
```
