#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "lockfree_linkedlist.h"

const int kMaxThreads = 8;
static_assert((kMaxThreads & (kMaxThreads - 1)) == 0,
              "Make sure kMaxThreads == 2^n");

int maxElements;
LockFreeLinkedList<int> list;
std::atomic<int> cnt(0);
std::atomic<bool> start(false);
std::atomic<bool> insert_finish(false);
std::unordered_map<int, int*> elements2timespan;

void onInsert(int divide) {
  while (!start) {
    std::this_thread::yield();
  }
  for (int i = 0; i < maxElements / divide; ++i) {
    if (list.Insert(i)) {
      ++cnt;
    }
  }
}

void onDelete(int divide) {
  while (!start) {
    std::this_thread::yield();
  }

  while (!insert_finish || list.size() > 0) {
    for (int i = 0; i < maxElements / divide; ++i) {
      if (list.Delete(i)) {
        --cnt;
      }
    }
  }
}

void TestConcurrentInsert() {
  insert_finish = false;
  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxThreads; ++i) {
    threads.push_back(std::thread(onInsert, kMaxThreads));
  }

  start = true;
  auto t1_ = std::chrono::steady_clock::now();
  for (int i = 0; i < kMaxThreads; ++i) {
    threads[i].join();
  }
  auto t2_ = std::chrono::steady_clock::now();

  assert(static_cast<int>(list.size()) == maxElements / kMaxThreads);
  int ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t2_ - t1_).count();
  elements2timespan[maxElements][0] += ms;
  std::cout << maxElements << " elements insert concurrently, timespan=" << ms
            << "ms"
            << "\n";
  start = false;
}

void TestConcurrentDelete() {
  insert_finish = true;

  std::vector<std::thread> threads;
  for (int i = 0; i < kMaxThreads; ++i) {
    threads.push_back(std::thread(onDelete, kMaxThreads));
  }

  cnt = 0;
  start = true;
  auto t1_ = std::chrono::steady_clock::now();
  for (int i = 0; i < kMaxThreads; ++i) {
    threads[i].join();
  }
  auto t2_ = std::chrono::steady_clock::now();

  assert(static_cast<int>(list.size()) == 0 &&
         cnt == -maxElements / kMaxThreads);
  int ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t2_ - t1_).count();
  elements2timespan[maxElements][1] += ms;
  std::cout << maxElements << " elements delete concurrently, timespan=" << ms
            << "ms"
            << "\n";

  cnt = 0;
  start = false;
}

void TestConcurrentInsertAndDequeue() {
  insert_finish = false;
  std::vector<std::thread> insert_threads;
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    insert_threads.push_back(std::thread(onInsert, kMaxThreads / 2));
  }

  std::vector<std::thread> delete_threads;
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    delete_threads.push_back(std::thread(onDelete, kMaxThreads / 2));
  }

  cnt = 0;
  start = true;
  auto t1_ = std::chrono::steady_clock::now();
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    insert_threads[i].join();
  }

  insert_finish = true;

  for (int i = 0; i < kMaxThreads / 2; ++i) {
    delete_threads[i].join();
  }
  auto t2_ = std::chrono::steady_clock::now();

  assert(static_cast<int>(list.size()) == 0 && cnt == 0);
  int ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t2_ - t1_).count();
  elements2timespan[maxElements][2] += ms;
  std::cout << maxElements
            << " elements insert and delete concurrently, timespan=" << ms
            << "ms"
            << "\n";

  cnt = 0;
  start = false;
}

std::unordered_map<int, int> element2count[kMaxThreads / 2];

const int kElements1 = 800;
const int kElements2 = 8000;
const int kElements3 = 80000;

int main(int argc, char const* argv[]) {
  (void)argc;
  (void)argv;

  std::cout << "Benchmark with " << kMaxThreads << " threads:"
            << "\n";

  int elements[] = {kElements1, kElements2, kElements3};
  int timespan1[] = {0, 0, 0};
  int timespan2[] = {0, 0, 0};
  int timespan3[] = {0, 0, 0};

  elements2timespan[kElements1] = timespan1;
  elements2timespan[kElements2] = timespan2;
  elements2timespan[kElements3] = timespan3;

  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < 3; ++j) {
      maxElements = elements[j];
      TestConcurrentInsert();
      TestConcurrentDelete();
      TestConcurrentInsertAndDequeue();
      std::cout << "\n";
    }
  }

  for (int i = 0; i < 3; ++i) {
    maxElements = elements[i];
    float avg = static_cast<float>(elements2timespan[maxElements][0]) / 10.0f;
    std::cout << maxElements
              << " elements insert concurrently, average timespan=" << avg
              << "ms"
              << "\n";
    avg = static_cast<float>(elements2timespan[maxElements][1]) / 10.0f;
    std::cout << maxElements
              << " elements delete concurrently, average timespan=" << avg
              << "ms"
              << "\n";
    avg = static_cast<float>(elements2timespan[maxElements][2]) / 10.0f;
    std::cout << maxElements
              << " elements insert and delete concurrently, average timespan="
              << avg << "ms"
              << "\n";
    std::cout << "\n";
  }

  return 0;
}