#include "pals_check.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "pals_util.h"

namespace {

// Every element kind PALS defines (lattice-element-kinds.md, s:ele.kinds),
// followed by the kinds that name something other than a lattice element: the
// two structural kinds (beamlines.md, lattice-construction.md) and the three
// definition kinds -- `constant` (fundamentals.md, s:constants), and `variable`
// and `Controller` (pals#240, pals#237).
const std::set<std::string>& known_kinds() {
    static const std::set<std::string> k = {
        "ACKicker",   "BeamBeam",  "BeginningEle",    "Bend",
        "Converter",  "CrabCavity", "Drift",          "EGun",
        "Feedback",   "Fiducial",  "FloorShift",      "Foil",
        "Fork",       "Girder",    "Instrument",      "Kicker",
        "Marker",     "Mask",      "Match",           "Multipole",
        "Octupole",   "Patch",     "Placeholder",     "Quadrupole",
        "ReferenceChange", "RFCavity", "Sextupole",   "Solenoid",
        "Taylor",     "UnionEle",  "Wiggler",
        // Not lattice elements:
        "BeamLine",   "Lattice",   "Controller",      "constant",
        "variable"};
    return k;
}

// Every parameter group PALS defines (lattice-element-parameter-groups.md, and
// one file each under source/parameters). ForkFromP is pals#272.
const std::set<std::string>& known_groups() {
    static const std::set<std::string> g = {
        "ACKickerP",         "ApertureP",   "BeamBeamP",   "BendP",
        "BodyShiftP",        "ConverterP",  "ElectricMultipoleP",
        "FloorP",            "FloorShiftP", "FoilP",       "ForkFromP",
        "ForkP",             "GirderP",     "MagneticMultipoleP",
        "MetaP",             "ParticleP",   "PatchP",      "ReferenceChangeP",
        "ReferenceP",        "RFP",         "SolenoidP",   "TaylorP",
        "TrackingP",         "TwissP"};
    return g;
}

// A parameter group is named in strict CamelCase and ends in `P`; every other
// key of an element is a plain lower-case parameter (`length`, `s_position`,
// `to_line`). So a key of this shape that is not a group PALS knows is a group
// name spelled wrong -- which is the point of the convention.
//
// Letters only: a digit or an underscore (`L20_BLW_P`) is a naming style PALS
// never uses for a group, and marks the key as something outside the standard.
bool looks_like_group(const std::string& key) {
    if (key.size() < 2 || key.back() != 'P' || key[0] < 'A' || key[0] > 'Z')
        return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    return true;
}

// The names registered under `PALS: extension_labels` (extensions.md,
// s:extension-syntax). A key or enum value matching one of these introduces
// extension data, which is outside the standard and so outside these checks.
struct Extensions {
    std::set<std::string> names, prefixes, suffixes;

    bool marks(const std::string& s) const {
        if (names.count(s)) return true;
        for (const std::string& p : prefixes)
            if (s.size() >= p.size() && s.compare(0, p.size(), p) == 0)
                return true;
        for (const std::string& x : suffixes)
            if (s.size() >= x.size() &&
                s.compare(s.size() - x.size(), x.size(), x) == 0)
                return true;
        return false;
    }
};

void collect_label_keys(const ryml::Tree& t, size_t parent, const char* key,
                        std::set<std::string>& out) {
    size_t m = t.find_child(parent, ryml::to_csubstr(key));
    if (m == ryml::NONE || !t.is_map(m)) return;
    for (size_t c = t.first_child(m); c != ryml::NONE; c = t.next_sibling(c))
        if (t.has_key(c)) out.insert(std::string(t.key(c).str, t.key(c).len));
}

Extensions collect_extensions(const ryml::Tree& t) {
    Extensions ext;
    // `extension_labels` must be a child of the `PALS` root node.
    size_t pals = t.find_child(t.root_id(), ryml::to_csubstr("PALS"));
    if (pals == ryml::NONE || !t.is_map(pals)) return ext;
    size_t labels = t.find_child(pals, ryml::to_csubstr("extension_labels"));
    if (labels == ryml::NONE || !t.is_map(labels)) return ext;
    collect_label_keys(t, labels, "names", ext.names);
    collect_label_keys(t, labels, "prefixes", ext.prefixes);
    collect_label_keys(t, labels, "suffixes", ext.suffixes);
    return ext;
}

// Levenshtein distance, abandoned once every cell of a row exceeds `cap`. Only
// small distances are of interest, so the cap keeps a comparison against a long
// unrelated name cheap.
int edit_distance(const std::string& a, const std::string& b, int cap) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        int row_min = cur[0];
        for (size_t j = 1; j <= b.size(); ++j) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min(sub, std::min(prev[j] + 1, cur[j - 1] + 1));
            row_min = std::min(row_min, cur[j]);
        }
        if (row_min > cap) return cap + 1;
        prev.swap(cur);
    }
    return prev[b.size()];
}

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The known name `bad` was most likely meant to be, or "" if nothing is close.
// A name that differs only in case is the answer outright -- `marker` for
// `Marker` -- since that is the mistake the CamelCase convention invites.
std::string suggest(const std::string& bad, const std::set<std::string>& known) {
    std::string low = lowered(bad);
    for (const std::string& k : known)
        if (lowered(k) == low) return k;

    // Otherwise the nearest name within a couple of edits, and never more than
    // a third of the name's length, so short names are not matched to anything
    // and everything.
    int cap = std::min(2, std::max(1, static_cast<int>(bad.size()) / 3));
    std::string best;
    int best_d = cap + 1;
    for (const std::string& k : known) {
        int d = edit_distance(low, lowered(k), cap);
        if (d < best_d) {
            best_d = d;
            best = k;
        } else if (d == best_d) {
            best.clear();  // ambiguous: offer nothing rather than a coin toss
        }
    }
    return best_d <= cap ? best : std::string();
}

// Append a problem, skipping exact duplicates (mirrors expansion's add_problem).
void add(std::vector<std::string>& problems, const std::string& msg) {
    for (const std::string& p : problems)
        if (p == msg) return;
    problems.push_back(msg);
}

// "element 'q1': " when the map is keyed, "" when it is not.
std::string where(const ryml::Tree& t, size_t node) {
    if (!t.has_key(node)) return "";
    return "element '" + std::string(t.key(node).str, t.key(node).len) + "': ";
}

void report(std::vector<std::string>& problems, const ryml::Tree& t,
            size_t node, const std::string& what, const std::string& bad,
            const std::set<std::string>& known) {
    std::string msg = where(t, node) + "unknown " + what + " '" + bad + "'";
    std::string fix = suggest(bad, known);
    if (!fix.empty()) msg += "; did you mean '" + fix + "'?";
    add(problems, msg);
}

void walk(const ryml::Tree& t, size_t node, const Extensions& ext,
          std::vector<std::string>& problems) {
    if (node == ryml::NONE) return;

    if (t.is_map(node)) {
        // An `extension` key hands the rest of this dictionary to an extension
        // schema, which PALS explicitly does not validate.
        if (t.find_child(node, ryml::to_csubstr("extension")) != ryml::NONE)
            return;

        size_t kind = t.find_child(node, ryml::to_csubstr("kind"));
        if (kind != ryml::NONE && t.has_val(kind)) {
            std::string v(t.val(kind).str, t.val(kind).len);
            if (!v.empty() && !known_kinds().count(v) && !ext.marks(v))
                report(problems, t, node, "kind", v, known_kinds());
        }
    }

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (t.has_key(c)) {
            std::string k(t.key(c).str, t.key(c).len);
            if (ext.marks(k)) continue;  // registered extension data
            if (k == "extension_labels") continue;
            if (looks_like_group(k) && !known_groups().count(k))
                report(problems, t, node, "parameter group", k,
                       known_groups());
        }
        walk(t, c, ext, problems);
    }
}

}  // namespace

void check_pals_names(const ryml::Tree& t, std::vector<std::string>& problems) {
    walk(t, t.root_id(), collect_extensions(t), problems);
}
