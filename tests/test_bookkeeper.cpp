#include "test_helpers.h"

#include <cmath>

// ============================================================
// ELEMENT BOOKKEEPER
// ============================================================
//
// The element bookkeeper (run_element_bookkeeper in pals_expand.cpp) walks each
// branch after expression evaluation and fills in the position-dependent output
// parameters: reference (species / energy / momentum / time), floor placement,
// s_position, and the field-dependent multipole / bend strengths. The floor math
// lives in the quaternion module pals_floor.cpp. These tests pin the values it
// produces for a small branch with a drift, a quadrupole, and a bend.

namespace {

// A branch: BeginningEle (1 GeV electrons) -> 2 m drift -> 0.5 m quad with a
// normal quadrupole field -> 1.5 m bend of 0.15 rad -> 1 m drift -> Marker.
const char* BOOKKEEPER_YAML =
    "PALS:\n"
    "  facility:\n"
    "    - test_line:\n"
    "        kind: BeamLine\n"
    "        line:\n"
    "          - begin:\n"
    "              kind: BeginningEle\n"
    "              ReferenceP:\n"
    "                species_ref: \"electron\"\n"
    "                E_tot_ref: 1.0e9\n"
    "          - d1:\n"
    "              kind: Drift\n"
    "              length: 2.0\n"
    "          - q1:\n"
    "              kind: Quadrupole\n"
    "              length: 0.5\n"
    "              MagneticMultipoleP:\n"
    "                Bn1: 1.2\n"
    "          - b1:\n"
    "              kind: Bend\n"
    "              length: 1.5\n"
    "              BendP:\n"
    "                angle_ref: 0.15\n"
    "          - d2:\n"
    "              kind: Drift\n"
    "              length: 1.0\n"
    "          - end:\n"
    "              kind: Marker\n"
    "    - lat:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - test_line\n"
    "    - use: lat\n";

// Value of ele>group.param in the expanded tree, as a double.
double param_num(YAMLTreeHandle t, const char* ele, const char* group,
                 const char* param) {
    YAMLNodeId e = find_by_key(t, ele);
    YAMLNodeId g = group ? get_child_by_key(t, e, group) : e;
    return num_val(t, get_child_by_key(t, g, param));
}

// Expand BOOKKEEPER_YAML from a temp file (the only expansion entry point takes
// a filename). The caller frees the returned trees and problem list.
struct lattices expand_bookkeeper() {
    const char* path = "tmp_bookkeeper.pals.yaml";
    write_tmp(path, BOOKKEEPER_YAML);
    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    rm_tmp(path);
    return lat;
}

}  // namespace

TEST_CASE("Bookkeeper fills reference energy, momentum, and time",
          "[bookkeeper]") {
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    // pc = sqrt(E^2 - m^2) with the electron rest energy ~0.511 MeV; every
    // element downstream carries the same species and (drifts don't change it)
    // energy/momentum.
    const double m_e = 510998.95;  // eV, apc electron rest energy
    const double pc = std::sqrt(1.0e9 * 1.0e9 - m_e * m_e);
    for (const char* ele : {"begin", "d1", "q1", "b1", "d2", "end"}) {
        REQUIRE(close_rel(param_num(t, ele, "ReferenceP", "pc_ref"), pc));
        REQUIRE(close_rel(param_num(t, ele, "ReferenceP", "E_tot_ref"), 1.0e9));
    }

    // Reference time is zero at the start and grows by length / (beta c) across
    // each element. beta = pc / E.
    const double c = 2.99792458e8;
    const double beta = pc / 1.0e9;
    REQUIRE(close(param_num(t, "begin", "ReferenceP", "time_ref"), 0.0));
    REQUIRE(close_rel(param_num(t, "q1", "ReferenceP", "time_ref"),
                      2.0 / (beta * c)));               // after the 2 m drift
    REQUIRE(close_rel(param_num(t, "end", "ReferenceP", "time_ref"),
                      5.0 / (beta * c)));               // total path length 5 m

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper accumulates s_position from element lengths",
          "[bookkeeper]") {
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    // s_position is the upstream end of each element: 0, then the running sum of
    // the preceding element lengths (0, 2, 0.5, 1.5, 1).
    REQUIRE(close(param_num(t, "begin", nullptr, "s_position"), 0.0));
    REQUIRE(close(param_num(t, "d1", nullptr, "s_position"), 0.0));
    REQUIRE(close(param_num(t, "q1", nullptr, "s_position"), 2.0));
    REQUIRE(close(param_num(t, "b1", nullptr, "s_position"), 2.5));
    REQUIRE(close(param_num(t, "d2", nullptr, "s_position"), 4.0));
    REQUIRE(close(param_num(t, "end", nullptr, "s_position"), 5.0));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper derives the normalized multipole strength", "[bookkeeper]") {
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    // Kn1 = (q / P0) Bn1 = (charge * c_light / pc) * Bn1, with charge = -1 for
    // the electron, referenced to q1's upstream momentum.
    const double m_e = 510998.95;
    const double pc = std::sqrt(1.0e9 * 1.0e9 - m_e * m_e);
    const double factor = -1.0 * 2.99792458e8 / pc;
    REQUIRE(close_rel(param_num(t, "q1", "MagneticMultipoleP", "Kn1"),
                      factor * 1.2));
    // The user-supplied field value is left as written.
    REQUIRE(close(param_num(t, "q1", "MagneticMultipoleP", "Bn1"), 1.2));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper caps each branch with a branch_end Placeholder",
          "[bookkeeper]") {
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    // The branch gains a trailing element named `branch_end` of kind Placeholder.
    YAMLNodeId be = find_by_key(t, "branch_end");
    REQUIRE(be != YAML_NULL_ID);
    REQUIRE(val_eq(t, get_child_by_key(t, be, "kind"), "Placeholder"));

    // It holds the branch's final state: the downstream end of the last element.
    // Here the last real element is the zero-length Marker `end`, so branch_end's
    // placement, reference, and s-position match it exactly.
    REQUIRE(close(param_num(t, "branch_end", nullptr, "s_position"),
                  param_num(t, "end", nullptr, "s_position")));
    REQUIRE(close(param_num(t, "branch_end", "FloorP", "z"),
                  param_num(t, "end", "FloorP", "z")));
    REQUIRE(close(param_num(t, "branch_end", "FloorP", "theta"), -0.15));

    const double m_e = 510998.95;
    const double pc = std::sqrt(1.0e9 * 1.0e9 - m_e * m_e);
    REQUIRE(close_rel(param_num(t, "branch_end", "ReferenceP", "pc_ref"), pc));
    REQUIRE(val_eq(t, get_child_by_key(t, get_child_by_key(t, be, "ReferenceP"),
                                       "species_ref"),
                   "electron"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

// A branch with a single RFCavity, whose RFP body is spliced in from `rfp_body`
// (raw YAML at 16-space indentation). Lets each RF test set voltage/gradient/
// L_active as it likes.
static std::string rf_yaml(const std::string& rfp_body) {
    return std::string(
               "PALS:\n"
               "  facility:\n"
               "    - test_line:\n"
               "        kind: BeamLine\n"
               "        line:\n"
               "          - begin:\n"
               "              kind: BeginningEle\n"
               "              ReferenceP:\n"
               "                species_ref: \"electron\"\n"
               "                E_tot_ref: 1.0e9\n"
               "          - cav:\n"
               "              kind: RFCavity\n"
               "              length: 0.4\n"
               "              RFP:\n") +
           rfp_body +
           "    - lat:\n"
           "        kind: Lattice\n"
           "        branches:\n"
           "          - test_line\n"
           "    - use: lat\n";
}

static struct lattices expand_rf(const std::string& rfp_body) {
    const char* path = "tmp_rf.pals.yaml";
    write_tmp(path, rf_yaml(rfp_body));
    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    rm_tmp(path);
    return lat;
}

static bool any_problem_contains(struct lattices& lat, const char* needle) {
    for (size_t i = 0; i < lat.problems.count; ++i)
        if (std::string(lat.problems.items[i]).find(needle) != std::string::npos)
            return true;
    return false;
}

// A branch: BeginningEle (1 GeV electrons) followed by a single element whose
// definition is spliced in from `ele_body` (raw YAML at 10-space indentation,
// e.g. "          - el:\n              kind: Bend\n ..."). Lets a test drive one
// element's dependent parameters in isolation.
static std::string one_ele_yaml(const std::string& ele_body) {
    return std::string(
               "PALS:\n"
               "  facility:\n"
               "    - test_line:\n"
               "        kind: BeamLine\n"
               "        line:\n"
               "          - begin:\n"
               "              kind: BeginningEle\n"
               "              ReferenceP:\n"
               "                species_ref: \"electron\"\n"
               "                E_tot_ref: 1.0e9\n") +
           ele_body +
           "    - lat:\n"
           "        kind: Lattice\n"
           "        branches:\n"
           "          - test_line\n"
           "    - use: lat\n";
}

static struct lattices expand_src(const std::string& src) {
    const char* path = "tmp_dep.pals.yaml";
    write_tmp(path, src);
    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    rm_tmp(path);
    return lat;
}

// The reference momentum and the field<->normalized conversion factor for a
// 1 GeV electron (charge -1), shared by the multipole / bend-field tests.
static double electron_pc() {
    const double m_e = 510998.95;  // eV, apc electron rest energy
    return std::sqrt(1.0e9 * 1.0e9 - m_e * m_e);
}
static double electron_factor() { return -1.0 * 2.99792458e8 / electron_pc(); }

TEST_CASE("Bookkeeper derives RF voltage from gradient and active length",
          "[bookkeeper]") {
    // Only the gradient is given; L_active defaults to the element length 0.4, so
    // voltage = gradient * L_active = 1e8 * 0.4 = 4e7. No inconsistency reported.
    struct lattices lat = expand_rf("                gradient: 1.0e8\n");
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close_rel(param_num(t, "cav", "RFP", "voltage"), 4.0e7));
    REQUIRE(close(param_num(t, "cav", "RFP", "gradient"), 1.0e8));
    REQUIRE_FALSE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper derives RF gradient from voltage and active length",
          "[bookkeeper]") {
    // Only the voltage is given; gradient = voltage / L_active = 8e6 / 0.4 = 2e7.
    struct lattices lat = expand_rf("                voltage: 8.0e6\n");
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close_rel(param_num(t, "cav", "RFP", "gradient"), 2.0e7));
    REQUIRE(close(param_num(t, "cav", "RFP", "voltage"), 8.0e6));
    REQUIRE_FALSE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper accepts consistent RF voltage and gradient",
          "[bookkeeper]") {
    // Both set and agreeing (2e7 * 0.4 = 8e6): no problem, values untouched.
    struct lattices lat =
        expand_rf("                gradient: 2.0e7\n"
                  "                voltage: 8.0e6\n");
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close(param_num(t, "cav", "RFP", "gradient"), 2.0e7));
    REQUIRE(close(param_num(t, "cav", "RFP", "voltage"), 8.0e6));
    REQUIRE_FALSE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper flags inconsistent RF voltage and gradient",
          "[bookkeeper]") {
    // The PALSJulia/alocal case: gradient 1e8 with L_active 0.4 implies voltage
    // 4e7, but voltage is set to 45. That must be reported as inconsistent.
    struct lattices lat =
        expand_rf("                gradient: 1.0e8\n"
                  "                voltage: 45\n");

    REQUIRE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper honors an explicit L_active for RF derivation",
          "[bookkeeper]") {
    // L_active (0.25) differs from the element length (0.4); the derived voltage
    // follows the active length: 1e8 * 0.25 = 2.5e7.
    struct lattices lat =
        expand_rf("                gradient: 1.0e8\n"
                  "                L_active: 0.25\n");
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close_rel(param_num(t, "cav", "RFP", "voltage"), 2.5e7));
    REQUIRE_FALSE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper propagates floor coordinates through a bend",
          "[bookkeeper]") {
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    // Everything up to the bend lies along +Z at the origin, unrotated.
    REQUIRE(close(param_num(t, "b1", "FloorP", "z"), 2.5));
    REQUIRE(close(param_num(t, "b1", "FloorP", "x"), 0.0));
    REQUIRE(close(param_num(t, "b1", "FloorP", "theta"), 0.0));

    // The bend has arc length 1.5 and angle 0.15, so rho = 10. Its downstream
    // frame (= d2's upstream frame) is displaced by (rho(cos a - 1), 0,
    // rho sin a) from the bend entrance at z = 2.5, and a positive bend angle
    // decreases theta.
    const double rho = 1.5 / 0.15;
    REQUIRE(close(param_num(t, "d2", "FloorP", "x"), rho * (std::cos(0.15) - 1.0)));
    REQUIRE(close(param_num(t, "d2", "FloorP", "z"), 2.5 + rho * std::sin(0.15)));
    REQUIRE(close(param_num(t, "d2", "FloorP", "theta"), -0.15));

    // The final 1 m drift advances along the rotated z-axis (azimuth -0.15).
    REQUIRE(close(param_num(t, "end", "FloorP", "theta"), -0.15));
    REQUIRE(close(param_num(t, "end", "FloorP", "x"),
                  param_num(t, "d2", "FloorP", "x") + 1.0 * std::sin(-0.15)));
    REQUIRE(close(param_num(t, "end", "FloorP", "z"),
                  param_num(t, "d2", "FloorP", "z") + 1.0 * std::cos(-0.15)));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper derives all bend strength forms from the angle",
          "[bookkeeper]") {
    // b1 gives only angle_ref 0.15 over length 1.5. The expanded lattice holds
    // every equivalent form: g_ref = angle/length = 0.1, rho_ref = 1/g_ref = 10,
    // and bend_field_ref = g_ref / factor (factor = -c/pc for the electron).
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close(param_num(t, "b1", "BendP", "angle_ref"), 0.15));
    REQUIRE(close(param_num(t, "b1", "BendP", "g_ref"), 0.1));
    REQUIRE(close(param_num(t, "b1", "BendP", "rho_ref"), 10.0));
    REQUIRE(close_rel(param_num(t, "b1", "BendP", "bend_field_ref"),
                      0.1 / electron_factor()));

    // The non-zero enum defaults of the present BendP group are made explicit.
    YAMLNodeId bp = get_child_by_key(t, find_by_key(t, "b1"), "BendP");
    REQUIRE(val_eq(t, get_child_by_key(t, bp, "ref_geometry"), "arc"));
    REQUIRE(val_eq(t, get_child_by_key(t, bp, "multipole_geometry"),
                   "follows_ref_geometry"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper derives integrated and normalized multipole forms",
          "[bookkeeper]") {
    // q1 gives only Bn1 1.2 over length 0.5. The expanded lattice holds all four
    // equivalent forms: the field (Bn1), its length integral (Bn1L = L*Bn1), the
    // normalized value (Kn1 = factor*Bn1), and the normalized integral (Kn1L).
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;
    const double f = electron_factor();

    REQUIRE(close(param_num(t, "q1", "MagneticMultipoleP", "Bn1"), 1.2));
    REQUIRE(close(param_num(t, "q1", "MagneticMultipoleP", "Bn1L"), 0.6));
    REQUIRE(close_rel(param_num(t, "q1", "MagneticMultipoleP", "Kn1"), f * 1.2));
    REQUIRE(close_rel(param_num(t, "q1", "MagneticMultipoleP", "Kn1L"), f * 0.6));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper derives the integrated electric multipole", "[bookkeeper]") {
    // Electric multipoles have no normalized form; only length-integration ties
    // En2 to En2L = length * En2 = 0.5 * 5 = 2.5.
    struct lattices lat = expand_src(one_ele_yaml(
        "          - em:\n"
        "              kind: Sextupole\n"
        "              length: 0.5\n"
        "              ElectricMultipoleP:\n"
        "                En2: 5.0\n"));
    YAMLTreeHandle t = lat.expanded;

    REQUIRE(close(param_num(t, "em", "ElectricMultipoleP", "En2"), 5.0));
    REQUIRE(close(param_num(t, "em", "ElectricMultipoleP", "En2L"), 2.5));
    REQUIRE_FALSE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper flags an inconsistent bend strength", "[bookkeeper]") {
    // angle_ref 0.15 over length 1.5 implies g_ref 0.1, but g_ref is set to 0.2.
    struct lattices lat = expand_src(one_ele_yaml(
        "          - bnd:\n"
        "              kind: Bend\n"
        "              length: 1.5\n"
        "              BendP:\n"
        "                angle_ref: 0.15\n"
        "                g_ref: 0.2\n"));

    REQUIRE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper flags an inconsistent multipole component",
          "[bookkeeper]") {
    // Bn1 1.2 over length 0.5 implies Bn1L 0.6, but Bn1L is set to 0.5.
    struct lattices lat = expand_src(one_ele_yaml(
        "          - sx:\n"
        "              kind: Sextupole\n"
        "              length: 0.5\n"
        "              MagneticMultipoleP:\n"
        "                Bn1: 1.2\n"
        "                Bn1L: 0.5\n"));

    REQUIRE(any_problem_contains(lat, "inconsistent"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper materializes RF enum defaults and L_active",
          "[bookkeeper]") {
    // A present RFP group gains its non-zero enum defaults and an explicit
    // L_active (= element length 0.4), alongside the derived voltage.
    struct lattices lat = expand_rf("                gradient: 1.0e8\n");
    YAMLTreeHandle t = lat.expanded;

    YAMLNodeId rfp = get_child_by_key(t, find_by_key(t, "cav"), "RFP");
    REQUIRE(val_eq(t, get_child_by_key(t, rfp, "cavity_type"), "STANDING_WAVE"));
    REQUIRE(val_eq(t, get_child_by_key(t, rfp, "zero_phase"), "ACCELERATING"));
    REQUIRE(close(param_num(t, "cav", "RFP", "L_active"), 0.4));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

TEST_CASE("Bookkeeper leaves absent parameter groups alone", "[bookkeeper]") {
    // Groups not present in the file are not materialized: a plain drift gains no
    // BendP/RFP/multipole groups. Only ReferenceP and FloorP are parser-added.
    struct lattices lat = expand_bookkeeper();
    YAMLTreeHandle t = lat.expanded;
    YAMLNodeId d1 = find_by_key(t, "d1");

    REQUIRE(get_child_by_key(t, d1, "BendP") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(t, d1, "RFP") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(t, d1, "MagneticMultipoleP") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(t, d1, "ReferenceP") != YAML_NULL_ID);
    REQUIRE(get_child_by_key(t, d1, "FloorP") != YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}
