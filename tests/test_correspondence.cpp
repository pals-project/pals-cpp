#include "test_helpers.h"

// ============================================================
// build_correspondence_map
// ============================================================

TEST_CASE("build_correspondence_map is empty for null handles", "[correspondence]") {
    struct correspondence_map m = build_correspondence_map(nullptr, nullptr, nullptr, nullptr);
    REQUIRE(m.count == 0);
    REQUIRE(m.links == nullptr);
    free_correspondence_map(m);  // must not crash
}

TEST_CASE("build_correspondence_map links the document roots", "[correspondence]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.full_expanded,
                                 lat.leftover);

    REQUIRE(m.count > 0);

    // leftover is what still carries the document root, so that is the node
    // corresponding to the combined root. (The expanded root is synthesised to
    // hold the lattice entry and has no counterpart — see below.)
    YAMLNodeId left_root = get_root(lat.leftover);

    bool found_root = false;
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].leftover == left_root) {
            found_root = true;
            REQUIRE(m.links[i].combined == get_root(lat.combined));
            // The original tree's first child is the top-level file's contents.
            REQUIRE(m.links[i].original ==
                    get_child_by_index(lat.original, get_root(lat.original), 0));
            REQUIRE(is_map(lat.combined, m.links[i].combined));
            REQUIRE(is_map(lat.original, m.links[i].original));
        }
        // A link names a node in exactly one of the two derived trees.
        REQUIRE((m.links[i].full_expanded == YAML_NULL_ID) !=
                (m.links[i].leftover == YAML_NULL_ID));
    }
    REQUIRE(found_root);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
}

// A definition that expansion substituted into the lattice is a copy: the
// definition still stands in leftover, so the same combined node reaches both
// trees.
TEST_CASE("build_correspondence_map ties the two trees through combined",
          "[correspondence]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.full_expanded,
                                 lat.leftover);

    // inj_line is defined at facility level and used by lat1's branches, so it
    // is expanded into lat1 while its definition stays in leftover.
    std::map<YAMLNodeId, int> sides;  // combined id -> bitmask of trees reached
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].combined == YAML_NULL_ID) continue;
        sides[m.links[i].combined] |=
            (m.links[i].full_expanded != YAML_NULL_ID) ? 1 : 2;
    }

    bool found_both = false;
    for (const auto& kv : sides)
        if (kv.second == 3) found_both = true;
    REQUIRE(found_both);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

TEST_CASE("build_correspondence_map connects a node across trees by value",
          "[correspondence]") {
    // A constant that lives outside the expanded lattice is not part of it, so
    // it lands in leftover; the map must still connect it back to combined and
    // original.
    const char* path = "tmp_corr.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        a_const: 0.3 * r_electron\n"
              "    - q1:\n"
              "        kind: Quadrupole\n"
              "        length: 1.0\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - q1\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.full_expanded,
                                 lat.leftover);

    // Locate a_const in the leftover tree: facility[0] -> constants -> a_const.
    YAMLNodeId l_const = get_child_by_index(lat.leftover, facility_of(lat.leftover), 0);
    YAMLNodeId l_a_const =
        get_child_by_key(lat.leftover,
                         get_child_by_key(lat.leftover, l_const, "constants"),
                         "a_const");
    REQUIRE(l_a_const != YAML_NULL_ID);
    // Expressions are evaluated across the whole document before it is split, so
    // this constant holds a number in leftover too; the combined/original copies
    // (checked below) still carry the original expression text.
    {
        char* s = as_string(lat.leftover, l_a_const);
        REQUIRE(s != nullptr);
        double got = std::strtod(s, nullptr);
        yaml_free_string(s);
        bool okc = false;
        double want = evaluate_pals_expression("0.3 * r_electron", &okc);
        REQUIRE(okc);
        REQUIRE(got == want);
    }

    // Find its link and follow it to the combined and original copies.
    bool found = false;
    for (size_t i = 0; i < m.count; i++) {
        if (m.links[i].leftover != l_a_const) continue;
        found = true;
        REQUIRE(m.links[i].full_expanded == YAML_NULL_ID);
        REQUIRE(m.links[i].combined != YAML_NULL_ID);
        REQUIRE(m.links[i].original != YAML_NULL_ID);
        REQUIRE(val_eq(lat.combined, m.links[i].combined, "0.3 * r_electron"));
        REQUIRE(val_eq(lat.original, m.links[i].original, "0.3 * r_electron"));
    }
    REQUIRE(found);

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("build_correspondence_map maps one source to many expanded copies",
          "[correspondence]") {
    // A `repeat` unrolls one combined element into several expanded nodes; the
    // map must link all copies back to a single combined source.
    const char* path = "tmp_corr_repeat.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - d1:\n"
              "        kind: Drift\n"
              "        length: 2.0\n"
              "    - cell:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - d1\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - cell:\n"
              "              repeat: 3\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    struct correspondence_map m =
        build_correspondence_map(lat.original, lat.combined, lat.full_expanded,
                                 lat.leftover);

    // Unrolling `repeat: 3` over a one-element cell produces three keyless `d1`
    // scalars in the expanded line, all copied from the same combined source.
    // Build a histogram of the combined ids that the expanded scalar `d1`
    // nodes point to; a single source must account for at least three copies.
    std::map<YAMLNodeId, int> combined_hits;
    for (size_t i = 0; i < m.count; i++) {
        // Skip the leftover half of the map: those ids index a different tree.
        if (m.links[i].full_expanded == YAML_NULL_ID) continue;
        if (!is_scalar(lat.full_expanded, m.links[i].full_expanded)) continue;
        if (!val_eq(lat.full_expanded, m.links[i].full_expanded, "d1")) continue;
        REQUIRE(m.links[i].combined != YAML_NULL_ID);  // has a source
        combined_hits[m.links[i].combined]++;
    }
    int max_copies = 0;
    for (auto& kv : combined_hits) max_copies = std::max(max_copies, kv.second);
    REQUIRE(max_copies >= 3);  // one combined node -> three expanded copies

    free_correspondence_map(m);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}
