// src/skills/dep_graph.cpp — YOU IMPLEMENT. Spec §6.2.

#include "skills/dep_graph.h"

#include <algorithm>

bool DepGraph::add_node(const std::string& name) {
    if (requires_.find(name) != requires_.end()) {
        return false;
    }
    requires_[name] = std::vector<std::string>();
    return true;
}

bool DepGraph::add_edge(const std::string& from, const std::string& to) {
    auto it = requires_.find(from);
    if (it == requires_.end()) {
        return false;
    }
    for (std::size_t i = 0; i < it->second.size(); i++) {
        if (it->second[i] == to) {
            return false;
        }
    }
    it->second.push_back(to);
    required_by_[to].push_back(from);
    return true;
}

bool DepGraph::has_node(const std::string& name) const {
    return requires_.find(name) != requires_.end();
}

std::size_t DepGraph::node_count() const noexcept {
    return requires_.size();
}

const std::vector<std::string>& DepGraph::requirements(const std::string& name) const {
    static const std::vector<std::string> kNone;
    auto it = requires_.find(name);
    if (it == requires_.end()) {
        return kNone;
    }
    return it->second;
}

static bool in_list(const std::vector<std::string>& list, const std::string& name) {
    for (std::size_t i = 0; i < list.size(); i++) {
        if (list[i] == name) {
            return true;
        }
    }
    return false;
}

bool DepGraph::dfs(const std::string& node, std::vector<std::string>& path,
                   std::vector<std::string>* out, std::string* error) const {
    path.push_back(node);

    const std::vector<std::string>& reqs = requires_.find(node)->second;
    for (std::size_t i = 0; i < reqs.size(); i++) {
        const std::string& next = reqs[i];

        if (requires_.find(next) == requires_.end()) {
            *error = "missing: " + next + " (required by " + node + ")";
            return false;
        }

        if (in_list(path, next) == true) {
            std::size_t start = 0;
            while (path[start] != next) {
                start = start + 1;
            }
            std::string message = "cycle: ";
            for (std::size_t j = start; j < path.size(); j++) {
                message = message + path[j] + " -> ";
            }
            message = message + next;
            *error = message;
            return false;
        }

        if (in_list(*out, next) == false) {
            if (dfs(next, path, out, error) == false) {
                return false;
            }
        }
    }

    path.pop_back();
    out->push_back(node);
    return true;
}

bool DepGraph::order_from(const std::string& root, std::vector<std::string>* out,
                          std::string* error) const {
    out->clear();
    if (requires_.find(root) == requires_.end()) {
        *error = "unknown: " + root;
        return false;
    }
    std::vector<std::string> path;
    return dfs(root, path, out, error);
}

std::vector<std::string> DepGraph::dependents(const std::string& name) const {
    std::vector<std::string> result;
    std::vector<std::string> seen;
    seen.push_back(name);
    std::vector<std::string> level;
    level.push_back(name);

    while (level.empty() == false) {
        std::vector<std::string> next_level;
        for (std::size_t i = 0; i < level.size(); i++) {
            auto it = required_by_.find(level[i]);
            if (it == required_by_.end()) {
                continue;
            }
            for (std::size_t j = 0; j < it->second.size(); j++) {
                const std::string& who = it->second[j];
                if (in_list(seen, who) == false) {
                    seen.push_back(who);
                    next_level.push_back(who);
                }
            }
        }
        std::sort(next_level.begin(), next_level.end());
        for (std::size_t i = 0; i < next_level.size(); i++) {
            result.push_back(next_level[i]);
        }
        level = next_level;
    }
    return result;
}
