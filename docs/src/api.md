# API Reference

The public interface is the C API declared in `yaml_c_wrapper.h`, together with
the expression evaluator in `pals_expression.h`. Everything below is generated
from the header doc comments by [Doxygen](https://www.doxygen.nl) and embedded
with [Breathe](https://breathe.readthedocs.io).

## YAML C wrapper — `yaml_c_wrapper.h`

Parsing, lattice expansion, tree navigation and editing, correspondence
mapping, name matching, and serialization.

```{doxygenfile} yaml_c_wrapper.h
:project: pals-cpp
```

## Expression evaluator — `pals_expression.h`

The recursive-descent evaluator behind the PALS expression grammar.

```{doxygenfile} pals_expression.h
:project: pals-cpp
```
