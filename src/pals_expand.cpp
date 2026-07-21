// PALS lattice expansion pipeline: builds the four-tree representation
// (original / combined / expanded / leftover) from a PALS YAML file. Splices
// includes, expands the selected lattice (repeats, inherits, forks), and
// evaluates expression/controller values into the expanded tree. Name matching
// and parameter lookup live in pals_match.cpp; the generic YAML tree wrapper in
// yaml_c_wrapper.cpp.

#include "yaml_c_wrapper.h"
#include "yaml_tree.h"
#include "pals_util.h"
#include "pals_floor.h"

#include "apc/apc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "pals_expression.h"

// ============================================================
// PROVENANCE-TRACKED COPY HELPERS
// ============================================================
//
// These mirror the plain copy/duplicate routines but record, for every node
// they create, which source node it came from. The recorded map (`prov`) is
// what lets build_correspondence_map() link a node across the four trees.

// Cross-tree deep copy (like deep_copy_recursive) that records
// prov[dst_node] = src_node for every copied node. The destination root keeps
// no key even if src_node has one (a tree root cannot be keyed). `skip` names a
// source node to leave out along with its descendants (ryml::NONE copies
// everything); it is how the leftover tree is built as the document minus the
// lattice that went into the expanded tree.
static void deep_copy_tracked_except(ryml::Tree& dst_t, size_t dst_node,
                                     const ryml::Tree& src_t, size_t src_node,
                                     size_t skip,
                                     std::map<size_t, size_t>& prov) {
    prov[dst_node] = src_node;

    // Copy type flags (MAP, SEQ, VAL, KEY). A tree root cannot carry a key, so
    // drop the KEY flag when the destination is the root.
    auto src_type = src_t.type(src_node);
    dst_t.ref(dst_node) |= (src_type & (ryml::MAP | ryml::SEQ | ryml::VAL));
    if (!dst_t.is_root(dst_node))
        dst_t.ref(dst_node) |= (src_type & ryml::KEY);

    if (src_t.has_key(src_node) && !dst_t.is_root(dst_node))
        dst_t.set_key(dst_node, dst_t.to_arena(src_t.key(src_node)));
    if (src_t.has_val(src_node))
        dst_t.set_val(dst_node, dst_t.to_arena(src_t.val(src_node)));

    std::vector<size_t> src_children;
    for (size_t c = src_t.first_child(src_node); c != ryml::NONE;
         c = src_t.next_sibling(c))
        if (c != skip) src_children.push_back(c);

    std::vector<size_t> dst_children;
    for (size_t i = 0; i < src_children.size(); i++) {
        ensure_capacity(dst_t);
        dst_children.push_back(dst_t.append_child(dst_node));
    }
    for (size_t i = 0; i < src_children.size(); i++)
        deep_copy_tracked_except(dst_t, dst_children[i], src_t, src_children[i],
                                 skip, prov);
}

static void deep_copy_tracked(ryml::Tree& dst_t, size_t dst_node,
                              const ryml::Tree& src_t, size_t src_node,
                              std::map<size_t, size_t>& prov) {
    deep_copy_tracked_except(dst_t, dst_node, src_t, src_node, ryml::NONE, prov);
}

// Rewrite `prov` (ids -> ids in some intermediate tree) so that it points at
// whatever that intermediate tree was itself derived from, using its own
// provenance. This is what lets `expanded` and `leftover` be cut out of the
// temporary work tree and still record provenance straight back to `combined`:
// the work tree is discarded, so links through it would dangle. Nodes with no
// entry in `via` (created during expansion, e.g. `fork_pointer`) have no source
// and drop out.
static void chain_prov(std::map<size_t, size_t>& prov,
                       const std::map<size_t, size_t>& via) {
    for (auto it = prov.begin(); it != prov.end();) {
        auto up = via.find(it->second);
        if (up == via.end()) {
            it = prov.erase(it);
        } else {
            it->second = up->second;
            ++it;
        }
    }
}

// Remove every entry for `node` and its descendants from `prov`. Call this
// before ryml removes a subtree, so freed ids that get reused later cannot
// carry a stale provenance link.
static void erase_prov_subtree(const ryml::Tree& t, size_t node,
                               std::map<size_t, size_t>& prov) {
    if (node == ryml::NONE) return;
    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        erase_prov_subtree(t, c, prov);
    prov.erase(node);
}

// After a within-tree duplicate produced `new_node` from `src_node` (an
// isomorphic subtree), walk both in parallel and give each new node the same
// provenance as its template — i.e. new copies point at the same source node
// their template did. Nodes whose template had no provenance stay unmapped.
static void assign_prov_parallel(const ryml::Tree& t, size_t new_node,
                                 size_t src_node,
                                 std::map<size_t, size_t>& prov) {
    auto it = prov.find(src_node);
    if (it != prov.end()) prov[new_node] = it->second;
    size_t nc = t.first_child(new_node);
    size_t sc = t.first_child(src_node);
    while (nc != ryml::NONE && sc != ryml::NONE) {
        assign_prov_parallel(t, nc, sc, prov);
        nc = t.next_sibling(nc);
        sc = t.next_sibling(sc);
    }
}

// Within-tree duplicate of `src` under `parent` (after `after`), recording
// provenance for the whole duplicated subtree.
static size_t duplicate_tracked(ryml::Tree& t, size_t src, size_t parent,
                                size_t after, std::map<size_t, size_t>& prov) {
    size_t nw = t.duplicate(src, parent, after);
    assign_prov_parallel(t, nw, src, prov);
    return nw;
}

// Provenance-tracked version of ryml's duplicate_children_no_rep: copies the
// children of `src` into `dst` (appending after `after`) but skips any child
// whose key already exists in `dst`. Used for `inherit`.
static void duplicate_children_no_rep_tracked(ryml::Tree& t, size_t src,
                                              size_t dst, size_t after,
                                              std::map<size_t, size_t>& prov) {
    for (size_t c = t.first_child(src); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (t.has_key(c) && t.find_child(dst, t.key(c)) != ryml::NONE)
            continue;  // key already present — do not overwrite
        after = duplicate_tracked(t, c, dst, after, prov);
    }
}

// Build a name->node map for all elements that have a "kind" key.
static void make_ele_map(std::map<std::string, size_t>& emap,
                         const ryml::Tree& t, size_t node) {
    // do nothing if node is a scalar
    if (node == ryml::NONE || t.is_val(node)) return;

    // if node is a map from a name to it's properties and if the properties
    // contain "kind", add the node to the element map
    if (t.is_map(node)) {
        size_t first = t.first_child(node);
        if (first != ryml::NONE && t.has_key(first) && t.is_map(first) &&
            t.has_child(first, ryml::to_csubstr("kind"))) {
            emap.emplace(std::string(t.key(first).str, t.key(first).len),
                         first);
        }
    }

    // if node is a map or sequence, recurse into children
    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        make_ele_map(emap, t, c);
}

// A list of human-readable problems found while building the expanded tree
// (undefined lattice, dangling references, undefined inherit/repeat/fork
// targets, un-evaluable expressions, ...). Returned to the caller so it can
// decide whether to print, save, or ignore them.
using ProblemList = std::vector<std::string>;

// Append a problem, skipping exact duplicates. Expansion copies a definition
// into every use, so the same underlying issue can be reached many times; the
// shallow locations used below keep those copies collapsing to one message.
static void add_problem(ProblemList& problems, const std::string& msg) {
    for (const std::string& p : problems)
        if (p == msg) return;
    problems.push_back(msg);
}

// A short "group.param" (or just "param") location for a value node, for use in
// problem messages. Deliberately shallow so the identical parameter reached
// through several expansion copies yields one message.
static std::string short_location(const ryml::Tree& t, size_t node) {
    std::string key =
        t.has_key(node) ? std::string(t.key(node).str, t.key(node).len) : "";
    size_t p = t.parent(node);
    std::string pk = (p != ryml::NONE && t.has_key(p))
                         ? std::string(t.key(p).str, t.key(p).len)
                         : "";
    if (!pk.empty() && !key.empty()) return pk + "." + key;
    return key;
}

// Forward declaration — handle_fork calls expand, expand calls handle_fork
static void expand(ryml::Tree& t, size_t node,
                   std::map<std::string, size_t>& emap,
                   std::map<size_t, size_t>& prov, ProblemList& problems,
                   size_t branches, std::map<size_t, int>& mp_pass);

// Helper: find an element named 'name' within a line sequence. The element can
// be just the scalar name or defined as a map.
static size_t find_in_line(const ryml::Tree& t, size_t line,
                           const std::string& name) {
    for (size_t c = t.first_child(line); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (t.is_val(c) && std::string(t.val(c).str, t.val(c).len) == name)
            return c;
        if (t.is_map(c)) {
            size_t entry = t.first_child(c);
            if (entry != ryml::NONE && t.has_key(entry) &&
                std::string(t.key(entry).str, t.key(entry).len) == name)
                return entry;
        }
    }
    return ryml::NONE;
}

// Handle a Fork element:
//  1. Reads the ForkP
//  2. If the beamline ForkP["to_line"] doesn't exist in the lattice,
//     create the target branch in branches
//  3. Rename the forked beamline to ForkP["new_branch"]
//  4. Create a fork_pointer in the element with pointing to "destination_element" in the
//     new bracnh.
static void handle_fork(ryml::Tree& t, size_t fork_node, size_t branches,
                        std::map<std::string, size_t>& emap,
                        std::map<size_t, size_t>& prov, ProblemList& problems,
                        std::map<size_t, int>& mp_pass) {
    std::string fork_name = t.has_key(fork_node)
                                ? std::string(t.key(fork_node).str,
                                              t.key(fork_node).len)
                                : "<fork>";
    if (branches == ryml::NONE) {
        add_problem(problems, "Fork element '" + fork_name +
                                  "': no branches to fork into");
        return;
    }

    size_t forkp = t.find_child(fork_node, ryml::to_csubstr("ForkP"));
    if (forkp == ryml::NONE) {
        add_problem(problems,
                    "Fork element '" + fork_name + "': missing ForkP");
        return;
    }
    if (!t.is_map(forkp)) {
        add_problem(problems,
                    "Fork element '" + fork_name +
                        "': ForkP must be a map of parameters (e.g. "
                        "\"to_line: ...\"), not a sequence");
        return;
    }

    // Only `to_line` is required. `destination_element` defaults to the
    // destination branch's beginning (first) element, and `new_branch` defaults
    // to the `to_line` name. An explicit `null` is treated as unset.
    auto unset = [](const std::string& v) { return v.empty() || v == "null"; };
    std::string to_line = child_val_str(t, forkp, "to_line");
    if (unset(to_line)) {
        add_problem(problems,
                    "Fork element '" + fork_name +
                        "': ForkP is missing the required field 'to_line'");
        return;
    }
    std::string to_element = child_val_str(t, forkp, "destination_element");
    if (unset(to_element)) to_element.clear();
    std::string branch_name = child_val_str(t, forkp, "new_branch");
    if (unset(branch_name)) branch_name = to_line;

    // Check whether to_line already exists as a branch (by its original element
    // name)
    size_t existing_branch = ryml::NONE;
    for (size_t c = t.first_child(branches); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (!t.is_map(c)) continue;
        size_t entry = t.first_child(c);
        if (entry != ryml::NONE && t.has_key(entry) &&
            std::string(t.key(entry).str, t.key(entry).len) == to_line) {
            existing_branch = entry;
            break;
        }
    }

    size_t branch_node = existing_branch;
    if (existing_branch == ryml::NONE && emap.count(to_line)) {
        size_t def = emap[to_line];
        // Append a new wrapper map to branches, then duplicate the definition
        // into it
        ensure_capacity(t, t.num_children(def) + 2);
        size_t wrapper = t.append_child(branches);
        t.to_map(wrapper);
        branch_node = duplicate_tracked(t, def, wrapper, ryml::NONE, prov);
        // Rename from original element name to branch_name
        t.set_key(branch_node, t.to_arena(ryml::to_csubstr(branch_name)));
        // Expand the new branch so its scalars and inherits are resolved
        expand(t, branch_node, emap, prov, problems, branches, mp_pass);
    }

    if (branch_node == ryml::NONE) {
        add_problem(problems, "Fork element '" + fork_name + "': to_line '" +
                                  to_line + "' is not defined");
        return;
    }

    // Find to_element within the new branch's line
    size_t line = t.find_child(branch_node, ryml::to_csubstr("line"));
    if (line == ryml::NONE || !t.is_seq(line)) {
        add_problem(problems, "Fork element '" + fork_name + "': to_line '" +
                                  to_line + "' has no line to fork into");
        return;
    }
    size_t target;
    if (to_element.empty()) {
        // Default destination is the branch's beginning (first) element. A line
        // entry may be a bare scalar or a keyed map; mirror find_in_line and
        // resolve a map entry to its keyed element.
        target = t.first_child(line);
        if (target != ryml::NONE && t.is_map(target))
            target = t.first_child(target);
        if (target == ryml::NONE) {
            add_problem(problems, "Fork element '" + fork_name + "': to_line '" +
                                      to_line +
                                      "' has no beginning element to fork into");
            return;
        }
    } else {
        target = find_in_line(t, line, to_element);
        if (target == ryml::NONE) {
            add_problem(problems, "Fork element '" + fork_name +
                                      "': destination_element '" + to_element +
                                      "' not found in '" + to_line + "'");
            return;
        }
    }

    // Add fork_pointer: <node id of target as string>
    ensure_capacity(t);
    std::string id_str = std::to_string(target);
    size_t fp_child = t.append_child(fork_node);
    t.ref(fp_child) |= ryml::KEY | ryml::VAL;
    t.set_key(fp_child, t.to_arena(ryml::to_csubstr("fork_pointer")));
    t.set_val(fp_child, t.to_arena(ryml::to_csubstr(id_str)));
}

// True for a YAML scalar meaning boolean true (`multipass: true`, `True`, `1`).
static bool is_true_flag(const std::string& v) {
    return v == "true" || v == "True" || v == "TRUE" || v == "1";
}

// Stamp `multipass_index: idx` on a line entry (a seq wrapper holding one keyed
// element map), unless the element already carries one. An existing index means
// a nearer (more deeply nested) multipass line already claimed the element, and
// per the standard the *first* multipass line up the chain wins, so it is not
// overwritten.
static void set_multipass_index(ryml::Tree& t, size_t entry, int idx) {
    if (entry == ryml::NONE || !t.is_map(entry)) return;
    size_t ele = t.first_child(entry);  // the keyed element map
    if (ele == ryml::NONE || !t.is_map(ele)) return;
    if (t.find_child(ele, ryml::to_csubstr("multipass_index")) != ryml::NONE)
        return;
    ensure_capacity(t);
    size_t mi = t.append_child(ele);
    t.ref(mi) |= ryml::KEY | ryml::VAL;
    t.set_key(mi, t.to_arena(ryml::to_csubstr("multipass_index")));
    t.set_val(mi, t.to_arena(ryml::to_csubstr(std::to_string(idx))));
}

// Stamp the multipass index `pass` on every entry of one instance of a
// multipass line (the run [first, stop), ryml::NONE == to the end of the
// sequence). Entries a nearer multipass line already claimed keep their index.
//
// The multipass index is the *pass number*: the number of times a particle
// will have travelled through the physical element by that point, i.e. the
// ordinal of this instance among the traversals of its multipass line. Every
// element the instance contributes gets the same pass number. Two elements are
// the same physical element when they share a multipass line and sit at the
// same position within it; those matching positions across successive instances
// carry the increasing pass numbers 1, 2, ... .
static void stamp_multipass_pass(ryml::Tree& t, size_t first, size_t stop,
                                 int pass) {
    for (size_t e = first; e != ryml::NONE && e != stop; e = t.next_sibling(e))
        set_multipass_index(t, e, pass);
}

/**
 * Perform lattice expansion on the element `node`.
 * 1. Substitute scalar elements with their full definition taken from emap.
 * 2. Beamlines that contain "repeat: n" have their contents repeated n times.
 * 3. Elements that contain "inherit: ancestor" have the contents of ancestor
 * copied into element.
 * 4. A beamline referenced by bare name inside a `line:` is a sub-line: its
 *    `line:` contents are spliced directly into the enclosing line. When the
 *    sub-line is `multipass`, the spliced run is stamped with its pass number.
 *
 * `mp_pass` counts, per multipass line definition, how many instances of that
 * line have been spliced so far; expansion visits them in traversal order, so
 * the count is the pass number to stamp on the next instance.
 */
static void expand(ryml::Tree& t, size_t node,
                   std::map<std::string, size_t>& emap,
                   std::map<size_t, size_t>& prov, ProblemList& problems,
                   size_t branches, std::map<size_t, int>& mp_pass) {
    if (node == ryml::NONE) return;

    // Sequence — handle 'repeat'
    if (t.is_seq(node)) {
        size_t child = t.first_child(node);
        // loop over all children
        while (child != ryml::NONE) {
            size_t next = t.next_sibling(child);
            if (t.is_map(child) && t.has_children(child)) {
                // first_child because elements are defined as maps with only
                // one key
                size_t entry = t.first_child(child);
                if (t.has_key(entry) && t.is_map(entry)) {
                    size_t repeat_id =
                        t.find_child(entry, ryml::to_csubstr("repeat"));
                    if (repeat_id != ryml::NONE && t.has_val(repeat_id)) {
                        std::string cnt_txt(t.val(repeat_id).str,
                                            t.val(repeat_id).len);
                        std::string target(t.key(entry).str, t.key(entry).len);

                        // stoi stops at the first character it cannot use and
                        // reports how far it got, so require the whole value to
                        // be consumed: "3x" is a typo, not a count of 3. A
                        // negative count is meaningless too. Both land on
                        // count < 0 and are reported the same way.
                        int count = -1;
                        try {
                            size_t used = 0;
                            count = std::stoi(cnt_txt, &used);
                            if (used != cnt_txt.size()) count = -1;
                        } catch (...) {
                            count = -1;
                        }

                        if (count < 0) {
                            add_problem(problems, "repeat: invalid count '" +
                                                      cnt_txt + "' for '" +
                                                      target + "'");
                        } else if (!emap.count(target)) {
                            // check if the beamline to be repeated has been
                            // defined in the file
                            add_problem(problems, "repeat: beamline '" + target +
                                                      "' is not defined");
                        } else {
                            size_t def = emap[target];
                            size_t line_id =
                                t.find_child(def, ryml::to_csubstr("line"));
                            size_t after = t.prev_sibling(child);
                            // if beamline to be repeated has a line, duplicate
                            // it `count` times, else just duplicate the name of
                            // the beamline `count` times
                            if (line_id != ryml::NONE && t.is_seq(line_id)) {
                                for (int r = 0; r < count; r++)
                                    for (size_t c2 = t.first_child(line_id);
                                         c2 != ryml::NONE;
                                         c2 = t.next_sibling(c2)) {
                                        ensure_capacity(t, 2);
                                        after = duplicate_tracked(t, c2, node,
                                                                  after, prov);
                                    }
                            } else {
                                for (int r = 0; r < count; r++) {
                                    ensure_capacity(t, 2);
                                    size_t wrapper =
                                        t.insert_child(node, after);
                                    t.ref(wrapper) |= ryml::MAP;
                                    duplicate_tracked(t, def, wrapper,
                                                      ryml::NONE, prov);
                                    after = wrapper;
                                }
                            }
                            erase_prov_subtree(t, child, prov);
                            t.remove(child);
                            child = next;
                            continue;
                        }
                        // Either problem path falls through to leave the entry
                        // exactly as written. Only a repeat we fully understood
                        // removes it: dropping an entry on the strength of a
                        // count we could not read would delete part of the
                        // lattice, leaving a plausible-looking `line: []` that
                        // is only explained by a problem the caller may not
                        // check.
                    }
                }
            }
            expand(t, child, emap, prov, problems, branches, mp_pass);
            child = next;
        }
        return;
    }

    // Standalone scalar value
    if (t.is_val(node) && !t.has_key(node)) {
        std::string name(t.val(node).str, t.val(node).len);
        size_t p = t.parent(node);
        bool in_line = (p != ryml::NONE && t.has_key(p) &&
                        t.key(p) == ryml::to_csubstr("line"));
        auto it = emap.find(name);
        if (it != emap.end()) {
            size_t def = it->second;
            size_t def_line = t.find_child(def, ryml::to_csubstr("line"));
            bool is_beamline = child_val_str(t, def, "kind") == "BeamLine";

            // Sub-line: a beamline referenced by bare name inside a `line:`
            // contributes its `line:` contents directly to the enclosing line
            // rather than surviving as a nested BeamLine. (A bare name in a
            // `branches:` sequence is a branch, not a sub-line — it falls
            // through to plain substitution and is stripped of its kind later.)
            if (in_line && is_beamline && def_line != ryml::NONE &&
                t.is_seq(def_line)) {
                bool multipass =
                    is_true_flag(child_val_str(t, def, "multipass"));
                // Splice the sub-line's entries in front of this reference, then
                // drop the reference itself. `before`/`stop` bracket the spliced
                // run; both are outside it and stay put while it is expanded.
                size_t before = t.prev_sibling(node);
                size_t stop = t.next_sibling(node);
                size_t after = before;
                for (size_t c2 = t.first_child(def_line); c2 != ryml::NONE;
                     c2 = t.next_sibling(c2)) {
                    ensure_capacity(t, 2);
                    after = duplicate_tracked(t, c2, p, after, prov);
                }
                erase_prov_subtree(t, node, prov);
                t.remove(node);

                // Expand the spliced run in place: element references become
                // element maps, and any nested sub-lines flatten (assigning
                // their own, nearer, multipass indices) in turn.
                size_t first = (before == ryml::NONE) ? t.first_child(p)
                                                      : t.next_sibling(before);
                for (size_t cur = first; cur != ryml::NONE && cur != stop;) {
                    size_t nx = t.next_sibling(cur);
                    expand(t, cur, emap, prov, problems, branches, mp_pass);
                    cur = nx;
                }

                // The spliced run is one instance (one pass) of this multipass
                // sub-line: stamp every entry a nearer multipass sub-line did
                // not already claim with this instance's pass number. Instances
                // of a given line are visited in traversal order, so the running
                // per-definition count is that pass number.
                if (multipass)
                    stamp_multipass_pass(t, first, stop, ++mp_pass[def]);
                return;
            }

            // Element (or a beamline outside a `line:`): substitute in place.
            ensure_capacity(t, 2);
            t.change_type(node, ryml::MAP);
            duplicate_tracked(t, def, node, ryml::NONE, prov);
            expand(t, node, emap, prov, problems, branches, mp_pass);
        } else {
            // A bare entry in a `line:` or `branches:` sequence names an element
            // or beamline; if it is not defined, the reference is dangling.
            std::string pk = (p != ryml::NONE && t.has_key(p))
                                 ? std::string(t.key(p).str, t.key(p).len)
                                 : "";
            if (pk == "line" || pk == "branches")
                add_problem(problems, "reference to undefined element or line '" +
                                          name + "'");
        }
        return;
    }

    // Map — handle inherit, detect Lattice/Fork, recurse
    if (t.is_map(node)) {
        // Inherit: merge fields from named element (existing keys win)
        size_t inherit_id = t.find_child(node, ryml::to_csubstr("inherit"));
        if (inherit_id != ryml::NONE) {
            std::string name(t.val(inherit_id).str, t.val(inherit_id).len);
            if (emap.count(name)) {
                ensure_capacity(t, t.num_children(emap[name]) + 1);
                duplicate_children_no_rep_tracked(t, emap[name], node,
                                                  ryml::NONE, prov);
            } else {
                add_problem(problems,
                            "inherit: '" + name + "' is not defined");
            }
        }

        // Detect kind to set context or trigger fork handling
        std::string kind = child_val_str(t, node, "kind");
        size_t node_branches = branches;
        if (kind == "Lattice") {
            node_branches = t.find_child(node, ryml::to_csubstr("branches"));
        } else if (kind == "Fork") {
            handle_fork(t, node, branches, emap, prov, problems, mp_pass);
        }

        size_t original_size = t.num_children(node);
        size_t c = t.first_child(node);
        for (size_t i = 0; i < original_size && c != ryml::NONE;
             i++, c = t.next_sibling(c))
            expand(t, c, emap, prov, problems, node_branches, mp_pass);
    }
}

/**
 * Drop `kind: BeamLine` from every branch of the expanded lattice `lat_node`.
 *
 * A `branches:` entry is a branch, not a BeamLine. It is instantiated from a
 * root BeamLine, and a branch carries no `kind` of its own -- its only optional
 * components are `inherit` and `periodic`. Every route a branch reaches its
 * contents by copies that root BeamLine's definition in wholesale, and so drags
 * the definition's `kind: BeamLine` along: name substitution of a bare
 * `- this_line`, an explicit `inherit: that_ring`, and a Fork building a new
 * branch out of its `to_line`. Rather than special-case each route, strip the
 * key once here, after expansion has produced every branch.
 *
 * Only branches carry a `kind: BeamLine` to strip. Sub-lines -- beamlines
 * referenced inside a `line:` -- do not survive expansion: their contents are
 * spliced into the enclosing line, so no nested BeamLine remains to consider.
 */
static void strip_branch_kinds(ryml::Tree& t, size_t lat_node,
                               std::map<size_t, size_t>& prov) {
    size_t branches = t.find_child(lat_node, ryml::to_csubstr("branches"));
    if (branches == ryml::NONE || !t.is_seq(branches)) return;

    for (size_t entry = t.first_child(branches); entry != ryml::NONE;
         entry = t.next_sibling(entry)) {
        // Both the written form (`- name: {...}`) and a substituted bare name
        // land as a map wrapping the single keyed branch node.
        if (!t.is_map(entry)) continue;
        size_t branch = t.first_child(entry);
        if (branch == ryml::NONE || !t.is_map(branch)) continue;
        // Anything other than BeamLine is a branch built from a definition that
        // is not a beamline at all. That is a mistake in the file, and leaving
        // the kind in place keeps the evidence of it visible.
        if (child_val_str(t, branch, "kind") != "BeamLine") continue;
        size_t kind = t.find_child(branch, ryml::to_csubstr("kind"));
        erase_prov_subtree(t, kind, prov);
        t.remove(kind);
    }
}

/**
 * Number the elements of every branch whose root line is itself `multipass`.
 *
 * A branch's own line is the top of the sub-line chain, so a `multipass: true`
 * branch is the nearest multipass line for any of its elements not already
 * claimed by a nested multipass sub-line (those were numbered as they flattened,
 * and keep their nearer index). Runs after expansion, once every branch line
 * holds its full flat element sequence.
 *
 * Each branch instance is one traversal (one pass) of its root line, so all of
 * its still-unclaimed elements share a pass number. Physical element sets group
 * by the root line *definition*, which can span branches: two branches built
 * from the same root line are two passes through the same physical elements, so
 * their matching positions carry successive pass numbers. Provenance recovers
 * that shared definition — branches copied from one root map back to the same
 * combined node — so the per-definition count is the pass number to stamp.
 */
static void number_multipass_branches(ryml::Tree& t, size_t lat_node,
                                      std::map<size_t, size_t>& prov) {
    size_t branches = t.find_child(lat_node, ryml::to_csubstr("branches"));
    if (branches == ryml::NONE || !t.is_seq(branches)) return;

    std::map<size_t, int> mp_pass;  // root-line definition -> traversals so far
    for (size_t entry = t.first_child(branches); entry != ryml::NONE;
         entry = t.next_sibling(entry)) {
        if (!t.is_map(entry)) continue;
        size_t branch = t.first_child(entry);
        if (branch == ryml::NONE || !t.is_map(branch)) continue;
        if (!is_true_flag(child_val_str(t, branch, "multipass"))) continue;
        size_t line = t.find_child(branch, ryml::to_csubstr("line"));
        if (line == ryml::NONE || !t.is_seq(line)) continue;
        // Key on the branch's definition. A branch with no recorded provenance
        // (none should occur here) falls back to its own node id, which is
        // unique and so numbers it as a lone first pass.
        auto it = prov.find(branch);
        size_t def_key = (it != prov.end()) ? it->second : branch;
        stamp_multipass_pass(t, t.first_child(line), ryml::NONE,
                             ++mp_pass[def_key]);
    }
}

/**
 * Recursive helper for make_combined_from_original. Starting from `node` in the
 * combined tree `t`, replace every "include: filename" element with the
 * contents of that file, sourced from the already-parsed `original` tree so
 * provenance can be recorded. Also recurses into spliced content to handle
 * nested include statements.
 */
static void make_combined_splice(ryml::Tree& t, size_t node, ParsedData* orig,
                                 std::map<size_t, size_t>& prov) {
    if (node == ryml::NONE) return;

    if (t.is_seq(node)) {
        size_t child = t.first_child(node);
        while (child != ryml::NONE) {
            size_t next = t.next_sibling(child);

            // Seq elements are anonymous MAP wrappers, so - include: "file"
            // looks like:
            //   anon wrapper (MAP, no key) -> KEYVAL(key="include", val="file")
            std::string filename;
            bool is_include = false;
            if (!t.has_key(child) && t.is_map(child) &&
                t.num_children(child) == 1) {
                size_t inner = t.first_child(child);
                if (inner != ryml::NONE && t.has_key(inner) &&
                    t.key(inner) == ryml::to_csubstr("include") &&
                    t.has_val(inner)) {
                    is_include = true;
                    filename = std::string(t.val(inner).str, t.val(inner).len);
                }
            }

            if (is_include) {
                // Look up the included file's raw contents in `original`. It is
                // stored keyed by the exact filename string used in the include.
                ryml::Tree& ot = orig->tree;
                size_t inc_root =
                    ot.find_child(ot.root_id(), ryml::to_csubstr(filename));
                size_t after = child;
                std::vector<size_t> inserted;
                if (inc_root != ryml::NONE) {
                    // Splice each child of the included file's root into this
                    // sequence, recording provenance into `original`.
                    if (ot.is_seq(inc_root)) {
                        for (size_t c = ot.first_child(inc_root);
                             c != ryml::NONE; c = ot.next_sibling(c)) {
                            ensure_capacity(t);
                            size_t n = t.insert_child(node, after);
                            deep_copy_tracked(t, n, ot, c, prov);
                            inserted.push_back(n);
                            after = n;
                        }
                    } else {
                        // Included root is not a sequence — insert as a single
                        // element
                        ensure_capacity(t);
                        size_t n = t.insert_child(node, after);
                        deep_copy_tracked(t, n, ot, inc_root, prov);
                        inserted.push_back(n);
                    }
                }
                // Recurse into inserted nodes to handle nested includes
                for (size_t n : inserted) make_combined_splice(t, n, orig, prov);
                erase_prov_subtree(t, child, prov);
                t.remove(child);
                child = next;
                continue;
            }

            make_combined_splice(t, child, orig, prov);
            child = next;
        }
        return;
    }

    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        make_combined_splice(t, c, orig, prov);
}

/**
 * Makes the combined lattice tree by deep-copying the top-level file's contents
 * out of the `original` tree and then splicing in every include (including
 * nested ones) from `original`. Records provenance mapping each combined node
 * to the original node it was copied from.
 *
 * @param orig     The already-built `original` tree (see make_original).
 * @param filename The top-level filename, used to find its entry in `original`.
 */
static YAMLTreeHandle make_combined_from_original(ParsedData* orig,
                                                  const char* filename) {
    if (!orig) return nullptr;
    ParsedData* data = new ParsedData();
    ryml::Tree& t = data->tree;
    t.reserve(t.capacity() + 128);
    t.reserve_arena(t.arena_capacity() + 65536);

    ryml::Tree& ot = orig->tree;
    // The top-level file is stored in `original` keyed by its filename.
    size_t top = ot.find_child(ot.root_id(), ryml::to_csubstr(filename));
    if (top == ryml::NONE) top = ot.first_child(ot.root_id());
    if (top == ryml::NONE) return data;

    deep_copy_tracked(t, t.root_id(), ot, top, data->provenance);
    make_combined_splice(t, t.root_id(), orig, data->provenance);
    return data;
}

// helper function for find_lattice
static size_t find_lattice_recursive(const ryml::Tree& t, size_t node,
                                     const std::string& name,
                                     size_t& last_lattice,
                                     std::string& use_name) {
    if (!t.is_map(node) && !t.is_seq(node)) return ryml::NONE;

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c)) {
        // Track "use" statements — last one wins
        if (t.has_key(c) && t.key(c) == ryml::to_csubstr("use") &&
            t.has_val(c)) {
            use_name = std::string(t.val(c).str, t.val(c).len);
            continue;
        }

        // Check if this node is a lattice (map with kind: Lattice)
        bool is_lattice = false;
        if (t.is_map(c)) {
            for (size_t child = t.first_child(c); child != ryml::NONE;
                 child = t.next_sibling(child)) {
                if (t.has_key(child) &&
                    t.key(child) == ryml::to_csubstr("kind") &&
                    t.has_val(child) &&
                    t.val(child) == ryml::to_csubstr("Lattice")) {
                    is_lattice = true;
                    break;
                }
            }
        }

        if (is_lattice) {
            if (!name.empty()) {
                if (t.has_key(c) &&
                    t.key(c) == ryml::csubstr(name.data(), name.size()))
                    return c;
            } else {
                last_lattice = c;
            }
            continue;
        }

        size_t found =
            find_lattice_recursive(t, c, name, last_lattice, use_name);
        if (found != ryml::NONE) return found;
    }

    return ryml::NONE;
}

/**
 * Finds the lattice to be expanded as specified in make_expanded_from_combined.
 */
static size_t find_lattice(ryml::Tree& t, const std::string& name) {
    size_t last_lattice = ryml::NONE;
    std::string use_name;

    size_t found =
        find_lattice_recursive(t, t.root_id(), name, last_lattice, use_name);
    if (found != ryml::NONE) return found;
    if (!name.empty()) return ryml::NONE;

    if (!use_name.empty()) return find_lattice(t, use_name);

    return last_lattice;
}

/**
 * Create the expanded lattice. Starts with the combined lattice, and expands
 * the lattice with the following priorities:
 *  1. If `root_lattice` != null, then expand the lattice called `root_lattice`
 *  2. If `root_lattice` == null, expand the lattice specified in the last `use`
 * statement.
 *  3. If no use statement is present, expand the lattice that occurs last in
 * the file. Last expansion performs the following:
 * 1. Substitute scalar elements with their full definition, if defined in the
 * file outside the lattice.
 * 2. Beamlines that contain "repeat: n" have their contents repeated n times.
 * 3. Elements that contain "inherit: ancestor" have the contents of ancestor
 * copied into element.
 */
// ============================================================
// EXPRESSION EVALUATION
// ============================================================
//
// After the tree is fully expanded, every scalar value that is a PALS
// mathematical expression is replaced with its evaluated number (immediate and
// `expr(...)`-delayed alike, per the standard's evaluation model as applied to
// the expanded tree). Values that are not expressions -- element/line name
// references, `kind:` names, booleans, etc. -- fail to evaluate and are left
// untouched. Expressions containing random()/random_gauss() are deferred
// (kept as text) so the expanded tree stays reproducible.
//
// The math, functions, built-in constants, and particle-data functions
// (mass_of/charge_of/anomalous_moment_of, from AtomicAndPhysicalConstantsCLib)
// live in pals_expression.cpp; this layer supplies user constants/variables and
// walks the tree.

// Keys whose scalar values are names/flags, never expressions. Skipping them
// avoids a stray collision between such a name and a constant (e.g. an element
// literally named `pi`).
static const std::set<std::string>& non_expr_keys() {
    static const std::set<std::string> keys = {
        "kind",       "include",     "use",
        "inherit",    "zero_point",  "to_line",
        "destination_element", "new_branch", "multipass",
        "propagate_reference", "name", "multipass_index"};
    return keys;
}

// Shortest decimal string that round-trips back to `v` (so integers stay
// integer-looking and no precision is lost).
static std::string format_double(double v) {
    char buf[64];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) return std::string(buf);
    }
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return std::string(buf);
}

// True if `node` is a map defining a `kind: Controller` element.
static bool is_controller(const ryml::Tree& t, size_t node) {
    if (node == ryml::NONE || !t.is_map(node)) return false;
    size_t kind = t.find_child(node, ryml::to_csubstr("kind"));
    if (kind == ryml::NONE || !t.has_val(kind)) return false;
    return t.val(kind) == ryml::to_csubstr("Controller");
}

// Collects user constant/variable definitions (name -> defining expression),
// in both the full (`kind: constant`/`value:`) and compact
// (`constants:`/`variables:` lists) forms. First definition of a name wins;
// the standard requires duplicates to share the same value. Controller subtrees
// are skipped: their variables are controller-scoped, not global symbols.
static void collect_defs(const ryml::Tree& t, size_t node,
                         std::map<std::string, std::string>& defs) {
    if (node == ryml::NONE || is_controller(t, node)) return;

    // Full form: a named map with `kind: constant|variable` and a `value:`.
    if (t.is_map(node) && t.has_key(node)) {
        size_t kind = t.find_child(node, ryml::to_csubstr("kind"));
        if (kind != ryml::NONE && t.has_val(kind)) {
            std::string k(t.val(kind).str, t.val(kind).len);
            if (k == "constant" || k == "variable") {
                size_t val = t.find_child(node, ryml::to_csubstr("value"));
                if (val != ryml::NONE && t.has_val(val)) {
                    defs.emplace(std::string(t.key(node).str, t.key(node).len),
                                 std::string(t.val(val).str, t.val(val).len));
                }
            }
        }
    }

    // Compact form: a `constants:`/`variables:` block. The standard writes it
    // as a sequence of single-key maps (`- const_a: ...`), but the plain-map
    // form (`const_a: ...` directly under the key) is also accepted.
    if (t.has_key(node)) {
        std::string k(t.key(node).str, t.key(node).len);
        if (k == "constants" || k == "variables") {
            auto emit = [&](size_t kv) {
                if (t.has_key(kv) && t.has_val(kv))
                    defs.emplace(std::string(t.key(kv).str, t.key(kv).len),
                                 std::string(t.val(kv).str, t.val(kv).len));
            };
            if (t.is_map(node)) {
                for (size_t kv = t.first_child(node); kv != ryml::NONE;
                     kv = t.next_sibling(kv))
                    emit(kv);
            } else if (t.is_seq(node)) {
                for (size_t el = t.first_child(node); el != ryml::NONE;
                     el = t.next_sibling(el)) {
                    if (!t.is_map(el)) continue;
                    for (size_t kv = t.first_child(el); kv != ryml::NONE;
                         kv = t.next_sibling(kv))
                        emit(kv);
                }
            }
        }
    }

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c))
        collect_defs(t, c, defs);
}

// True if a scalar was *meant* to be an expression — it carries an arithmetic
// operator, grouping, an element-parameter reference (`>`), or an explicit
// `expr(...)` wrapper — as opposed to a plain name, label, or boolean. Used to
// decide whether a value that failed to evaluate is worth reporting: a bare
// word that does not resolve is almost always a name, not a broken expression.
// (`-` is excluded so a hyphenated string is not mistaken for a subtraction.)
static bool looks_like_expression(const std::string& body, bool was_expr) {
    if (was_expr) return true;
    for (char c : body)
        if (c == '+' || c == '*' || c == '/' || c == '^' || c == '(' ||
            c == ')' || c == '>')
            return true;
    return false;
}

// Replaces every evaluable scalar value in the subtree with its numeric value.
// Controller subtrees are skipped: their variables and control expressions use
// a controller-scoped symbol table and are handled by evaluate_controllers.
// A value that looks like an expression but cannot be evaluated is recorded in
// `problems`.
static void substitute_values(ryml::Tree& t, size_t node,
                              const pals::SymbolLookup& resolve,
                              const pals::SpeciesLookup& species,
                              ProblemList& problems) {
    if (node == ryml::NONE || is_controller(t, node)) return;

    if (t.has_val(node)) {
        bool skip = false;
        if (t.has_key(node)) {
            std::string k(t.key(node).str, t.key(node).len);
            skip = non_expr_keys().count(k) != 0;
        } else {
            // Bare sequence element: skip beamline `line:` name references.
            size_t p = t.parent(node);
            if (p != ryml::NONE && t.has_key(p) &&
                t.key(p) == ryml::to_csubstr("line"))
                skip = true;
        }
        if (!skip) {
            bool was_expr = false;
            std::string body = strip_expr_wrapper(
                std::string(t.val(node).str, t.val(node).len), was_expr);
            pals::EvalOutcome r = pals::eval_expression(body, resolve, species);
            std::string sp;
            if (r.ok) {
                t.set_val(node,
                          t.to_arena(ryml::to_csubstr(format_double(r.value))));
            } else if (!r.deferred && species && species(body, sp)) {
                // A bare identifier that names a species-valued constant or
                // variable (e.g. `species_ref: species` with `species: "#3He"`)
                // is replaced by its species-name string. Double-quoted so a
                // leading `#` survives YAML's comment rule on re-emit.
                t.set_val(node, t.to_arena(ryml::to_csubstr(sp)));
                t.set_val_style(node, ryml::VAL_DQUO);
            } else if (!r.deferred && looks_like_expression(body, was_expr)) {
                std::string loc = short_location(t, node);
                std::string msg = "could not evaluate expression";
                if (!loc.empty()) msg += " for " + loc;
                msg += ": " + body;
                add_problem(problems, msg);
            }
        }
    }

    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c))
        substitute_values(t, c, resolve, species, problems);
}

// Collects every `kind: Controller` node in the subtree.
static void collect_controllers(const ryml::Tree& t, size_t node,
                                std::vector<size_t>& out) {
    if (node == ryml::NONE) return;
    if (is_controller(t, node)) {
        out.push_back(node);
        return;  // controllers do not nest
    }
    for (size_t c = t.first_child(node); c != ryml::NONE;
         c = t.next_sibling(c))
        collect_controllers(t, c, out);
}

// A controller `variables` entry awaiting evaluation.
struct CtrlVar {
    size_t node;         // scalar value node to overwrite in place
    size_t ctrl;         // index into the controllers vector
    std::string qname;   // fully-qualified name, `controller>variable`
    std::string bare;    // unqualified name, as used within the controller
    std::string text;    // defining expression
    bool done = false;
};

// Iterates the `variables` of a controller, in both the documented map form
// (`cur1: 0.023`) and the compact seq-of-single-key-maps form, invoking `emit`
// with each (name, value-node) pair.
template <typename F>
static void for_each_ctrl_var(const ryml::Tree& t, size_t vars, F&& emit) {
    if (vars == ryml::NONE) return;
    if (t.is_map(vars)) {
        for (size_t kv = t.first_child(vars); kv != ryml::NONE;
             kv = t.next_sibling(kv))
            if (t.has_key(kv) && t.has_val(kv))
                emit(std::string(t.key(kv).str, t.key(kv).len), kv);
    } else if (t.is_seq(vars)) {
        for (size_t el = t.first_child(vars); el != ryml::NONE;
             el = t.next_sibling(el))
            for (size_t kv = t.first_child(el); kv != ryml::NONE;
                 kv = t.next_sibling(kv))
                if (t.has_key(kv) && t.has_val(kv))
                    emit(std::string(t.key(kv).str, t.key(kv).len), kv);
    }
}

// Evaluates controllers. Each controller's `variables` form a controller-scoped
// symbol table (variables may reference earlier variables of the same
// controller and, via the `controller>variable` syntax, variables of another
// controller). Once the tables are resolved, every control `expression` is
// computed with its controller's table and the numeric value is written into
// the control entry. Controller expressions are "delayed" in the standard, but
// per the PALS expansion model the expanded tree carries the computed value.
static void evaluate_controllers(ryml::Tree& t,
                                 const pals::SymbolLookup& global_resolve,
                                 const pals::SpeciesLookup& species,
                                 ProblemList& problems) {
    std::vector<size_t> controllers;
    collect_controllers(t, t.root_id(), controllers);
    if (controllers.empty()) return;

    std::vector<std::string> names(controllers.size());
    for (size_t i = 0; i < controllers.size(); ++i)
        if (t.has_key(controllers[i]))
            names[i].assign(t.key(controllers[i]).str,
                            t.key(controllers[i]).len);

    // Symbol tables, filled in as variables resolve.
    std::vector<std::map<std::string, double>> locals(controllers.size());
    std::map<std::string, double> qualified;  // `controller>variable` -> value

    // Resolver scoped to controller `ci`: unqualified names come from that
    // controller's table, `>`-qualified names from any controller, and anything
    // else falls through to the global resolver (built-in / user constants).
    auto make_resolver = [&locals, &qualified, &global_resolve](
                             size_t ci) -> pals::SymbolLookup {
        return [ci, &locals, &qualified, &global_resolve](
                   const std::string& name, double& out) -> bool {
            if (name.find('>') != std::string::npos) {
                auto it = qualified.find(name);
                if (it == qualified.end()) return false;
                out = it->second;
                return true;
            }
            auto& lv = locals[ci];
            auto it = lv.find(name);
            if (it != lv.end()) {
                out = it->second;
                return true;
            }
            return global_resolve ? global_resolve(name, out) : false;
        };
    };

    // Gather every variable definition across all controllers.
    std::vector<CtrlVar> vars;
    for (size_t ci = 0; ci < controllers.size(); ++ci) {
        size_t vnode = t.find_child(controllers[ci], ryml::to_csubstr("variables"));
        for_each_ctrl_var(t, vnode, [&](const std::string& vname, size_t vn) {
            CtrlVar v;
            v.node = vn;
            v.ctrl = ci;
            v.bare = vname;
            v.qname = names[ci] + ">" + vname;
            v.text = std::string(t.val(vn).str, t.val(vn).len);
            vars.push_back(std::move(v));
        });
    }

    // Fixed-point evaluation resolves acyclic dependencies regardless of the
    // order variables are written (within a controller or across controllers).
    for (size_t pass = 0, limit = vars.size() + 1; pass <= limit; ++pass) {
        bool changed = false;
        for (CtrlVar& v : vars) {
            if (v.done) continue;
            pals::SymbolLookup res = make_resolver(v.ctrl);
            bool was_expr = false;
            std::string body = strip_expr_wrapper(v.text, was_expr);
            pals::EvalOutcome r = pals::eval_expression(body, res, species);
            if (r.ok) {
                locals[v.ctrl][v.bare] = r.value;
                qualified[v.qname] = r.value;
                t.set_val(v.node,
                          t.to_arena(ryml::to_csubstr(format_double(r.value))));
                v.done = true;
                changed = true;
            } else if (r.deferred) {
                v.done = true;  // random(); leave the text untouched
            }
        }
        if (!changed) break;
    }

    // Any variable still unresolved is a genuine problem (unknown symbol or a
    // dependency cycle); random()-deferred ones were marked done above.
    for (const CtrlVar& v : vars)
        if (!v.done)
            add_problem(problems, "controller '" + names[v.ctrl] +
                                      "' variable '" + v.bare +
                                      "': could not evaluate '" + v.text + "'");

    // Compute each control `expression` with its controller's table and store
    // the value back in the control entry.
    for (size_t ci = 0; ci < controllers.size(); ++ci) {
        size_t controls =
            t.find_child(controllers[ci], ryml::to_csubstr("controls"));
        if (controls == ryml::NONE) continue;
        pals::SymbolLookup res = make_resolver(ci);
        for (size_t entry = t.first_child(controls); entry != ryml::NONE;
             entry = t.next_sibling(entry)) {
            if (!t.is_map(entry)) continue;
            size_t enode = t.find_child(entry, ryml::to_csubstr("expression"));
            if (enode == ryml::NONE || !t.has_val(enode)) continue;
            bool was_expr = false;
            std::string body = strip_expr_wrapper(
                std::string(t.val(enode).str, t.val(enode).len), was_expr);
            pals::EvalOutcome r = pals::eval_expression(body, res, species);
            if (r.ok)
                t.set_val(enode,
                          t.to_arena(ryml::to_csubstr(format_double(r.value))));
            else if (!r.deferred)
                add_problem(problems, "controller '" + names[ci] +
                                          "' control expression could not be "
                                          "evaluated: " + body);
        }
    }
}

// Resolves an element-parameter reference `element>group.sub. ... .param` to
// the scalar value node it names, or NONE. Only the exact single-element form
// is supported (a value expression references one specific parameter; pattern
// matching and branch/lattice qualifiers are not permitted here, per the
// standard). `emap` maps element names to their definition maps.
static size_t resolve_ele_param_ref(const ryml::Tree& t,
                                    const std::map<std::string, size_t>& emap,
                                    const std::string& ref) {
    size_t gt = ref.find('>');
    if (gt == std::string::npos) return ryml::NONE;
    std::string elem = ref.substr(0, gt);
    std::string rest = ref.substr(gt + 1);
    // Reject empty parts and any remaining '>' (e.g. `branch>>ele>...`).
    if (elem.empty() || rest.empty() || rest.find('>') != std::string::npos)
        return ryml::NONE;
    auto it = emap.find(elem);
    if (it == emap.end()) return ryml::NONE;
    size_t node = resolve_param_path(t, it->second, split_dots(rest));
    if (node == ryml::NONE || !t.has_val(node)) return ryml::NONE;
    return node;
}

// Evaluates all expressions in the (already expanded) tree in place. Records
// any that could not be evaluated in `problems`.
static void evaluate_expressions(ryml::Tree& t, ProblemList& problems) {
    std::map<std::string, std::string> defs;
    collect_defs(t, t.root_id(), defs);

    // Element name -> definition map, so expressions may reference another
    // element's parameter via `element>group. ... .param`.
    std::map<std::string, size_t> emap;
    make_ele_map(emap, t, t.root_id());

    // Resolves a symbol whose value is a species-name string (e.g.
    // `species: "#3He"`), so a particle-data function may take it by name:
    // `mass_of(species)`. The stored value is returned verbatim (trimmed, with
    // any surrounding quotes stripped); the expression evaluator validates it.
    pals::SpeciesLookup species = [&defs](const std::string& name,
                                          std::string& out) -> bool {
        auto di = defs.find(name);
        if (di == defs.end()) return false;
        std::string v = di->second;
        size_t a = v.find_first_not_of(" \t\r\n");
        size_t b = v.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return false;
        v = v.substr(a, b - a + 1);
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') &&
            v.back() == v.front())
            v = v.substr(1, v.size() - 2);
        out = v;
        return true;
    };

    // Lazily evaluate user symbols on demand, memoizing results and guarding
    // against reference cycles. A symbol whose defining expression is itself
    // unresolvable is reported as unknown.
    auto cache = std::make_shared<std::map<std::string, double>>();
    auto active = std::make_shared<std::set<std::string>>();
    std::shared_ptr<pals::SymbolLookup> resolve =
        std::make_shared<pals::SymbolLookup>();
    *resolve = [&defs, &t, &emap, cache, active, resolve, species](
                   const std::string& name, double& out) -> bool {
        auto ci = cache->find(name);
        if (ci != cache->end()) {
            out = ci->second;
            return true;
        }
        // An element-parameter reference (`element>group. ... .param`) resolves
        // to that parameter's value, evaluated as an expression in turn.
        std::string body;
        if (name.find('>') != std::string::npos) {
            size_t vn = resolve_ele_param_ref(t, emap, name);
            if (vn == ryml::NONE) return false;
            body = std::string(t.val(vn).str, t.val(vn).len);
        } else {
            auto di = defs.find(name);
            if (di == defs.end()) return false;
            body = di->second;
        }
        if (!active->insert(name).second) return false;  // cycle
        bool was_expr = false;
        body = strip_expr_wrapper(body, was_expr);
        pals::EvalOutcome r = pals::eval_expression(body, *resolve, species);
        active->erase(name);
        if (!r.ok) return false;
        (*cache)[name] = r.value;
        out = r.value;
        return true;
    };

    // Controllers first (controller-scoped symbol tables), then the generic
    // pass over the rest of the tree (which skips controller subtrees).
    evaluate_controllers(t, *resolve, species, problems);
    substitute_values(t, t.root_id(), *resolve, species, problems);
}

// ============================================================
// ELEMENT BOOKKEEPING
// ============================================================
//
// Once every expression has been reduced to a number, each branch is walked
// element-by-element from its BeginningEle to compute the output parameters that
// depend on where an element sits in the branch: the reference parameters
// (species / energy / momentum / time), the floor placement, the s-position,
// and the field-dependent parameters (normalized <-> unnormalized multipole and
// bend strengths). See the "Lattice Expansion" step in
// pals/source/lattice-construction.md; the floor math (Eqs. wws etc.) lives in
// pals_floor.cpp.
//
// All ReferenceP / FloorP values describe the *upstream* end of their element.
// The bookkeeper persists each element's results into the tree before moving on,
// so _element_bookkeeper reads the previous element's stored parameters and
// "propagates" them onto the current one, exactly as the standard describes.

// Parse the whole of `s` (bar surrounding whitespace) as a finite double. Values
// in the expanded tree are already evaluated to plain numbers by this point.
static bool parse_num(const std::string& s, double& out) {
    const char* p = s.c_str();
    char* end = nullptr;
    double d = std::strtod(p, &end);
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if (end != p && *end == '\0' && std::isfinite(d)) {
        out = d;
        return true;
    }
    return false;
}

// Numeric value of a keyed child, if present and numeric.
static bool get_num_child(const ryml::Tree& t, size_t parent, const char* key,
                          double& out) {
    if (parent == ryml::NONE) return false;
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c == ryml::NONE || !t.has_val(c)) return false;
    return parse_num(std::string(t.val(c).str, t.val(c).len), out);
}

// String value of a keyed child, trimmed and with any surrounding quotes
// stripped; false (and `out` untouched) if absent or empty.
static bool get_str_child(const ryml::Tree& t, size_t parent, const char* key,
                          std::string& out) {
    if (parent == ryml::NONE) return false;
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c == ryml::NONE || !t.has_val(c)) return false;
    std::string v(t.val(c).str, t.val(c).len);
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    v = v.substr(a, b - a + 1);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') &&
        v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    if (v.empty()) return false;
    out = v;
    return true;
}

// A keyed map child of `parent`, created (empty) if absent. An existing scalar
// placeholder of the same key is converted to a map in place.
static size_t find_or_add_map_child(ryml::Tree& t, size_t parent,
                                    const char* key) {
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c != ryml::NONE) {
        if (!t.is_map(c)) {
            t.change_type(c, ryml::KEYMAP);
            t.set_key(c, t.to_arena(ryml::to_csubstr(key)));
        }
        return c;
    }
    ensure_capacity(t, 2);
    c = t.append_child(parent);
    t.ref(c) |= ryml::KEY | ryml::MAP;
    t.set_key(c, t.to_arena(ryml::to_csubstr(key)));
    return c;
}

// Set (or create) a keyed scalar child to the shortest round-tripping decimal.
static void set_num_child(ryml::Tree& t, size_t parent, const char* key,
                          double v) {
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c == ryml::NONE) {
        ensure_capacity(t, 2);
        c = t.append_child(parent);
        t.ref(c) |= ryml::KEY | ryml::VAL;
        t.set_key(c, t.to_arena(ryml::to_csubstr(key)));
    }
    t.set_val(c, t.to_arena(ryml::to_csubstr(format_double(v))));
}

// Set (or create) a keyed scalar child to a string. Double-quoted so a leading
// '#' in a species name survives YAML's comment rule on re-emit.
static void set_str_child(ryml::Tree& t, size_t parent, const char* key,
                          const std::string& v) {
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c == ryml::NONE) {
        ensure_capacity(t, 2);
        c = t.append_child(parent);
        t.ref(c) |= ryml::KEY | ryml::VAL;
        t.set_key(c, t.to_arena(ryml::to_csubstr(key)));
    }
    t.set_val(c, t.to_arena(ryml::to_csubstr(v)));
    t.set_val_style(c, ryml::VAL_DQUO);
}

// Set (or create) a keyed scalar child to a bare (unquoted) token. Used for
// enum and boolean defaults (e.g. `cavity_type: STANDING_WAVE`, `direction:
// FORWARDS`, `aperture_active: true`), which are plain YAML scalars, not strings.
static void set_plain_child(ryml::Tree& t, size_t parent, const char* key,
                            const char* v) {
    size_t c = t.find_child(parent, ryml::to_csubstr(key));
    if (c == ryml::NONE) {
        ensure_capacity(t, 2);
        c = t.append_child(parent);
        t.ref(c) |= ryml::KEY | ryml::VAL;
        t.set_key(c, t.to_arena(ryml::to_csubstr(key)));
    }
    t.set_val(c, t.to_arena(ryml::to_csubstr(v)));
}

// Reference parameters at one end of an element (ReferenceP). Energy and
// momentum are related through the reference mass; a flag records which of them
// is known so complete_energy() can fill the other.
struct RefState {
    std::string species;
    double E_tot = 0.0;  // [eV] total energy
    double pc = 0.0;     // [eV] momentum * c
    double time = 0.0;   // [s]
    bool has_species = false;
    bool has_E = false;
    bool has_pc = false;
};

// Fill in whichever of E_tot / pc is missing from the other, using the reference
// species mass (E^2 = (pc)^2 + (m c^2)^2, all in eV). A no-op if the species is
// unknown, both are set, or neither is.
static void complete_energy(RefState& r) {
    if (!r.has_species) return;
    double m;
    try {
        m = apc::mass_of(r.species);
    } catch (...) {
        return;  // unknown species name; leave energy/momentum as-is
    }
    if (r.has_E && !r.has_pc) {
        double v = r.E_tot * r.E_tot - m * m;
        if (v >= 0.0) {
            r.pc = std::sqrt(v);
            r.has_pc = true;
        }
    } else if (r.has_pc && !r.has_E) {
        r.E_tot = std::sqrt(r.pc * r.pc + m * m);
        r.has_E = true;
    }
}

// Read the ReferenceP group of an element into a RefState (its upstream values).
static RefState read_ref(const ryml::Tree& t, size_t ele) {
    RefState r;
    size_t rp = t.find_child(ele, ryml::to_csubstr("ReferenceP"));
    if (rp == ryml::NONE) return r;
    r.has_species = get_str_child(t, rp, "species_ref", r.species);
    r.has_E = get_num_child(t, rp, "E_tot_ref", r.E_tot);
    r.has_pc = get_num_child(t, rp, "pc_ref", r.pc);
    get_num_child(t, rp, "time_ref", r.time);  // defaults to 0
    return r;
}

// Write a RefState back into an element's ReferenceP group (creating it).
static void write_ref(ryml::Tree& t, size_t ele, const RefState& r) {
    size_t rp = find_or_add_map_child(t, ele, "ReferenceP");
    if (r.has_species) set_str_child(t, rp, "species_ref", r.species);
    if (r.has_E) set_num_child(t, rp, "E_tot_ref", r.E_tot);
    if (r.has_pc) set_num_child(t, rp, "pc_ref", r.pc);
    set_num_child(t, rp, "time_ref", r.time);
}

// Apply a ReferenceChange element's ReferenceChangeP adjustments to a RefState
// (see referencechange.md): a species, an absolute or delta energy/momentum, and
// an absolute or delta reference time. Clearing has_E/has_pc lets the subsequent
// complete_energy() recompute the partner quantity with the new mass/energy.
static void apply_ref_change(const ryml::Tree& t, size_t ele, RefState& r) {
    size_t rc = t.find_child(ele, ryml::to_csubstr("ReferenceChangeP"));
    if (rc == ryml::NONE) return;

    std::string sp;
    if (get_str_child(t, rc, "species_ref", sp)) {
        r.species = sp;
        r.has_species = true;
        r.has_pc = false;  // recompute momentum for the new mass
    }
    double v;
    if (get_num_child(t, rc, "E_tot_ref", v)) {
        r.E_tot = v;
        r.has_E = true;
        r.has_pc = false;
    } else if (get_num_child(t, rc, "pc_ref", v)) {
        r.pc = v;
        r.has_pc = true;
        r.has_E = false;
    } else if (get_num_child(t, rc, "dE_ref", v)) {
        if (r.has_E) {
            r.E_tot += v;
            r.has_pc = false;
        }
    } else if (get_num_child(t, rc, "dpc_ref", v)) {
        if (r.has_pc) {
            r.pc += v;
            r.has_E = false;
        }
    }
    if (get_num_child(t, rc, "time_ref", v))
        r.time = v;
    else if (get_num_child(t, rc, "dtime_ref", v))
        r.time += v;
}

// Reference parameters at the downstream end of an element, given its completed
// upstream parameters. Species and energy carry through unchanged for most
// kinds; an RFCavity shifts the energy by RFP.dE_ref and a ReferenceChange
// applies its ReferenceChangeP. The reference time advances by the transit time
// (reference.md).
static RefState downstream_ref(const ryml::Tree& t, size_t ele,
                               const std::string& kind, const RefState& up,
                               double length, ProblemList& problems) {
    RefState down = up;

    if (kind == "RFCavity") {
        size_t rfp = t.find_child(ele, ryml::to_csubstr("RFP"));
        double dE;
        if (get_num_child(t, rfp, "dE_ref", dE) && down.has_E) {
            down.E_tot = up.E_tot + dE;
            down.has_pc = false;  // recompute momentum for the new energy
        }
    } else if (kind == "ReferenceChange") {
        apply_ref_change(t, ele, down);
    } else if (kind == "Foil") {
        // A Foil can strip electrons, changing the downstream species (and hence
        // energy). The stripping model is not defined by the standard, so the
        // downstream species is left equal to the upstream one and the omission
        // is flagged rather than guessed at.
        std::string ename = t.has_key(ele) ? std::string(t.key(ele).str,
                                                          t.key(ele).len)
                                           : "<foil>";
        add_problem(problems, "Foil element '" + ename +
                                  "': downstream species change is not computed");
    }

    complete_energy(down);  // refill whichever of E/pc a change above cleared

    // Reference transit time: length * (E_up + E_down) / (c * (pc_up + pc_down)).
    if (up.has_E && down.has_E && up.has_pc && down.has_pc) {
        double denom = apc::C_LIGHT * (up.pc + down.pc);
        if (denom != 0.0)
            down.time = up.time + length * (up.E_tot + down.E_tot) / denom;
    }
    return down;
}

// Read an element's FloorP group into a FloorState. Missing components default
// to zero (origin / identity orientation). Returns false if the group is absent.
static bool read_floor(const ryml::Tree& t, size_t ele, pals::FloorState& s) {
    size_t fp = t.find_child(ele, ryml::to_csubstr("FloorP"));
    if (fp == ryml::NONE) return false;
    double x = 0, y = 0, z = 0, th = 0, ph = 0, ps = 0;
    get_num_child(t, fp, "x", x);
    get_num_child(t, fp, "y", y);
    get_num_child(t, fp, "z", z);
    get_num_child(t, fp, "theta", th);
    get_num_child(t, fp, "phi", ph);
    get_num_child(t, fp, "psi", ps);
    s.r = pals::Vec3{x, y, z};
    s.q = pals::quat_from_floor_angles(pals::FloorAngles{th, ph, ps});
    return true;
}

// Write a FloorState back into an element's FloorP group (creating it),
// converting the orientation quaternion to (theta, phi, psi).
static void write_floor(ryml::Tree& t, size_t ele, const pals::FloorState& s) {
    size_t fp = find_or_add_map_child(t, ele, "FloorP");
    set_num_child(t, fp, "x", s.r.x);
    set_num_child(t, fp, "y", s.r.y);
    set_num_child(t, fp, "z", s.r.z);
    pals::FloorAngles a = pals::floor_angles_from_quat(s.q);
    set_num_child(t, fp, "theta", a.theta);
    set_num_child(t, fp, "phi", a.phi);
    set_num_child(t, fp, "psi", a.psi);
}

// Build the floor displacement L and coordinate rotation S for an element's
// reference curve, dispatched on kind: bends use their BendP angle/tilt, patches
// their PatchP offsets/rotations, everything else is a straight segment of the
// element length. (FloorShift, which also redirects the reference curve, is not
// yet handled and is treated as straight.)
static void element_LS(const ryml::Tree& t, size_t ele, const std::string& kind,
                       double length, pals::Vec3& L, pals::Quat& S) {
    if (kind == "Bend") {
        size_t bp = t.find_child(ele, ryml::to_csubstr("BendP"));
        double angle = 0.0;
        if (!get_num_child(t, bp, "angle_ref", angle)) {
            double g = 0.0;
            if (get_num_child(t, bp, "g_ref", g)) angle = g * length;
        }
        double tilt = 0.0;
        get_num_child(t, bp, "tilt_ref", tilt);
        pals::bend_LS(length, angle, tilt, L, S);
    } else if (kind == "Patch") {
        size_t pp = t.find_child(ele, ryml::to_csubstr("PatchP"));
        double xo = 0, yo = 0, zo = 0, xr = 0, yr = 0, zr = 0;
        get_num_child(t, pp, "x_offset", xo);
        get_num_child(t, pp, "y_offset", yo);
        get_num_child(t, pp, "z_offset", zo);
        get_num_child(t, pp, "x_rot", xr);
        get_num_child(t, pp, "y_rot", yr);
        get_num_child(t, pp, "z_rot", zr);
        pals::patch_LS(xo, yo, zo, xr, yr, zr, L, S);
    } else {
        pals::straight_LS(length, L, S);
    }
}

// The name of an element (its map key), or a placeholder for the unkeyed case.
static std::string ele_name(const ryml::Tree& t, size_t ele) {
    return t.has_key(ele) ? std::string(t.key(ele).str, t.key(ele).len)
                          : "<element>";
}

// Two numbers agree "to high accuracy" (rf.md): equal within a relative
// tolerance, with both-zero counted as equal.
static bool approx_eq(double a, double b) {
    double scale = std::max(std::fabs(a), std::fabs(b));
    return scale == 0.0 || std::fabs(a - b) <= 1e-9 * scale;
}

// ---------------------------------------------------------------------------
// DEPENDENT PARAMETERS
//
// Many parameters within a group are not independent: one is a fixed multiple
// (or the reciprocal) of another. The expanded lattice holds every non-zero
// parameter, so when the author sets one member of such a relation the parser
// derives the rest. When the author sets more than one, the parser instead
// verifies they agree to high accuracy and flags an inconsistency (rather than
// silently overwriting). link_pair / reciprocal_link are the two primitives;
// the per-group resolvers below wire them up per the parameter docs.
// ---------------------------------------------------------------------------

// Relate two proportional parameters of a group, `b_key = factor * a_key`.
//  - both present  -> verify agreement, flag an inconsistency if not.
//  - only `a`      -> derive b = factor*a (unless the result is zero: a zero
//                     parameter is not "held").
//  - only `b`      -> derive a = b/factor (needs an invertible, non-zero factor).
// Returns true if it wrote a value, so callers can iterate to a fixed point.
static bool link_pair(ryml::Tree& t, size_t group, const char* a_key,
                      const char* b_key, double factor, const std::string& ctx,
                      ProblemList& problems) {
    double a, b;
    bool has_a = get_num_child(t, group, a_key, a);
    bool has_b = get_num_child(t, group, b_key, b);
    if (has_a && has_b) {
        double expect = factor * a;
        if (!approx_eq(b, expect)) {
            std::ostringstream m;
            m << ctx << ": '" << b_key << "' and '" << a_key
              << "' are inconsistent (" << b_key << " = " << format_double(b)
              << ", but " << format_double(factor) << " * " << a_key << " = "
              << format_double(expect) << ").";
            add_problem(problems, m.str());
        }
        return false;
    }
    if (has_a && factor * a != 0.0) {
        set_num_child(t, group, b_key, factor * a);
        return true;
    }
    if (has_b && factor != 0.0 && b / factor != 0.0) {
        set_num_child(t, group, a_key, b / factor);
        return true;
    }
    return false;
}

// Relate two reciprocal parameters of a group, `b_key = 1 / a_key` (e.g.
// `rho_ref = 1 / g_ref`). A zero value has no finite reciprocal, so it is
// neither a source nor a target. Consistency (a*b == 1) is only meaningful when
// both are non-zero. Returns true if it wrote a value.
static bool reciprocal_link(ryml::Tree& t, size_t group, const char* a_key,
                            const char* b_key, const std::string& ctx,
                            ProblemList& problems) {
    double a, b;
    bool has_a = get_num_child(t, group, a_key, a);
    bool has_b = get_num_child(t, group, b_key, b);
    if (has_a && has_b) {
        if (a != 0.0 && b != 0.0 && !approx_eq(a * b, 1.0)) {
            std::ostringstream m;
            m << ctx << ": '" << a_key << "' and '" << b_key
              << "' are inconsistent (" << a_key << " * " << b_key << " = "
              << format_double(a * b) << ", expected 1).";
            add_problem(problems, m.str());
        }
        return false;
    }
    if (has_a && a != 0.0) {
        set_num_child(t, group, b_key, 1.0 / a);
        return true;
    }
    if (has_b && b != 0.0) {
        set_num_child(t, group, a_key, 1.0 / b);
        return true;
    }
    return false;
}

// Resolve the BendP dependent parameters (bend.md). The reference bend strength
// has four equivalent forms tied together by the element length and the
// reference momentum:
//   angle_ref     = length * g_ref
//   rho_ref       = 1 / g_ref
//   g_ref         = factor * bend_field_ref     (factor = q*c/pc)
// and the "actual" output pair g_actual = factor * bend_field_actual. The
// geometric relations need no momentum; only the field<->strength legs do, so
// they run only when `has_factor`. Iterated to a fixed point so a value given in
// any one form fills the others.
static void resolve_bend(ryml::Tree& t, size_t ele, double length,
                         bool has_factor, double factor,
                         const std::string& ename, ProblemList& problems) {
    size_t bp = t.find_child(ele, ryml::to_csubstr("BendP"));
    if (bp == ryml::NONE) return;
    std::string ctx = "element '" + ename + "' BendP";

    reciprocal_link(t, bp, "g_ref", "rho_ref", ctx, problems);
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        changed |= link_pair(t, bp, "g_ref", "angle_ref", length, ctx, problems);
        if (has_factor)
            changed |= link_pair(t, bp, "bend_field_ref", "g_ref", factor, ctx,
                                 problems);
        if (!changed) break;
    }
    // g_ref may have been derived above; fill rho_ref from it now.
    reciprocal_link(t, bp, "g_ref", "rho_ref", ctx, problems);
    if (has_factor)
        link_pair(t, bp, "bend_field_actual", "g_actual", factor, ctx, problems);
}

// Split a multipole component key into its (normal/skew char, order digits),
// e.g. "Bn2L" -> ('n', "2"). `prefixes` is the pair of accepted leading tokens
// (magnetic "Bn"/"Bs"/"Kn"/"Ks" collapse to the field forms here; electric
// "En"/"Es"). Returns false for taper keys (an underscore) and anything that is
// not <prefix><digits>[L].
static bool parse_multipole_key(const std::string& k, char first,
                                char& ns_out, std::string& order_out) {
    if (k.size() < 3 || k[0] != first) return false;
    if (k[1] != 'n' && k[1] != 's') return false;
    if (k.find('_') != std::string::npos) return false;  // *_taper etc.
    std::string rest = k.substr(2);
    if (!rest.empty() && rest.back() == 'L') rest.pop_back();
    if (rest.empty()) return false;
    for (char c : rest)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    ns_out = k[1];
    order_out = rest;
    return true;
}

// Collect the set of (normal/skew, order) components present in a multipole
// group, judged by the field-form prefix `first` ('B' magnetic, 'E' electric)
// and its normalized partner `norm` ('K', or '\0' when there is none).
static std::set<std::pair<char, std::string>> multipole_components(
    const ryml::Tree& t, size_t group, char first, char norm) {
    std::set<std::pair<char, std::string>> comps;
    for (size_t c = t.first_child(group); c != ryml::NONE;
         c = t.next_sibling(c)) {
        if (!t.has_key(c)) continue;
        std::string k(t.key(c).str, t.key(c).len);
        char ns;
        std::string order;
        if (parse_multipole_key(k, first, ns, order) ||
            (norm && parse_multipole_key(k, norm, ns, order)))
            comps.emplace(ns, order);
    }
    return comps;
}

// Resolve MagneticMultipoleP components (magneticmultipole.md). Each component
// of order N has four equivalent forms tied by length and reference momentum:
//   BnNL = length * BnN,   KnN = factor * BnN,   KnNL = factor * length * BnN
// (and likewise for the skew "s" forms). Length-integration needs no momentum;
// the field<->normalized legs run only when `has_factor`. Iterated to a fixed
// point so a value given in any one form fills the rest.
static void resolve_magnetic_multipoles(ryml::Tree& t, size_t ele, double length,
                                        bool has_factor, double factor,
                                        const std::string& ename,
                                        ProblemList& problems) {
    size_t mp = t.find_child(ele, ryml::to_csubstr("MagneticMultipoleP"));
    if (mp == ryml::NONE) return;
    std::string ctx = "element '" + ename + "' MagneticMultipoleP";

    for (const auto& comp : multipole_components(t, mp, 'B', 'K')) {
        std::string B = std::string("B") + comp.first + comp.second;
        std::string BL = B + "L";
        std::string K = std::string("K") + comp.first + comp.second;
        std::string KL = K + "L";
        for (int pass = 0; pass < 4; ++pass) {
            bool changed = false;
            changed |= link_pair(t, mp, B.c_str(), BL.c_str(), length, ctx,
                                 problems);
            changed |= link_pair(t, mp, K.c_str(), KL.c_str(), length, ctx,
                                 problems);
            if (has_factor) {
                changed |= link_pair(t, mp, B.c_str(), K.c_str(), factor, ctx,
                                     problems);
                changed |= link_pair(t, mp, BL.c_str(), KL.c_str(), factor, ctx,
                                     problems);
            }
            if (!changed) break;
        }
    }
}

// Resolve ElectricMultipoleP components (electricmultipole.md). Electric
// multipoles have no normalized form, so a component of order N is only tied by
// length-integration: EnNL = length * EnN (and the skew "s" forms). No momentum
// is needed.
static void resolve_electric_multipoles(ryml::Tree& t, size_t ele, double length,
                                        const std::string& ename,
                                        ProblemList& problems) {
    size_t ep = t.find_child(ele, ryml::to_csubstr("ElectricMultipoleP"));
    if (ep == ryml::NONE) return;
    std::string ctx = "element '" + ename + "' ElectricMultipoleP";

    for (const auto& comp : multipole_components(t, ep, 'E', '\0')) {
        std::string E = std::string("E") + comp.first + comp.second;
        std::string EL = E + "L";
        link_pair(t, ep, E.c_str(), EL.c_str(), length, ctx, problems);
    }
}

// Compute the field-dependent output parameters of an element. Uses the
// element's *upstream* reference (parameters are referenced to the upstream
// end). The reference momentum sets the field<->normalized conversion factor
// K = factor * B; when the species/momentum is unknown the momentum-dependent
// legs are skipped, but the purely geometric relations (bend angle/radius,
// multipole length-integration) still run.
static void compute_dependent(ryml::Tree& t, size_t ele, const std::string& kind,
                              const RefState& up, double length,
                              ProblemList& problems) {
    bool has_factor = false;
    double factor = 0.0;
    if (up.has_species && up.has_pc && up.pc > 0.0) {
        try {
            factor = apc::charge_of(up.species) * apc::C_LIGHT / up.pc;
            has_factor = true;
        } catch (...) {
            has_factor = false;
        }
    }

    std::string ename = ele_name(t, ele);
    if (kind == "Bend")
        resolve_bend(t, ele, length, has_factor, factor, ename, problems);
    resolve_magnetic_multipoles(t, ele, length, has_factor, factor, ename,
                                problems);
    resolve_electric_multipoles(t, ele, length, ename, problems);
}

// Resolve the RFP dependent parameters (rf.md). The active length `L_active`
// defaults to the element length `L`; it is materialized when non-zero. The RF
// voltage and gradient are then tied by `voltage = gradient * L_active`: one is
// derived from the other, or an inconsistency is flagged when both are set.
// (frequency <-> harmon is *not* resolved here: it needs the change in reference
// time across the whole branch, which is not known element-by-element.) Any
// element carrying an RFP group is handled, keyed on the group's presence.
static void compute_rf_dependent(ryml::Tree& t, size_t ele, double length,
                                 ProblemList& problems) {
    size_t rfp = t.find_child(ele, ryml::to_csubstr("RFP"));
    if (rfp == ryml::NONE) return;

    // L_active defaults to the element length; hold it when non-zero.
    double L_active = 0.0;
    if (!get_num_child(t, rfp, "L_active", L_active)) {
        L_active = length;
        if (L_active != 0.0) set_num_child(t, rfp, "L_active", L_active);
    }

    std::string ctx = "element '" + ele_name(t, ele) + "' RFP";
    link_pair(t, rfp, "gradient", "voltage", L_active, ctx, problems);
}

// Parameters with a non-zero (or enum/boolean) default, filled in for any group
// that is *present* in the element. The expanded lattice holds every non-zero
// parameter, so an author who writes a group but omits these gets the defaults
// made explicit. A group absent from the element is left untouched -- only
// ReferenceP and FloorP are added by the parser. Parameters whose default is
// zero/null/false are not listed: they are not "held".
struct GroupDefault {
    const char* group;
    const char* key;
    const char* value;
};
static const GroupDefault kGroupDefaults[] = {
    {"RFP", "cavity_type", "STANDING_WAVE"},
    {"RFP", "zero_phase", "ACCELERATING"},
    {"BendP", "ref_geometry", "arc"},
    {"BendP", "multipole_geometry", "follows_ref_geometry"},
    {"ApertureP", "shape", "ELLIPTICAL"},
    {"ApertureP", "location", "ENTRANCE_END"},
    {"ApertureP", "aperture_active", "true"},
    {"ForkP", "direction", "FORWARDS"},
    {"ForkP", "propagate_reference", "true"},
};

// Write the non-zero/enum defaults (kGroupDefaults) into every group the element
// already carries, leaving any value the author set in place.
static void materialize_group_defaults(ryml::Tree& t, size_t ele) {
    for (const GroupDefault& d : kGroupDefaults) {
        size_t g = t.find_child(ele, ryml::to_csubstr(d.group));
        if (g == ryml::NONE || !t.is_map(g)) continue;
        if (t.find_child(g, ryml::to_csubstr(d.key)) == ryml::NONE)
            set_plain_child(t, g, d.key, d.value);
    }
}

// The per-element bookkeeper. Given the previous element in the branch (NONE for
// the first, the BeginningEle) and the current element, compute and store the
// current element's upstream reference parameters, floor placement, s-position,
// and field-dependent parameters. Order matters: the reference momentum is
// needed for the dependent-parameter conversions, so reference comes first.
//
// `seed_fork` names the Fork element that instantiated this branch, or NONE. It
// is set only for the first element of a branch created by a Fork whose
// `propagate_reference` is true: that beginning (destination) element then
// inherits the Fork's reference species/energy and floor placement instead of
// starting from the branch's own (usually empty) inputs (fork.md, s:forking).
static void _element_bookkeeper(ryml::Tree& t, size_t prev, size_t ele,
                                ProblemList& problems,
                                size_t seed_fork = ryml::NONE) {
    std::string kind = child_val_str(t, ele, "kind");

    RefState up;
    pals::FloorState floor;
    double s_pos = 0.0;

    if (prev == ryml::NONE && seed_fork != ryml::NONE) {
        // Beginning element of a forked branch with propagate_reference: inherit
        // the Fork element's reference and floor. A Fork has zero length and a
        // unit transfer map, so its stored (upstream) reference and floor are
        // also its values at the connection point. The new branch's s-coordinate
        // still starts at zero.
        up = read_ref(t, seed_fork);
        complete_energy(up);
        read_floor(t, seed_fork, floor);
    } else if (prev == ryml::NONE) {
        // First element of the branch (BeginningEle): reference, floor and
        // s-position are user inputs. Complete the reference (fill pc from E or
        // vice versa) and normalize the floor placement; both default sensibly
        // when unset (zero energy/momentum, origin, identity orientation, s = 0).
        up = read_ref(t, ele);
        complete_energy(up);
        read_floor(t, ele, floor);  // leaves the origin/identity default if absent
        get_num_child(t, ele, "s_position", s_pos);
    } else {
        // Any later element: its upstream parameters are the previous element's
        // downstream parameters, propagated through the previous element.
        std::string pkind = child_val_str(t, prev, "kind");
        double plen = 0.0;
        get_num_child(t, prev, "length", plen);

        RefState pup = read_ref(t, prev);
        complete_energy(pup);
        up = downstream_ref(t, prev, pkind, pup, plen, problems);

        pals::FloorState pfloor;
        read_floor(t, prev, pfloor);
        pals::Vec3 L;
        pals::Quat S;
        element_LS(t, prev, pkind, plen, L, S);
        floor = pals::floor_propagate(pfloor, L, S);

        double ps = 0.0;
        get_num_child(t, prev, "s_position", ps);
        s_pos = ps + plen;
    }

    // Store the outputs. Reference before dependent: the latter reads pc_ref.
    write_ref(t, ele, up);
    write_floor(t, ele, floor);
    set_num_child(t, ele, "s_position", s_pos);

    // Fill in dependent parameters and non-zero/enum defaults of the groups the
    // element carries. The expanded lattice holds every non-zero parameter.
    double length = 0.0;
    get_num_child(t, ele, "length", length);
    materialize_group_defaults(t, ele);
    compute_dependent(t, ele, kind, up, length, problems);
    compute_rf_dependent(t, ele, length, problems);
}

// Append a zero-length `Placeholder` element named `branch_end` to a branch's
// `line` and return its definition node. This is the marker that holds the
// branch's final floor placement and reference parameters; the bookkeeper fills
// those in by treating it like any other element (its upstream parameters are
// the downstream end of the last real element).
static size_t append_branch_end(ryml::Tree& t, size_t line) {
    ensure_capacity(t, 4);
    size_t wrapper = t.append_child(line);  // the anonymous seq-entry map
    t.ref(wrapper) |= ryml::MAP;
    size_t def = t.append_child(wrapper);  // the keyed element definition
    t.ref(def) |= ryml::KEY | ryml::MAP;
    t.set_key(def, t.to_arena(ryml::to_csubstr("branch_end")));
    size_t kind = t.append_child(def);
    t.ref(kind) |= ryml::KEY | ryml::VAL;
    t.set_key(kind, t.to_arena(ryml::to_csubstr("kind")));
    t.set_val(kind, t.to_arena(ryml::to_csubstr("Placeholder")));
    return def;
}

// Walk every branch of the expanded lattice and run _element_bookkeeper on each
// element in order, threading each element's results (persisted in the tree)
// into the next. After the branch's elements, a `branch_end` Placeholder is
// appended and bookkept to hold the branch's final floor placement and reference
// parameters (the downstream end of the last element).
//
// A Fork whose `propagate_reference` is true seeds its destination branch's
// beginning element from its own reference/floor. Branches are visited in order
// and a new branch is always appended after the Fork that creates it, so by the
// time a forked branch is reached its Fork has been bookkept: `fork_seed` maps
// the destination element (by node id, matching the raw `fork_pointer`) to that
// Fork. `fork_pointer` still holds the work-tree node id here; it is remapped to
// the split-out tree only later.
static void run_element_bookkeeper(ryml::Tree& t, size_t lat_node,
                                   ProblemList& problems) {
    size_t branches = t.find_child(lat_node, ryml::to_csubstr("branches"));
    if (branches == ryml::NONE || !t.is_seq(branches)) return;

    std::map<size_t, size_t> fork_seed;  // destination element -> its Fork

    for (size_t entry = t.first_child(branches); entry != ryml::NONE;
         entry = t.next_sibling(entry)) {
        if (!t.is_map(entry)) continue;
        size_t branch = t.first_child(entry);
        if (branch == ryml::NONE || !t.is_map(branch)) continue;
        size_t line = t.find_child(branch, ryml::to_csubstr("line"));
        if (line == ryml::NONE || !t.is_seq(line)) continue;

        size_t prev = ryml::NONE;
        for (size_t le = t.first_child(line); le != ryml::NONE;
             le = t.next_sibling(le)) {
            // A line entry is a wrapper map whose first child is the keyed
            // element definition; bare unresolved references have no parameters.
            if (!t.is_map(le)) continue;
            size_t def = t.first_child(le);
            if (def == ryml::NONE || !t.has_key(def)) continue;

            size_t seed = ryml::NONE;
            if (prev == ryml::NONE) {
                auto it = fork_seed.find(def);
                if (it != fork_seed.end()) seed = it->second;
            }
            _element_bookkeeper(t, prev, def, problems, seed);

            // Record where a Fork propagates its reference/floor to. The default
            // (materialized above) is propagate_reference: true; only an explicit
            // false opts out.
            if (child_val_str(t, def, "kind") == "Fork") {
                size_t forkp = t.find_child(def, ryml::to_csubstr("ForkP"));
                std::string prop =
                    forkp != ryml::NONE
                        ? child_val_str(t, forkp, "propagate_reference")
                        : "";
                std::string ptr = child_val_str(t, def, "fork_pointer");
                if (prop != "false" && !ptr.empty()) {
                    try {
                        fork_seed[static_cast<size_t>(std::stoull(ptr))] = def;
                    } catch (...) {
                    }
                }
            }
            prev = def;
        }

        // An empty branch has no final state to record; otherwise cap it with a
        // `branch_end` Placeholder carrying the downstream end of the last
        // element.
        if (prev != ryml::NONE)
            _element_bookkeeper(t, prev, append_branch_end(t, line), problems);
    }
}

// Rewrite the `fork_pointer` scalars of a freshly split-out tree. handle_fork
// stores the raw node id of the fork's destination element, but it runs while
// expansion is still on the work tree; cutting the lattice out into its own tree
// renumbers every node, so each pointer has to be translated to the id its
// target now carries. `from_work` maps work ids to ids in `t`. A pointer whose
// target did not come across (it should always be inside the lattice) is left
// alone and reported.
static void remap_fork_pointers(ryml::Tree& t, size_t node,
                                const std::map<size_t, size_t>& from_work,
                                ProblemList& problems) {
    if (node == ryml::NONE) return;

    if (t.has_key(node) && t.key(node) == ryml::to_csubstr("fork_pointer") &&
        t.has_val(node)) {
        std::string old(t.val(node).str, t.val(node).len);
        size_t target = 0;
        try {
            target = static_cast<size_t>(std::stoull(old));
        } catch (...) {
            return;
        }
        auto it = from_work.find(target);
        if (it == from_work.end()) {
            add_problem(problems,
                        "fork_pointer target is outside the expanded lattice");
            return;
        }
        std::string id_str = std::to_string(it->second);
        t.set_val(node, t.to_arena(ryml::to_csubstr(id_str)));
    }

    for (size_t c = t.first_child(node); c != ryml::NONE; c = t.next_sibling(c))
        remap_fork_pointers(t, c, from_work, problems);
}

// Builds the `expanded` and `leftover` trees from `combined`.
//
// Expansion has to run on the whole document at once — the lattice pulls in
// element and beamline definitions from the rest of the file — so it happens on
// a single throwaway work tree, which is then cut in two: `expanded` takes the
// root lattice and nothing else, `leftover` takes everything the lattice left
// behind. Both record provenance straight back to `combined`, so the work tree
// can be discarded.
static void make_expanded_and_leftover(ParsedData* comb,
                                       const char* root_lattice,
                                       ProblemList& problems,
                                       YAMLTreeHandle& expanded_out,
                                       YAMLTreeHandle& leftover_out) {
    expanded_out = nullptr;
    leftover_out = nullptr;
    if (!comb) return;

    ParsedData work;
    ryml::Tree& t = work.tree;
    t.reserve(t.capacity() + comb->tree.capacity() + 10000);
    t.reserve_arena(t.arena_capacity() + comb->tree.arena_capacity() + 100000);

    // Start from a full copy of combined, recording work->combined provenance
    // for every node.
    deep_copy_tracked(t, t.root_id(), comb->tree, comb->tree.root_id(),
                      work.provenance);

    std::map<std::string, size_t> emap;
    make_ele_map(emap, t, t.root_id());

    std::string name_str = root_lattice ? root_lattice : "";
    size_t lat_node = find_lattice(t, name_str);

    // `skip` is the node the leftover tree must not copy: the lattice, together
    // with the facility list entry wrapping it, so leftover is not left holding
    // an empty entry. A lattice that is not a lone entry under a wrapper (e.g.
    // one keyed directly into a map) is skipped on its own.
    size_t skip = ryml::NONE;

    if (lat_node == ryml::NONE) {
        add_problem(problems, name_str.empty()
                                  ? "no lattice found to expand"
                                  : "lattice '" + name_str + "' not found");
    } else {
        size_t branches = t.find_child(lat_node, ryml::to_csubstr("branches"));
        std::map<size_t, int> mp_pass;  // multipass line def -> traversals so far
        expand(t, lat_node, emap, work.provenance, problems, branches, mp_pass);

        // Runs after expand, not inside it: a Fork appends branches as it goes,
        // so only once expand has returned does `branches` hold them all.
        strip_branch_kinds(t, lat_node, work.provenance);

        // A branch whose root line is itself `multipass` numbers its elements
        // here — the branch line is not reached through a sub-line flatten, so
        // its multipass indexing cannot happen during expand().
        number_multipass_branches(t, lat_node, work.provenance);

        // Evaluate every mathematical expression to a number (immediate and
        // expr()-delayed alike). Node ids are unchanged -- only scalar text is
        // rewritten -- so provenance stays valid.
        evaluate_expressions(t, problems);

        // With every input now a plain number, walk each branch element-by-
        // element to fill in the reference, floor, s-position, and field-
        // dependent output parameters. This adds new ReferenceP/FloorP nodes,
        // which carry no provenance (like fork_pointer) and so simply do not
        // appear in the correspondence map.
        run_element_bookkeeper(t, lat_node, problems);

        size_t wrapper = t.parent(lat_node);
        skip = (wrapper != ryml::NONE && !t.is_root(wrapper) &&
                t.num_children(wrapper) == 1)
                   ? wrapper
                   : lat_node;
    }

    // expanded: a map holding just the lattice entry, keyed by its name. The
    // root is synthesised (a ryml root cannot itself carry a key), so it has no
    // counterpart in combined and no provenance entry. When no lattice was
    // found the tree stays an empty map.
    ParsedData* exp = new ParsedData();
    exp->tree.reserve(exp->tree.capacity() + t.capacity() + 16);
    exp->tree.reserve_arena(exp->tree.arena_capacity() + t.arena_capacity());
    exp->tree.ref(exp->tree.root_id()) |= ryml::MAP;
    if (lat_node != ryml::NONE) {
        ensure_capacity(exp->tree);
        size_t entry = exp->tree.append_child(exp->tree.root_id());
        deep_copy_tracked(exp->tree, entry, t, lat_node, exp->provenance);

        // Before provenance is chained up to combined it still reads
        // expanded->work, which inverts into exactly the renaming the fork
        // pointers need.
        std::map<size_t, size_t> from_work;
        for (const auto& kv : exp->provenance) from_work[kv.second] = kv.first;
        remap_fork_pointers(exp->tree, exp->tree.root_id(), from_work, problems);

        chain_prov(exp->provenance, work.provenance);
    }

    // leftover: the whole document minus what went to expanded.
    ParsedData* left = new ParsedData();
    left->tree.reserve(left->tree.capacity() + t.capacity() + 16);
    left->tree.reserve_arena(left->tree.arena_capacity() + t.arena_capacity());
    deep_copy_tracked_except(left->tree, left->tree.root_id(), t, t.root_id(),
                             skip, left->provenance);
    chain_prov(left->provenance, work.provenance);

    expanded_out = exp;
    leftover_out = left;
}

/**
 * Recursive helper for make_original. For each included file in `src`:
 *  1. Load the file into a new temporary tree.
 *  2. Make a new key-value pair in `master` with key = file name and value =
 * file contents
 *  3. Delete the temporary tree.
 */
static void add_to_master_tree(ryml::Tree& master, const ryml::Tree& src,
                               size_t node) {
    if (node == ryml::NONE || src.is_val(node)) return;
    for (size_t c = src.first_child(node); c != ryml::NONE;
         c = src.next_sibling(c)) {
        add_to_master_tree(master, src, c);
        if (src.has_key(c) && src.key(c) == "include" && src.has_val(c)) {
            std::string filename(src.val(c).str, src.val(c).len);
            ParsedData* child =
                static_cast<ParsedData*>(parse_file(filename.c_str()));
            if (child) {
                ensure_capacity(master, 2);
                size_t dest = master.append_child(master.root_id());
                deep_copy_recursive(master, dest, child->tree,
                                    child->tree.root_id());
                // Root has no key, so stamp the filename on after the copy
                master.ref(dest) |= ryml::KEY;
                master.set_key(dest,
                               master.to_arena(ryml::to_csubstr(filename)));
                add_to_master_tree(master, child->tree, child->tree.root_id());
                delete child;
            }
        }
    }
}

/**
 * Makes the original lattice. Creates a tree that maps included files to their
 * contents.
 *
 * If the top-level file cannot be read or is not valid YAML, `parse_error` is
 * set to a human-readable description (with the offending line/column for a
 * syntax error) and the returned tree is an empty MAP. Callers treat a non-empty
 * `parse_error` as a fatal failure: there is no document to expand.
 */
static YAMLTreeHandle make_original(const char* filename,
                                    std::string& parse_error) {
    ParsedData* master = new ParsedData();
    master->tree.rootref() |= ryml::MAP;
    ParsedData* src = static_cast<ParsedData*>(parse_file(filename));
    if (src) {
        ensure_capacity(master->tree, 2);
        size_t dest = master->tree.append_child(master->tree.root_id());
        deep_copy_recursive(master->tree, dest, src->tree, src->tree.root_id());
        // Root has no key, so stamp the filename on after the copy
        master->tree.ref(dest) |= ryml::KEY;
        master->tree.set_key(dest,
                             master->tree.to_arena(ryml::to_csubstr(filename)));
        add_to_master_tree(master->tree, src->tree, src->tree.root_id());
        delete src;
    } else {
        // parse_file recorded why on this thread; nothing else has parsed since.
        parse_error = yaml_last_parse_error();
        if (parse_error.empty()) parse_error = "parse failed";
    }
    return master;
}

extern "C" {

YAML_API struct lattices parse_and_expand_PALS(const char* filename,
                                      const char* root_lattice) {
    struct lattices lat = {};
    ProblemList problems;
    // Built as a derivation chain so provenance can be recorded at each step:
    //   original --(splice includes)--> combined --(expand, split)--> expanded
    //                                                              \-> leftover
    std::string parse_error;
    lat.original = make_original(filename, parse_error);
    if (!parse_error.empty()) {
        // The top-level file is not valid YAML: there is no tree to expand.
        // Free the empty stand-in, leave all four handles NULL, and report the
        // location as the single problem so the caller can pinpoint the fault.
        delete_tree(lat.original);
        lat.original = nullptr;
        problems.push_back("could not parse '" + std::string(filename) +
                           "': " + parse_error);
    } else {
        lat.combined = make_combined_from_original(
            static_cast<ParsedData*>(lat.original), filename);
        make_expanded_and_leftover(static_cast<ParsedData*>(lat.combined),
                                   root_lattice, problems, lat.expanded,
                                   lat.leftover);
    }

    // Hand the problem list to the caller as an owning C string array (freed
    // with free_lattice_problems). The library never prints — the caller
    // decides whether to report, save, or ignore.
    lat.problems.count = problems.size();
    lat.problems.items =
        problems.empty() ? nullptr : new char*[problems.size()];
    for (size_t i = 0; i < problems.size(); ++i) {
        lat.problems.items[i] = new char[problems[i].size() + 1];
        std::memcpy(lat.problems.items[i], problems[i].c_str(),
                    problems[i].size() + 1);
    }
    return lat;
}

YAML_API void free_lattice_problems(struct string_list problems) {
    for (size_t i = 0; i < problems.count; ++i) delete[] problems.items[i];
    delete[] problems.items;
}

YAML_API double evaluate_pals_expression(const char* expr, bool* ok) {
    if (ok) *ok = false;
    if (!expr) return 0.0;
    bool was_expr = false;
    std::string body = strip_expr_wrapper(expr, was_expr);
    pals::EvalOutcome r = pals::eval_expression(body, pals::SymbolLookup{});
    if (!r.ok) return 0.0;
    if (ok) *ok = true;
    return r.value;
}

YAML_API struct correspondence_map build_correspondence_map(
    YAMLTreeHandle original, YAMLTreeHandle combined, YAMLTreeHandle expanded,
    YAMLTreeHandle leftover) {
    (void)original;  // provenance is stored in combined, expanded & leftover
    struct correspondence_map out = {nullptr, 0};
    if (!combined || (!expanded && !leftover)) return out;

    ParsedData* comb = static_cast<ParsedData*>(combined);
    const std::map<size_t, size_t>& c2o = comb->provenance;  // combined->original

    std::vector<struct node_link> links;

    // Emit one link per node of a derived tree, walking it from the root so that
    // exactly the live nodes are visited. Expansion splits the document in two,
    // so a link names a node in one derived tree and YAML_NULL_ID in the other;
    // the shared combined id is what ties the two sides together.
    auto walk = [&](YAMLTreeHandle handle, bool is_leftover) {
        if (!handle) return;
        ParsedData* pd = static_cast<ParsedData*>(handle);
        const std::map<size_t, size_t>& d2c = pd->provenance;  // derived->combined
        ryml::Tree& dt = pd->tree;

        std::vector<size_t> stack;
        if (dt.root_id() != ryml::NONE) stack.push_back(dt.root_id());
        while (!stack.empty()) {
            size_t n = stack.back();
            stack.pop_back();

            struct node_link link;
            link.expanded = is_leftover ? YAML_NULL_ID : n;
            link.leftover = is_leftover ? n : YAML_NULL_ID;
            auto dc = d2c.find(n);
            if (dc != d2c.end()) {
                link.combined = dc->second;
                auto co = c2o.find(dc->second);
                link.original = (co != c2o.end()) ? co->second : YAML_NULL_ID;
            } else {
                link.combined = YAML_NULL_ID;
                link.original = YAML_NULL_ID;
            }
            links.push_back(link);

            for (size_t c = dt.first_child(n); c != ryml::NONE;
                 c = dt.next_sibling(c))
                stack.push_back(c);
        }
    };

    walk(expanded, false);
    walk(leftover, true);

    out.count = links.size();
    if (out.count > 0) {
        out.links = new struct node_link[out.count];
        std::copy(links.begin(), links.end(), out.links);
    }
    return out;
}

YAML_API void free_correspondence_map(struct correspondence_map map) {
    delete[] map.links;
}

}  // extern "C"
