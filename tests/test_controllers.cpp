#include "test_helpers.h"

// Controllers: miscellaneous.md (s:controller) for what a Controller is, and
// lattice-construction.md (s:lattice.expand) for where applying the ABSOLUTE
// ones sits in lattice expansion.

namespace {

// Every problem message parse_and_expand_PALS reported, as strings.
std::vector<std::string> problem_list(const struct lattices& lat) {
    std::vector<std::string> msgs;
    for (size_t i = 0; i < lat.problems.count; ++i)
        msgs.emplace_back(lat.problems.items[i]);
    return msgs;
}

// The problems as one string, so a failing REQUIRE shows what they were.
std::string joined(const struct lattices& lat) {
    std::string s;
    for (const std::string& m : problem_list(lat)) s += m + "; ";
    return s;
}

bool any_contains(const std::vector<std::string>& msgs, const char* needle) {
    for (const std::string& m : msgs)
        if (m.find(needle) != std::string::npos) return true;
    return false;
}

void free_all(struct lattices& lat) {
    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

// A one-branch lattice holding `body` elements, wrapped in the PALS/facility
// boilerplate every controller test needs. `defs` is spliced in ahead of the
// lattice. The BeginningEle (1 GeV electrons) is what lets the bookkeeper
// compute reference parameters, so a test that drives a normalized strength can
// check the unnormalized partner derived from it.
std::string lattice_with(const std::string& defs, const std::string& body) {
    return "PALS:\n"
           "  facility:\n" +
           defs +
           "    - lat1:\n"
           "        kind: Lattice\n"
           "        branches:\n"
           "          - main:\n"
           "              kind: BeamLine\n"
           "              line:\n"
           "                - begin:\n"
           "                    kind: BeginningEle\n"
           "                    ReferenceP:\n"
           "                      species_ref: \"electron\"\n"
           "                      E_tot_ref: 1.0e9\n" +
           body +
           "                - fin:\n"
           "                    kind: Marker\n"
           "    - use: \"lat1\"\n";
}

// The value of `group.param` on the first element named `ele` in the expanded
// lattice.
double expanded_param(YAMLTreeHandle t, const char* ele, const char* group,
                      const char* param) {
    YAMLNodeId e = find_by_key(t, ele);
    REQUIRE(e != YAML_NULL_ID);
    YAMLNodeId g = get_child_by_key(t, e, group);
    REQUIRE(g != YAML_NULL_ID);
    return num_val(t, get_child_by_key(t, g, param));
}

}  // namespace

TEST_CASE("parse_and_expand_PALS evaluates controller expressions",
          "[expr][lattices][controller]") {
    const char* path = "tmp_controller.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - my_const:\n"
                  "        kind: constant\n"
                  "        value: 2.0\n"
                  "    - ps27:\n"
                  "        kind: Controller\n"
                  "        control_type: ABSOLUTE\n"
                  "        MetaP:\n"
                  "          description: Model Mitsubishi 800KL\n"
                  "        variables:\n"
                  "          cur1: 0.023\n"
                  "          cur2: 1e8 / c_light\n"
                  "        controls:\n"
                  "          - parameter: Qa.*>MagneticMultipoleP.Ks2L\n"
                  "            expression: 0.075*sin(cur1) + 0.3*cur2\n"
                  "          - parameter: Qb>MagneticMultipoleP.Kn1L\n"
                  "            expression: cur1 * my_const\n"
                  "          - parameter: Qc>MagneticMultipoleP.Kn0\n"
                  "            expression: 0.01 + random_gauss()\n",
                  "                - Qa1:\n"
                  "                    kind: Sextupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Ks2L: 0.0\n"
                  "                - Qb:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.0\n"
                  "                - Qc:\n"
                  "                    kind: Kicker\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn0: 0.5\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.leftover != nullptr);

    const double cur1 = 0.023;
    const double cur2 = 1e8 / 2.99792458e8;

    // Controllers are facility-level, so they are leftover rather than part of
    // the lattice; their expressions are evaluated all the same. An initial
    // value is a constant expression -- it may use the built-in and user
    // constants (c_light here) but no variable.
    YAMLNodeId ps27 = facility_param(lat.leftover, "ps27");
    REQUIRE(ps27 != YAML_NULL_ID);
    YAMLNodeId vars = get_child_by_key(lat.leftover, ps27, "variables");
    REQUIRE(close(num_val(lat.leftover, get_child_by_key(lat.leftover, vars,
                                                         "cur2")),
                  cur2));

    // Each control `expression` is computed and its value stored in place.
    YAMLNodeId controls = get_child_by_key(lat.leftover, ps27, "controls");
    YAMLNodeId c0 = get_child_by_index(lat.leftover, controls, 0);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, c0, "expression")),
                  0.075 * std::sin(cur1) + 0.3 * cur2));
    // Control expressions may reference lattice constants (my_const = 2).
    YAMLNodeId c1 = get_child_by_index(lat.leftover, controls, 1);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, c1, "expression")),
                  cur1 * 2.0));
    // random_gauss() stays deferred, exactly as elsewhere.
    YAMLNodeId c2 = get_child_by_index(lat.leftover, controls, 2);
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, c2, "expression"),
                   "0.01 + random_gauss()"));

    // The `parameter` target spec and `control_type` are names, left untouched,
    // and a MetaP rides along for documentation.
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, c0, "parameter"),
                   "Qa.*>MagneticMultipoleP.Ks2L"));
    REQUIRE(val_eq(lat.leftover,
                   get_child_by_key(lat.leftover, ps27, "control_type"),
                   "ABSOLUTE"));
    REQUIRE(get_child_by_key(lat.leftover, ps27, "MetaP") != YAML_NULL_ID);

    // The combined tree keeps the original controller expression text.
    YAMLNodeId c_ps27 = facility_param(lat.combined, "ps27");
    YAMLNodeId c_controls = get_child_by_key(lat.combined, c_ps27, "controls");
    YAMLNodeId c_c0 = get_child_by_index(lat.combined, c_controls, 0);
    REQUIRE(val_eq(lat.combined,
                   get_child_by_key(lat.combined, c_c0, "expression"),
                   "0.075*sin(cur1) + 0.3*cur2"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("ABSOLUTE controllers drive the parameters they match",
          "[expr][lattices][controller]") {
    // The `parameter` target is a name-matching pattern, so one entry drives
    // every element it matches; an element the pattern does not reach keeps its
    // own value.
    const char* path = "tmp_ctrl_absolute.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - ps1:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          cur: 0.4\n"
                  "        controls:\n"
                  "          - parameter: Qa.*>MagneticMultipoleP.Kn1L\n"
                  "            expression: 2 * cur\n",
                  "                - Qa1:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.11\n"
                  "                - Qa2:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.22\n"
                  "                - Qb1:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.33\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(expanded_param(lat.expanded, "Qa1", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.8));
    REQUIRE(close(expanded_param(lat.expanded, "Qa2", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.8));
    REQUIRE(close(expanded_param(lat.expanded, "Qb1", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.33));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("several ABSOLUTE controllers on one parameter sum",
          "[expr][lattices][controller]") {
    // miscellaneous.md: "the value of the parameter is the sum of the values set
    // by the individual controllers". The parameter's own value is replaced, not
    // added to -- ABSOLUTE control completely determines it. `control_type` is
    // omitted here, so it defaults to ABSOLUTE.
    const char* path = "tmp_ctrl_sum.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - ps1:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          cur: 0.023\n"
                  "        controls:\n"
                  "          - parameter: a_kicker>MagneticMultipoleP.Kn0\n"
                  "            expression: 0.075*sin(cur)\n"
                  "    - ps2:\n"
                  "        kind: Controller\n"
                  "        control_type: ABSOLUTE\n"
                  "        variables:\n"
                  "          cur: 0.044\n"
                  "        controls:\n"
                  "          - parameter: a_kicker>MagneticMultipoleP.Kn0\n"
                  "            expression: 0.123*cur\n",
                  "                - a_kicker:\n"
                  "                    kind: Kicker\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn0: 9.9\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(expanded_param(lat.expanded, "a_kicker", "MagneticMultipoleP",
                                 "Kn0"),
                  0.075 * std::sin(0.023) + 0.123 * 0.044));

    // An omitted control_type is materialized as the ABSOLUTE default.
    YAMLNodeId ps1 = facility_param(lat.leftover, "ps1");
    REQUIRE(val_eq(lat.leftover,
                   get_child_by_key(lat.leftover, ps1, "control_type"),
                   "ABSOLUTE"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("an ABSOLUTE controller creates the parameter it drives",
          "[expr][lattices][controller]") {
    // The element carries no MagneticMultipoleP at all: naming the parameter in
    // a controller is what says it has a value, so the group is created and the
    // bookkeeper then fills its defaults and derived partners in as usual.
    const char* path = "tmp_ctrl_create.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        controls:\n"
                           "          - parameter: a_kicker>MagneticMultipoleP.Kn0L\n"
                           "            expression: 0.25\n",
                           "                - a_kicker:\n"
                           "                    kind: Kicker\n"
                           "                    length: 0.3\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(expanded_param(lat.expanded, "a_kicker", "MagneticMultipoleP",
                                 "Kn0L"),
                  0.25));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a driven parameter feeds the dependent-parameter bookkeeping",
          "[expr][lattices][controller]") {
    // lattice-construction.md orders lattice expansion so that the ABSOLUTE
    // controllers are applied before the reference and dependent parameters are
    // computed. Q1's Kn1L comes from a controller and Q2's is written out; the
    // unnormalized Bn1L the bookkeeper derives from the reference momentum must
    // come out the same for both.
    const char* path = "tmp_ctrl_dependent.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        controls:\n"
                           "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                           "            expression: 0.15 + 0.25\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"
                           "                    length: 0.5\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: 0.0\n"
                           "                - Q2:\n"
                           "                    kind: Quadrupole\n"
                           "                    length: 0.5\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: 0.4\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    double b1 = expanded_param(lat.expanded, "Q1", "MagneticMultipoleP", "Bn1L");
    double b2 = expanded_param(lat.expanded, "Q2", "MagneticMultipoleP", "Bn1L");
    REQUIRE(b1 != 0.0);
    REQUIRE(close_rel(b1, b2));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a RELATIVE controller leaves the lattice alone",
          "[expr][lattices][controller]") {
    // The chromaticity-knob example of miscellaneous.md: at read-in S1 keeps the
    // Kn2L its own definition gives. The knob's expression is still evaluated,
    // for the program that will vary `command` later.
    const char* path = "tmp_ctrl_relative.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - chrom_a:\n"
                  "        kind: Controller\n"
                  "        control_type: RELATIVE\n"
                  "        variables:\n"
                  "          command: 0.4\n"
                  "        controls:\n"
                  "          - parameter: S1>MagneticMultipoleP.Kn2L\n"
                  "            expression: 5.62 * command + 0.02 * command^2\n",
                  "                - S1:\n"
                  "                    kind: Sextupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn2L: 0.33\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(expanded_param(lat.expanded, "S1", "MagneticMultipoleP",
                                 "Kn2L"),
                  0.33));

    YAMLNodeId chrom = facility_param(lat.leftover, "chrom_a");
    YAMLNodeId cc0 = get_child_by_index(
        lat.leftover, get_child_by_key(lat.leftover, chrom, "controls"), 0);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, cc0, "expression")),
                  5.62 * 0.4 + 0.02 * 0.4 * 0.4));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a controller drives another controller's variable",
          "[expr][lattices][controller]") {
    // Controllers form a hierarchy and are evaluated from the top down, so
    // `low>cur` holds what `high` set it to -- not its own initial value -- by
    // the time `low` drives the lattice. The order they are written in does not
    // matter: `low` comes first in the file here.
    const char* path = "tmp_ctrl_hierarchy.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - low:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          cur: 0.1\n"
                  "        controls:\n"
                  "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                  "            expression: 10 * cur\n"
                  "    - high:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          knob: 3\n"
                  "        controls:\n"
                  "          - parameter: low>cur\n"
                  "            expression: knob / 4\n",
                  "                - Q1:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.0\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    // low>cur becomes 3/4, so Q1's Kn1L is 10 * 0.75.
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  7.5));
    YAMLNodeId low = facility_param(lat.leftover, "low");
    YAMLNodeId lvars = get_child_by_key(lat.leftover, low, "variables");
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, lvars, "cur")),
                  0.75));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a variable with no value defaults to zero",
          "[expr][lattices][controller]") {
    const char* path = "tmp_ctrl_default_var.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        variables:\n"
                           "          cur:\n"
                           "        controls:\n"
                           "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                           "            expression: 5 + cur\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: 0.0\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  5.0));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("controller expressions may not reach outside their controller",
          "[expr][lattices][controller][problems]") {
    // Neither a lattice parameter nor another controller's variable may appear
    // in an initial value or a control expression -- both are spelled with `>`,
    // and both would make the evaluation order depend on more than the
    // `controls` hierarchy.
    const char* path = "tmp_ctrl_refs.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - ps1:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          cur: 0.5\n"
                  "    - ps2:\n"
                  "        kind: Controller\n"
                  "        variables:\n"
                  "          derived: ps1>cur * 2\n"
                  "        controls:\n"
                  "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                  "            expression: Q1>length + 1\n",
                  "                - Q1:\n"
                  "                    kind: Quadrupole\n"
                  "                    length: 0.5\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.0\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    std::vector<std::string> msgs = problem_list(lat);
    REQUIRE(any_contains(msgs, "variable 'derived' may not reference"));
    REQUIRE(any_contains(msgs, "control expression may not reference"));

    // Q1 keeps its own value: the rejected entry drives nothing.
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.0));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a variable initial value may not reference a variable",
          "[expr][lattices][controller][problems]") {
    // An initial value is a constant expression: it may use the built-in and
    // user constants but no variable, not even one of its own controller's, so
    // that no initial value has to be evaluated before any other.
    const char* path = "tmp_ctrl_var_ref.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        variables:\n"
                           "          cur1: 0.023\n"
                           "          cur2: cur1 / c_light\n"
                           "        controls:\n"
                           "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                           "            expression: cur1 + cur2\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: 0.0\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "variable 'cur2': an initial value may not reference "
                         "the variable 'cur1'"));

    // The rejected initial value falls back to the default, zero, so the
    // control expression is still evaluated: cur1 + 0.
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.023));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a constant is not mistaken for a variable in an initial value",
          "[expr][lattices][controller]") {
    // The check is on whole identifiers: a variable named `light` must not make
    // `c_light` look like a reference to it.
    const char* path = "tmp_ctrl_var_substring.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        variables:\n"
                           "          light: 2\n"
                           "          scaled: 1e8 / c_light\n"
                           "        controls:\n"
                           "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                           "            expression: light * scaled\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: 0.0\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  2.0 * (1e8 / 2.99792458e8)));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a circular control hierarchy is reported",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_cycle.pals.yaml";
    write_tmp(path, lattice_with("    - a:\n"
                                 "        kind: Controller\n"
                                 "        variables:\n"
                                 "          va: 1\n"
                                 "        controls:\n"
                                 "          - parameter: b>vb\n"
                                 "            expression: va\n"
                                 "    - b:\n"
                                 "        kind: Controller\n"
                                 "        variables:\n"
                                 "          vb: 2\n"
                                 "        controls:\n"
                                 "          - parameter: a>va\n"
                                 "            expression: vb\n",
                                 "                - Q1:\n"
                                 "                    kind: Quadrupole\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "circular control hierarchy: 'a', 'b'"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a parameter may not be driven by both controller types",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_mixed.pals.yaml";
    write_tmp(path,
              lattice_with(
                  "    - ps1:\n"
                  "        kind: Controller\n"
                  "        control_type: ABSOLUTE\n"
                  "        controls:\n"
                  "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                  "            expression: 0.5\n"
                  "    - knob:\n"
                  "        kind: Controller\n"
                  "        control_type: RELATIVE\n"
                  "        controls:\n"
                  "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                  "            expression: 0.25\n",
                  "                - Q1:\n"
                  "                    kind: Quadrupole\n"
                  "                    MagneticMultipoleP:\n"
                  "                      Kn1L: 0.11\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "Q1>MagneticMultipoleP.Kn1L is controlled by both an "
                         "ABSOLUTE and a RELATIVE controller"));
    // Neither is applied, so the element keeps its own value.
    REQUIRE(close(expanded_param(lat.expanded, "Q1", "MagneticMultipoleP",
                                 "Kn1L"),
                  0.11));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a controlled parameter may not also be delayed",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_delayed.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        controls:\n"
                           "          - parameter: Q1>MagneticMultipoleP.Kn1L\n"
                           "            expression: 0.5\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"
                           "                    MagneticMultipoleP:\n"
                           "                      Kn1L: expr(0.1 + 0.2)\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "Q1>MagneticMultipoleP.Kn1L is both controlled and "
                         "assigned a delayed evaluation expression"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a controller target that matches nothing is reported",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_nomatch.pals.yaml";
    write_tmp(path,
              lattice_with("    - ps1:\n"
                           "        kind: Controller\n"
                           "        controls:\n"
                           "          - parameter: nosuch>MagneticMultipoleP.Kn1L\n"
                           "            expression: 0.5\n",
                           "                - Q1:\n"
                           "                    kind: Quadrupole\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "target 'nosuch>MagneticMultipoleP.Kn1L' matches "
                         "nothing in the expanded lattice"));

    free_all(lat);
    rm_tmp(path);
}

// A branch written as `- ln:` with no `inherit` used not to expand at all, so
// every element in it was missing and the first sign of it was the controller
// target failing to match. The target resolves, and drives its element.
TEST_CASE("a controller reaches an element in a branch named by its key",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_default_branch.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - begin:\n"
              "        kind: BeginningEle\n"
              "        ReferenceP:\n"
              "          species_ref: \"electron\"\n"
              "          E_tot_ref: 1.0e9\n"
              "    - q:\n"
              "        kind: Quadrupole\n"
              "        length: 1\n"
              "        MagneticMultipoleP:\n"
              "          Kn1: 256\n"
              "    - ln:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - begin\n"
              "          - q\n"
              "    - machine:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ln:\n"
              "              periodic: false\n"
              "    - oo:\n"
              "        kind: Controller\n"
              "        control_type: ABSOLUTE\n"
              "        variables:\n"
              "          vv: 2\n"
              "        controls:\n"
              "          - parameter: q>MagneticMultipoleP.Kn1\n"
              "            expression: 4^vv\n"
              "    - use: \"machine\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE_FALSE(any_contains(problem_list(lat), "matches nothing"));
    REQUIRE(joined(lat).find("branch 'ln'") == std::string::npos);
    REQUIRE(close(expanded_param(lat.expanded, "q", "MagneticMultipoleP", "Kn1"),
                  16.0));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("an unknown control_type is reported",
          "[expr][lattices][controller][problems]") {
    const char* path = "tmp_ctrl_badtype.pals.yaml";
    write_tmp(path, lattice_with("    - ps1:\n"
                                 "        kind: Controller\n"
                                 "        control_type: SOMETHING\n"
                                 "        controls:\n"
                                 "          - parameter: Q1>length\n"
                                 "            expression: 0.5\n",
                                 "                - Q1:\n"
                                 "                    kind: Quadrupole\n"
                                 "                    length: 0.2\n"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "control_type must be ABSOLUTE or RELATIVE, not "
                         "SOMETHING"));

    free_all(lat);
    rm_tmp(path);
}
