// PALS name matching and parameter lookup: resolves a PALS name-matching
// string (see match_names in yaml_c_wrapper.h) against a tree using PCRE2, and
// reads the value of a matched element parameter, constant, or variable. The
// lattice expansion that produces the trees this runs on lives in
// pals_expand.cpp; the generic YAML tree wrapper in yaml_c_wrapper.cpp.

#include "yaml_c_wrapper.h"
#include "yaml_tree.h"
#include "pals_util.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <set>
#include <string>
#include <vector>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

// ============================================================
// NAME MATCHING
// ============================================================
//
// Implements PALS name matching (the "Name Matching" and "Element Name
// Matching" sections of the standard). A query string selects a set of named
// constructs — elements, element parameter groups, element parameters,
// constants, and variables. The syntax is:
//
//   [{lattice}>>>][{branch}>>][{kind}::]{name}[>{group}.{sub}. ... .{param}]
//
// {lattice}, {branch}, and {name} are PCRE2 patterns matched against the whole
// name (anchored at both ends); {kind} is matched exactly; the parameter path
// after the single '>' is matched exactly, key by key. {branch} matches an
// element if ANY enclosing BeamLine/Branch name matches, so elements in
// sub-lines are included. A bare name — no lattice/branch/kind qualifier and no
// parameter path — additionally matches constants and variables. The node
// returned for a match is whatever the string resolves to: the element node,
// the parameter-group or parameter node, or the constant/variable node.
//
// Not yet implemented from Element Name Matching: '#N' instance selection,
// '{e1}:{e2}' ranges, ',' unions, and '&' intersections.

// Compile a PCRE2 pattern. Returns nullptr on error (message to stderr).
static pcre2_code* compile_pattern(const std::string& pat) {
    int errnum = 0;
    PCRE2_SIZE erroff = 0;
    pcre2_code* re =
        pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pat.c_str()), pat.size(), 0,
                      &errnum, &erroff, nullptr);
    if (!re) {
        PCRE2_UCHAR buf[256];
        pcre2_get_error_message(errnum, buf, sizeof(buf));
        std::cerr << "match_names: bad regex '" << pat << "' at offset "
                  << erroff << ": " << buf << "\n";
    }
    return re;
}

// Whole-string (anchored at both ends) match of `subject` against a compiled
// pattern. A null pattern means "match anything" (an omitted component).
static bool full_match(pcre2_code* re, const std::string& subject) {
    if (!re) return true;
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(re, nullptr);
    int rc = pcre2_match(re, reinterpret_cast<PCRE2_SPTR>(subject.c_str()),
                         subject.size(), 0,
                         PCRE2_ANCHORED | PCRE2_ENDANCHORED, md, nullptr);
    pcre2_match_data_free(md);
    return rc >= 0;
}

static std::string node_key_str(const ryml::Tree& t, size_t n) {
    if (n == ryml::NONE || !t.has_key(n)) return "";
    return std::string(t.key(n).str, t.key(n).len);
}

// A parsed name-matching string. Compiled patterns are null when the component
// is absent or its pattern was empty (meaning "match any"). The `*_present`
// flags record whether the qualifier appeared at all, which is what
// distinguishes a bare name (that also matches constants/variables) from a
// qualified one.
struct NameSelector {
    pcre2_code* lattice = nullptr;
    pcre2_code* branch = nullptr;
    pcre2_code* name = nullptr;
    std::string kind;
    bool lattice_present = false;
    bool branch_present = false;
    bool has_kind = false;
    bool has_param = false;
    std::vector<std::string> path;
    bool valid = true;
};

// Compile `pat` into `re`, unless it is empty (leave null = match any). On a
// bad pattern set `ok` false. Records presence of the (possibly empty) pattern.
static void take_pattern(const std::string& pat, pcre2_code*& re, bool& ok) {
    if (pat.empty()) return;
    re = compile_pattern(pat);
    if (!re) ok = false;
}

// Parse a query string into a NameSelector, compiling its patterns.
static NameSelector parse_selector(const std::string& spec) {
    NameSelector sel;
    std::string s = spec;

    size_t p3 = s.find(">>>");
    std::string lattice;
    if (p3 != std::string::npos) {
        lattice = s.substr(0, p3);
        s = s.substr(p3 + 3);
        sel.lattice_present = true;
    }
    size_t p2 = s.find(">>");
    std::string branch;
    if (p2 != std::string::npos) {
        branch = s.substr(0, p2);
        s = s.substr(p2 + 2);
        sel.branch_present = true;
    }
    size_t p1 = s.find('>');
    std::string elem_part;
    if (p1 != std::string::npos) {
        elem_part = s.substr(0, p1);
        sel.path = split_dots(s.substr(p1 + 1));
        sel.has_param = true;
    } else {
        elem_part = s;
    }
    std::string name = elem_part;
    size_t pc = elem_part.find("::");
    if (pc != std::string::npos) {
        sel.kind = elem_part.substr(0, pc);
        name = elem_part.substr(pc + 2);
        sel.has_kind = !sel.kind.empty();
    }

    bool ok = true;
    take_pattern(lattice, sel.lattice, ok);
    take_pattern(branch, sel.branch, ok);
    take_pattern(name, sel.name, ok);
    sel.valid = ok;
    return sel;
}

static void free_selector(NameSelector& sel) {
    if (sel.lattice) pcre2_code_free(sel.lattice);
    if (sel.branch) pcre2_code_free(sel.branch);
    if (sel.name) pcre2_code_free(sel.name);
}

// Heap-copy a std::string into a null-terminated C string the caller frees with
// yaml_free_string() (i.e. allocated with new[], matching that free).
static char* new_c_string(const std::string& s) {
    char* result = new char[s.size() + 1];
    std::memcpy(result, s.c_str(), s.size() + 1);
    return result;
}

// Turn a raw scalar value into a param_value, returning it as stored — the value
// is NOT evaluated. A plain numeric literal (the whole string, bar surrounding
// whitespace, is a finite number) becomes a number; anything else — an
// expression like "0.3 * 5", a species name like "#3He", or any other text — is
// returned verbatim as a string. Evaluation is the job of lattice expansion or
// evaluate_pals_expression, not of this accessor: on the `expanded` tree values
// are already numbers, while the raw views keep their expressions.
static struct param_value value_from_raw(const std::string& raw) {
    struct param_value v = {PARAM_VALUE_STRING, 0.0, nullptr};
    const char* s = raw.c_str();
    char* end = nullptr;
    double d = std::strtod(s, &end);  // skips leading whitespace itself
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (end != s && *end == '\0' && std::isfinite(d)) {
        v.kind = PARAM_VALUE_NUMBER;
        v.number = d;
    } else {
        v.string = new_c_string(raw);
    }
    return v;
}

// Resolve one element's parameter into a param_value. `ele` is the element
// definition map; `path` the dotted parameter path walked from it. A parameter
// that is not present yields the default (numeric 0 for now); a scalar value is
// returned as stored (see value_from_raw); a path that stops on a whole
// parameter group rather than a single value is not a parameter value and yields
// PARAM_VALUE_MISSING.
static struct param_value param_value_for_element(
    const ryml::Tree& t, size_t ele, const std::vector<std::string>& path) {
    size_t node = resolve_param_path(t, ele, path);
    if (node == ryml::NONE) {  // element found, parameter unset -> default
        struct param_value v = {PARAM_VALUE_NUMBER, 0.0, nullptr};
        return v;
    }
    if (!t.has_val(node)) {  // resolved to a group/sequence, not a value
        struct param_value v = {PARAM_VALUE_MISSING, 0.0, nullptr};
        return v;
    }
    return value_from_raw(std::string(t.val(node).str, t.val(node).len));
}

// The value of an already-matched node, for a bare-name lookup that resolves to
// a constant or variable. A compact-form entry (`name: value`) is itself the
// scalar; a full-form entry (a map with `kind: constant|variable`) carries its
// value under a `value:` child. Any other node — an element definition or a
// parameter group — has no single scalar value and yields PARAM_VALUE_MISSING.
static struct param_value value_of_const_var(const ryml::Tree& t, size_t node) {
    struct param_value v = {PARAM_VALUE_MISSING, 0.0, nullptr};
    if (node == ryml::NONE) return v;
    if (t.has_val(node))  // compact form: the matched node is the value scalar
        return value_from_raw(std::string(t.val(node).str, t.val(node).len));
    if (t.is_map(node)) {
        std::string kind = child_val_str(t, node, "kind");
        if (kind == "constant" || kind == "variable") {
            size_t val = t.find_child(node, ryml::to_csubstr("value"));
            if (val != ryml::NONE && t.has_val(val))
                return value_from_raw(std::string(t.val(val).str, t.val(val).len));
        }
    }
    return v;
}

// Whether two param_values carry the same value. Used to collapse several
// matches into one result: identical values agree, conflicting values leave the
// parameter unidentified.
static bool param_value_eq(const struct param_value& a,
                           const struct param_value& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == PARAM_VALUE_NUMBER) return a.number == b.number;
    if (a.kind == PARAM_VALUE_STRING) return std::strcmp(a.string, b.string) == 0;
    return true;  // both PARAM_VALUE_MISSING
}

// Reduce a list of per-match param_values to a single result: the shared value
// when they all agree (the same element reused, or several that all default),
// or PARAM_VALUE_MISSING when any two conflict. Consumes `vs`, freeing every
// owned string except the one handed back.
static struct param_value reduce_param_values(std::vector<struct param_value>& vs) {
    struct param_value missing = {PARAM_VALUE_MISSING, 0.0, nullptr};
    if (vs.empty()) return missing;
    bool conflict = false;
    for (size_t i = 1; i < vs.size(); ++i)
        if (!param_value_eq(vs[0], vs[i])) { conflict = true; break; }
    if (conflict) {
        for (struct param_value& v : vs)
            if (v.string) yaml_free_string(v.string);
        return missing;
    }
    for (size_t i = 1; i < vs.size(); ++i)  // keep vs[0]'s string, free the rest
        if (vs[i].string) yaml_free_string(vs[i].string);
    return vs[0];
}

// True if `re` (null = match any) matches any of `names`.
static bool any_full_match(pcre2_code* re, const std::vector<std::string>& names) {
    if (!re) return true;
    for (const std::string& n : names)
        if (full_match(re, n)) return true;
    return false;
}

// Recursively match elements (and their parameter groups / parameters). A
// "beamline" is any map that owns a `line` sequence; its name is the map's key.
// `lattice` is the name of the enclosing Lattice, `beamlines` the names of all
// enclosing beamlines (so a branch qualifier can match through sub-lines).
//
// `resolve_params` chooses what a match yields when the selector names a
// parameter: the parameter node itself (what match_names reports, so an element
// lacking it simply does not match), or the element definition (what a caller
// that means to create the parameter needs).
static void match_elements(const ryml::Tree& t, size_t node,
                           const std::string& lattice,
                           const std::vector<std::string>& beamlines,
                           const NameSelector& sel, bool resolve_params,
                           std::vector<size_t>& out, std::set<size_t>& seen) {
    if (node == ryml::NONE) return;

    std::string cur_lattice = lattice;
    if (t.is_map(node) && child_val_str(t, node, "kind") == "Lattice")
        cur_lattice = node_key_str(t, node);

    std::vector<std::string> cur_beamlines = beamlines;
    size_t line = t.is_map(node) ? t.find_child(node, ryml::to_csubstr("line"))
                                 : ryml::NONE;
    if (line != ryml::NONE && t.is_seq(line)) {
        cur_beamlines.push_back(node_key_str(t, node));
        // Element-level filters that do not depend on the element itself.
        bool lattice_ok = !sel.lattice || full_match(sel.lattice, cur_lattice);
        bool branch_ok = any_full_match(sel.branch, cur_beamlines);
        if (lattice_ok && branch_ok) {
            for (size_t entry = t.first_child(line); entry != ryml::NONE;
                 entry = t.next_sibling(entry)) {
                // A named element is a map whose first child is keyed with the
                // element name; bare scalar entries are unresolved references
                // with no parameters, so they are skipped.
                if (!t.is_map(entry)) continue;
                size_t def = t.first_child(entry);
                if (def == ryml::NONE || !t.has_key(def)) continue;
                if (!full_match(sel.name, node_key_str(t, def))) continue;
                if (sel.has_kind && child_val_str(t, def, "kind") != sel.kind)
                    continue;
                size_t hit = (sel.has_param && resolve_params)
                                 ? resolve_param_path(t, def, sel.path)
                                 : def;
                if (hit != ryml::NONE && seen.insert(hit).second)
                    out.push_back(hit);
            }
        }
    }

    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        match_elements(t, c, cur_lattice, cur_beamlines, sel, resolve_params,
                       out, seen);
}

// Classify a single keyed node as a constant/variable and, if its name matches,
// record it. Handles both the full form (a map with kind: constant|variable)
// and the compact form (a `constants:`/`variables:` sequence of name: value).
static void collect_const_var(const ryml::Tree& t, size_t named,
                              pcre2_code* name_re, std::vector<size_t>& out,
                              std::set<size_t>& seen) {
    if (named == ryml::NONE || !t.has_key(named)) return;
    std::string key = node_key_str(t, named);
    if (key == "constants" || key == "variables") {
        // Compact form. The list of `name: value` pairs may be written either as
        // a YAML sequence (each entry a single-key map) or as a plain map.
        for (size_t e = t.first_child(named); e != ryml::NONE;
             e = t.next_sibling(e)) {
            size_t item = t.has_key(e) ? e : t.first_child(e);
            if (item != ryml::NONE && t.has_key(item) &&
                full_match(name_re, node_key_str(t, item)) &&
                seen.insert(item).second)
                out.push_back(item);
        }
        return;
    }
    if (t.is_map(named)) {
        std::string kind = child_val_str(t, named, "kind");
        if ((kind == "constant" || kind == "variable") &&
            full_match(name_re, key) && seen.insert(named).second)
            out.push_back(named);
    }
}

// Match a constant/variable name pattern against every constant and variable
// defined directly under a single PALS node (or its facility sub-node).
static void match_const_var_in_pals(const ryml::Tree& t, size_t pals,
                                    pcre2_code* name_re,
                                    std::vector<size_t>& out,
                                    std::set<size_t>& seen) {
    if (pals == ryml::NONE) return;
    for (size_t c = t.first_child(pals); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (node_key_str(t, c) == "facility") {
            // facility is a sequence of single-key wrapper maps.
            for (size_t w = t.first_child(c); w != ryml::NONE;
                 w = t.next_sibling(w))
                collect_const_var(t, t.is_map(w) ? t.first_child(w) : ryml::NONE,
                                  name_re, out, seen);
        } else {
            collect_const_var(t, c, name_re, out, seen);
        }
    }
}

// Match constant/variable names across the tree. The PALS node sits at the root
// in the combined, leftover and expanded views; in the `original` view it is
// nested one level down under a per-file wrapper keyed by filename, and there
// may be several (one per included file). Look in both places so a constant is
// found in whichever tree actually holds it. The `seen` set keeps the root-level
// PALS node from being scanned twice.
static void match_const_var(const ryml::Tree& t, pcre2_code* name_re,
                            std::vector<size_t>& out, std::set<size_t>& seen) {
    size_t root = t.root_id();
    if (root == ryml::NONE || !t.is_map(root)) return;

    match_const_var_in_pals(t, t.find_child(root, ryml::to_csubstr("PALS")),
                            name_re, out, seen);
    for (size_t c = t.first_child(root); c != ryml::NONE; c = t.next_sibling(c))
        if (t.is_map(c))
            match_const_var_in_pals(
                t, t.find_child(c, ryml::to_csubstr("PALS")), name_re, out, seen);
}

ElementMatches match_element_parameters(const ryml::Tree& t, size_t root,
                                        const std::string& spec) {
    ElementMatches out;
    NameSelector sel = parse_selector(spec);
    out.valid = sel.valid;
    out.has_param = sel.has_param;
    out.path = sel.path;
    if (sel.valid) {
        std::set<size_t> seen;
        match_elements(t, root == ryml::NONE ? t.root_id() : root, "", {}, sel,
                       false, out.elements, seen);
    }
    free_selector(sel);
    return out;
}

ElementMatches match_definition_parameters(const ryml::Tree& t,
                                           const std::vector<size_t>& entries,
                                           const std::string& spec) {
    ElementMatches out;
    NameSelector sel = parse_selector(spec);
    out.valid = sel.valid && !sel.lattice_present && !sel.branch_present;
    out.has_param = sel.has_param;
    out.path = sel.path;
    if (out.valid) {
        std::set<size_t> seen;
        for (size_t entry : entries) {
            // A facility entry is an anonymous map wrapping one keyed
            // definition; anything else (a bare `use` scalar, say) names no
            // element.
            if (entry == ryml::NONE || !t.is_map(entry)) continue;
            size_t def = t.first_child(entry);
            if (def == ryml::NONE || !t.has_key(def) || !t.is_map(def)) continue;
            if (full_match(sel.name, node_key_str(t, def)) &&
                !(sel.has_kind && child_val_str(t, def, "kind") != sel.kind) &&
                seen.insert(def).second)
                out.elements.push_back(def);
            // An element may equally be defined inline inside a beamline of
            // this entry rather than as an entry of its own, and it is just as
            // much "defined by this point" in the list.
            match_elements(t, entry, "", {}, sel, false, out.elements, seen);
        }
    }
    free_selector(sel);
    return out;
}

extern "C" {

YAML_API struct name_matches match_names(YAMLTreeHandle tree,
                                          const char* match_string) {
    struct name_matches out = {nullptr, 0};
    if (!tree || !match_string) return out;
    ryml::Tree& t = GET_TREE(tree);

    NameSelector sel = parse_selector(std::string(match_string));
    std::vector<size_t> hits;
    std::set<size_t> seen;

    if (sel.valid) {
        match_elements(t, t.root_id(), "", {}, sel, true, hits, seen);

        // A bare name (no lattice/branch/kind qualifier and no parameter path)
        // also matches constants and variables directly by name.
        bool bare = !sel.lattice_present && !sel.branch_present &&
                    !sel.has_kind && !sel.has_param;
        if (bare) match_const_var(t, sel.name, hits, seen);
    }

    free_selector(sel);

    out.count = hits.size();
    if (out.count > 0) {
        out.nodes = new YAMLNodeId[out.count];
        std::copy(hits.begin(), hits.end(), out.nodes);
    }
    return out;
}

YAML_API void free_name_matches(struct name_matches matches) {
    delete[] matches.nodes;
}

YAML_API struct param_value get_parameter_value(YAMLTreeHandle tree,
                                                const char* match_string) {
    struct param_value out = {PARAM_VALUE_MISSING, 0.0, nullptr};
    if (!tree || !match_string) return out;
    ryml::Tree& t = GET_TREE(tree);

    NameSelector sel = parse_selector(std::string(match_string));
    if (!sel.valid) {  // a bad pattern identifies nothing
        free_selector(sel);
        return out;
    }

    std::vector<struct param_value> values;

    if (sel.has_param) {
        // An element parameter. Find the matching element(s) ignoring the
        // parameter path — with has_param cleared, match_elements yields the
        // element definition maps themselves — then walk the path down from
        // each, so an element that exists but does not set the parameter still
        // yields its default.
        std::vector<std::string> path = sel.path;
        sel.has_param = false;
        std::vector<size_t> eles;
        std::set<size_t> seen;
        match_elements(t, t.root_id(), "", {}, sel, true, eles, seen);
        free_selector(sel);
        if (eles.empty()) return out;  // no such element -> missing
        for (size_t ele : eles)
            values.push_back(param_value_for_element(t, ele, path));
    } else {
        // No parameter path. A bare name — no lattice/branch/kind qualifier —
        // resolves to a constant or variable, mirroring match_names. A
        // qualified name with no path identifies an element, which has no
        // scalar value, so it stays missing.
        bool bare = !sel.lattice_present && !sel.branch_present && !sel.has_kind;
        std::vector<size_t> nodes;
        std::set<size_t> seen;
        if (bare) match_const_var(t, sel.name, nodes, seen);
        free_selector(sel);
        if (nodes.empty()) return out;  // no such constant/variable -> missing
        for (size_t node : nodes)
            values.push_back(value_of_const_var(t, node));
    }

    return reduce_param_values(values);
}

YAML_API struct param_value get_lattice_parameter_value(
    YAMLTreeHandle full_expanded, YAMLTreeHandle leftover,
    const char* match_string) {
    // Element parameters live in the expanded lattice; look there first.
    struct param_value v = get_parameter_value(full_expanded, match_string);
    if (v.kind != PARAM_VALUE_MISSING) return v;
    // Constants, variables and unused definitions live in the facility
    // scaffolding kept by the leftover tree.
    return get_parameter_value(leftover, match_string);
}

}  // extern "C"
