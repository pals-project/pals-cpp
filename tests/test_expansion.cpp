#include "test_helpers.h"

// ============================================================
// parse_and_expand_PALS (smoke test — requires the example lattice files)
// ============================================================

TEST_CASE("parse_and_expand_PALS returns four non-null handles", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", nullptr);
    REQUIRE(lat.original != nullptr);
    REQUIRE(lat.combined != nullptr);
    REQUIRE(lat.expanded != nullptr);
    REQUIRE(lat.leftover != nullptr);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
}

// The expanded tree holds the root lattice and nothing else: no PALS/facility
// wrapper, and only the one entry.
TEST_CASE("expanded holds only the root lattice", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    YAMLNodeId root = get_root(lat.expanded);

    REQUIRE(is_map(lat.expanded, root));
    REQUIRE(get_size(lat.expanded, root) == 1);
    REQUIRE(get_child_by_key(lat.expanded, root, "PALS") == YAML_NULL_ID);

    YAMLNodeId entry = get_child_by_index(lat.expanded, root, 0);
    char* key = get_node_key(lat.expanded, entry);
    REQUIRE(std::string(key) == "lat1");
    yaml_free_string(key);
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, entry, "kind"),
                   "Lattice"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

// Everything else stays behind, under its PALS/facility scaffolding — including
// the lattice that was not expanded.
TEST_CASE("leftover keeps the rest of the document", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");
    YAMLNodeId fac = facility_of(lat.leftover);
    REQUIRE(fac != YAML_NULL_ID);

    bool saw_lat1 = false, saw_lat2 = false, saw_thingB = false;
    for (size_t i = 0; i < get_size(lat.leftover, fac); i++) {
        YAMLNodeId item = get_child_by_index(lat.leftover, fac, i);
        if (get_size(lat.leftover, item) != 1) continue;
        char* key = get_node_key(lat.leftover,
                                 get_child_by_index(lat.leftover, item, 0));
        std::string k(key ? key : "");
        yaml_free_string(key);
        if (k == "lat1") saw_lat1 = true;
        if (k == "lat2") saw_lat2 = true;
        if (k == "thingB") saw_thingB = true;
    }

    REQUIRE_FALSE(saw_lat1);  // moved to expanded
    REQUIRE(saw_lat2);        // a non-root Lattice is leftover like anything else
    REQUIRE(saw_thingB);      // element definitions stay put

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

// handle_fork writes the raw node id of the fork's destination element. That id
// is assigned before the lattice is cut out into its own tree, so it must be
// translated to survive the renumbering.
TEST_CASE("fork_pointer resolves inside the expanded tree", "[lattices]") {
    struct lattices lat = parse_and_expand_PALS("../lattice_files/ex.pals.yaml", "lat1");

    // Find the fork_pointer scalar anywhere in the expanded tree.
    YAMLNodeId fp = YAML_NULL_ID;
    std::vector<YAMLNodeId> stack{get_root(lat.expanded)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* key = get_node_key(lat.expanded, n);
        if (key && std::string(key) == "fork_pointer") fp = n;
        yaml_free_string(key);
        for (size_t i = 0; i < get_size(lat.expanded, n); i++)
            stack.push_back(get_child_by_index(lat.expanded, n, i));
    }
    REQUIRE(fp != YAML_NULL_ID);

    char* val = as_string(lat.expanded, fp);
    YAMLNodeId target = (YAMLNodeId)std::stoull(val);
    yaml_free_string(val);

    // It names the fork's destination_element in the branch expansion created.
    char* dest = as_string(lat.expanded, target);
    REQUIRE(std::string(dest) == "dump_begin");
    yaml_free_string(dest);

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    free_lattice_problems(lat.problems);
}

TEST_CASE("Expansion drops `kind: BeamLine` from every branch", "[lattices]") {
    // The three routes a branch gets its contents by, in one file: `main` by
    // name substitution, `alt` by `inherit`, and `to_dump` built by a Fork out
    // of `dump_line`. Each copies a BeamLine definition in, and none of the
    // copies may keep the definition's kind.
    const char* path = "tmp_branch_kind.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - q1:\n"
              "        kind: Quadrupole\n"
              "        length: 0.5\n"
              "    - dump_begin:\n"
              "        kind: Marker\n"
              "    - sub:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - q1\n"
              "    - dump_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - dump_begin\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        periodic: false\n"
              "        line:\n"
              "          - sub\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "                destination_element: dump_begin\n"
              "                new_branch: to_dump\n"
              "    - main:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - q1\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main\n"
              "          - alt:\n"
              "              inherit: ring\n"
              "              periodic: true\n"
              "    - use: lat1\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    YAMLNodeId lat1 = get_child_by_index(lat.expanded, get_root(lat.expanded), 0);
    YAMLNodeId branches = get_child_by_key(lat.expanded, lat1, "branches");
    REQUIRE(get_size(lat.expanded, branches) == 3);

    // The lattice itself is not a branch and keeps its kind.
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, lat1, "kind"),
                   "Lattice"));

    const char* names[] = {"main", "alt", "to_dump"};
    for (size_t i = 0; i < 3; i++) {
        YAMLNodeId branch = get_child_by_index(
            lat.expanded, get_child_by_index(lat.expanded, branches, i), 0);
        REQUIRE(key_eq(lat.expanded, branch, names[i]));
        REQUIRE(get_child_by_key(lat.expanded, branch, "kind") == YAML_NULL_ID);
        REQUIRE(get_child_by_key(lat.expanded, branch, "line") != YAML_NULL_ID);
    }

    // A branch keeps the components that are its own. `periodic: true` also
    // survives the merge of `ring`'s `periodic: false`, as the branch setting
    // overrides the root BeamLine's.
    YAMLNodeId alt = get_child_by_index(
        lat.expanded, get_child_by_index(lat.expanded, branches, 1), 0);
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, alt, "inherit"),
                   "ring"));
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, alt, "periodic"),
                   "true"));

    // `sub` sits inside a `line:`, so it is a sub-line: expansion splices its
    // contents into the enclosing line rather than leaving a nested BeamLine.
    // `alt` inherits `ring`, whose line is `[sub, f1]`; after flattening,
    // `sub`'s only element `q1` takes its place, so line[0] is `q1` itself.
    YAMLNodeId first = get_child_by_index(
        lat.expanded,
        get_child_by_index(lat.expanded,
                           get_child_by_key(lat.expanded, alt, "line"), 0),
        0);
    REQUIRE(key_eq(lat.expanded, first, "q1"));
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, first, "kind"),
                   "Quadrupole"));

    // Expansion copies a definition rather than moving it, and the definition
    // left standing in leftover is a BeamLine still.
    YAMLNodeId l_main = facility_param(lat.leftover, "main");
    REQUIRE(l_main != YAML_NULL_ID);
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, l_main, "kind"),
                   "BeamLine"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("parse_and_expand_PALS reports expansion problems",
          "[expr][lattices][problems]") {
    // Every silent failure of expansion/evaluation is surfaced in the
    // `problems` list: dangling line references, undefined inherit/repeat
    // targets, and expressions that cannot be evaluated.
    const char* path = "tmp_problems.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - constants:\n"
              "        a_const: 0.3 * undefined_thing\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        MagneticMultipoleP:\n"
              "          Kn2L: 0.1\n"
              "    - DH1A:\n"
              "        kind: Bend\n"
              "        BendP:\n"
              "          edge_int2: 0.02 * thingB>MagneticMultipoleP.NotThere\n"
              "          e1: 3 * missing_const\n"
              "    - ghost_child:\n"
              "        kind: Bend\n"
              "        inherit: ghost_ancestor\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - DH1A\n"
              "          - ghost_child\n"
              "          - NoSuchElement\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    // Collect the messages so the assertions do not depend on their order.
    std::vector<std::string> msgs;
    for (size_t i = 0; i < lat.problems.count; ++i)
        msgs.emplace_back(lat.problems.items[i]);

    auto has = [&](const std::string& needle) {
        for (const std::string& m : msgs)
            if (m.find(needle) != std::string::npos) return true;
        return false;
    };

    REQUIRE(has("reference to undefined element or line 'NoSuchElement'"));
    REQUIRE(has("inherit: 'ghost_ancestor' is not defined"));
    REQUIRE(has("could not evaluate expression for constants.a_const"));
    REQUIRE(has("could not evaluate expression for BendP.edge_int2"));
    REQUIRE(has("could not evaluate expression for BendP.e1"));

    // Exactly those five: plain names (`kind: Bend`, the line references that
    // DO resolve) are not reported, and duplicate copies made by expansion
    // collapse to one message each.
    REQUIRE(lat.problems.count == 5);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("repeat with an unusable count keeps the entry",
          "[lattices][problems]") {
    // A repeat count that cannot be read is reported *and* left alone. It used
    // to be reported and then unrolled zero times, which removed the entry: the
    // lattice came back with an empty `line`, silently missing a beamline.
    auto counts_rejected = [](const char* count) {
        const char* path = "tmp_repeat_bad.pals.yaml";
        std::string doc = std::string(
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
            "              repeat: ") + count + "\n" +
            "    - lat1:\n"
            "        kind: Lattice\n"
            "        branches:\n"
            "          - main_line\n"
            "    - use: \"lat1\"\n";
        write_tmp(path, doc.c_str());

        struct lattices lat = parse_and_expand_PALS(path, nullptr);
        REQUIRE(lat.expanded != nullptr);

        bool reported = false;
        for (size_t i = 0; i < lat.problems.count; ++i)
            if (std::string(lat.problems.items[i])
                    .find("repeat: invalid count") != std::string::npos)
                reported = true;

        // The `line` under main_line must still hold the cell entry rather than
        // having been emptied.
        YAMLNodeId line = find_by_key(lat.expanded, "line");
        bool kept = line != YAML_NULL_ID && get_size(lat.expanded, line) > 0;

        free_lattice_problems(lat.problems);
        delete_tree(lat.original);
        delete_tree(lat.combined);
        delete_tree(lat.expanded);
        delete_tree(lat.leftover);
        rm_tmp(path);
        return reported && kept;
    };

    REQUIRE(counts_rejected("bogus"));  // not a number at all
    REQUIRE(counts_rejected("3x"));     // stoi would stop early and return 3
    REQUIRE(counts_rejected("-1"));     // parses, but meaningless
}

TEST_CASE("parse_and_expand_PALS reports a missing lattice",
          "[lattices][problems]") {
    const char* path = "tmp_nolattice.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - thingB:\n"
              "        kind: Sextupole\n"
              "        length: 0.3\n");

    struct lattices lat = parse_and_expand_PALS(path, "not_here");
    REQUIRE(lat.problems.count == 1);
    REQUIRE(std::string(lat.problems.items[0]) == "lattice 'not_here' not found");

    // With no lattice to expand, expanded is an empty map and the whole document
    // is leftover — both handles are still valid.
    REQUIRE(lat.expanded != nullptr);
    REQUIRE(lat.leftover != nullptr);
    REQUIRE(get_size(lat.expanded, get_root(lat.expanded)) == 0);
    REQUIRE(facility_of(lat.leftover) != YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a Fork with a ForkP sequence is reported as wrong-shape, not missing",
          "[lattices][problems]") {
    // A ForkP written as a sequence (a stray leading dash) *is* present, so the
    // old "missing ForkP" message was misleading. The diagnostic must say the
    // ForkP is the wrong shape rather than claim it is absent.
    const char* path = "tmp_forkp_seq.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - dump_begin:\n"
              "        kind: Marker\n"
              "    - dump_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - dump_begin\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                - to_line: dump_line\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    bool wrong_shape = false;
    bool missing = false;
    for (size_t i = 0; i < lat.problems.count; ++i) {
        std::string msg = lat.problems.items[i];
        if (msg.find("f1") != std::string::npos) {
            if (msg.find("must be a map") != std::string::npos) wrong_shape = true;
            if (msg.find("missing ForkP") != std::string::npos) missing = true;
        }
    }
    REQUIRE(wrong_shape);
    REQUIRE_FALSE(missing);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a Fork needs only to_line; destination_element and new_branch default",
          "[lattices][problems]") {
    // Per the schema only `to_line` is required: `destination_element` defaults
    // to the destination branch's beginning element and `new_branch` defaults to
    // the `to_line` name. A ForkP with just `to_line` must expand without any
    // "missing a required field" problem.
    const char* path = "tmp_fork_defaults.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - dump_begin:\n"
              "        kind: Marker\n"
              "    - dump_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - dump_begin\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    for (size_t i = 0; i < lat.problems.count; ++i)
        REQUIRE(std::string(lat.problems.items[i]).find("f1") ==
                std::string::npos);

    // The fork resolved to a target, so the element carries a fork_pointer and
    // the defaulted branch (named after to_line) exists.
    YAMLNodeId fp = find_by_key(lat.expanded, "fork_pointer");
    REQUIRE(fp != YAML_NULL_ID);

    // Expansion resolves ForkP.new_branch to the name of the branch it made,
    // which for the default is the to_line name.
    YAMLNodeId forkp = find_by_key(lat.expanded, "ForkP");
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, forkp, "new_branch"),
                   "dump_line"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

// Helper: the keyed element definition of a branch's first line element.
// expanded -> lat1(0) -> branches -> branch entry(idx) -> branch map(0) ->
// line -> line entry(0) -> element def(0).
static YAMLNodeId first_element_of_branch(YAMLTreeHandle t, size_t branch_idx) {
    YAMLNodeId lat1 = get_child_by_index(t, get_root(t), 0);
    YAMLNodeId branches = get_child_by_key(t, lat1, "branches");
    YAMLNodeId branch = get_child_by_index(
        t, get_child_by_index(t, branches, branch_idx), 0);
    YAMLNodeId line = get_child_by_key(t, branch, "line");
    YAMLNodeId entry = get_child_by_index(t, line, 0);
    return get_child_by_index(t, entry, 0);
}

TEST_CASE("a Fork propagates reference and floor into its new branch",
          "[lattices]") {
    // propagate_reference is true by default, so the destination branch's
    // beginning element inherits the Fork element's reference species/energy and
    // floor placement (fork.md), even when the forked-to line has no BeginningEle
    // of its own.
    const char* path = "tmp_fork_propagate.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - begin:\n"
              "        kind: BeginningEle\n"
              "        FloorP:\n"
              "          x: 1.5\n"
              "        ReferenceP:\n"
              "          species_ref: proton\n"
              "          E_tot_ref: 1.0e9\n"
              "    - m1:\n"
              "        kind: Marker\n"
              "    - ext_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m1\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - begin\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: ext_line\n"
              "                new_branch: extraction\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    // Branch 1 is `extraction`; its first (destination) element is `m1`.
    YAMLNodeId dest = first_element_of_branch(lat.expanded, 1);
    REQUIRE(key_eq(lat.expanded, dest, "m1"));

    YAMLNodeId refp = get_child_by_key(lat.expanded, dest, "ReferenceP");
    REQUIRE(refp != YAML_NULL_ID);
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, refp, "species_ref"),
                   "proton"));
    // Energy carried through the zero-length Fork unchanged, and completion
    // filled in the momentum from it.
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, refp, "E_tot_ref"), "1e+09"));
    REQUIRE(get_child_by_key(lat.expanded, refp, "pc_ref") != YAML_NULL_ID);

    // Floor placement of the source (x = 1.5) reaches the destination too.
    YAMLNodeId floorp = get_child_by_key(lat.expanded, dest, "FloorP");
    REQUIRE(floorp != YAML_NULL_ID);
    REQUIRE(val_eq(lat.expanded, get_child_by_key(lat.expanded, floorp, "x"),
                   "1.5"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("propagate_reference: false leaves the new branch's reference unset",
          "[lattices]") {
    // With propagate_reference explicitly false, nothing is carried across the
    // Fork: the destination element keeps only its own (here empty) reference.
    const char* path = "tmp_fork_noprop.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - begin:\n"
              "        kind: BeginningEle\n"
              "        ReferenceP:\n"
              "          species_ref: proton\n"
              "          E_tot_ref: 1.0e9\n"
              "    - m1:\n"
              "        kind: Marker\n"
              "    - ext_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m1\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - begin\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: ext_line\n"
              "                new_branch: extraction\n"
              "                propagate_reference: false\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    YAMLNodeId dest = first_element_of_branch(lat.expanded, 1);
    REQUIRE(key_eq(lat.expanded, dest, "m1"));

    YAMLNodeId refp = get_child_by_key(lat.expanded, dest, "ReferenceP");
    // A ReferenceP is still written (time_ref), but no species/energy propagated.
    REQUIRE(get_child_by_key(lat.expanded, refp, "species_ref") == YAML_NULL_ID);
    REQUIRE(get_child_by_key(lat.expanded, refp, "E_tot_ref") == YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("new_branch: null forks into an existing branch, creating none",
          "[lattices]") {
    // `new_branch: null` (fork.md, s:fork.params) points the Fork at an existing
    // branch instead of instantiating one. No branch is added, and because the
    // destination is not the beginning element of a *new* branch, nothing is
    // propagated: the existing branch keeps its own reference. (`other` is listed
    // first so it is already expanded when `ring`'s Fork resolves.)
    const char* path = "tmp_fork_null.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - begin:\n"
              "        kind: BeginningEle\n"
              "        ReferenceP:\n"
              "          species_ref: proton\n"
              "          E_tot_ref: 1.0e9\n"
              "    - eb:\n"
              "        kind: BeginningEle\n"
              "        ReferenceP:\n"
              "          species_ref: electron\n"
              "          E_tot_ref: 5.0e8\n"
              "    - other:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - eb\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - begin\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: other\n"
              "                new_branch: null\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - other\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    for (size_t i = 0; i < lat.problems.count; ++i)
        REQUIRE(std::string(lat.problems.items[i]).find("f1") ==
                std::string::npos);

    // Exactly the two declared branches: the Fork added none.
    YAMLNodeId lat1 = get_child_by_index(lat.expanded, get_root(lat.expanded), 0);
    YAMLNodeId branches = get_child_by_key(lat.expanded, lat1, "branches");
    REQUIRE(get_size(lat.expanded, branches) == 2);

    // The Fork still connects: it carries a fork_pointer.
    REQUIRE(find_by_key(lat.expanded, "fork_pointer") != YAML_NULL_ID);

    // ForkP.new_branch stays `null` — no branch was made.
    YAMLNodeId forkp = find_by_key(lat.expanded, "ForkP");
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, forkp, "new_branch"),
                   "null"));

    // `other` (branch 0) keeps its own reference — the Fork's proton/1e9 was not
    // propagated into an existing branch.
    YAMLNodeId dest = first_element_of_branch(lat.expanded, 0);
    REQUIRE(key_eq(lat.expanded, dest, "eb"));
    YAMLNodeId refp = get_child_by_key(lat.expanded, dest, "ReferenceP");
    REQUIRE(val_eq(lat.expanded,
                   get_child_by_key(lat.expanded, refp, "species_ref"),
                   "electron"));

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a destination of a kind that cannot be forked to is reported",
          "[lattices][problems]") {
    // Only a Marker, a BeginningEle, or another Fork may be forked to
    // (lattice-construction.md, s:fork): the destination has to share the Fork's
    // zero length and unit transfer map. `dump_line` begins with a Drift, so
    // both the defaulted destination and one named outright are rejected, and
    // neither Fork is linked.
    const char* path = "tmp_fork_bad_kind.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - d1:\n"
              "        kind: Drift\n"
              "        length: 1\n"
              "    - dump_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - d1\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "          - f2:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "                new_branch: null\n"
              "                destination_element: d1\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "          - dump_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    int reported = 0;
    for (size_t i = 0; i < lat.problems.count; ++i) {
        std::string p(lat.problems.items[i]);
        if (p.find("must be a Marker") != std::string::npos) {
            REQUIRE(p.find("'d1'") != std::string::npos);
            REQUIRE(p.find("'Drift'") != std::string::npos);
            reported++;
        }
    }
    REQUIRE(reported == 2);

    // Neither Fork resolved, so nothing was linked in either direction.
    REQUIRE(find_by_key(lat.expanded, "fork_pointer") == YAML_NULL_ID);
    REQUIRE(find_by_key(lat.expanded, "ForkFromP") == YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a fork destination lists its incoming Forks in ForkFromP",
          "[lattices]") {
    // ForkFromP (forkfrom.md, s:fork.from.params) is the reverse link of ForkP:
    // the destination element carries one entry per Fork pointing at it, keyed
    // `{branch-name}>>{element-name}` with the Fork's 1-based index in its own
    // branch's line. Two Forks in `ring` aim at the same element, so both land
    // in the one group, in branch order.
    const char* path = "tmp_fork_from.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - m0:\n"
              "        kind: Marker\n"
              "    - dump_begin:\n"
              "        kind: Marker\n"
              "    - dump_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - dump_begin\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m0\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "          - m0\n"
              "          - f2:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: dump_line\n"
              "                new_branch: null\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    // f1 built the `dump_line` branch; f2 (new_branch: null) pointed into it.
    YAMLNodeId dest = first_element_of_branch(lat.expanded, 1);
    REQUIRE(key_eq(lat.expanded, dest, "dump_begin"));

    YAMLNodeId group = get_child_by_key(lat.expanded, dest, "ForkFromP");
    REQUIRE(group != YAML_NULL_ID);
    REQUIRE(get_size(lat.expanded, group) == 2);

    // f1 is the 2nd element of ring, f2 the 4th.
    YAMLNodeId e0 = get_child_by_index(
        lat.expanded, get_child_by_index(lat.expanded, group, 0), 0);
    REQUIRE(key_eq(lat.expanded, e0, "ring>>f1"));
    REQUIRE(val_eq(lat.expanded, e0, "2"));

    YAMLNodeId e1 = get_child_by_index(
        lat.expanded, get_child_by_index(lat.expanded, group, 1), 0);
    REQUIRE(key_eq(lat.expanded, e1, "ring>>f2"));
    REQUIRE(val_eq(lat.expanded, e1, "4"));

    // The group is built only for elements a Fork actually points at.
    REQUIRE(get_child_by_key(lat.expanded, first_element_of_branch(lat.expanded, 0),
                             "ForkFromP") == YAML_NULL_ID);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a Fork inside a forked-to branch resolves exactly once",
          "[lattices]") {
    // A chained fork: `ring` forks to `mid`, and `mid` itself forks to `end`.
    // handle_fork expands `mid` eagerly, the moment it has to look inside it for
    // the destination element, and appends it to `branches` -- which the
    // enclosing walk over `branches` is still iterating. Expanding it a second
    // time would re-run the Fork it holds, giving two `end` branches and two
    // fork_pointers on the one element, so the branch is expanded only once.
    //
    // `ring` is followed by a second listed branch, so the walk over `branches`
    // has somewhere to go after `ring` and reaches the entry `ring`'s Fork
    // appended behind it. A Fork in the *last* listed branch never showed the
    // duplication: the walk stops before the entry it added.
    const char* path = "tmp_fork_chained.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - m1:\n"
              "        kind: Marker\n"
              "    - m2:\n"
              "        kind: Marker\n"
              "    - end_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m1\n"
              "    - mid_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f2:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: end_line\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: mid_line\n"
              "    - spectator:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m2\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "          - spectator\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    // Exactly four branches: the two listed, and one each from the two Forks.
    YAMLNodeId lat1 = get_child_by_index(lat.expanded, get_root(lat.expanded), 0);
    YAMLNodeId branches = get_child_by_key(lat.expanded, lat1, "branches");
    REQUIRE(get_size(lat.expanded, branches) == 4);

    // f2, the Fork in the branch that f1 built, ran once: one fork_pointer.
    YAMLNodeId f2 = first_element_of_branch(lat.expanded, 2);
    REQUIRE(key_eq(lat.expanded, f2, "f2"));
    int pointers = 0;
    for (size_t i = 0; i < get_size(lat.expanded, f2); ++i) {
        char* k = get_node_key(lat.expanded,
                               get_child_by_index(lat.expanded, f2, i));
        if (k && std::string(k) == "fork_pointer") pointers++;
        yaml_free_string(k);
    }
    REQUIRE(pointers == 1);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

// Every node keyed `key` in the tree, in walk order.
static std::vector<std::string> all_values_for(YAMLTreeHandle t,
                                               const char* key) {
    std::vector<std::string> out;
    std::vector<YAMLNodeId> stack{get_root(t)};
    while (!stack.empty()) {
        YAMLNodeId n = stack.back();
        stack.pop_back();
        char* k = get_node_key(t, n);
        bool hit = k && std::string(k) == key;
        yaml_free_string(k);
        if (hit) {
            char* v = as_string(t, n);
            out.push_back(v ? std::string(v) : std::string());
            yaml_free_string(v);
        }
        for (size_t i = 0; i < get_size(t, n); i++)
            stack.push_back(get_child_by_index(t, n, i));
    }
    return out;
}

TEST_CASE("a fork_pointer survives expression substitution intact",
          "[lattices][problems]") {
    // A fork_pointer holds a node id, which is a number, so the expression pass
    // used to "evaluate" it and write the result back through format_double --
    // the shortest text that round-trips as a double. For most ids that is the
    // digits themselves, but an id of 110 comes back as `1.1e+02`, and every
    // reader parses the pointer with std::stoull, which stops at the `.` and
    // yields 1. That silently cost the fork its ForkFromP entry and its
    // reference propagation, and left remap_fork_pointers reporting the target
    // as outside the lattice. non_expr_keys() now skips the key.
    //
    // Only a minority of ids format that way -- a multiple of 10 from 100 up --
    // so the three unused `pad` Drifts below are there to shift the work-tree
    // ids until two of the five destinations land in that range. Without them
    // every pointer happens to come out in plain digits and the bug hides. The
    // assertions hold whatever the ids turn out to be; they simply stop
    // exercising this if an unrelated change shifts the ids again.
    const char* path = "tmp_fork_pointer_fmt.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - begin1:\n"
              "        kind: BeginningEle\n"
              "        ReferenceP:\n"
              "          species_ref: \"electron\"\n"
              "          E_tot_ref: 1e7\n"
              "    - m:\n"
              "        kind: Marker\n"
              "    - dft:\n"
              "        kind: Drift\n"
              "        length: 2\n"
              "    - pad1:\n"
              "        kind: Drift\n"
              "        length: 1\n"
              "    - pad2:\n"
              "        kind: Drift\n"
              "        length: 1\n"
              "    - pad3:\n"
              "        kind: Drift\n"
              "        length: 1\n"
              "    - a_fork:\n"
              "        kind: Fork\n"
              "        ForkP:\n"
              "          to_line: a_line\n"
              "    - b_fork:\n"
              "        kind: Fork\n"
              "        ForkP:\n"
              "          to_line: b_line\n"
              "    - c_fork:\n"
              "        kind: Fork\n"
              "        ForkP:\n"
              "          to_line: c_line\n"
              "    - a_back_fork:\n"
              "        kind: Fork\n"
              "        ForkP:\n"
              "          to_line: a_line\n"
              "    - zero_line:\n"
              "        kind: BeamLine\n"
              "        line: [begin1, dft, a_fork]\n"
              "    - one_line:\n"
              "        kind: BeamLine\n"
              "        line: [begin1, dft, c_fork]\n"
              "    - a_line:\n"
              "        kind: BeamLine\n"
              "        line: [m, dft, b_fork]\n"
              "    - b_line:\n"
              "        kind: BeamLine\n"
              "        line: [m, dft]\n"
              "    - c_line:\n"
              "        kind: BeamLine\n"
              "        line: [m, dft, a_back_fork]\n"
              "    - root_lat:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - zero_line\n"
              "          - one_line\n"
              "    - use: \"root_lat\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);
    REQUIRE(lat.problems.count == 0);

    // Five Forks resolve: two chains of three (zero_line -> a_line -> b_line,
    // one_line -> c_line -> a_line -> b_line), sharing no branch instances.
    std::vector<std::string> ptrs = all_values_for(lat.expanded, "fork_pointer");
    REQUIRE(ptrs.size() == 5);
    for (const std::string& p : ptrs) {
        INFO("fork_pointer: " << p);
        REQUIRE(!p.empty());
        REQUIRE(p.find_first_not_of("0123456789") == std::string::npos);
    }

    // Each of those Forks reaches its destination, so each destination carries
    // the reverse link. A misparsed pointer drops the entry without a word.
    REQUIRE(all_values_for(lat.expanded, "ForkFromP").size() == 5);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("new_branch: null with a to_line that is not a branch is reported",
          "[lattices][problems]") {
    // `null` requires `to_line` to name an existing branch. A bare BeamLine
    // definition (not listed among the lattice's branches) does not qualify, and
    // the mismatch is surfaced rather than silently creating a branch.
    const char* path = "tmp_fork_null_bad.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - m1:\n"
              "        kind: Marker\n"
              "    - ext_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - m1\n"
              "    - ring:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - f1:\n"
              "              kind: Fork\n"
              "              ForkP:\n"
              "                to_line: ext_line\n"
              "                new_branch: null\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - ring\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.expanded != nullptr);

    bool reported = false;
    for (size_t i = 0; i < lat.problems.count; ++i)
        if (std::string(lat.problems.items[i]).find("is not an existing branch") !=
            std::string::npos)
            reported = true;
    REQUIRE(reported);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}

TEST_CASE("a malformed top-level file is a fatal parse problem, not a crash",
          "[lattices][problems]") {
    // A sequence item missing its ':' (here `- cav` followed by an indented
    // mapping) is a YAML syntax error. ryml would abort the process; instead
    // parse_and_expand_PALS must return NULL handles and one problem that names
    // the file and pinpoints the line.
    const char* path = "tmp_malformed.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - cav\n"
              "        kind: RFCavity\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);

    // No usable tree: every handle is NULL.
    REQUIRE(lat.original == nullptr);
    REQUIRE(lat.combined == nullptr);
    REQUIRE(lat.expanded == nullptr);
    REQUIRE(lat.leftover == nullptr);

    // A single problem, naming the file and the offending line.
    REQUIRE(lat.problems.count == 1);
    std::string msg = lat.problems.items[0];
    REQUIRE(msg.find(path) != std::string::npos);
    REQUIRE(msg.find("line") != std::string::npos);

    free_lattice_problems(lat.problems);
    delete_tree(lat.original);  // all NULL — delete_tree(NULL) is a safe no-op
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}
