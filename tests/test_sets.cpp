#include "test_helpers.h"

// `set` commands: miscellaneous.md (s:set) for the syntax, and
// lattice-construction.md (s:lattice.expand, s:expand.lat) for where a set acts
// depending on which side of `expand_lattice` it sits on.

namespace {

std::vector<std::string> problem_list(const struct lattices& lat) {
    std::vector<std::string> msgs;
    for (size_t i = 0; i < lat.problems.count; ++i)
        msgs.emplace_back(lat.problems.items[i]);
    return msgs;
}

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

// A file whose `facility` list is exactly `body` (10-space indented entries),
// followed by a lattice built from the beamline named `line_name` and a `use`.
std::string facility_file(const std::string& body, const char* line_name) {
    return "PALS:\n"
           "  facility:\n" +
           body + "    - lat1:\n"
                  "        kind: Lattice\n"
                  "        branches:\n"
                  "          - " +
           line_name +
           "\n"
           "    - use: \"lat1\"\n";
}

// The value of `group.param` on the `index`-th element named `ele` in the
// expanded lattice (index 0 being the first).
double nth_param(YAMLTreeHandle t, const char* ele, size_t index,
                 const char* group, const char* param) {
    std::vector<YAMLNodeId> hits;
    std::vector<YAMLNodeId> stack{get_root(t)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* k = get_node_key(t, n);
        if (k && std::string(k) == ele) hits.push_back(n);
        yaml_free_string(k);
        // Push children in reverse so the walk visits them in document order.
        size_t sz = get_size(t, n);
        for (size_t i = sz; i-- > 0;) stack.push_back(get_child_by_index(t, n, i));
    }
    REQUIRE(hits.size() > index);
    YAMLNodeId e = hits[index];
    YAMLNodeId g = group ? get_child_by_key(t, e, group) : e;
    REQUIRE(g != YAML_NULL_ID);
    return num_val(t, get_child_by_key(t, g, param));
}

double one_param(YAMLTreeHandle t, const char* ele, const char* group,
                 const char* param) {
    return nth_param(t, ele, 0, group, param);
}

// A beamline holding one BeginningEle plus whatever `body` adds.
const char* BEGIN_ENTRY =
    "          - begin:\n"
    "              kind: BeginningEle\n"
    "              ReferenceP:\n"
    "                species_ref: \"electron\"\n"
    "                E_tot_ref: 1.0e9\n";

}  // namespace

TEST_CASE("a set writes every element its pattern matches",
          "[expr][lattices][set]") {
    // miscellaneous.md: `PARAMETER` is the current value of the parameter being
    // written and `SELF` reaches the rest of the element.
    const char* path = "tmp_set_basic.pals.yaml";
    write_tmp(path,
              facility_file(std::string(
                                "    - main:\n"
                                "        kind: BeamLine\n"
                                "        line:\n") +
                                BEGIN_ENTRY +
                                "          - B1a:\n"
                                "              kind: Bend\n"
                                "              length: 1.0\n"
                                "              BendP:\n"
                                "                e1: 0.1\n"
                                "                g_ref: 0.02\n"
                                "          - B1b:\n"
                                "              kind: Bend\n"
                                "              length: 1.0\n"
                                "              BendP:\n"
                                "                e1: 0.3\n"
                                "                g_ref: 0.02\n"
                                "          - Q1:\n"
                                "              kind: Quadrupole\n"
                                "              length: 0.5\n"
                                "    - set:\n"
                                "        parameter: B1.*>BendP.e1\n"
                                "        value: 2*PARAMETER + atan(SELF.BendP.g_ref)\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(one_param(lat.expanded, "B1a", "BendP", "e1"),
                  2 * 0.1 + std::atan(0.02)));
    REQUIRE(close(one_param(lat.expanded, "B1b", "BendP", "e1"),
                  2 * 0.3 + std::atan(0.02)));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a set only reaches elements defined before it",
          "[expr][lattices][set]") {
    // The example of lattice-construction.md, s:lattice.expand: Q2 is defined
    // after the set, so the set does not touch it.
    const char* path = "tmp_set_order.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.1\n"
                            "    - set:\n"
                            "        parameter: Q.*>MagneticMultipoleP.Kn1L\n"
                            "        value: PARAMETER + 0.02\n"
                            "    - Q2:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.1\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - Q1\n"
                                "          - Q2\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(one_param(lat.expanded, "Q1", "MagneticMultipoleP", "Kn1L"),
                  0.12));
    REQUIRE(close(one_param(lat.expanded, "Q2", "MagneticMultipoleP", "Kn1L"),
                  0.1));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a set creates a parameter that defaults to zero",
          "[expr][lattices][set]") {
    // s:lattice.expand: an unwritten parameter of a known element reads as zero,
    // so `PARAMETER + 0.02` on an element with no MagneticMultipoleP is 0.02.
    const char* path = "tmp_set_default.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "    - set:\n"
                            "        parameter: Q1>MagneticMultipoleP.Kn0\n"
                            "        value: PARAMETER + 0.02\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) + "          - Q1\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "Q1", "MagneticMultipoleP", "Kn0"),
                  0.02));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("reading a not-yet-derived parameter in a set is an error",
          "[expr][lattices][set][problems]") {
    // The second example of s:lattice.expand: Q1's Bs1 will be derived from its
    // Ks1 and the reference momentum, which is not known when the set runs, so
    // reading Bs1 here is an error rather than reading zero.
    const char* path = "tmp_set_pending.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Ks1: 0.34\n"
                            "    - Q2:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "    - set:\n"
                            "        parameter: Q2>MagneticMultipoleP.Bs1\n"
                            "        value: Q1>MagneticMultipoleP.Bs1\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - Q1\n"
                                "          - Q2\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "'Q1>MagneticMultipoleP.Bs1' has no value yet"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("an unwritten parameter with no written family member reads as zero",
          "[expr][lattices][set]") {
    // The same shape as the error case, but with Ks1 left unset: nothing in the
    // family is written, so Bs1 is simply zero and there is no error.
    const char* path = "tmp_set_nopending.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "    - Q2:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "    - set:\n"
                            "        parameter: Q2>MagneticMultipoleP.Kn1L\n"
                            "        value: Q1>MagneticMultipoleP.Bs1 + 0.5\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - Q1\n"
                                "          - Q2\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "Q2", "MagneticMultipoleP", "Kn1L"),
                  0.5));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("the compact sets form writes each pair", "[expr][lattices][set]") {
    const char* path = "tmp_sets_compact.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "    - D1:\n"
                            "        kind: Drift\n"
                            "        length: 1.0\n"
                            "    - sets:\n"
                            "        - Q1>MagneticMultipoleP.Kn1L: 0.25\n"
                            "        - D1>length: 2 * 1.5\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - Q1\n"
                                "          - D1\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "Q1", "MagneticMultipoleP", "Kn1L"),
                  0.25));
    REQUIRE(close(one_param(lat.expanded, "D1", nullptr, "length"), 3.0));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("without expand_lattice a set writes the single definition",
          "[expr][lattices][set]") {
    // s:expand.lat: the repeated element has not been instantiated yet, so the
    // set targets the one definition and all three copies inherit its value.
    const char* path = "tmp_set_norepeat.pals.yaml";
    write_tmp(path,
              facility_file("    - q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.375\n"
                            "    - set:\n"
                            "        parameter: q1>MagneticMultipoleP.Kn1L\n"
                            "        value: PARAMETER * 2\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - q1:\n"
                                "              repeat: 3\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(close(nth_param(lat.expanded, "q1", i, "MagneticMultipoleP",
                                "Kn1L"),
                      0.75));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("after expand_lattice a set reaches each expanded copy",
          "[expr][lattices][set]") {
    // The point of `expand_lattice`: with the lattice built, the three copies of
    // `q1` are separate elements and the set writes each of them. The value used
    // here depends on the copy's own s_position, which only exists after
    // expansion, so it also pins down that the bookkeeper has already run.
    const char* path = "tmp_set_expanded.pals.yaml";
    write_tmp(path,
              facility_file("    - q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.375\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - q1:\n"
                                "              repeat: 3\n"
                                "    - expand_lattice\n"
                                "    - set:\n"
                                "        parameter: lat1>>>q1>MagneticMultipoleP.Kn1L\n"
                                "        value: PARAMETER + SELF.s_position\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    // The copies sit at s = 0, 0.5 and 1.0.
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(close(nth_param(lat.expanded, "q1", i, "MagneticMultipoleP",
                                "Kn1L"),
                      0.375 + 0.5 * static_cast<double>(i)));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a post-expansion set makes the bookkeeper redo the family",
          "[expr][lattices][set]") {
    // Writing Kn1L after the bookkeeper has already derived Bn1L from the old
    // value must not leave the two inconsistent (nor be reported as such): the
    // stale family members are dropped and the second bookkeeper pass rebuilds
    // them. q2 is written out with the value the set gives q1, so the derived
    // Bn1L must come out the same for both.
    const char* path = "tmp_set_rederive.pals.yaml";
    write_tmp(path,
              facility_file("    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - q1:\n"
                                "              kind: Quadrupole\n"
                                "              length: 0.5\n"
                                "              MagneticMultipoleP:\n"
                                "                Kn1L: 0.375\n"
                                "          - q2:\n"
                                "              kind: Quadrupole\n"
                                "              length: 0.5\n"
                                "              MagneticMultipoleP:\n"
                                "                Kn1L: 0.75\n"
                                "    - expand_lattice\n"
                                "    - set:\n"
                                "        parameter: q1>MagneticMultipoleP.Kn1L\n"
                                "        value: PARAMETER * 2\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");

    REQUIRE(close(one_param(lat.expanded, "q1", "MagneticMultipoleP", "Kn1L"),
                  0.75));
    double b1 = one_param(lat.expanded, "q1", "MagneticMultipoleP", "Bn1L");
    double b2 = one_param(lat.expanded, "q2", "MagneticMultipoleP", "Bn1L");
    REQUIRE(b1 != 0.0);
    REQUIRE(close_rel(b1, b2));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a post-expansion set that changes a length moves the lattice",
          "[expr][lattices][set]") {
    // "Recalculate floor and reference parameters as needed": lengthening the
    // drift after expansion has to move everything downstream of it.
    const char* path = "tmp_set_length.pals.yaml";
    write_tmp(path,
              facility_file("    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - d1:\n"
                                "              kind: Drift\n"
                                "              length: 1.0\n"
                                "          - m1:\n"
                                "              kind: Marker\n"
                                "    - expand_lattice\n"
                                "    - set:\n"
                                "        parameter: d1>length\n"
                                "        value: 3.5\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "m1", nullptr, "s_position"), 3.5));
    REQUIRE(close(one_param(lat.expanded, "m1", "FloorP", "z"), 3.5));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("controllers stay authoritative over a post-expansion set",
          "[expr][lattices][set][controller]") {
    // s:lattice.expand applies the ABSOLUTE controllers after the
    // post-expansion list, so a controller-driven parameter keeps the
    // controller's value even when a later set writes it.
    const char* path = "tmp_set_controller.pals.yaml";
    write_tmp(path,
              facility_file("    - ps1:\n"
                            "        kind: Controller\n"
                            "        controls:\n"
                            "          - parameter: q1>MagneticMultipoleP.Kn1L\n"
                            "            expression: 0.9\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) +
                                "          - q1:\n"
                                "              kind: Quadrupole\n"
                                "              length: 0.5\n"
                                "              MagneticMultipoleP:\n"
                                "                Kn1L: 0.1\n"
                                "    - expand_lattice\n"
                                "    - set:\n"
                                "        parameter: q1>MagneticMultipoleP.Kn1L\n"
                                "        value: 0.2\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "q1", "MagneticMultipoleP", "Kn1L"),
                  0.9));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a set with an error term reports that it is not applied",
          "[expr][lattices][set][problems]") {
    // The standard gives the error magnitude but not its distribution, and this
    // library never invents randomness, so the deterministic value is written
    // and the error is flagged.
    const char* path = "tmp_set_error.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.4\n"
                            "    - set:\n"
                            "        parameter: Q1>MagneticMultipoleP.Kn1L\n"
                            "        value: PARAMETER\n"
                            "        relative_error: 1e-4\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) + "          - Q1\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "absolute_error/relative_error are not applied"));
    REQUIRE(close(one_param(lat.expanded, "Q1", "MagneticMultipoleP", "Kn1L"),
                  0.4));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a set whose target matches nothing is reported",
          "[expr][lattices][set][problems]") {
    const char* path = "tmp_set_nomatch.pals.yaml";
    write_tmp(path,
              facility_file("    - set:\n"
                            "        parameter: nosuch>length\n"
                            "        value: 1.0\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY),
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(any_contains(problem_list(lat),
                         "set 'nosuch>length': target matches nothing defined "
                         "before it"));

    free_all(lat);
    rm_tmp(path);
}

TEST_CASE("a set with a random value is deferred", "[expr][lattices][set]") {
    // random()/random_gauss() are deferred everywhere so the expanded tree stays
    // reproducible; a set driven by one leaves its parameter alone.
    const char* path = "tmp_set_random.pals.yaml";
    write_tmp(path,
              facility_file("    - Q1:\n"
                            "        kind: Quadrupole\n"
                            "        length: 0.5\n"
                            "        MagneticMultipoleP:\n"
                            "          Kn1L: 0.375\n"
                            "    - set:\n"
                            "        parameter: Q1>MagneticMultipoleP.Kn1L\n"
                            "        value: PARAMETER * (1 + 1e-4*random_gauss())\n"
                            "    - main:\n"
                            "        kind: BeamLine\n"
                            "        line:\n" +
                                std::string(BEGIN_ENTRY) + "          - Q1\n",
                            "main"));

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(joined(lat) == "");
    REQUIRE(close(one_param(lat.expanded, "Q1", "MagneticMultipoleP", "Kn1L"),
                  0.375));

    free_all(lat);
    rm_tmp(path);
}
