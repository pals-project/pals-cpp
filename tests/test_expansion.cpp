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
