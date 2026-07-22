#ifndef PALS_CHECK_H
#define PALS_CHECK_H

// Spelling checks against the fixed vocabulary PALS defines: element kinds and
// parameter group names. A misspelling is not a structural error -- a `FlorP`
// group or a `kind: marker` parses as valid YAML and simply goes unrecognised
// later -- so it is caught here and reported rather than silently ignored.
// Not part of the public C API declared in yaml_c_wrapper.h.

#include <string>
#include <vector>

#include <ryml.hpp>

// Check every `kind` value and every parameter group name in `t`, appending one
// message per unrecognised name to `problems` (with a "did you mean" suggestion
// where a near match exists). Extension data, which is outside the standard by
// definition, is skipped: a dictionary holding an `extension` key, and any name
// registered under `PALS: extension_labels`.
void check_pals_names(const ryml::Tree& t, std::vector<std::string>& problems);

#endif  // PALS_CHECK_H
