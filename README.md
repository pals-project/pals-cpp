## Introduction
Pals-cpp is the parser library for PALS accelerator lattice files. It uses rapidyaml https://github.com/biojppm/rapidyaml to read lattices into memory and provides additional capabilities like printing to console, writing to files, and searching for elements. One major component is performing lattice expansion following the PALS specifications:
1. The lattice to be expanded can be specified by the user. If none is specified, the one in the last `use: root_lattice` statement will be expanded. If none exists, the last lattice in the file is expanded. 
2. Content from other files can be brought into scope in two ways. An `include: filename` splices that file's contents in verbatim at the point the `include` is written. A `load` list under the `PALS` node instead merges whole files subnode by subnode — list subnodes concatenate in load order, dictionary subnodes take the union, and `SELF` marks where the joining file's own contents belong — which is how a layout file and a settings file are composed into one lattice. Paths are relative to the file naming them, and both can nest to any depth.
3. Elements that inherit parameters from other elements will have those properties brought into scope.
4. Beamlines in a lattice with a `repeat: count` will have their contents repeated `count` times in the lattice.
5. Elements in a lattice defined outside of it will have their definitions brought it.
6. Fork elements will create new branches in the lattice to their destination branch. 
7. Mathematical expressions are evaluated to numbers (see below).
8. `set` commands are executed and controllers are applied to the parameters they drive, on either side of an `expand_lattice` node (see below).

## Expression Evaluation
As a final step of expansion, every scalar value in the `expanded` tree that is a
PALS mathematical expression is replaced with its numeric value. This covers the
full PALS expression grammar — arithmetic (`+ - * / ^`), parentheses, the
[built-in constants](https://github.com/pals-project/pals) (`pi`, `c_light`,
`r_electron`, …), the math functions (`sqrt`, `log`, `sin`, `floor`, `modulo`, …),
and user-defined constants and variables (both the `kind: constant`/`value:` and
the compact `constants:`/`variables:` forms — the compact form may be written as
a sequence of single-key maps or as a plain map — resolved in dependency order).
Both immediate expressions and `expr(...)`-delayed expressions are evaluated to a
number in the expanded tree. Values that are not expressions — element/line name
references, `kind:` names, booleans — are left untouched, and expressions
containing `random()`/`random_gauss()` are left as text so the expanded tree stays
reproducible. The `combined` and `original` trees always retain the original
expression text.

Operator precedence is standard, with two conventions worth noting: `^` (power)
is right-associative, so `2^3^2` is `2^(3^2) = 512`; and a unary sign binds
*looser* than `^`, so `-2^2` is `-(2^2) = -4` while a signed exponent still works
(`2^-2` is `2^(-2) = 0.25`) — the Fortran/Bmad convention used across the
ecosystem.

**Controllers.** A `kind: Controller` element bundles expressions that drive
lattice parameters. Its `variables:` form a controller-scoped symbol table —
each initial value is a constant expression (constants and functions only, no
variables), and each entry in `controls:` pairs a `parameter` target — a
name-matching string — with an `expression` over that controller's own
variables, reaching nothing outside the controller. `control_type:
ABSOLUTE` (the default) means the controllers determine the parameter outright,
so during expansion each matched parameter is set to the sum of the values of
every ABSOLUTE controller driving it; `control_type: RELATIVE` describes a knob
the simulation program varies afterwards and so changes nothing at expansion.
A controller may also drive another controller's variable, named
`controller>variable`, which makes controllers a hierarchy evaluated from the
top down. See [Evaluating expressions](docs/src/guide/expressions.md).

The particle-data functions `mass_of`, `charge_of`, and `anomalous_moment_of` take
a quoted species name (e.g. `mass_of("#3He")`); an unquoted name is an error. A
mass number must carry a leading `#` (`"#3He"`, not `"3He"`). These
functions and the physical values behind the named constants are provided by
[AtomicAndPhysicalConstantsCLib](https://github.com/pals-project/AtomicAndPhysicalConstantsCLib)
(a C++ mirror of [AtomicAndPhysicalConstants.jl](https://github.com/bmad-sim/AtomicAndPhysicalConstants.jl)),
fetched automatically by CMake. A single expression can also be evaluated on its
own via `evaluate_pals_expression()`.

**Set commands.** A `set` writes a value into every parameter its `parameter`
name-matching string selects; in the `value` expression `PARAMETER` is the
current value and `SELF` the element that owns it
(`value: 2*PARAMETER + atan(SELF.BendP.g_ref)`). The compact `sets:` form is a
list of `target: value` pairs. An `expand_lattice` node in the `facility` list
decides what a set acts on: before it, the element *definitions* (and only those
defined earlier in the list); after it, the already-expanded lattice, so each
copy of a repeated element is written separately. `absolute_error` and
`relative_error` are reported rather than applied — the standard does not
specify the error distribution.

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
- `original` is a map containing the base lattice as well as any file it reaches
by `include` or `load`.
- `combined` is the base lattice but with all included files substituted in and
all loaded files merged in.
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

### Source layout
The library sources live in `src/`, split by concern:

- `yaml_c_wrapper.h` — the public C API: the opaque handle types and every
  exported function. This is the only header consumers include.
- `yaml_c_wrapper.cpp` — the generic YAML tree wrapper over rapidyaml (parse,
  traverse, query, modify, emit). Knows nothing about PALS.
- `yaml_tree.h` — internal declarations shared between the wrapper and the PALS
  code: the tree representation behind `YAMLTreeHandle` (`ParsedData`) plus the
  low-level tree helpers (`ensure_capacity`, `deep_copy_recursive`).
- `pals_expand.cpp` — the lattice expansion pipeline that builds the four-tree
  representation (`original` / `combined` / `expanded` / `leftover`): include
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
