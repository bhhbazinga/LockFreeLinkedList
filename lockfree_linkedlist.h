#ifndef LOCKFREE_LINKEDLIST_H
#define LOCKFREE_LINKEDLIST_H

#include <atomic>
#include <cstdio>
#include <functional>

#define log(fmt, ...)                  \
  do {                                 \
    fprintf(stderr, fmt, __VA_ARGS__); \
  } while (0)

template <typename T>
class LockFreeLinkedList {
  struct Node;

 public:
  LockFreeLinkedList() : head_(new Node()), size_(0) {}

  ~LockFreeLinkedList() {
    Node* p = head_->next;
    while (p != nullptr) {
      Node* temp = p;
      p = p->next;
      delete temp;
    }
  }

  // Find the first node which data is equals or greater
  // than the given data, then insert the new node before it.
  void Insert(const T& data) { InsertNode(new Node(data)); }
  void Insert(T&& data) { InsertNode(new Node(std::move(data))); }

  // Find the first node which data is equals to the given data,
  // then delete it.
  bool Delete(const T& data);

  // Find the first node which data is equals to the given data.
  bool Find(const T& data) {
    Node* left_node;
    Node* right_node;
    right_node = Search(data, &left_node);

    return right_node != nullptr && Equals(right_node->data, data);
  }

  // Get size of the list.
  size_t size() const { return size_.load(std::memory_order_relaxed); }

  // void Foreach(std::function<void, const T & data>) const;

  void Dump() {
    Node* p = head_->next;
    while (p != nullptr) {
      log("%d,ismark=%d->", p->data,
          is_marked_reference(p->next.load(std::memory_order_acquire)));
      p = p->next;
    }
  }

 private:
  void InsertNode(Node* new_node);

  Node* Search(const T& data, Node** left_node);

  bool Less(const T& data1, const T& data2) const { return data1 < data2; }

  bool Equals(const T& data1, const T& data2) const {
    return !Less(data1, data2) && !Less(data2, data1);
  }

  bool is_marked_reference(Node* next) const {
    return (reinterpret_cast<unsigned long>(next) & 0x1) != 0;
  }

  Node* get_marked_reference(Node* next) const {
    return reinterpret_cast<Node*>(reinterpret_cast<unsigned long>(next) | 0x1);
  }

  Node* get_unmarked_reference(Node* next) const {
    return reinterpret_cast<Node*>(reinterpret_cast<unsigned long>(next) &
                                   ~0x1);
  }

  struct Node {
    Node() : next(nullptr){};
    Node(const T& data_) : data(data_), next(nullptr) {}
    Node(T&& data_) : data(std::move(data_)), next(nullptr) {}

    ~Node(){};

    T data;
    std::atomic<Node*> next;
  };

  Node* head_;
  std::atomic<size_t> size_;
};

template <typename T>
void LockFreeLinkedList<T>::InsertNode(Node* new_node) {
  Node* left_node;
  Node* right_node;

  do {
    right_node = Search(new_node->data, &left_node);
    new_node->next.store(right_node, std::memory_order_release);
  } while (!left_node->next.compare_exchange_weak(right_node, new_node,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
  size_.fetch_add(1, std::memory_order_relaxed);
}

template <typename T>
bool LockFreeLinkedList<T>::Delete(const T& data) {
  Node* left_node;
  Node* right_node;
  Node* right_node_next;

  do {
    do {
      right_node = Search(data, &left_node);
      if (nullptr == right_node || !Equals(right_node->data, data))
        return false;

      right_node_next = right_node->next.load(std::memory_order_acquire);
    } while (is_marked_reference(right_node_next));
    // Logically delete right_node by marking right_node->next.
  } while (!right_node->next.compare_exchange_weak(
      right_node_next, get_marked_reference(right_node_next),
      std::memory_order_release, std::memory_order_relaxed));

  if (left_node->next.compare_exchange_strong(right_node, right_node_next,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
    // delete right_node;
  } else {
    right_node = Search(data, &left_node);
  }

  size_.fetch_sub(1, std::memory_order_relaxed);
  return true;
}

// Return adjacent left_node and right_node, and make sure
// both nodes are unmarked, data of right_node is equals to or greater than
// the given data.
template <typename T>
typename LockFreeLinkedList<T>::Node* LockFreeLinkedList<T>::Search(
    const T& data, Node** left_node) {
  Node* left_node_next;
  Node* right_node;

  for (;;) {
    Node* cur = head_;
    Node* cur_next = head_->next.load(std::memory_order_acquire);

    // 1.Find left_node and right_node.
    do {
      if (!is_marked_reference(cur_next)) {
        *left_node = static_cast<Node*>(cur);
        left_node_next = cur_next;
      }

      cur = get_unmarked_reference(cur_next);
      if (nullptr == cur) break;

      cur_next = cur->next.load(std::memory_order_acquire);
    } while (is_marked_reference(cur_next) || (cur->data < data));
    right_node = cur;

    // 2. Check nodes are adjacent.
    if (left_node_next == right_node) {
      if (right_node != nullptr && is_marked_reference(right_node->next.load(
                                       std::memory_order_acquire))) {
        continue;
      } else {
        return right_node;
      }
    }

    // 3.Remove one or more marked nodes.
    if ((*left_node)
            ->next.compare_exchange_strong(left_node_next, right_node,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
      if (right_node != nullptr && is_marked_reference(right_node->next.load(
                                       std::memory_order_acquire))) {
        continue;
      } else {
        return right_node;
      }
    }
  }
}

#endif  // LOCKFREE_LINKEDLIST_H