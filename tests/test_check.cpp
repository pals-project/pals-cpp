#include "test_helpers.h"

// ============================================================
// Spelling checks against the PALS vocabulary (src/pals_check.cpp)
// ============================================================

namespace {

// Every problem reported for `yaml`, so a test can assert on the whole set.
std::vector<std::string> problems_for(const char* path, const char* yaml) {
    write_tmp(path, yaml);
    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    std::vector<std::string> out;
    for (size_t i = 0; i < lat.problems.count; ++i)
        out.push_back(lat.problems.items[i]);
    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
    return out;
}

bool has(const std::vector<std::string>& ps, const char* needle) {
    for (const std::string& p : ps)
        if (p.find(needle) != std::string::npos) return true;
    return false;
}

int count_with(const std::vector<std::string>& ps, const char* needle) {
    int n = 0;
    for (const std::string& p : ps)
        if (p.find(needle) != std::string::npos) n++;
    return n;
}

}  // namespace

TEST_CASE("a misspelled element kind is reported with a suggestion",
          "[check][problems]") {
    // `marker` parses as perfectly good YAML and would simply go unrecognised
    // downstream, so the mistake has to be caught by name. Case is the mistake
    // the CamelCase convention invites, so a name that differs only in case is
    // suggested outright.
    auto ps = problems_for("tmp_check_kind.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - m:\n"
                           "        kind: marker\n"
                           "    - q1:\n"
                           "        kind: Quadrapole\n"
                           "        length: 1\n");

    REQUIRE(has(ps, "element 'm': unknown kind 'marker'; did you mean 'Marker'?"));
    REQUIRE(has(ps,
                "element 'q1': unknown kind 'Quadrapole'; did you mean "
                "'Quadrupole'?"));
}

TEST_CASE("a misspelled parameter group is reported with a suggestion",
          "[check][problems]") {
    // A group is the one part of an element written in CamelCase ending in `P`,
    // so a key of that shape that PALS does not define is a group name spelled
    // wrong. Plain lower-case parameters are not checked -- they are not drawn
    // from a fixed vocabulary.
    auto ps = problems_for("tmp_check_group.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        FlorP:\n"
                           "          r: [0, 0, 0]\n"
                           "        Refereence:\n"
                           "          species_ref: electron\n"
                           "        L20_BLW_P:\n"
                           "          gain: 1\n");

    REQUIRE(has(ps,
                "element 'q1': unknown parameter group 'FlorP'; did you mean "
                "'FloorP'?"));
    // `Refereence` does not end in `P`, so it is not taken for a group at all.
    REQUIRE(!has(ps, "Refereence"));
    // Nor is `L20_BLW_P`: PALS never digits or underscores a group name, so the
    // shape says "outside the standard" rather than "misspelled".
    REQUIRE(!has(ps, "L20_BLW_P"));
}

TEST_CASE("correctly spelled kinds and groups are not reported",
          "[check][problems]") {
    auto ps = problems_for("tmp_check_clean.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - b1:\n"
                           "        kind: Bend\n"
                           "        length: 1\n"
                           "        ApertureP:\n"
                           "          x_limit: [-0.1, 0.1]\n"
                           "        BodyShiftP:\n"
                           "          x_offset: 0.001\n"
                           "        MagneticMultipoleP:\n"
                           "          - order: 0\n"
                           "            Kn: 0.1\n"
                           "    - my_const:\n"
                           "        kind: constant\n"
                           "        value: 3\n");

    REQUIRE(count_with(ps, "unknown kind") == 0);
    REQUIRE(count_with(ps, "unknown parameter group") == 0);
}

TEST_CASE("extension data is exempt from the spelling checks",
          "[check][problems]") {
    // Two ways to mark extension data (extensions.md, s:extension-syntax): an
    // `extension` key, which hands the rest of that dictionary to an extension
    // schema, and a name registered under `PALS: extension_labels`, which may be
    // matched in full, by prefix, or by suffix. Neither is PALS' to validate,
    // and an extension name may be used as an enum value -- `kind: Rotator`.
    auto ps = problems_for("tmp_check_ext.pals.yaml",
                           "PALS:\n"
                           "  extension_labels:\n"
                           "    names:\n"
                           "      Rotator: a new kind of element\n"
                           "    prefixes:\n"
                           "      SciBmad_: for the SciBmad ecosystem\n"
                           "    suffixes:\n"
                           "      _local: site-local data\n"
                           "  facility:\n"
                           "    - r23:\n"
                           "        kind: Rotator\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        SciBmad_connect:\n"
                           "          BogusP: anything at all\n"
                           "        tuning_local:\n"
                           "          AlsoBogusP: and here too\n"
                           "    - synch_connect:\n"
                           "        extension: Cornell_CESR_Connect\n"
                           "        NotAGroupP: alarm system stuff\n");

    REQUIRE(count_with(ps, "unknown kind") == 0);
    REQUIRE(count_with(ps, "unknown parameter group") == 0);
}

TEST_CASE("an unrecognisable name is reported without a guess",
          "[check][problems]") {
    // A suggestion is offered only when something is genuinely close; a name
    // that resembles nothing is reported on its own rather than paired with the
    // nearest string in the table.
    auto ps = problems_for("tmp_check_wild.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - x1:\n"
                           "        kind: Zzzyzx\n");

    REQUIRE(has(ps, "element 'x1': unknown kind 'Zzzyzx'"));
    REQUIRE(!has(ps, "'Zzzyzx'; did you mean"));
}
