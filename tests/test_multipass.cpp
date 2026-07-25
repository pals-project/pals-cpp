#include "test_helpers.h"

// The `multipass_index` value node of the i-th element in the (single-branch)
// expanded lattice's line (YAML_NULL_ID when the element carries none).
static YAMLNodeId mp_index_at(YAMLTreeHandle t, size_t i) {
    YAMLNodeId lat1 = get_child_by_index(t, get_root(t), 0);
    YAMLNodeId branches = get_child_by_key(t, lat1, "branches");
    YAMLNodeId branch = get_child_by_index(t, get_child_by_index(t, branches, 0), 0);
    YAMLNodeId line = get_child_by_key(t, branch, "line");
    YAMLNodeId ele = get_child_by_index(t, get_child_by_index(t, line, i), 0);
    return get_child_by_key(t, ele, "multipass_index");
}

TEST_CASE("Multipass sub-lines are numbered with a multipass_index",
          "[multipass][lattices]") {
    // ERL-shaped: a multipass `linac` traversed twice, plus a non-multipass
    // `inj`. The multipass index is the pass number: every element of a given
    // traversal of the line shares it, so the first `linac` pass stamps 1 on
    // both its cavities and the second pass stamps 2 on both. Matching positions
    // across passes are the same physical element on successive turns. `inj`'s
    // element, in no multipass line, gets none.
    const char* path = "tmp_multipass.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - cavA:\n"
              "        kind: Quadrupole\n"
              "    - mark:\n"
              "        kind: Marker\n"
              "    - inj:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - mark\n"
              "    - linac:\n"
              "        kind: BeamLine\n"
              "        multipass: true\n"
              "        line:\n"
              "          - cavA\n"
              "          - cavA\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - inj\n"
              "          - linac\n"
              "          - linac\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: lat1\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.full_expanded != nullptr);

    // Flat line: mark, cavA, cavA, cavA, cavA (5 elements), plus the appended
    // `branch_end` Placeholder the bookkeeper caps every branch with (6 total).
    YAMLNodeId lat1 = get_child_by_index(lat.full_expanded, get_root(lat.full_expanded), 0);
    YAMLNodeId branch = get_child_by_index(
        lat.full_expanded,
        get_child_by_index(lat.full_expanded,
                           get_child_by_key(lat.full_expanded, lat1, "branches"), 0),
        0);
    REQUIRE(get_size(lat.full_expanded, get_child_by_key(lat.full_expanded, branch, "line")) == 6);

    // mark, from the non-multipass inj, carries no index.
    REQUIRE(mp_index_at(lat.full_expanded, 0) == YAML_NULL_ID);
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 1), "1"));  // linac pass 1
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 2), "1"));
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 3), "2"));  // linac pass 2
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 4), "2"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("Nested multipass lines: the nearest multipass line wins",
          "[multipass][lattices]") {
    // `outer` (multipass) contains `inner` (multipass) twice, between two bare
    // `x`. The multipass index an element gets comes from the *first* multipass
    // line up its chain, and is that line's pass number. `outer` is traversed
    // once, so its two direct `x` are pass 1. `inner` is traversed twice inside
    // that one `outer` pass, so its first instance's `y` are pass 1 and its
    // second instance's `y` are pass 2 — the nearer line's count, not `outer`'s.
    const char* path = "tmp_multipass_nested.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - x:\n"
              "        kind: Marker\n"
              "    - y:\n"
              "        kind: Marker\n"
              "    - inner:\n"
              "        kind: BeamLine\n"
              "        multipass: true\n"
              "        line:\n"
              "          - y\n"
              "          - y\n"
              "    - outer:\n"
              "        kind: BeamLine\n"
              "        multipass: true\n"
              "        line:\n"
              "          - x\n"
              "          - inner\n"
              "          - inner\n"
              "          - x\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - outer\n"
              "    - use: lat1\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.full_expanded != nullptr);

    // Flat line: x, y, y, y, y, x.
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 0), "1"));  // outer, pass 1
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 1), "1"));  // inner instance 1
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 2), "1"));  // inner instance 1
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 3), "2"));  // inner instance 2
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 4), "2"));  // inner instance 2
    REQUIRE(val_eq(lat.full_expanded, mp_index_at(lat.full_expanded, 5), "1"));  // outer, pass 1

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}
