#ifndef PALS_UTIL_H
#define PALS_UTIL_H

// Small helpers shared between the PALS expansion pipeline (pals_expand.cpp)
// and PALS name matching / parameter lookup (pals_match.cpp): dotted
// parameter-path utilities and the expr(...) unwrapper. Not part of the public
// C API declared in yaml_c_wrapper.h.

#include <cstddef>
#include <string>
#include <vector>

#include <ryml.hpp>

// Get the string value of a keyed child of `parent`; "" if not found.
std::string child_val_str(const ryml::Tree& t, size_t parent, const char* key);

// Split a dotted parameter path ("a.b.c") into its components.
std::vector<std::string> split_dots(const std::string& s);

// Walk a dotted parameter path down from an element's definition map. Each
// component is an exact key lookup. Returns the final node, or ryml::NONE if
// any component is missing.
size_t resolve_param_path(const ryml::Tree& t, size_t ele,
                          const std::vector<std::string>& path);

// Trims surrounding whitespace and, if the whole value is `expr(...)`, unwraps
// it. `was_expr` reports whether an `expr(...)` wrapper was present.
std::string strip_expr_wrapper(const std::string& s, bool& was_expr);

#endif  // PALS_UTIL_H
