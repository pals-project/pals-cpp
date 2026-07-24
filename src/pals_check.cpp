#include "pals_check.h"

#include <algorithm>
#include <map>
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

// The components of each parameter group, from its section under
// source/parameters. A group absent from this table is one whose contents are
// not a fixed vocabulary (see open_groups) or whose names are generated rather
// than listed (see is_multipole_param).
const std::map<std::string, std::set<std::string>>& group_params() {
    static const std::map<std::string, std::set<std::string>> p = {
        {"ApertureP",
         {"x_min", "x_max", "x_center", "x_width", "y_min", "y_max", "y_center",
          "y_width", "shape", "location", "vertices", "material", "thickness",
          "aperture_shifts_with_body", "aperture_active"}},
        {"BeamBeamP",
         {"sigma_x", "sigma_y", "sigma_z", "alpha_x", "beta_x", "alpha_y",
          "beta_y", "charge", "energy", "N_particle"}},
        {"BendP",
         {"angle_ref", "Bn0_ref", "e1", "e2",
          "e1_rect", "e2_rect", "edge1_int", "edge2_int", "g_ref",
          "h1", "h2", "Kn0_from_g_ref", "L_chord", "L_sagitta", "L_rectangle",
          "multipole_geometry", "radius_ref", "ref_geometry", "tilt_ref"}},
        {"BodyShiftP",
         {"x_offset", "y_offset", "z_offset", "x_rot", "y_rot", "z_rot"}},
        {"ConverterP", {"species_out", "E_tot_ref_out"}},
        {"FloorP", {"x", "y", "z", "theta", "phi", "psi", "user_set"}},
        {"FloorShiftP",
         {"x_offset", "y_offset", "z_offset", "t_offset", "x_rot", "y_rot",
          "z_rot"}},
        {"FoilP", {"dE_ref"}},
        {"ForkP",
         {"to_line", "destination_element", "direction", "new_branch",
          "propagate_reference", "forked_to"}},
        {"ParticleP",
         {"x",           "px",          "y",          "py",
          "z",           "pz",          "spin_x",     "spin_y",
          "spin_z",      "sigma_xx",    "sigma_xpx",  "sigma_xy",
          "sigma_xpy",   "sigma_xz",    "sigma_xpz",  "sigma_pxpx",
          "sigma_pxy",   "sigma_pxpy",  "sigma_pxz",  "sigma_pxpz",
          "sigma_yy",    "sigma_ypy",   "sigma_yz",   "sigma_ypz",
          "sigma_pypy",  "sigma_pyz",   "sigma_pypz", "sigma_zz",
          "sigma_zpz",   "sigma_pzpz"}},
        {"PatchP",
         {"x_offset", "y_offset", "z_offset", "x_rot", "y_rot", "z_rot",
          "flexible", "ref_coords", "user_sets_length"}},
        {"ReferenceChangeP",
         {"dtime_ref", "dE_ref", "dpc_ref", "time_ref", "E_tot_ref", "pc_ref",
          "species_ref"}},
        {"ReferenceP", {"species_ref", "pc_ref", "E_tot_ref", "time_ref"}},
        {"RFP",
         {"frequency", "harmon", "voltage", "gradient", "phase",
          "multipass_phase", "cavity_type", "num_cells", "zero_phase",
          "L_active", "dE_ref"}},
        {"SolenoidP", {"Ksol", "Bsol"}},
        {"TaylorP",
         {"x_out", "px_out", "y_out", "py_out", "z_out", "pz_out", "S_q1_out",
          "S_qx_out", "S_qy_out", "S_qz_out", "ref_in"}},
        {"TwissP",
         {"alpha_a", "alpha_b", "beta_a", "beta_b", "cmat11", "cmat12",
          "cmat21", "cmat22", "eta_x", "eta_y", "etap_x", "etap_y",
          "deta_x_ds", "deta_y_ds", "phi_a", "phi_b"}}};
    return p;
}

// The parameter groups each element kind may carry, from the "Element parameter
// groups associated with this element kind are" list in its section of
// lattice-element-kinds.md.
//
// A kind absent from this table is one nothing is documented for, and so is not
// checked: `Feedback`, `Fiducial` and `Girder` are still "Under Construction",
// and `BeamLine`, `Lattice`, `Controller`, `constant` and `variable` are not
// lattice elements at all. `Placeholder` is present with an empty list on
// purpose -- its section says it has no associated parameter groups.
const std::map<std::string, std::set<std::string>>& kind_groups() {
    // Carried by every physical element.
    static const std::set<std::string> common = {
        "ApertureP", "BodyShiftP", "FloorP", "MetaP", "ReferenceP",
        "TrackingP"};
    // The above plus multipoles, which is the list most magnets have.
    static const std::set<std::string> magnet = [] {
        std::set<std::string> s = common;
        s.insert({"ElectricMultipoleP", "MagneticMultipoleP"});
        return s;
    }();
    auto with = [](std::set<std::string> base,
                   std::initializer_list<const char*> extra) {
        for (const char* e : extra) base.insert(e);
        return base;
    };

    static const std::map<std::string, std::set<std::string>> g = {
        {"ACKicker", with(common, {"ACKickerP"})},
        {"BeamBeam", with(common, {"BeamBeamP"})},
        // TwissP and ParticleP are not in the BeginningEle list in
        // lattice-element-kinds.md yet; they are being added there. Both state
        // the initial conditions of a branch -- twiss.md says as much -- and
        // the beginning element is where a branch states them, which is why no
        // other kind's list mentions either group. ForkFromP is the reverse
        // fork link (pals#272), which the parser writes onto the three kinds a
        // Fork may point at.
        {"BeginningEle", with(common, {"TwissP", "ParticleP", "ForkFromP"})},
        {"Bend", with(magnet, {"BendP"})},
        {"Converter", with(magnet, {"ConverterP"})},
        {"CrabCavity", magnet},
        {"Drift", common},
        {"EGun", magnet},
        {"FloorShift", {"FloorShiftP", "MetaP", "ReferenceP"}},
        {"Foil", with(common, {"FoilP"})},
        {"Fork", with(common, {"ForkP", "ForkFromP"})},
        {"Instrument", magnet},
        {"Kicker", magnet},
        // ApertureP is not in the Marker list in lattice-element-kinds.md yet;
        // it is being added there. A Marker often stands in for a beam position
        // monitor, which has an aperture like any other physical element.
        {"Marker",
         {"ApertureP", "BodyShiftP", "FloorP", "MetaP", "ReferenceP",
          "ForkFromP"}},
        {"Mask", magnet},
        {"Match", common},
        {"Multipole", magnet},
        {"Octupole", magnet},
        {"Patch", with(common, {"PatchP"})},
        {"Placeholder", {}},
        {"Quadrupole", magnet},
        {"ReferenceChange",
         {"FloorP", "MetaP", "ReferenceChangeP", "ReferenceP", "TrackingP"}},
        {"RFCavity", with(magnet, {"RFP", "SolenoidP"})},
        {"Sextupole", magnet},
        {"Solenoid", with(magnet, {"SolenoidP"})},
        {"Taylor", with(common, {"TaylorP"})},
        {"UnionEle", common},
        {"Wiggler", magnet}};
    return g;
}

// Groups whose contents are deliberately not a closed vocabulary, so their
// entries are left alone:
//  - MetaP may hold arbitrary metadata beyond its six components (meta.md).
//  - TrackingP is program-specific by design (tracking.md).
//  - ACKickerP and GirderP are "In Construction" -- nothing to check against.
//  - ForkFromP is built by the parser, keyed `{branch}>>{element}` (pals#272).
const std::set<std::string>& open_groups() {
    static const std::set<std::string> g = {"MetaP", "TrackingP", "ACKickerP",
                                            "GirderP", "ForkFromP"};
    return g;
}

// The keys the `PALS` root node may carry: the six of fundamentals.md
// (s:palsroot), `phase_space_coordinates` (coordinates.md), and the two ways a
// file names another, `include` and `load` (fundamentals.md, s:include).
const std::set<std::string>& known_pals_keys() {
    static const std::set<std::string> k = {
        "authors", "extension_labels",        "facility",  "include", "load",
        "notes",   "phase_space_coordinates", "reminders", "version"};
    return k;
}

// Keys any parameter group may carry: a group defined outside an element is
// given a `kind` naming the group and may be given a `name`, and any group may
// pull its values from another with `inherit` (lattice-element-parameters.md,
// s:inherit.params).
bool is_group_meta_key(const std::string& key) {
    return key == "inherit" || key == "name" || key == "kind";
}

// True if `key` names a multipole component of `group`. These names are
// generated from the multipole order rather than listed: a field component is
// `Bn`/`Bs`/`Kn`/`Ks` (magnetic) or `En`/`Es` (electric) followed by the order,
// optionally `L` for the length-integrated form and `_taper` for the tapering
// parameter that goes with it; the tilt of an order is `tilt` followed by the
// order (magneticmultipole.md, electricmultipole.md).
bool is_multipole_param(const std::string& group, const std::string& key) {
    static const std::set<std::string> magnetic = {"Bn", "Bs", "Kn", "Ks"};
    static const std::set<std::string> electric = {"En", "Es"};
    const std::set<std::string>& field =
        group == "MagneticMultipoleP" ? magnetic : electric;

    // `geometry` selects how the multipoles of a bend are evaluated.
    if (key == "geometry") return true;

    std::string s = key;
    // `_taper` names the tapering parameter of the field parameter it follows.
    const std::string taper = "_taper";
    if (s.size() > taper.size() &&
        s.compare(s.size() - taper.size(), taper.size(), taper) == 0)
        s.resize(s.size() - taper.size());
    // The length-integrated form appends `L`.
    if (s.size() > 1 && s.back() == 'L') s.pop_back();

    std::string stem;
    if (s.compare(0, 4, "tilt") == 0 && s.size() > 4) {
        // Tilt has neither an integrated nor a tapering form, so the trimming
        // above must not have fired.
        if (s != key) return false;
        stem = "tilt";
    } else if (s.size() > 2 && field.count(s.substr(0, 2))) {
        stem = s.substr(0, 2);
    } else {
        return false;
    }
    // Whatever follows the stem is the order, and must be a number.
    for (size_t i = stem.size(); i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return s.size() > stem.size();
}

// The parameters an element of `kind` may carry outside any parameter group:
// the six that fit no group (lattice-element-parameters.md, s:non.params),
// `inherit` and the line-item modifiers usable on an element defined in place,
// and then whatever the kind itself is built from.
const std::set<std::string>& element_params(const std::string& kind) {
    static const std::set<std::string> base = {
        "field_master", "is_on",  "kind",      "length",
        "name",         "s_position", "inherit", "direction",
        "multipass_index"};
    static const std::set<std::string> beamline = [] {
        std::set<std::string> s = base;
        s.insert({"line", "multipass", "zero_point", "repeat"});
        return s;
    }();
    static const std::set<std::string> lattice = [] {
        std::set<std::string> s = base;
        s.insert("branches");
        return s;
    }();
    static const std::set<std::string> controller = [] {
        std::set<std::string> s = base;
        s.insert({"control_type", "variables", "controls"});
        return s;
    }();
    static const std::set<std::string> definition = [] {
        std::set<std::string> s = base;
        s.insert("value");
        return s;
    }();

    if (kind == "BeamLine") return beamline;
    if (kind == "Lattice") return lattice;
    if (kind == "Controller") return controller;
    if (kind == "constant" || kind == "variable") return definition;
    return base;
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
//
// `max_edits` loosens the cap for a caller whose vocabulary is small and whose
// names are nothing like each other, where a farther guess is still safe.
std::string suggest(const std::string& bad, const std::set<std::string>& known,
                    int max_edits = 2) {
    std::string low = lowered(bad);
    for (const std::string& k : known)
        if (lowered(k) == low) return k;

    // Otherwise the nearest name within a couple of edits, and never more than
    // a third of the name's length, so short names are not matched to anything
    // and everything.
    int cap = std::min(max_edits, std::max(1, static_cast<int>(bad.size()) / 3));
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

// "element 'q1': " when the map is keyed, "" when it is not. `group` names the
// parameter group being looked at, and is empty at element level; it joins the
// element name with the same `>` the parameter paths use: "element 'q1>ForkP': ".
std::string where(const ryml::Tree& t, size_t node, const std::string& group) {
    std::string name;
    if (t.has_key(node)) name = std::string(t.key(node).str, t.key(node).len);
    if (name.empty()) return group.empty() ? "" : group + ": ";
    if (!group.empty()) name += ">" + group;
    return "element '" + name + "': ";
}

void report(std::vector<std::string>& problems, const ryml::Tree& t,
            size_t node, const std::string& group, const std::string& what,
            const std::string& bad, const std::set<std::string>& known) {
    std::string msg =
        where(t, node, group) + "unknown " + what + " '" + bad + "'";
    std::string fix = suggest(bad, known);
    if (!fix.empty()) msg += "; did you mean '" + fix + "'?";
    add(problems, msg);
}

// Check the entries of one parameter group. `owner` is the element the group
// hangs off (or the group's own definition when it stands alone), used only to
// name the group in the message.
void check_group(const ryml::Tree& t, size_t owner, size_t group_node,
                 const std::string& group, const Extensions& ext,
                 std::vector<std::string>& problems) {
    auto entry = group_params().find(group);
    bool listed = entry != group_params().end();
    bool multipole = group == "MagneticMultipoleP" ||
                     group == "ElectricMultipoleP";
    if (!listed && !multipole) return;

    // A group may be given as a sequence of single-key maps as well as a map;
    // MagneticMultipoleP is written both ways.
    std::vector<size_t> maps;
    if (t.is_map(group_node)) {
        maps.push_back(group_node);
    } else if (t.is_seq(group_node)) {
        for (size_t e = t.first_child(group_node); e != ryml::NONE;
             e = t.next_sibling(e))
            if (t.is_map(e)) maps.push_back(e);
    }

    for (size_t m : maps) {
        // An `extension` key hands the rest of the dictionary to an extension
        // schema, exactly as it does at element level.
        if (t.find_child(m, ryml::to_csubstr("extension")) != ryml::NONE)
            continue;
        for (size_t c = t.first_child(m); c != ryml::NONE;
             c = t.next_sibling(c)) {
            if (!t.has_key(c)) continue;
            std::string k(t.key(c).str, t.key(c).len);
            if (ext.marks(k) || is_group_meta_key(k)) continue;
            if (multipole ? is_multipole_param(group, k)
                          : entry->second.count(k) != 0)
                continue;
            // A multipole name is generated, so there is no table to suggest
            // from -- the nearest listed name would be a guess at a different
            // order, which is worse than no guess at all.
            report(problems, t, owner, group, "parameter", k,
                   multipole ? std::set<std::string>() : entry->second);
        }
    }
}

// Check the parameters an element carries outside any parameter group.
void check_element(const ryml::Tree& t, size_t node, const std::string& kind,
                   const Extensions& ext,
                   std::vector<std::string>& problems) {
    const std::set<std::string>& known = element_params(kind);
    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (!t.has_key(c)) continue;
        std::string k(t.key(c).str, t.key(c).len);
        // Group names are judged by looks_like_group, extension data not at all.
        if (ext.marks(k) || looks_like_group(k) || known.count(k)) continue;
        // A key whose value holds an `extension` marks itself: the name is the
        // extension's to choose, since `<some_name>: {extension: ...}` is the
        // form used where no label is registered (extensions.md).
        if (t.is_map(c) &&
            t.find_child(c, ryml::to_csubstr("extension")) != ryml::NONE)
            continue;
        report(problems, t, node, "", "parameter", k, known);
    }
}

void walk(const ryml::Tree& t, size_t node, const Extensions& ext,
          std::vector<std::string>& problems) {
    if (node == ryml::NONE) return;

    std::string ele_kind;  // set when this map is an element definition
    if (t.is_map(node)) {
        // An `extension` key hands the rest of this dictionary to an extension
        // schema, which PALS explicitly does not validate.
        if (t.find_child(node, ryml::to_csubstr("extension")) != ryml::NONE)
            return;

        size_t kind = t.find_child(node, ryml::to_csubstr("kind"));
        if (kind != ryml::NONE && t.has_val(kind)) {
            std::string v(t.val(kind).str, t.val(kind).len);
            if (known_groups().count(v)) {
                // A parameter group defined outside an element names itself
                // with `kind` (lattice-element-parameters.md, s:inherit.params),
                // so this map is the group rather than an element.
                check_group(t, node, node, v, ext, problems);
            } else if (known_kinds().count(v)) {
                ele_kind = v;
                check_element(t, node, v, ext, problems);
            } else if (!v.empty() && !ext.marks(v)) {
                report(problems, t, node, "", "kind", v, known_kinds());
            }
        }
    }

    // The groups this element's kind may carry, or null when the kind does not
    // constrain them (or this map is not an element at all).
    const std::set<std::string>* allowed = nullptr;
    if (!ele_kind.empty()) {
        auto it = kind_groups().find(ele_kind);
        if (it != kind_groups().end()) allowed = &it->second;
    }

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (t.has_key(c)) {
            std::string k(t.key(c).str, t.key(c).len);
            if (ext.marks(k)) continue;  // registered extension data
            if (k == "extension_labels") continue;
            if (looks_like_group(k)) {
                if (!known_groups().count(k)) {
                    report(problems, t, node, "", "parameter group", k,
                           known_groups());
                    continue;
                }
                // A group PALS defines, but not one this kind has: a
                // `SolenoidP` on a `Drift` is a real group in the wrong place,
                // so it is reported against the kind rather than the spelling.
                if (allowed && !allowed->count(k)) {
                    add(problems, where(t, node, "") + "parameter group '" + k +
                                      "' is not valid for kind '" + ele_kind +
                                      "'");
                    continue;
                }
                // Not a closed vocabulary: leave the whole subtree alone rather
                // than judge names the standard does not define.
                if (open_groups().count(k)) continue;
                check_group(t, node, c, k, ext, problems);
            }
        }
        walk(t, c, ext, problems);
    }
}

// Check the keys of the `PALS` root node itself. walk() judges what is below it
// in context, but the root's own vocabulary is closed and small, and a
// misspelling there is otherwise silent: `extension_names` registers no
// extension at all, and the first sign of it is a complaint about whatever the
// extension was meant to cover, an element away from the actual mistake.
//
// Anything outside the `PALS` node is outside the standard and ignored
// (fundamentals.md, s:palsroot), so only this node's own keys are checked.
void check_pals_root(const ryml::Tree& t, const Extensions& ext,
                     std::vector<std::string>& problems) {
    size_t pals = t.find_child(t.root_id(), ryml::to_csubstr("PALS"));
    if (pals == ryml::NONE || !t.is_map(pals)) return;
    for (size_t c = t.first_child(pals); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (!t.has_key(c)) continue;
        std::string k(t.key(c).str, t.key(c).len);
        if (ext.marks(k) || known_pals_keys().count(k)) continue;
        // Nine names with nothing in common, so a cap of four edits still
        // cannot match at random -- and it reaches the wrong word in a compound
        // name, which is the mistake this vocabulary invites.
        std::string msg = "PALS node: unknown key '" + k + "'";
        std::string fix = suggest(k, known_pals_keys(), 4);
        if (!fix.empty()) msg += "; did you mean '" + fix + "'?";
        add(problems, msg);
    }
}

}  // namespace

void check_pals_names(const ryml::Tree& t, std::vector<std::string>& problems) {
    Extensions ext = collect_extensions(t);
    check_pals_root(t, ext, problems);
    walk(t, t.root_id(), ext, problems);
}
