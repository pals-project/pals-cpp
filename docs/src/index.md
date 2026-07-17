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

Lattice expansion follows the PALS specification and produces four views of a
document:

1. **`original`** — the raw tree mapping each file (including `include`d files)
   to its unparsed contents.
2. **`combined`** — the tree with every `include` directive resolved and spliced
   inline.
3. **`expanded`** — the selected lattice fully expanded, and nothing else:
   elements substituted with their definitions, `repeat`ed beamlines unrolled,
   `inherit`ed ancestors merged, forks resolved, and every mathematical
   expression evaluated to a number. It is rooted at the lattice entry itself,
   without the `PALS`/`facility` scaffolding it was defined under.
4. **`leftover`** — everything the expanded tree does not carry, keeping that
   scaffolding: element and beamline definitions, `use` statements, constants,
   controllers, and any lattice that was not the one expanded. A definition
   substituted into the lattice is copied, so it appears in both views.

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
char* s = tree_to_string(lat.expanded);   // the expanded lattice as YAML
// ... use it ...
yaml_free_string(s);
delete_tree(lat.original);
delete_tree(lat.combined);
delete_tree(lat.expanded);
```
