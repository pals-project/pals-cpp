#ifndef PALS_CHECK_H
#define PALS_CHECK_H

// Checks against the fixed vocabularies PALS defines: element kinds, parameter
// group names, and the words an enumerated parameter accepts. Nothing here is a
// structural fault -- a `FlorP` group, a `kind: marker` or a `shape: eliptical`
// all parse as valid YAML and simply go unrecognised later -- which is exactly
// why they are caught here. Expansion has nothing to object to, so the trees
// come back sound but holding something other than what the author wrote, with
// no other pass to notice. Reported as PROBLEM_ERROR for that reason.
// Not part of the public C API declared in yaml_c_wrapper.h.

#include <string>
#include <vector>

#include <ryml.hpp>

#include "pals_problem.h"

// Check every `kind` value and every parameter group name in `t`, appending one
// message per unrecognised name to `problems` (with a "did you mean" suggestion
// where a near match exists). Extension data, which is outside the standard by
// definition, is skipped: a dictionary holding an `extension` key, and any name
// registered under `PALS: extension_labels`.
void check_pals_names(const ryml::Tree& t, pals::ProblemList& problems);

#endif  // PALS_CHECK_H
