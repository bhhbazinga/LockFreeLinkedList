#ifndef LOCKFREE_LINKEDLIST_H
#define LOCKFREE_LINKEDLIST_H

#include <atomic>
#include <cstdio>

#include "reclaimer.h"

template <typename T>
class LockFreeLinkedList {
  struct Node;

 public:
  LockFreeLinkedList() : head_(new Node()), size_(0) {}

  LockFreeLinkedList(const LockFreeLinkedList& other) = delete;
  LockFreeLinkedList(LockFreeLinkedList&& other) = delete;

  LockFreeLinkedList& operator=(const LockFreeLinkedList& other) = delete;
  LockFreeLinkedList& operator=(LockFreeLinkedList&& other) = delete;

  ~LockFreeLinkedList() {
    Node* p = head_;
    Reclaimer& reclaimer = Reclaimer::GetInstance();
    while (p != nullptr) {
      Node* tmp = p;
      p = p->next.load(std::memory_order_acquire);
      // When list destructing, all nodes must not hazard,
      // because each thread clear its own hazard pointers before exit.
      assert(!reclaimer.Hazard(tmp));
      delete tmp;
    }
  }

  // Find the first node which data is greater than the given data,
  // then insert the new node before it then return true, else if
  // data is already exist in list then return false.
  bool Insert(const T& data) { return InsertNode(new Node(data)); }
  bool Insert(T&& data) { return InsertNode(new Node(std::move(data))); }

  // Find the first node which data is equals to the given data,
  // then delete it and return true, if not found the given data then
  // return false.
  bool Delete(const T& data);

  // Find the first node which data is equals to the given data, if not found
  // the given data then return false.
  bool Find(const T& data) {
    Node* prev;
    Node* cur;

    bool found = Search(data, &prev, &cur);
    ClearHazardPointer();
    return found;
  }

  // Get size of the list.
  size_t size() const { return size_.load(std::memory_order_relaxed); }

 private:
  bool InsertNode(Node* new_node);

  bool Search(const T& data, Node** prev_ptr, Node** cur_ptr);

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

  // After invoke Search, we should clear hazard pointer,
  // invoke ClearHazardPointer after Insert and Delete.
  void ClearHazardPointer() {
    Reclaimer& reclaimer = Reclaimer::GetInstance();
    reclaimer.MarkHazard(0, nullptr);
    reclaimer.MarkHazard(1, nullptr);
    reclaimer.ReclaimNoHazardPointer();
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
bool LockFreeLinkedList<T>::InsertNode(Node* new_node) {
  Node* prev;
  Node* cur;
  do {
    if (Search(new_node->data, &prev, &cur)) {
      // List already contains new_node->data.
      ClearHazardPointer();
      delete new_node;
      return false;
    }

    new_node->next.store(cur, std::memory_order_release);
  } while (!prev->next.compare_exchange_weak(
      cur, new_node, std::memory_order_release, std::memory_order_relaxed));

  size_.fetch_add(1, std::memory_order_relaxed);
  ClearHazardPointer();
  return true;
}

template <typename T>
bool LockFreeLinkedList<T>::Delete(const T& data) {
  Node* prev;
  Node* cur;
  Node* next;
  do {
    do {
      if (!Search(data, &prev, &cur)) {
        ClearHazardPointer();
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
    Reclaimer& reclaimer = Reclaimer::GetInstance();
    reclaimer.ReclaimLater(cur,
                           [](void* ptr) { delete static_cast<Node*>(ptr); });
  } else {
    Search(data, &prev, &cur);
  }

  ClearHazardPointer();
  return true;
}

// Find the first node which data is equals to the given data, if not found
// the given data then return false. *cur_ptr point to that node, *prev_ptr is
// the predecessor of that node.
template <typename T>
bool LockFreeLinkedList<T>::Search(const T& data, Node** prev_ptr,
                                   Node** cur_ptr) {
try_again:
  Node* prev = head_;
  Node* cur = prev->next.load(std::memory_order_acquire);
  Node* next;
  Reclaimer& reclaimer = Reclaimer::GetInstance();
  while (true) {
    reclaimer.MarkHazard(0, cur);
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

      reclaimer.ReclaimLater(cur,
                             [](void* ptr) { delete static_cast<Node*>(ptr); });
      size_.fetch_sub(1, std::memory_order_relaxed);
      cur = get_unmarked_reference(next);
    } else {
      const T& cur_data = cur->data;
      // Make sure prev is the predecessor of cur,
      // so that cur_data is correct.
      if (prev->next.load(std::memory_order_acquire) != cur) goto try_again;

      // Can not get cur_data after above invocation,
      // because prev may not be the predecessor of cur at this point.
      if (GreaterOrEquals(cur_data, data)) {
        *prev_ptr = prev;
        *cur_ptr = cur;
        assert(prev != nullptr);
        assert(!is_marked_reference(prev));
        return Equals(cur_data, data);
      }
      // swap two hazard pointers.
      void* hp0 = reclaimer.GetHazardPtr(0);
      void* hp1 = reclaimer.GetHazardPtr(1);
      reclaimer.MarkHazard(2, hp0);  // Temporarily save hp0.
      reclaimer.MarkHazard(0, hp1);
      reclaimer.MarkHazard(1, hp0);
      reclaimer.MarkHazard(2, nullptr);

      prev = cur;
      cur = next;
    }
  };

  assert(false);
  return false;
}

#endif  // LOCKFREE_LINKEDLIST_H