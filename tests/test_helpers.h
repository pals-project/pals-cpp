#pragma once

// Shared helpers for the split test files. Definitions are `inline` so the
// header can be included from every test translation unit without ODR clashes.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "../src/yaml_c_wrapper.h"

// ─── generic file helpers ───────────────────────────────────────────────────
inline void write_tmp(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

inline void rm_tmp(const std::string& path) {
    std::remove(path.c_str());
}

// Convenience: get a string value and compare, then free it.
inline bool val_eq(YAMLTreeHandle tree, YAMLNodeId node, const char* expected) {
    char* s = as_string(tree, node);
    if (!s) return false;
    bool ok = std::string(s) == expected;
    yaml_free_string(s);
    return ok;
}

// Convenience: compare a node's key, then free it.
inline bool key_eq(YAMLTreeHandle tree, YAMLNodeId node, const char* expected) {
    char* s = get_node_key(tree, node);
    if (!s) return false;
    bool ok = std::string(s) == expected;
    yaml_free_string(s);
    return ok;
}

// ─── expanded-tree navigation ───────────────────────────────────────────────
// Helper: navigate root -> PALS -> facility for a given tree.
inline YAMLNodeId facility_of(YAMLTreeHandle t) {
    YAMLNodeId pals = get_child_by_key(t, get_root(t), "PALS");
    return get_child_by_key(t, pals, "facility");
}

// Navigate root -> PALS -> facility -> the value node keyed `name` inside the
// facility entry whose single key is `entry` (facility is a sequence of
// single-key maps).
inline YAMLNodeId facility_param(YAMLTreeHandle t, const char* entry) {
    YAMLNodeId fac = facility_of(t);
    size_t n = get_size(t, fac);
    for (size_t i = 0; i < n; i++) {
        YAMLNodeId e = get_child_by_index(t, fac, i);
        YAMLNodeId c = get_child_by_key(t, e, entry);
        if (c != YAML_NULL_ID) return c;
    }
    return YAML_NULL_ID;
}

inline double num_val(YAMLTreeHandle t, YAMLNodeId n) {
    char* s = as_string(t, n);
    double v = s ? std::strtod(s, nullptr) : NAN;
    yaml_free_string(s);
    return v;
}

// The first node keyed `key` anywhere in `t`, found depth-first. Used to reach
// an element that expansion inlined into the lattice, wherever it ended up.
inline YAMLNodeId find_by_key(YAMLTreeHandle t, const char* key) {
    std::vector<YAMLNodeId> stack{get_root(t)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* k = get_node_key(t, n);
        bool hit = k && std::string(k) == key;
        yaml_free_string(k);
        if (hit) return n;
        for (size_t i = 0; i < get_size(t, n); i++)
            stack.push_back(get_child_by_index(t, n, i));
    }
    return YAML_NULL_ID;
}

// ─── float comparison ───────────────────────────────────────────────────────
// True if two doubles agree to a relative/absolute tolerance. The max(1.0, ...)
// floors the tolerance at 1e-9 absolute, which keeps comparisons against zero
// workable but makes this useless for values much smaller than 1e-9 — every
// such comparison passes. Use close_rel for those.
inline bool close(double got, double want) {
    return std::fabs(got - want) <= 1e-9 * std::max(1.0, std::fabs(want));
}

// Purely relative comparison, for quantities whose magnitude is far from 1.
inline bool close_rel(double got, double want) {
    return std::fabs(got - want) <= 1e-9 * std::fabs(want);
}

// ─── shared matching fixture ────────────────────────────────────────────────
// A small two-lattice lattice covering constants, variables, elements with
// ungrouped (length) and grouped (BendP.e1) parameters, a sub-line (sub/S1),
// and a repeated element name (B1a in both lattices) to exercise `>>>`.
inline const char* MATCH_YAML =
    "PALS:\n"
    "  facility:\n"
    "    - constants:\n"
    "        - a_const: 0.3 * r_electron\n"
    "        - a_two: 5\n"
    "    - my_var:\n"
    "        kind: variable\n"
    "        value: 37\n"
    "    - lat1:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - main:\n"
    "              kind: BeamLine\n"
    "              line:\n"
    "                - B1a:\n"
    "                    kind: Bend\n"
    "                    length: 1.2\n"
    "                    BendP:\n"
    "                      e1: 0.1\n"
    "                      g_ref: 0.02\n"
    "                - B1b:\n"
    "                    kind: Bend\n"
    "                    length: 1.5\n"
    "                    BendP:\n"
    "                      e1: 0.3\n"
    "                - Q1:\n"
    "                    kind: Quadrupole\n"
    "                    length: 0.5\n"
    "                - sub:\n"
    "                    kind: BeamLine\n"
    "                    line:\n"
    "                      - S1:\n"
    "                          kind: Sextupole\n"
    "                          length: 0.2\n"
    "    - lat2:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - other:\n"
    "              kind: BeamLine\n"
    "              line:\n"
    "                - B1a:\n"
    "                    kind: Bend\n"
    "                    length: 9.9\n";
