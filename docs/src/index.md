# pals-cpp

**pals-cpp** is a C++ parser library for the Particle Accelerator Lattice
Standard ([PALS](https://github.com/pals-project/pals)). It reads PALS-format
YAML lattice files with [rapidyaml](https://github.com/biojppm/rapidyaml),
expands a lattice according to the PALS specification, evaluates the
mathematical expressions in the expanded lattice, and exposes everything through
a C API (`yaml_c_wrapper.h`) that other languages — such as
[PALSJulia](https://github.com/pals-project/PALSJulia) — wrap.

```{toctree}
:hidden:
:caption: User Guide

guide/usage
guide/expressions
```

```{toctree}
:hidden:
:caption: Reference

api
```

## What it does

Lattice expansion follows the PALS specification and produces five views of a
document:

1. **`original`** — the raw tree mapping each file the document is built from —
   the top-level file and every file it reaches by `include` or `load`, at any
   depth — to its unparsed contents, keyed by path.
2. **`combined`** — the tree with every `include` directive resolved and spliced
   inline, and every `load`ed file merged in subnode by subnode under the `PALS`
   root.
3. **`full_expanded`** — the selected lattice fully expanded, and nothing else:
   elements substituted with their definitions, `repeat`ed beamlines unrolled,
   `inherit`ed ancestors merged, forks resolved, every mathematical expression
   evaluated to a number, `set` commands executed, and the ABSOLUTE controllers
   applied to the parameters they drive. It is rooted at the lattice entry
   itself, without the `PALS`/`facility` scaffolding it was defined under. Every
   dependent parameter has been computed: each element carries its `ReferenceP`,
   `FloorP` and `s_position`, the derived members of every parameter family it
   uses, and the non-zero defaults of the groups it carries; each branch is
   capped with a `branch_end` Placeholder holding its final reference and floor.
4. **`expanded`** — the same lattice with all of that removed. What the author
   wrote decides which parameters stay; the values are the finished ones. It is
   `full_expanded` with nodes removed rather than an earlier snapshot, so a
   parameter present in both trees holds the same value in both, with every
   `set` and ABSOLUTE controller applied. Use it to see the inputs rather than
   their consequences, or to write a lattice back out without the computed
   values.
5. **`leftover`** — everything the expanded trees do not carry, keeping that
   scaffolding: element and beamline definitions, `use` statements, constants,
   controllers, `set` commands, and any lattice that was not the one expanded.
   A definition substituted into the lattice is copied, so it appears in both
   views.

See [Building and using the library](guide/usage.md) to get started,
[Evaluating expressions](guide/expressions.md) for the expression grammar and
evaluation model, and the [API Reference](api.md) for the full C interface.

## Quick start

```console
cmake -S . -B build
cmake --build build
```

This builds `libyaml_c_wrapper`, a shared library other languages can load.

```cpp
#include "yaml_c_wrapper.h"

struct lattices lat = parse_and_expand_PALS("ex.pals.yaml", nullptr);
char* s = tree_to_string(lat.full_expanded);  // the expanded lattice as YAML
// ... use it ...
yaml_free_string(s);
delete_tree(lat.original);
delete_tree(lat.combined);
delete_tree(lat.expanded);
delete_tree(lat.full_expanded);
delete_tree(lat.leftover);
```
