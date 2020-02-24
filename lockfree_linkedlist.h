#ifndef LOCKFREE_LINKEDLIST_H
#define LOCKFREE_LINKEDLIST_H

#include <atomic>
#include <cstdio>

#include <iostream>

#include "HazardPointer/reclaimer.h"

template <typename T>
class ListReclaimer;

template <typename T>
class LockFreeLinkedList {
  static_assert(std::is_copy_constructible_v<T>, "T requires copy constructor");
  friend ListReclaimer<T>;

  struct Node;

 public:
  LockFreeLinkedList() : head_(new Node()), size_(0) {}

  LockFreeLinkedList(const LockFreeLinkedList& other) = delete;
  LockFreeLinkedList(LockFreeLinkedList&& other) = delete;

  LockFreeLinkedList& operator=(const LockFreeLinkedList& other) = delete;
  LockFreeLinkedList& operator=(LockFreeLinkedList&& other) = delete;

  ~LockFreeLinkedList() {
    Node* p = head_;
    while (p != nullptr) {
      Node* tmp = p;
      p = p->next.load(std::memory_order_acquire);
      // We can safely delete node, because each thread exits before list
      // destruct, while thead exiting, it wait all hazard pointers hand over.
      delete tmp;
    }
  }

  // Find the first node which data is greater than the given data,
  // then insert the new node before it then return true, else if
  // data is already exist in list then return false.
  template <typename... Args>
  bool Emplace(Args&&... args);

  bool Insert(const T& data) {
    static_assert(std::is_copy_constructible<T>::value,
                  "T must be copy constructible");
    return Emplace(data);
  }

  bool Insert(T&& data) {
    static_assert(std::is_constructible_v<T, T&&>,
                  "T must be constructible with T&&");
    return Emplace(std::forward<T>(data));
  }

  // Find the first node which data is equals to the given data,
  // then delete it and return true, if not found the given data then
  // return false.
  bool Delete(const T& data);

  // Find the first node which data is equals to the given data, if not found
  // the given data then return false.
  bool Find(const T& data) {
    Node* prev;
    Node* cur;
    HazardPointer prev_hp, cur_hp;
    bool found = Search(data, &prev, &cur, prev_hp, cur_hp);
    return found;
  }

  // Get size of the list.
  size_t size() const { return size_.load(std::memory_order_relaxed); }

 private:
  bool InsertNode(Node* new_node);

  bool Search(const T& data, Node** prev_ptr, Node** cur_ptr,
              HazardPointer& prev_hp, HazardPointer& cur_hp);

  bool Less(const T& data1, const T& data2) const { return data1 < data2; }

  bool GreaterOrEquals(const T& data1, const T& data2) const {
    return !(Less(data1, data2));
  }

  bool Equals(const T& data1, const T& data2) const {
    return !Less(data1, data2) && !Less(data2, data1);
  }

  bool is_marked_reference(Node* next) const {
    return (reinterpret_cast<unsigned long>(next) & 0x1) == 0x1;
  }

  Node* get_marked_reference(Node* next) const {
    return reinterpret_cast<Node*>(reinterpret_cast<unsigned long>(next) | 0x1);
  }

  Node* get_unmarked_reference(Node* next) const {
    return reinterpret_cast<Node*>(reinterpret_cast<unsigned long>(next) &
                                   ~0x1);
  }

  static void OnDeleteNode(void* ptr) { delete static_cast<Node*>(ptr); }

  struct Node {
    Node() : data(nullptr), next(nullptr){};

    template <typename... Args>
    Node(Args&&... args)
        : data(new T(std::forward<Args>(args)...)), next(nullptr) {}

    ~Node() {
      if (data != nullptr) delete data;
    };

    T* data;
    std::atomic<Node*> next;
  };

  Node* head_;
  std::atomic<size_t> size_;
  static Reclaimer::HazardPointerList global_hp_list_;
};

template <typename T>
Reclaimer::HazardPointerList LockFreeLinkedList<T>::global_hp_list_;

template <typename T>
class ListReclaimer : public Reclaimer {
  friend LockFreeLinkedList<T>;

 private:
  ListReclaimer(HazardPointerList& hp_list) : Reclaimer(hp_list) {}
  ~ListReclaimer() override = default;

  static ListReclaimer<T>& GetInstance() {
    thread_local static ListReclaimer reclaimer(
        LockFreeLinkedList<T>::global_hp_list_);
    return reclaimer;
  }
};

template <typename T>
template <typename... Args>
bool LockFreeLinkedList<T>::Emplace(Args&&... args) {
  Node* new_node = new Node(std::forward<Args>(args)...);
  Node* prev;
  Node* cur;
  HazardPointer prev_hp, cur_hp;
  do {
    if (Search(*new_node->data, &prev, &cur, prev_hp, cur_hp)) {
      // List already contains *new_node->data.
      delete new_node;
      return false;
    }

    new_node->next.store(cur, std::memory_order_release);
  } while (!prev->next.compare_exchange_weak(
      cur, new_node, std::memory_order_release, std::memory_order_relaxed));

  size_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

template <typename T>
bool LockFreeLinkedList<T>::Delete(const T& data) {
  Node* prev;
  Node* cur;
  Node* next;
  HazardPointer prev_hp, cur_hp;
  do {
    do {
      if (!Search(data, &prev, &cur, prev_hp, cur_hp)) {
        return false;
      }
      next = cur->next.load(std::memory_order_acquire);
    } while (is_marked_reference(next));
    // Logically delete cur by marking cur->next.
  } while (!cur->next.compare_exchange_weak(next, get_marked_reference(next),
                                            std::memory_order_release,
                                            std::memory_order_relaxed));

  if (prev->next.compare_exchange_strong(cur, next, std::memory_order_release,
                                         std::memory_order_relaxed)) {
    size_.fetch_sub(1, std::memory_order_relaxed);
    auto& reclaimer = ListReclaimer<T>::GetInstance();
    reclaimer.ReclaimLater(cur, LockFreeLinkedList<T>::OnDeleteNode);
    reclaimer.ReclaimNoHazardPointer();
  } else {
    prev_hp.UnMark();
    cur_hp.UnMark();
    Search(data, &prev, &cur, prev_hp, cur_hp);
  }

  return true;
}

// Find the first node which data is equals to the given data, if not found
// the given data then return false. *cur_ptr point to that node, *prev_ptr is
// the predecessor of that node.
template <typename T>
bool LockFreeLinkedList<T>::Search(const T& data, Node** prev_ptr,
                                   Node** cur_ptr, HazardPointer& prev_hp,
                                   HazardPointer& cur_hp) {
  auto& reclaimer = ListReclaimer<T>::GetInstance();
try_again:
  Node* prev = head_;
  Node* cur = prev->next.load(std::memory_order_acquire);
  Node* next;
  while (true) {
    cur_hp.UnMark();
    cur_hp = HazardPointer(&reclaimer, cur);
    // Make sure prev is the predecessor of cur,
    // so that cur is properly marked as hazard.
    if (prev->next.load(std::memory_order_acquire) != cur) goto try_again;

    if (nullptr == cur) {
      *prev_ptr = prev;
      *cur_ptr = cur;
      return false;
    };

    next = cur->next.load(std::memory_order_acquire);
    if (is_marked_reference(next)) {
      if (!prev->next.compare_exchange_strong(cur,
                                              get_unmarked_reference(next)))
        goto try_again;

      reclaimer.ReclaimLater(cur, LockFreeLinkedList<T>::OnDeleteNode);
      reclaimer.ReclaimNoHazardPointer();
      size_.fetch_sub(1, std::memory_order_relaxed);
      cur = get_unmarked_reference(next);
    } else {
      const T& cur_data = *cur->data;
      // Make sure prev is the predecessor of cur,
      // so that cur_data is correct.
      if (prev->next.load(std::memory_order_acquire) != cur) goto try_again;

      // Can not get cur_data after above invocation,
      // because prev may not be the predecessor of cur at this point.
      if (GreaterOrEquals(cur_data, data)) {
        *prev_ptr = prev;
        *cur_ptr = cur;
        return Equals(cur_data, data);
      }

      // Swap cur_hp and prev_hp.
      HazardPointer tmp = std::move(cur_hp);
      cur_hp = std::move(prev_hp);
      prev_hp = std::move(tmp);

      prev = cur;
      cur = next;
    }
  };

  assert(false);
  return false;
}

#endif  // LOCKFREE_LINKEDLIST_H