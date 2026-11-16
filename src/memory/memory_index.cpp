// src/memory/memory_index.cpp — YOU IMPLEMENT. Spec §4.3.

#include "memory/memory_index.h"

#include <utility>

MemoryIndex::~MemoryIndex() {
    destroy(root_);
    root_ = nullptr;
}

void MemoryIndex::destroy(Node* node) {
    if (node == nullptr) {
        return;
    }
    destroy(node->left);
    destroy(node->right);
    delete node;
}

bool MemoryIndex::insert(MemoryRecord rec) {
    Node* current = root_;
    Node* parent = nullptr;
    bool go_left = false;
    while (current != nullptr) {
        parent = current;
        if (rec.id == current->rec.id) {
            return false;
        }
        else if (rec.id < current->rec.id) {
            go_left = true;
            current = current->left;
        }
        else {
            go_left = false;
            current = current->right;
        }
    }
    Node* node = new Node{std::move(rec), nullptr, nullptr};
    if (parent == nullptr) {
        root_ = node;
    }
    else if (go_left == true) {
        parent->left = node;
    }
    else {
        parent->right = node;
    }
    count_ = count_ + 1;
    return true;
}

const MemoryRecord* MemoryIndex::find(std::uint64_t id) const {
    Node* current = root_;
    while (current != nullptr) {
        if (id == current->rec.id) {
            return &current->rec;
        }
        else if (id < current->rec.id) {
            current = current->left;
        }
        else {
            current = current->right;
        }
    }
    return nullptr;
}

MemoryIndex::Node* MemoryIndex::remove_node(Node* node, std::uint64_t id, bool* removed) {
    if (node == nullptr) {
        return nullptr;
    }
    if (id < node->rec.id) {
        node->left = remove_node(node->left, id, removed);
        return node;
    }
    if (id > node->rec.id) {
        node->right = remove_node(node->right, id, removed);
        return node;
    }

    *removed = true;
    if (node->left == nullptr) {
        Node* right_child = node->right;
        delete node;
        return right_child;
    }
    if (node->right == nullptr) {
        Node* left_child = node->left;
        delete node;
        return left_child;
    }

    Node* successor = node->right;
    while (successor->left != nullptr) {
        successor = successor->left;
    }
    std::uint64_t successor_id = successor->rec.id;
    node->rec = successor->rec;
    node->right = remove_node(node->right, successor_id, removed);
    return node;
}

bool MemoryIndex::remove(std::uint64_t id) {
    bool removed = false;
    root_ = remove_node(root_, id, &removed);
    if (removed == true) {
        count_ = count_ - 1;
    }
    return removed;
}

std::size_t MemoryIndex::size() const noexcept { return count_; }

std::size_t MemoryIndex::height_of(const Node* node) {
    if (node == nullptr) {
        return 0;
    }
    std::size_t left_height = height_of(node->left);
    std::size_t right_height = height_of(node->right);
    if (left_height > right_height) {
        return left_height + 1;
    }
    return right_height + 1;
}

std::size_t MemoryIndex::height() const {
    return height_of(root_);
}

void MemoryIndex::in_order(const Node* node, const std::function<void(const MemoryRecord&)>& fn) {
    if (node == nullptr) {
        return;
    }
    in_order(node->left, fn);
    fn(node->rec);
    in_order(node->right, fn);
}

void MemoryIndex::for_each_in_order(const std::function<void(const MemoryRecord&)>& fn) const {
    in_order(root_, fn);
}

void MemoryIndex::clear() {
    destroy(root_);
    root_ = nullptr;
    count_ = 0;
}
