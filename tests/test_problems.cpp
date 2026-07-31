#include "test_helpers.h"

// ============================================================
// The classification carried on each problem (severity / origin / path)
// ============================================================
//
// Every problem the library reports says more than what went wrong: whether the
// expanded trees can still be trusted (severity) and whether the lattice author
// is the one who can fix it (origin). A caller that only prints the message does
// not care, but one deciding whether to fail a build, or where to put a squiggle
// in an editor, does. These tests pin the classification of one case of each
// kind, since nothing else in the suite reads those fields.

namespace {

// The single problem whose message contains `needle`. Fails the test if the
// document produced no such problem, so every assertion below is about a
// problem that was actually raised rather than a default-constructed struct.
struct problem find(const struct lattices& lat, const char* needle) {
    for (size_t i = 0; i < lat.problems.count; ++i)
        if (std::string(lat.problems.items[i].message).find(needle) !=
            std::string::npos)
            return lat.problems.items[i];
    std::string all;
    for (size_t i = 0; i < lat.problems.count; ++i)
        all += std::string("\n  ") + lat.problems.items[i].message;
    FAIL("no problem matching '" << needle << "'; got:" << all);
    return lat.problems.items[0];  // unreachable; FAIL throws
}

void free_all(struct lattices& lat) {
    free_lattice_problems(lat.problems);
    delete_tree(lat.original);
    delete_tree(lat.combined);
    delete_tree(lat.expanded);
    delete_tree(lat.full_expanded);
    delete_tree(lat.adjunct);
}

const char* BEGIN_ENTRY =
    "          - begin:\n"
    "              kind: BeginningEle\n"
    "              ReferenceP:\n"
    "                species_ref: \"electron\"\n"
    "                E_tot_ref: 1.0e9\n";

std::string facility_file(const std::string& body) {
    return "PALS:\n"
           "  facility:\n" +
           body +
           "    - lat1:\n"
           "        kind: Lattice\n"
           "        branches:\n"
           "          - main\n"
           "    - use: \"lat1\"\n";
}

}  // namespace

TEST_CASE("a dangling reference is a fatal problem the author can fix",
          "[problems]") {
    // Nothing can be expanded in place of an element that does not exist, so the
    // lattice really is missing a piece: PROBLEM_ERROR. And the document is what
    // is wrong, so it is the author who can fix it: PROBLEM_INPUT.
    struct lattices lat = expand_PALS_string(
        facility_file("    - main:\n"
                      "        kind: BeamLine\n"
                      "        line:\n" +
                      std::string(BEGIN_ENTRY) + "          - nosuchele\n")
            .c_str(),
        nullptr);

    struct problem p = find(lat, "undefined element or line");
    REQUIRE(p.severity == PROBLEM_ERROR);
    REQUIRE(p.origin == PROBLEM_INPUT);

    free_all(lat);
}

TEST_CASE("a misspelled name is fatal, not advisory", "[problems][check]") {
    // `Quadrapole` is valid YAML that expansion carries through untouched, so
    // the trees come back structurally sound -- but holding something other than
    // what the author asked for, with nothing downstream to say so. That is a
    // wrong answer rather than a blemish, so it is PROBLEM_ERROR, and the author
    // is the one who can fix it.
    struct lattices lat = expand_PALS_string("PALS:\n"
                                             "  facility:\n"
                                             "    - q1:\n"
                                             "        kind: Quadrapole\n",
                                             nullptr);

    struct problem p = find(lat, "unknown kind 'Quadrapole'");
    REQUIRE(p.severity == PROBLEM_ERROR);
    REQUIRE(p.origin == PROBLEM_INPUT);
    REQUIRE(std::string(p.path) == "q1");

    free_all(lat);
}

TEST_CASE("a gap in this library is not blamed on the document",
          "[problems]") {
    // A Foil changes the species passing through it, and this library does not
    // compute that change. The lattice is valid PALS: editing it cannot make the
    // message go away, so the origin must not point at the author.
    struct lattices lat = expand_PALS_string(
        facility_file("    - main:\n"
                      "        kind: BeamLine\n"
                      "        line:\n" +
                      std::string(BEGIN_ENTRY) +
                      "          - fl:\n"
                      "              kind: Foil\n"
                      "              length: 0.1\n")
            .c_str(),
        nullptr);

    struct problem p = find(lat, "downstream species change is not computed");
    REQUIRE(p.origin == PROBLEM_UNSUPPORTED);
    // The upstream species is carried through unchanged rather than guessed at,
    // so everything else in the trees still holds.
    REQUIRE(p.severity == PROBLEM_WARNING);

    free_all(lat);
}

TEST_CASE("a gap in the standard is blamed on neither side", "[problems]") {
    // The standard gives an error magnitude but not its distribution, so there
    // is nothing for the author to fix and nothing for this library to
    // implement. The deterministic value is still written, hence WARNING.
    struct lattices lat = expand_PALS_string(
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
                      std::string(BEGIN_ENTRY) + "          - Q1\n")
            .c_str(),
        nullptr);

    struct problem p = find(lat, "absolute_error/relative_error are not");
    REQUIRE(p.origin == PROBLEM_UNSPECIFIED);
    REQUIRE(p.severity == PROBLEM_WARNING);

    free_all(lat);
}

TEST_CASE("every problem carries readable strings, never null", "[problems]") {
    // `path` is empty rather than null when a problem is not tied to one spot,
    // so a caller can read both fields without a null check. A document with
    // several unrelated faults is used so this covers more than one code path.
    struct lattices lat = expand_PALS_string(
        facility_file("    - q1:\n"
                      "        kind: Quadrapole\n"
                      "        FlorP:\n"
                      "          x: 1\n"
                      "    - main:\n"
                      "        kind: BeamLine\n"
                      "        line:\n" +
                      std::string(BEGIN_ENTRY) + "          - nosuchele\n")
            .c_str(),
        nullptr);

    REQUIRE(lat.problems.count > 1);
    for (size_t i = 0; i < lat.problems.count; ++i) {
        INFO("problem " << i);
        REQUIRE(lat.problems.items[i].message != nullptr);
        REQUIRE(lat.problems.items[i].path != nullptr);
        REQUIRE(std::string(lat.problems.items[i].message).size() > 0);
    }

    free_all(lat);
}
