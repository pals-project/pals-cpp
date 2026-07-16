#ifndef PALS_EXPRESSION_H
#define PALS_EXPRESSION_H

/**
 * @file pals_expression.h
 * @brief Evaluator for PALS mathematical expressions (the "Mathematical
 *        Expressions", "Functions", and "Constants and Variables" sections of
 *        the PALS standard).
 *
 * Particle-data functions (mass_of, charge_of, anomalous_moment_of), which take
 * a quoted species name such as mass_of("#3He"), and the
 * named physical constants (c_light, r_electron, ...) are sourced from
 * AtomicAndPhysicalConstantsCLib so the whole PALS/Bmad ecosystem shares one
 * set of numbers.
 */

#include <functional>
#include <string>

namespace pals {

// Outcome of evaluating a PALS expression.
//   ok=true                  -> `value` holds the finite numeric result.
//   deferred=true            -> the expression contains random()/random_gauss(),
//                               which must stay unevaluated to keep the expanded
//                               tree reproducible. `value` is unspecified.
//   ok=false, deferred=false -> not a (valid) evaluable expression: parse error,
//                               unknown identifier, unknown species, or a
//                               non-finite result. The caller should leave the
//                               original text untouched.
struct EvalOutcome {
  bool ok = false;
  bool deferred = false;
  double value = 0.0;
};

// Looks up a user-defined constant/variable by name. Returns true and sets
// `out` if the name is known. Built-in PALS constants (pi, c_light, ...) are
// resolved inside the evaluator and need not be provided here.
using SymbolLookup = std::function<bool(const std::string& name, double& out)>;

// Evaluates `text` as a PALS mathematical expression. A leading `expr(...)`
// wrapper, if present, must be stripped by the caller. `lookup` resolves user
// symbols; an empty std::function is fine when none are available.
EvalOutcome eval_expression(const std::string& text, const SymbolLookup& lookup);

// If `name` is a built-in PALS constant, sets `out` to its value and returns
// true; otherwise returns false.
bool builtin_constant(const std::string& name, double& out);

}  // namespace pals

#endif  // PALS_EXPRESSION_H
