#ifndef RECLAIMER_H
#define RECLAIMER_H

#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// A coefficient that used to calcuate the max number
// of reclaim node in reclaim list
const int kCoefficient = 4 + 1 / 4;

struct HazardPointer {
  HazardPointer() : ptr(nullptr), next(nullptr) {}
  ~HazardPointer() {}

  HazardPointer(const HazardPointer& other) = delete;
  HazardPointer(HazardPointer&& other) = delete;
  HazardPointer& operator=(const HazardPointer& other) = delete;
  HazardPointer& operator=(HazardPointer&& other) = delete;

  std::atomic_flag flag;
  // We must use atomic pointer to ensure the modification
  // is visible to other threads
  std::atomic<void*> ptr;
  HazardPointer* next;
};

struct HazardPointerList {
  HazardPointerList() : head(new HazardPointer()) {}
  ~HazardPointerList() {
    // HazardPointerList destruct when program exit
    HazardPointer* p = head.load(std::memory_order_acquire);
    while (p) {
      HazardPointer* temp = p;
      p = p->next;
      delete temp;
    }
  }

  size_t get_size() const { return size.load(std::memory_order_relaxed); }

  std::atomic<HazardPointer*> head;
  std::atomic<size_t> size;
};

static HazardPointerList g_hazard_pointer_list;

class Reclaimer {
 public:
  static Reclaimer& GetInstance() {
    // Each thread has its own reclaimer
    thread_local static Reclaimer reclaimer;
    return reclaimer;
  }

  Reclaimer(const Reclaimer&) = delete;
  Reclaimer(Reclaimer&&) = delete;
  Reclaimer& operator=(const Reclaimer&) = delete;
  Reclaimer& operator=(Reclaimer&&) = delete;

  // Mark ptr as an hazard pointer
  // If ptr == nullptr then mask last ptr(that is hazard) as no hazard
  void MarkHazard(void* const ptr) {
    // TODO:Try to degrade memory order?
    hazard_pointer_->ptr.store(ptr, std::memory_order_seq_cst);
  }

  // Check if the ptr is hazard
  bool Hazard(void* const ptr) {
    std::atomic<HazardPointer*>& head = g_hazard_pointer_list.head;
    HazardPointer* p = head.load(std::memory_order_acquire);
    do {
      // TODO:Try to degrade memory order?
      if (p->ptr.load(std::memory_order_seq_cst) == ptr) {
        return true;
      }
      p = p->next;
    } while (p);

    return false;
  }

  // If ptr is hazard then reclaim it later
  void ReclaimLater(void* const ptr, std::function<void(void*)>&& func) {
    ReclaimNode* new_node = reclaim_pool_.Pop();
    new_node->ptr = ptr;
    new_node->delete_func = std::move(func);
    reclaim_map_.insert(std::make_pair(ptr, new_node));
  }

  // Try to reclaim all no hazard pointers
  void ReclaimNoHazardPointer() {
    if (reclaim_map_.size() < kCoefficient * g_hazard_pointer_list.get_size()) {
      return;
    }

    // Used to speed up the inspection of the ptr
    std::unordered_set<void*> not_allow_delete_set;
    std::atomic<HazardPointer*>& head = g_hazard_pointer_list.head;
    HazardPointer* p = head.load(std::memory_order_acquire);
    do {
      void* const ptr = p->ptr.load(std::memory_order_seq_cst);
      if (nullptr != ptr) {
        not_allow_delete_set.insert(ptr);
      }
      p = p->next;
    } while (p);

    for (auto it = reclaim_map_.begin(); it != reclaim_map_.end();) {
      if (not_allow_delete_set.find(it->first) == not_allow_delete_set.end()) {
        ReclaimNode* node = it->second;
        node->delete_func(node->ptr);
        delete node;
        it = reclaim_map_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  Reclaimer() : hazard_pointer_(nullptr) {
    std::atomic<HazardPointer*>& head = g_hazard_pointer_list.head;
    HazardPointer* p = head.load(std::memory_order_acquire);
    do {
      // Try to get the idle hazard pointer
      if (!p->flag.test_and_set()) {
        hazard_pointer_ = p;
        break;
      }
      p = p->next;
    } while (p);

    if (nullptr == hazard_pointer_) {
      // No idle hazard pointer, allocate new one
      HazardPointer* new_head = new HazardPointer();
      new_head->flag.test_and_set();
      hazard_pointer_ = new_head;
      g_hazard_pointer_list.size.fetch_add(1, std::memory_order_relaxed);
      HazardPointer* old_head = head.load(std::memory_order_relaxed);
      do {
        new_head->next = old_head;
      } while (!head.compare_exchange_weak(old_head, new_head,
                                           std::memory_order_release,
                                           std::memory_order_relaxed));
    }

    assert(nullptr != hazard_pointer_);
  }

  ~Reclaimer() {
    // The Reclaimer destruct when the thread exit
    assert(nullptr == hazard_pointer_->ptr.load(std::memory_order_relaxed));

    // 1.Hand over the hazard pointer
    hazard_pointer_->flag.clear();

    // 2.Make sure reclaim all no hazard pointers
    for (auto it = reclaim_map_.begin(); it != reclaim_map_.end();) {
      // Wait until pointer is no hazard
      while (Hazard(it->first)) {
        std::this_thread::yield();
      }

      ReclaimNode* node = it->second;
      node->delete_func(node->ptr);
      delete node;
      it = reclaim_map_.erase(it);
    }
  }

  struct ReclaimNode {
    ReclaimNode() : ptr(nullptr), next(nullptr), delete_func(nullptr) {}
    ~ReclaimNode() {}

    void* ptr;
    ReclaimNode* next;
    std::function<void(void*)> delete_func;
  };

  // Reuse ReclaimNode
  struct ReclaimPool {
    ReclaimPool() : head(new ReclaimNode()) {}
    ~ReclaimPool() {
      ReclaimNode* temp;
      while (head) {
        temp = head;
        head = head->next;
        delete temp;
      }
    }

    void Push(ReclaimNode* node) {
      node->next = head;
      head = node;
    }

    ReclaimNode* Pop() {
      if (nullptr == head->next) {
        return new ReclaimNode();
      }

      ReclaimNode* temp = head;
      head = head->next;
      temp->next = nullptr;
      return temp;
    }

    ReclaimNode* head;
  };

  HazardPointer* hazard_pointer_;
  std::unordered_map<void*, ReclaimNode*> reclaim_map_;
  ReclaimPool reclaim_pool_;
};
#endif  // RECLAIMER_H
