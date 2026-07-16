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

`parse_and_expand_PALS` reads a lattice file, resolves its includes, expands the
selected lattice, and returns a `lattices` struct with three independent views —
`original`, `combined`, and `expanded` (see [What it does](../index.md)). Each is
a `YAMLTreeHandle` and must be freed with `delete_tree`.

```cpp
#include "yaml_c_wrapper.h"

struct lattices lat = parse_and_expand_PALS("ex.pals.yaml", nullptr);

char* s = tree_to_string(lat.expanded);
std::puts(s);
yaml_free_string(s);

// Report any problems met while expanding (see below), then free everything.
for (size_t i = 0; i < lat.problems.count; ++i)
    std::fprintf(stderr, "  - %s\n", lat.problems.items[i]);
free_lattice_problems(lat.problems);

delete_tree(lat.original);
delete_tree(lat.combined);
delete_tree(lat.expanded);
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

## Navigating and editing trees

A tree is addressed by an opaque `YAMLTreeHandle` plus integer `YAMLNodeId`s.
Walk it with `get_root`, `get_child_by_key`, `get_child_by_index`, `get_parent`,
and `get_size`; inspect nodes with `is_map`, `is_sequence`, `is_scalar`,
`get_node_key`, and `as_string`; and edit with `add_scalar`, `add_map`,
`add_sequence`, `set_scalar`, `set_node_key`, `remove_node`, `deep_copy_node`,
and `deep_copy_children`. Serialize with `node_to_string` / `tree_to_string`, or
`write_file`. Every one of these is listed in the [API Reference](../api.md).

Two more entry points build on the expanded tree:

- `build_correspondence_map` links a node across the three views (their
  provenance is recorded as the trees are derived from one another).
- `match_names` finds every named construct that a PALS *Name Matching* string
  refers to.

## Examples

The `examples/` directory contains runnable programs:

- **`examples/example_rw.cpp`** — read a lattice, do basic manipulations (add,
  remove, rename), print, and write it back out.

  ```console
  ./example_rw
  ```

- **`examples/print_lattices`** — expand a lattice and print all three views. It
  takes a file name and an optional `-lat <name>` flag:

  ```console
  ./print_lattices ex.pals.yaml -lat lat2
  ```

## Extensions

[PALSJulia](https://github.com/pals-project/PALSJulia) extends pals-cpp with a
Julia interface to the C API and translators from PALS to
[Bmad](https://github.com/bmad-sim/bmad-ecosystem) and
[SciBmad](https://github.com/bmad-sim/SciBmad.jl) lattice files.
