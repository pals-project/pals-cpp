# Evaluating expressions

A PALS lattice may write numeric values as mathematical *expressions* —
`0.3 * r_electron`, `a_const^2`, `mass_of("proton")`. As the final step of
expansion, `parse_and_expand_PALS` evaluates every such expression in the
**`expanded`** tree to a plain number, so the expanded lattice is fully numeric.
The `original` and `combined` views always keep the expression text exactly as
written.

Only scalars that are genuine expressions are touched. Names that happen to sit
where a value could go — element and line references in a `line:`, `kind:`
names, booleans — are not expressions and are left untouched.

## What gets evaluated, and when

Two kinds of expression are evaluated to a number in the expanded tree:

- **Immediate** expressions — a bare value such as `length: 0.3 * r_electron`.
- **Delayed** expressions wrapped in `expr(...)`, such as
  `Kn1: expr(3.74 * a_var)`. In the fully expanded tree the distinction no
  longer matters: both become numbers.

One case is deliberately **not** evaluated. An expression that calls `random()`
or `random_gauss()` is left as text, so that expanding the same lattice twice
gives byte-identical output:

```yaml
Kn2: 0.01 + 0.003*random_gauss()   # kept verbatim in `expanded`
```

## Grammar

The expression grammar is the arithmetic you would expect:

- operators `+` `-` `*` `/` `^`, with the usual precedence;
- `^` (power) is **right-associative**, so `2^3^2` is `2^(3^2) = 512`;
- a unary sign binds *looser* than `^`, so `-2^2` is `-(2^2) = -4`, while a
  signed exponent still works (`2^-2` is `2^(-2) = 0.25`) — the Fortran/Bmad
  convention used across the ecosystem;
- parentheses group sub-expressions.

## Built-in constants

The named physical constants below are available in every expression. Their
values come from
[AtomicAndPhysicalConstantsCLib](https://github.com/pals-project/AtomicAndPhysicalConstantsCLib)
(APC — a C++ mirror of
[AtomicAndPhysicalConstants.jl](https://github.com/bmad-sim/AtomicAndPhysicalConstants.jl),
CODATA 2022), fetched automatically by CMake, so PALS shares one set of numbers
with the rest of the toolchain.

| Constant | Meaning |
| --- | --- |
| `pi` | π |
| `c_light` | speed of light |
| `h_planck` | Planck constant |
| `hbar` | reduced Planck constant |
| `k_boltzmann` | Boltzmann constant |
| `r_electron`, `r_proton` | classical electron / proton radius |
| `e_charge` | elementary charge |
| `mu_0`, `epsilon_0` | vacuum permeability / permittivity |
| `classical_radius_factor` | 1 / (4π ε₀ c²) |
| `fine_structure` | fine-structure constant |
| `n_avogadro` | Avogadro constant |

## Functions

Standard math functions are available: `sqrt`, `exp`, `log`, `abs`, `sign`,
`factorial`; the trigonometric and hyperbolic families and their inverses
(`sin`, `cos`, `tan`, `cot`, `sinc`, `asin`, `acos`, `atan`, `atan2`, `sinh`,
`cosh`, `tanh`, `coth`, `asinh`, `acosh`, `atanh`, `acoth`); and the rounding
helpers `int` (toward zero), `nint` (nearest), `floor`, `ceiling`, and
`modulo(x, p)`.

### Particle-data functions

`mass_of`, `charge_of`, and `anomalous_moment_of` look a particle up by name and
return its mass (eV), charge (units of `e`), or anomalous magnetic moment, drawn
from APC. **The species name must be quoted** (single or double quotes); an
unquoted name is an error. A mass number must carry a leading `#` — write
`"#3He"`, not `"3He"` (matching
[AtomicAndPhysicalConstants.jl](https://github.com/bmad-sim/AtomicAndPhysicalConstants.jl)).
Quoting also lets that `#` be written without tripping YAML's comment rule:

```yaml
m_e:     mass_of("electron")
q_he:    charge_of("helion")
b_const: 0.45 * mass_of("#3He")
```

## User constants and variables

A lattice can define its own constants and variables and refer to them by name
from later expressions. Both the full form and the compact form are recognised:

```yaml
facility:
  # Full form.
  - r_scaled:
      kind: constant
      value: 0.3 * r_electron
  # Compact form (a seq of single-key maps, or a plain map — both accepted).
  - constants:
      a_const: 0.3 * r_electron
      b_const: 0.45
  - variables:
      a_var: a_const^2          # may reference an earlier definition
```

Definitions are resolved in dependency order, so a later value may reference an
earlier one (`a_var` uses `a_const` above). A reference that cannot be resolved,
or a genuine cycle, leaves the value as text rather than aborting the expansion.

## Element-parameter references

An expression may also reference another element's parameter by name, using the
`element>group.sub. ... .param` syntax (the same parameter path used elsewhere in
the standard). It resolves to that parameter's value, itself evaluated as an
expression:

```yaml
- thingB:
    kind: Sextupole
    MagneticMultipoleP:
      Kn2L: 0.1
- DH1A:
    kind: Bend
    BendP:
      edge_int2: 0.02 * thingB>MagneticMultipoleP.Kn2L   # → 0.002
```

The reference names one specific element (an exact name — pattern matching is
not used in a value expression) and its full parameter path. As with any other
reference, one that cannot be resolved leaves the value as text.

## Controllers

A `Controller` element bundles expressions that drive lattice parameters. Its
`variables:` form a symbol table *scoped to that controller*, and each
`controls:` entry pairs a `parameter` target with an `expression`. During
expansion the controller variables are evaluated against that scoped table, and
each control `expression` is computed and written back into its control entry.
Controller variables may reference one another and, via the
`controller>variable` syntax, variables of another controller:

```yaml
- ps27:
    kind: Controller
    control_type: ABSOLUTE
    variables:
      cur1: 0.023
      cur2: cur1 / c_light      # references an earlier controller variable
    controls:
      - parameter: Qa.*>MagneticMultipoleP.Ks2L
        expression: 0.075*sin(cur1) + 0.3*cur2   # → a number in `expanded`
```

The `parameter` target specification and `control_type` are names, not
expressions, and are left untouched.

## Evaluating a single expression

`evaluate_pals_expression` evaluates one expression string on its own and
returns a `double`, setting an optional `ok` flag:

```cpp
bool ok = false;
double v = evaluate_pals_expression("3.75e7 / c_light^2", &ok);
// ok == true, v == 4.172…e-10
```

This evaluates a *standalone* string, so user-defined constants and variables
are **not** in scope — use `parse_and_expand_PALS` for whole-lattice evaluation,
whose `expanded` tree already has every expression resolved. `ok` is set to
`false` on a parse error, an unknown identifier or species, an unquoted species
name, a `random()`/`random_gauss()` expression (intentionally deferred), or a
non-finite result.
