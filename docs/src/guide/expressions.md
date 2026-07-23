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

The argument may also be a **user constant or variable whose value is a species
name**, passed by name (without quotes). This lets one definition fix the species
and every expression refer to it:

```yaml
- constants:
    species: "#3He"                 # a species-valued constant
    b_const: 0.45 * mass_of(species) # resolves species -> "#3He"
```

Such a species-valued constant may also be referenced **directly** wherever a
species name is expected: a bare identifier used as a parameter value is
replaced by the constant's species string in the expanded tree.

```yaml
- begin:
    kind: BeginningEle
    ReferenceP:
      species_ref: species          # -> "#3He" in `expanded`
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
each control `expression` is computed and written back into its control entry:

```yaml
- ps27:
    kind: Controller
    control_type: ABSOLUTE
    variables:
      cur1: 0.023
      cur2: 1e8 / c_light       # a constant expression, as initial values are
    controls:
      - parameter: Qa.*>MagneticMultipoleP.Ks2L
        expression: 0.075*sin(cur1) + 0.3*cur2   # → a number in `expanded`
```

A variable's **initial value is a constant expression**: it may use the built-in
and user-defined constants and the functions, but no variable at all — not even
another variable of the same controller. That is what makes the initial values
independent of the order they are written in. A variable written with no value
is zero.

A control **`expression` uses its own controller's variables**, and nothing from
outside the controller: neither an initial value nor a control expression may
reference a lattice parameter or another controller's variable. Both of those
are spelled with `>`, and keeping them out is what makes the evaluation order
well defined. The `parameter` target specification and `control_type` are names,
not expressions, and are left untouched.

### How controllers are applied

The `parameter` target is a [name-matching string](../api.md#name-matching), so one entry
drives every element it matches. What happens to those parameters depends on
`control_type`:

- **`ABSOLUTE`** (the default, materialized into the tree when the key is
  omitted) means the controllers determine the parameter outright. Each matched
  parameter is set to the **sum** of the values of every ABSOLUTE controller
  driving it, replacing whatever the element itself gave. A parameter the
  element does not carry is created, group and all.
- **`RELATIVE`** describes a knob — an orbit bump, a chromaticity family —
  whose *changes* the simulation program applies after the file is read. It
  changes nothing during expansion: the parameter keeps the value its element
  gives, and only the control expression is evaluated, for the program's use.

The controllers are applied after the branches are expanded and before the
element bookkeeper runs, so the reference, floor and dependent parameters are
computed from the driven values — a driven `Kn1L` yields the matching `Bn1L`.

A controller may also drive another controller's variable, which is named
`controller>variable`:

```yaml
- high:
    kind: Controller
    variables:
      knob: 3
    controls:
      - parameter: low>cur       # a variable of the `low` controller
        expression: knob / 4
```

Controllers therefore form a hierarchy, which is evaluated from the top
downwards; a driven variable takes its driving value instead of its own initial
value. The order controllers are written in the file does not matter. These are
reported as problems: a circular control hierarchy, a parameter driven by both
an ABSOLUTE and a RELATIVE controller, a parameter that is both controlled and
assigned a delayed (`expr(...)`) expression, a target that matches nothing in
the expanded lattice, and a `control_type` that is neither `ABSOLUTE` nor
`RELATIVE`.

A control expression containing `random()`/`random_gauss()` is deferred like any
other, keeping its text — and so drives nothing, leaving the expanded tree
reproducible.

## Set commands

A `set` writes a value into every parameter its `parameter` name-matching string
selects. In the `value` expression, `PARAMETER` stands for the current value of
the parameter being written and `SELF` for the element that owns it:

```yaml
- set:
    parameter: B1.*>BendP.e1
    value: 2*PARAMETER + atan(SELF.BendP.g_ref)
```

Pattern matching applies to the `parameter` target only, never inside `value`.
The compact form is a list of `target: value` pairs with no error terms:

```yaml
- sets:
    - Q1>MagneticMultipoleP.Kn1L: 0.25
    - D1>length: 2 * 1.5
```

A parameter of a known element that has not been written reads as **zero**, so
`PARAMETER + 0.02` works on an element that carries no such group yet — the
group is created. The exception is a parameter that is *still to be derived*:
if any member of its linked family is written (the four interchangeable forms
`BnN`/`BnNL`/`KnN`/`KnNL` of a magnetic multipole component, the two forms of an
electric one, or the tied BendP geometry), then its value comes from the
bookkeeper and reading it before that is an error:

```yaml
- Q1:
    kind: Quadrupole
    MagneticMultipoleP:
      Ks1: 0.34
- set:
    parameter: Q2>MagneticMultipoleP.Bs1
    value: Q1>MagneticMultipoleP.Bs1   # error: Bs1 needs the reference momentum
```

`absolute_error` and `relative_error` are read but **not applied**: the standard
gives the error magnitude (`absolute_error + relative_error * |value|`) but not
its distribution, and this library never invents randomness. The deterministic
`value` is written and a problem is reported. A `value` that itself calls
`random()`/`random_gauss()` is deferred like any other, leaving its parameter
untouched.

### `expand_lattice` and where a set acts

An `expand_lattice` node divides the `facility` list in two, and which side a
set sits on decides what it writes:

```yaml
facility:
  - q1:
      kind: Quadrupole
      MagneticMultipoleP:
        Kn1L: 0.375
  - bline:
      kind: BeamLine
      line:
        - q1:
            repeat: 3
  - lat: {kind: Lattice, branches: bline}

  - expand_lattice

  - set:
      parameter: lat>>>q1>MagneticMultipoleP.Kn1L
      value: PARAMETER * (1 + 1e-4*random_gauss())
```

- **Before** `expand_lattice` a set acts on the element *definitions*, and only
  on those defined earlier in the list — an element defined after it is
  untouched. One definition is written once, so all three copies of `q1` would
  inherit the same value.
- **After** `expand_lattice` a set acts on the already-expanded lattice, so each
  of the three copies is a separate element and is written separately. The
  bookkeeper has already run at this point, so a value may use computed
  parameters such as `SELF.s_position`.

The full expansion order is: pre-expansion sets → branch and fork expansion →
ABSOLUTE controllers → bookkeeper → post-expansion sets → ABSOLUTE controllers
again → bookkeeper again. Applying the controllers last is what keeps a
controller authoritative over a parameter a later set also writes; the second
bookkeeper pass recomputes the reference, floor and s-position values, and the
family members a post-expansion set invalidated.

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
