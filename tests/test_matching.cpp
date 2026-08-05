#include "test_helpers.h"

// ============================================================
// NAME MATCHING
// ============================================================

TEST_CASE("match_names handles null args", "[matching]") {
    struct name_matches m = match_names(nullptr, "a_const");
    REQUIRE(m.count == 0);
    REQUIRE(m.nodes == nullptr);
    free_name_matches(m);

    YAMLTreeHandle t = parse_string(MATCH_YAML);
    m = match_names(t, nullptr);
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a bare name matches a single constant", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "a_const");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(val_eq(t, m.nodes[0], "0.3 * r_electron"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a constant/variable pattern matches by name across forms",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // a_.* matches the two compact constants but not the full-form variable.
    struct name_matches m = match_names(t, "a_.*");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(key_eq(t, m.nodes[1], "a_two"));
    free_name_matches(m);

    // A full-form variable is matched too; the returned node is its named node.
    m = match_names(t, "my_var");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "my_var"));
    REQUIRE(is_map(t, m.nodes[0]));  // full form: kind/value live underneath
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("compact constants written as a plain map are matched too",
          "[matching]") {
    // The compact form may be a YAML map (name: value) rather than a sequence
    // of single-key maps; both must be matched.
    YAMLTreeHandle t = parse_string(
        "PALS:\n"
        "  facility:\n"
        "    - constants:\n"
        "        a_const: 0.3 * r_electron\n"
        "        b_const: 0.45\n"
        "    - variables:\n"
        "        a_var: 5\n");
    struct name_matches m = match_names(t, ".*_const");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "a_const"));
    REQUIRE(key_eq(t, m.nodes[1], "b_const"));
    free_name_matches(m);

    m = match_names(t, "a_var");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "5"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("the name pattern is a whole-name (anchored) match", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // "a" alone must not match "a_const" — the whole name has to match.
    struct name_matches m = match_names(t, "a");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a bare name matches the elements themselves", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1a exists in both lattices; each occurrence is returned as its element
    // (map) node.
    struct name_matches m = match_names(t, "B1a");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "B1a"));
    REQUIRE(is_map(t, m.nodes[0]));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an element pattern with a grouped parameter matches all elements",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1.* matches B1a and B1b in lat1; both carry BendP.e1. (lat2's B1a has no
    // BendP.)
    struct name_matches m = match_names(t, "B1.*>BendP.e1");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "e1"));
    REQUIRE(val_eq(t, m.nodes[0], "0.1"));
    REQUIRE(val_eq(t, m.nodes[1], "0.3"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an ungrouped parameter resolves directly on the element",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // B1a appears in both lattices, so its length matches twice.
    struct name_matches m = match_names(t, "B1a>length");
    REQUIRE(m.count == 2);
    REQUIRE(key_eq(t, m.nodes[0], "length"));
    REQUIRE(val_eq(t, m.nodes[0], "1.2"));
    REQUIRE(val_eq(t, m.nodes[1], "9.9"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("an omitted element matches the parameter in every element",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // >length : the length of every element, in both lattices and the sub-line.
    struct name_matches m = match_names(t, ">length");
    REQUIRE(m.count == 5);
    free_name_matches(m);

    // A parameter only some elements have (g_ref) matches only those.
    m = match_names(t, ">BendP.g_ref");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "g_ref"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("`::` restricts a match to an element kind", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // Only the Quadrupole matches.
    struct name_matches m = match_names(t, "Quadrupole::.*>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.5"));
    free_name_matches(m);

    // Both B1a's are Bends.
    m = match_names(t, "Bend::B1a>length");
    REQUIRE(m.count == 2);
    free_name_matches(m);

    // A kind that no matching element has yields nothing.
    m = match_names(t, "Sextupole::B1a>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("`>>` filters by BeamLine/Branch, including sub-lines", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // main contains B1a and B1b directly.
    struct name_matches m = match_names(t, "main>>B1.*>length");
    REQUIRE(m.count == 2);
    free_name_matches(m);

    // S1 lives in sub-line `sub` of `main`; the `main>>` qualifier reaches it.
    m = match_names(t, "main>>S1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.2"));
    free_name_matches(m);

    // A branch that does not exist yields nothing.
    m = match_names(t, "nobranch>>B1.*>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

namespace {

// The example of lattice-elements.md (element name matching): `q1` defined as a
// facility entry, and a second `q1` written inside the beamline `myline`. Only
// the first is what `facility>>q1` names.
const char* FACILITY_YAML =
    "PALS:\n"
    "  facility:\n"
    "    - q1:\n"
    "        kind: Quadrupole\n"
    "        length: 0.1\n"
    "    - unused:\n"
    "        kind: Sextupole\n"
    "        length: 0.7\n"
    "    - myline:\n"
    "        kind: BeamLine\n"
    "        line:\n"
    "          - q1:\n"
    "              kind: Quadrupole\n"
    "              length: 0.9\n"
    "    - mylat:\n"
    "        kind: Lattice\n"
    "        branches:\n"
    "          - mybranch:\n"
    "              inherit: myline\n";

}  // namespace

TEST_CASE("the branch name `facility` selects the facility node's definitions",
          "[matching]") {
    YAMLTreeHandle t = parse_string(FACILITY_YAML);

    // The definition standing outside any beamline, not the `q1` in myline.
    struct name_matches m = match_names(t, "facility>>q1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.1"));
    free_name_matches(m);

    // A beamline qualifier still names the one inside that beamline.
    m = match_names(t, "myline>>q1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.9"));
    free_name_matches(m);

    // A definition no beamline uses is reachable only this way: elements are
    // otherwise matched where they sit in a `line`, and this one sits in none.
    m = match_names(t, "facility>>unused>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.7"));
    free_name_matches(m);

    // The name is still a pattern, and `::` still filters by kind.
    m = match_names(t, "facility>>.*>length");
    REQUIRE(m.count == 2);
    free_name_matches(m);

    m = match_names(t, "facility>>Sextupole::.*>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.7"));
    free_name_matches(m);

    delete_tree(t);
}

TEST_CASE("a bare line reference is matched only through its beamline",
          "[matching]") {
    // `- q1` names the element without holding any parameter of its own, but it
    // can be given them, so it is an element of myline like any other. Named
    // without saying which line it sits in, though, `q1` is the definition it
    // points at -- otherwise a set before expansion could not target the
    // definition alone (lattice-construction.md, s:expand.lat).
    YAMLTreeHandle t = parse_string(
        "PALS:\n"
        "  facility:\n"
        "    - q1:\n"
        "        kind: Quadrupole\n"
        "        length: 0.1\n"
        "    - myline:\n"
        "        kind: BeamLine\n"
        "        line:\n"
        "          - q1\n");

    struct name_matches m = match_names(t, "myline>>q1");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "q1"));  // still the bare reference scalar
    free_name_matches(m);

    // Unqualified, the reference is passed over, and nothing else here sits in
    // a line -- the definition is reached by naming the facility node instead.
    m = match_names(t, "q1>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);

    m = match_names(t, "facility>>q1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.1"));
    free_name_matches(m);

    delete_tree(t);
}

TEST_CASE("`facility` is a keyword where a branch name goes, not a pattern",
          "[matching]") {
    YAMLTreeHandle t = parse_string(FACILITY_YAML);

    // A branch pattern wide enough to spell `facility` still means branches
    // only: it reaches the `q1` in myline and nothing in the facility list.
    struct name_matches m = match_names(t, ".*>>q1>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "0.9"));
    free_name_matches(m);

    // A lattice qualifier cannot apply: a definition belongs to no lattice
    // until a lattice is expanded around it.
    m = match_names(t, "mylat>>>facility>>q1>length");
    REQUIRE(m.count == 0);
    free_name_matches(m);

    delete_tree(t);
}

TEST_CASE("`>>>` selects among lattices with the same element name",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "lat1>>>B1a>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "1.2"));
    free_name_matches(m);

    m = match_names(t, "lat2>>>B1a>length");
    REQUIRE(m.count == 1);
    REQUIRE(val_eq(t, m.nodes[0], "9.9"));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("dropping the parameter matches the parameter group", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    // Only lat1's B1a has a BendP group.
    struct name_matches m = match_names(t, "B1a>BendP");
    REQUIRE(m.count == 1);
    REQUIRE(key_eq(t, m.nodes[0], "BendP"));
    REQUIRE(is_map(t, m.nodes[0]));
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("no matches yields an empty result", "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "nosuch>foo");
    REQUIRE(m.count == 0);
    REQUIRE(m.nodes == nullptr);
    free_name_matches(m);

    // A missing parameter path on an existing element also yields nothing.
    m = match_names(t, "B1a>BendP.nope");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}

TEST_CASE("a malformed pattern yields an empty result, not a crash",
          "[matching]") {
    YAMLTreeHandle t = parse_string(MATCH_YAML);
    struct name_matches m = match_names(t, "(unclosed");
    REQUIRE(m.count == 0);
    free_name_matches(m);
    delete_tree(t);
}
