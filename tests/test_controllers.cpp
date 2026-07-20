#include "test_helpers.h"

TEST_CASE("parse_and_expand_PALS evaluates controller expressions",
          "[expr][lattices][controller]") {
    const char* path = "tmp_controller.pals.yaml";
    write_tmp(path,
              "PALS:\n"
              "  facility:\n"
              "    - my_const:\n"
              "        kind: constant\n"
              "        value: 2.0\n"
              "    - ps27:\n"
              "        kind: Controller\n"
              "        control_type: ABSOLUTE\n"
              "        variables:\n"
              "          cur1: 0.023\n"
              "          cur2: cur1 / c_light\n"
              "        controls:\n"
              "          - parameter: Qa.*>MagneticMultipoleP.Ks2L\n"
              "            expression: 0.075*sin(cur1) + 0.3*cur2\n"
              "          - parameter: Qb>MagneticMultipoleP.Kn1L\n"
              "            expression: cur1 * my_const\n"
              "          - parameter: Qc>MagneticMultipoleP.Kn0\n"
              "            expression: 0.01 + random_gauss()\n"
              "    - chrom_a:\n"
              "        kind: Controller\n"
              "        control_type: RELATIVE\n"
              "        variables:\n"
              "          command: 0.4\n"
              "          derived: ps27>cur1 * 2\n"
              "        controls:\n"
              "          - parameter: S1>MagneticMultipoleP.Kn2L\n"
              "            expression: 5.62 * command + 0.02 * command^2\n"
              "    - main_line:\n"
              "        kind: BeamLine\n"
              "        line:\n"
              "          - my_const\n"
              "    - lat1:\n"
              "        kind: Lattice\n"
              "        branches:\n"
              "          - main_line\n"
              "    - use: \"lat1\"\n");

    struct lattices lat = parse_and_expand_PALS(path, nullptr);
    REQUIRE(lat.leftover != nullptr);

    const double cur1 = 0.023;
    const double cur2 = cur1 / 2.99792458e8;

    // Controllers are facility-level, so they are leftover rather than part
    // of the lattice; their expressions are evaluated all the same.
    // Controller variables are evaluated with the controller's own symbol
    // table: cur2 references the earlier variable cur1 and the constant c_light.
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

    // The `parameter` target spec and `control_type` are names, left untouched.
    REQUIRE(val_eq(lat.leftover, get_child_by_key(lat.leftover, c0, "parameter"),
                   "Qa.*>MagneticMultipoleP.Ks2L"));
    REQUIRE(val_eq(lat.leftover,
                   get_child_by_key(lat.leftover, ps27, "control_type"),
                   "ABSOLUTE"));

    // A second controller may reference the first's variables via `name>var`.
    YAMLNodeId chrom = facility_param(lat.leftover, "chrom_a");
    YAMLNodeId cvars = get_child_by_key(lat.leftover, chrom, "variables");
    REQUIRE(close(num_val(lat.leftover, get_child_by_key(lat.leftover, cvars,
                                                         "derived")),
                  cur1 * 2.0));
    YAMLNodeId ccontrols = get_child_by_key(lat.leftover, chrom, "controls");
    YAMLNodeId cc0 = get_child_by_index(lat.leftover, ccontrols, 0);
    REQUIRE(close(num_val(lat.leftover,
                          get_child_by_key(lat.leftover, cc0, "expression")),
                  5.62 * 0.4 + 0.02 * 0.4 * 0.4));

    // The combined tree keeps the original controller expression text.
    YAMLNodeId c_ps27 = facility_param(lat.combined, "ps27");
    YAMLNodeId c_controls = get_child_by_key(lat.combined, c_ps27, "controls");
    YAMLNodeId c_c0 = get_child_by_index(lat.combined, c_controls, 0);
    REQUIRE(val_eq(lat.combined,
                   get_child_by_key(lat.combined, c_c0, "expression"),
                   "0.075*sin(cur1) + 0.3*cur2"));

    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.leftover);
    rm_tmp(path);
}
