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
    delete_tree(lat.full_expanded);
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
    // Neither `Refereence` nor `L20_BLW_P` has the shape of a group -- one does
    // not end in `P`, and PALS never digits or underscores a group name -- so
    // neither is judged against the group table. They are still keys of a
    // Quadrupole that PALS does not define, and are caught as parameters.
    REQUIRE(!has(ps, "parameter group 'Refereence'"));
    REQUIRE(!has(ps, "parameter group 'L20_BLW_P'"));
    REQUIRE(has(ps, "element 'q1': unknown parameter 'Refereence'"));
    REQUIRE(has(ps, "element 'q1': unknown parameter 'L20_BLW_P'"));
}

TEST_CASE("a misspelled parameter inside a group is reported",
          "[check][problems]") {
    // Each group has a fixed component list (one section each under
    // source/parameters), so a key that is not on it is a mistake -- `xxx` is
    // no more a ForkP parameter than `FlorP` is a group.
    auto ps = problems_for("tmp_check_param.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - f1:\n"
                           "        kind: Fork\n"
                           "        ForkP:\n"
                           "          to_line: dump\n"
                           "          xxx: abc\n"
                           "    - b1:\n"
                           "        kind: Bend\n"
                           "        length: 1\n"
                           "        BendP:\n"
                           "          edge_int2: 0.02\n"
                           "          e1: 0.1\n");

    REQUIRE(has(ps, "element 'f1>ForkP': unknown parameter 'xxx'"));
    // `edge_int2` is Bmad's spelling of what bend.md calls `edge2_int`. It is
    // reported without a guess: two edits from `edge2_int` and equally two from
    // `edge1_int`, and a tie is not worth resolving by coin toss.
    REQUIRE(has(ps, "element 'b1>BendP': unknown parameter 'edge_int2'"));
    REQUIRE(!has(ps, "'edge_int2'; did you mean"));
    // Correct names are left alone.
    REQUIRE(!has(ps, "'to_line'"));
    REQUIRE(!has(ps, "'e1'"));
}

TEST_CASE("the retired actual-field bend parameters are reported",
          "[check][problems]") {
    // bend.md decoupled the actual field from the reference one: the field the
    // particle sees is `Bn0`/`Kn0` of MagneticMultipoleP, defaulted from
    // `g_ref` when `Kn0_from_g_ref` is set. `g_actual` and `bend_field_actual`
    // are gone from BendP, and a lattice still using them is told so.
    auto ps = problems_for("tmp_check_actual.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - b1:\n"
                           "        kind: Bend\n"
                           "        length: 1\n"
                           "        BendP:\n"
                           "          g_ref: 0.1\n"
                           "          g_actual: 0.12\n"
                           "          bend_field_actual: 1.4\n");

    REQUIRE(has(ps, "element 'b1>BendP': unknown parameter 'g_actual'"));
    REQUIRE(
        has(ps, "element 'b1>BendP': unknown parameter 'bend_field_actual'"));
    REQUIRE(!has(ps, "unknown parameter 'g_ref'"));
}

TEST_CASE("the momentum dependence of the Twiss parameters is accepted",
          "[check][problems]") {
    // twiss.md gained the dependence of the Twiss, alpha and dispersion
    // components on momentum (pals#284). The names pair a component with the
    // variable differentiated against, so a mode letter used where an axis
    // belongs -- `dbeta_x_dpz` for what is `dbeta_a_dpz` or `dbeta_b_dpz` --
    // is still caught.
    auto ps = problems_for("tmp_check_dpz.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - beg:\n"
                           "        kind: BeginningEle\n"
                           "        TwissP:\n"
                           "          beta_a: 12.5\n"
                           "          dbeta_a_dpz: 1.5\n"
                           "          dalpha_a_dpz: 0.1\n"
                           "          dbeta_b_dpz: -2.0\n"
                           "          dalpha_b_dpz: 0.2\n"
                           "          deta_x_dpz: 0.3\n"
                           "          detap_x_dpz: 0.4\n"
                           "          deta_y_dpz: 0.5\n"
                           "          detap_y_dpz: 0.6\n"
                           "          dbeta_x_dpz: 1.0\n");

    REQUIRE(has(ps, "element 'beg>TwissP': unknown parameter 'dbeta_x_dpz'"));
    REQUIRE(count_with(ps, "unknown parameter") == 1);
    // `deta_x_ds` is the derivative with respect to s and stays a parameter of
    // its own alongside `deta_x_dpz`.
    REQUIRE(!has(ps, "'deta_x_ds'"));
}

TEST_CASE("multipole component names are accepted by shape, not by list",
          "[check][problems]") {
    // A multipole component name is generated from its order, so there is no
    // list to check against: `Bn`/`Bs`/`Kn`/`Ks` (magnetic) or `En`/`Es`
    // (electric) plus the order, optionally `L` for the length-integrated form
    // and `_taper` for the tapering parameter, and `tilt` plus the order.
    auto ps = problems_for("tmp_check_mult.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        MagneticMultipoleP:\n"
                           "          tilt7: 0.7\n"
                           "          Bn3: 27.3\n"
                           "          Bn2L: 34.7\n"
                           "          Ks1: 1\n"
                           "          Kn2L_taper: 0.1\n"
                           "          Kn_bogus: 5\n"
                           "          tiltL: 3\n"
                           "        ElectricMultipoleP:\n"
                           "          En3: 1\n"
                           "          Es2L: 2\n"
                           "          Bn3: 4\n");

    REQUIRE(count_with(ps, "unknown parameter") == 3);
    REQUIRE(has(ps, "MagneticMultipoleP': unknown parameter 'Kn_bogus'"));
    // `tilt` has no length-integrated form, so the trailing `L` is not one.
    REQUIRE(has(ps, "MagneticMultipoleP': unknown parameter 'tiltL'"));
    // A magnetic component in the electric group is not an electric one.
    REQUIRE(has(ps, "ElectricMultipoleP': unknown parameter 'Bn3'"));
    // No suggestion is offered: the nearest listed name would be a guess at a
    // different multipole order.
    REQUIRE(!has(ps, "'Kn_bogus'; did you mean"));
}

TEST_CASE("a misspelled element parameter is reported", "[check][problems]") {
    // The parameters outside any group are a short fixed list
    // (lattice-element-parameters.md, s:non.params), extended by what the kind
    // is built from -- `line` for a BeamLine, `branches` for a Lattice.
    auto ps = problems_for("tmp_check_ele.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        lenght: 1\n"
                           "        s_position: 2\n"
                           "        is_on: true\n"
                           "    - ring:\n"
                           "        kind: BeamLine\n"
                           "        line:\n"
                           "          - q1\n"
                           "    - lat1:\n"
                           "        kind: Lattice\n"
                           "        branches:\n"
                           "          - ring\n");

    REQUIRE(has(ps,
                "element 'q1': unknown parameter 'lenght'; did you mean "
                "'length'?"));
    REQUIRE(count_with(ps, "unknown parameter") == 1);
}

TEST_CASE("groups with no fixed vocabulary are left alone",
          "[check][problems]") {
    // MetaP may hold arbitrary metadata beyond its six components (meta.md) and
    // TrackingP is program-specific by design (tracking.md), so neither is
    // checked -- nor is anything nested inside them.
    auto ps = problems_for("tmp_check_open.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        MetaP:\n"
                           "          alias: QUAD1\n"
                           "          blueprint: anything at all\n"
                           "          power_supply:\n"
                           "            NotAGroupP: 3\n"
                           "        TrackingP:\n"
                           "          SciBmad:\n"
                           "            ds_step: 0.3\n"
                           "        GirderP:\n"
                           "          undocumented_so_far: 1\n");

    REQUIRE(count_with(ps, "unknown parameter") == 0);
    REQUIRE(count_with(ps, "unknown parameter group") == 0);
}

TEST_CASE("a parameter group the element's kind does not have is reported",
          "[check][problems]") {
    // Each kind lists the groups it carries (lattice-element-kinds.md). A group
    // PALS defines but this kind does not have is a real group in the wrong
    // place, so it is reported against the kind, not the spelling.
    auto ps = problems_for("tmp_check_kindgroup.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - d1:\n"
                           "        kind: Drift\n"
                           "        length: 1\n"
                           "        SolenoidP:\n"
                           "          Ksol: 10\n"
                           "        ApertureP:\n"
                           "          x_width: 0.02\n"
                           "    - s1:\n"
                           "        kind: Solenoid\n"
                           "        length: 1\n"
                           "        SolenoidP:\n"
                           "          Ksol: 10\n"
                           "    - beg:\n"
                           "        kind: BeginningEle\n"
                           "        TwissP:\n"
                           "          beta_a: 12.5\n"
                           "        ParticleP:\n"
                           "          x: 1\n");

    REQUIRE(has(ps,
                "element 'd1': parameter group 'SolenoidP' is not valid for "
                "kind 'Drift'"));
    // A Drift does have an aperture, and a Solenoid does have SolenoidP.
    REQUIRE(count_with(ps, "not valid for kind") == 1);
    // TwissP and ParticleP state the initial conditions of a branch, which is
    // what the beginning element is for.
    REQUIRE(!has(ps, "TwissP"));
    REQUIRE(!has(ps, "ParticleP"));
}

TEST_CASE("kinds with no documented group list are not constrained",
          "[check][problems]") {
    // `Girder` is still "Under Construction" -- nothing is documented to check
    // its groups against -- while `Placeholder` says outright that it has none.
    auto ps = problems_for("tmp_check_nolist.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - g1:\n"
                           "        kind: Girder\n"
                           "        BodyShiftP:\n"
                           "          x_offset: 0.001\n"
                           "    - p1:\n"
                           "        kind: Placeholder\n"
                           "        ApertureP:\n"
                           "          x_width: 0.02\n");

    REQUIRE(!has(ps, "kind 'Girder'"));
    REQUIRE(has(ps,
                "element 'p1': parameter group 'ApertureP' is not valid for "
                "kind 'Placeholder'"));
}

TEST_CASE("a parameter group defined on its own is checked as a group",
          "[check][problems]") {
    // A group defined outside an element names itself with `kind` and may be
    // given a `name` or `inherit` another (lattice-element-parameters.md,
    // s:inherit.params). `kind: ApertureP` is a group name, not an element
    // kind, and its entries are ApertureP's.
    auto ps = problems_for("tmp_check_named.pals.yaml",
                           "PALS:\n"
                           "  facility:\n"
                           "    - ap1:\n"
                           "        kind: ApertureP\n"
                           "        x_min: -0.03\n"
                           "        x_max: 0.04\n"
                           "        x_mim: 0.01\n"
                           "    - q0:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        ApertureP:\n"
                           "          inherit: ap1\n");

    REQUIRE(!has(ps, "unknown kind 'ApertureP'"));
    REQUIRE(has(ps,
                "element 'ap1>ApertureP': unknown parameter 'x_mim'; did you "
                "mean 'x_min'?"));
    // `inherit` is a group's own key, not one of ApertureP's components.
    REQUIRE(!has(ps, "'inherit'"));
    REQUIRE(count_with(ps, "unknown parameter") == 1);
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

TEST_CASE("a misspelled key of the PALS root node is reported",
          "[check][problems]") {
    // The root's vocabulary is closed (fundamentals.md, s:palsroot), and a
    // misspelling there is silent otherwise: `extension_names` registers no
    // extension, so the only sign of it would be a complaint about the data the
    // extension was meant to cover -- here `Bmad`, an element away.
    auto ps = problems_for("tmp_check_root.pals.yaml",
                           "PALS:\n"
                           "  extension_names:\n"
                           "    names:\n"
                           "      Bmad: bmad-specific data\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        Bmad:\n"
                           "          bmad_key: Quadrupole\n");

    REQUIRE(has(ps,
                "PALS node: unknown key 'extension_names'; did you mean "
                "'extension_labels'?"));
}

TEST_CASE("the documented keys of the PALS root node are not reported",
          "[check][problems]") {
    // Every key of s:palsroot, plus `phase_space_coordinates` (coordinates.md)
    // and `include` (fundamentals.md, s:include). `load` has its own tests.
    auto ps = problems_for("tmp_check_root_ok.pals.yaml",
                           "PALS:\n"
                           "  version: 1\n"
                           "  authors:\n"
                           "    - author:\n"
                           "        name: Lastname, Firstname\n"
                           "  notes:\n"
                           "    - a note\n"
                           "  reminders:\n"
                           "    - a reminder\n"
                           "  phase_space_coordinates: standard\n"
                           "  extension_labels:\n"
                           "    names:\n"
                           "      Bmad: bmad-specific data\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n"
                           "        Bmad:\n"
                           "          bmad_key: Quadrupole\n");

    REQUIRE(count_with(ps, "PALS node:") == 0);
    // The registered name covers the element data it was registered for.
    REQUIRE(count_with(ps, "unknown parameter") == 0);
}

TEST_CASE("a root key registered as an extension is left alone",
          "[check][problems]") {
    auto ps = problems_for("tmp_check_root_ext.pals.yaml",
                           "PALS:\n"
                           "  extension_labels:\n"
                           "    prefixes:\n"
                           "      Bmad_: bmad-specific data\n"
                           "  Bmad_globals:\n"
                           "    anything: at all\n"
                           "  facility:\n"
                           "    - q1:\n"
                           "        kind: Quadrupole\n"
                           "        length: 1\n");

    REQUIRE(count_with(ps, "PALS node:") == 0);
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
