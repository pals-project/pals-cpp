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
guide/trees
guide/expressions
```

```{toctree}
:hidden:
:caption: Reference

api
```

```{toctree}
:hidden:
:caption: Development

develop
```

## What it does

Lattice expansion follows the PALS specification: elements are substituted with
their definitions, `repeat`ed beamlines unrolled, `inherit`ed ancestors merged,
forks resolved, expressions evaluated, `set` commands executed, controllers
applied, and the reference, floor and dependent parameters computed.

The result is returned as five independent views of the document:

| Tree | Holds |
| --- | --- |
| `original` | every file the document is built from, verbatim, keyed by path |
| `combined` | those files spliced into one document, still unevaluated |
| `full_expanded` | one lattice, expanded, with every computed value |
| `expanded` | the same lattice, back to the author's inputs |
| `leftover` | everything the expanded trees do not carry — definitions, constants, controllers, unexpanded lattices |

See [The five trees](guide/trees.md) for what each one holds and which to reach
for, [Building and using the library](guide/usage.md) to get started,
[Evaluating expressions](guide/expressions.md) for this library's evaluation
model, and the [API Reference](api.md) for the full C interface.
[Working on pals-cpp](develop.md) covers the source layout, the memory model,
the tests, and building this site.

The PALS standard itself — the schema, the element kinds and parameter groups,
the expression grammar, the built-in constants and functions — is documented at
[pals-project.readthedocs.io](https://pals-project.readthedocs.io) and is not
restated here.

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
