// Small helpers shared between pals_expand.cpp and pals_match.cpp. See
// pals_util.h for what each does and why they are shared.

#include "pals_util.h"

#include <string>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

std::string child_val_str(const ryml::Tree& t, size_t parent, const char* key) {
    size_t id = t.find_child(parent, ryml::to_csubstr(key));
    if (id == ryml::NONE || !t.has_val(id)) return "";
    return std::string(t.val(id).str, t.val(id).len);
}

std::vector<std::string> split_dots(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '.') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

size_t resolve_param_path(const ryml::Tree& t, size_t ele,
                          const std::vector<std::string>& path) {
    size_t cur = ele;
    for (const std::string& comp : path) {
        if (cur == ryml::NONE || !t.is_map(cur)) return ryml::NONE;
        cur = t.find_child(cur, ryml::to_csubstr(comp));
    }
    return cur;
}

std::string strip_expr_wrapper(const std::string& s, bool& was_expr) {
    was_expr = false;
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    std::string t = s.substr(a, b - a + 1);
    const std::string pre = "expr(";
    if (t.size() > pre.size() && t.compare(0, pre.size(), pre) == 0 &&
        t.back() == ')') {
        was_expr = true;
        return t.substr(pre.size(), t.size() - pre.size() - 1);
    }
    return t;
}
