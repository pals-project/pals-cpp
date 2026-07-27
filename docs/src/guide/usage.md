# Building and using the library

## Building

From the repository root:

```console
cmake -S . -B build
cmake --build build
```

This builds `libyaml_c_wrapper` (`.dylib`/`.so`), a shared library that other
languages can load. CMake fetches the dependencies —
[rapidyaml](https://github.com/biojppm/rapidyaml),
[PCRE2](https://www.pcre.org), and
[AtomicAndPhysicalConstantsCLib](https://github.com/pals-project/AtomicAndPhysicalConstantsCLib) —
automatically. Rebuild after changing any source file, and before running the
tests. Put lattice files under `lattice_files/`.

## Expanding a lattice

`parse_and_expand_PALS` reads a lattice file, resolves its includes and loads,
expands the selected lattice, and returns a `lattices` struct with five
independent views —
`original`, `combined`, `expanded`, `full_expanded`, and `leftover` (see
[The five trees](trees.md)). Each is a `YAMLTreeHandle` and must be freed with
`delete_tree`.

```cpp
#include "yaml_c_wrapper.h"

struct lattices lat = parse_and_expand_PALS("ex.pals.yaml", nullptr);

char* s = tree_to_string(lat.full_expanded);
std::puts(s);
yaml_free_string(s);

// Report any problems met while expanding (see below), then free everything.
for (size_t i = 0; i < lat.problems.count; ++i)
    std::fprintf(stderr, "  - %s\n", lat.problems.items[i]);
free_lattice_problems(lat.problems);

delete_tree(lat.original);
delete_tree(lat.combined);
delete_tree(lat.expanded);
delete_tree(lat.full_expanded);
delete_tree(lat.leftover);
```

The second argument names the lattice to expand. Pass `nullptr` (or an empty
string) to expand the lattice named by the last `use` statement, or — if there
is none — the last lattice defined in the file.

### Problems found during expansion

Expansion does not abort on a recoverable problem — a `line` reference to an
undefined element, a missing `inherit`/`repeat`/`Fork` target, or an expression
that cannot be evaluated (an unknown constant, a dangling element‑parameter
reference, a cycle). Instead it leaves the offending value as text and appends a
human‑readable message to `lat.problems`, an owning `string_list`. The list is
empty when expansion was clean. The library never prints — the caller decides
whether to report, save, or ignore the messages — and must release the list with
`free_lattice_problems`. Only values that look like expressions (an operator, a
parenthesis, a `>` reference, or an explicit `expr(...)`) are flagged when they
fail to evaluate, so plain names and labels are not mistaken for broken math.

### Reading the expanded lattice

Which of the five trees answers a given question, and what the expansion adds to
`full_expanded` that appears nowhere else — `element_index`, `s_position`,
`ReferenceP`, `FloorP`, the derived parameter families, the `branch_end` cap —
is covered in [The five trees](trees.md).

## Navigating and editing trees

A tree is addressed by an opaque `YAMLTreeHandle` plus integer `YAMLNodeId`s.
Walk it with `get_root`, `get_child_by_key`, `get_child_by_index`, `get_parent`,
and `get_size`; inspect nodes with `is_map`, `is_sequence`, `is_scalar`,
`get_node_key`, and `as_string`; and edit with `add_scalar`, `add_map`,
`add_sequence`, `set_scalar`, `set_node_key`, `remove_node`, `deep_copy_node`,
and `deep_copy_children`. Serialize with `node_to_string` / `tree_to_string`, or
`write_file`. Every one of these is listed in the [API Reference](../api.md).

Three more entry points build on the expanded tree:

- `build_correspondence_map` links a node across the derivation chain — `original`,
  `combined`, `full_expanded` and `leftover` (their provenance is recorded as the
  trees are derived from one another). `expanded` takes no part: it is a pruned
  copy of `full_expanded`, so its nodes are found by path.
- `match_names` finds every named construct that a PALS *Name Matching* string
  refers to.
- `get_parameter_value` looks up a single parameter value by a *Name Matching*
  string — an element parameter (with a path) or, as a bare name, a constant or
  variable. It returns a `param_value` tagged as a number, a string, or missing.
  The value is returned as stored, **not** evaluated: a plain number comes back as
  `PARAM_VALUE_NUMBER`, while an expression (e.g. `0.3 * 5`), a species name such
  as `#3He`, or other text comes back verbatim as a string (evaluating is the job
  of expansion — the expanded trees already hold numbers). An unset element
  parameter yields its default (`0` for now); and a string that identifies no
  single value — no matching element/constant/variable, a bare element name, a
  path stopping on a whole group, or matches that disagree — yields
  `PARAM_VALUE_MISSING`. When the tag is `PARAM_VALUE_STRING`, free the returned
  `string` with `yaml_free_string`.

  ```c
  struct param_value v = get_parameter_value(full_expanded, "lat1>>>B1a>BendP.e1");
  if (v.kind == PARAM_VALUE_NUMBER) printf("%g\n", v.number);
  else if (v.kind == PARAM_VALUE_STRING) { puts(v.string); yaml_free_string(v.string); }
  ```
- `get_lattice_parameter_value` is the whole-lattice form: pass the `full_expanded`
  and `leftover` handles from one `parse_and_expand_PALS()` result and it looks in
  `full_expanded` first (element parameters), then `leftover` (constants, variables,
  and unused definitions) — never in the raw `original`/`combined` trees. Values
  therefore come back already evaluated. Pass `full_expanded` rather than
  `expanded`: a dependent parameter is a legitimate thing to ask for, and only the
  former carries one.

## Examples

The `examples/` directory contains runnable programs:

- **`examples/example_rw.cpp`** — read a lattice, do basic manipulations (add,
  remove, rename), print, and write it back out.

  ```console
  ./example_rw
  ```

- **`examples/print_lattices`** — expand a lattice and print all five views. It
  takes a file name and an optional `-lat <name>` flag naming the lattice to
  expand:

  ```console
  ./print_lattices ex.pals.yaml -lat lat2
  ```

  The file name is resolved under `../lattice_files/`, so run it from a
  directory that sits beside `lattice_files/` — `build/`, say — and put the
  lattice in `lattice_files/`.

## Extensions

[PALSJulia](https://github.com/pals-project/PALSJulia) extends pals-cpp with a
Julia interface to the C API and translators from PALS to
[Bmad](https://github.com/bmad-sim/bmad-ecosystem) and
[SciBmad](https://github.com/bmad-sim/SciBmad.jl) lattice files.
