#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "lockfree_linkedlist.h"

const int kMaxThreads = std::thread::hardware_concurrency();

int maxElements;
LockFreeLinkedList<int> list;

// Insert sucessfully then ++cnt,  delete succesfully then --cnt.
std::atomic<int> cnt = 0;
std::atomic<bool> start = false;
std::unordered_map<int, int*> elements2timespan;

void onInsert(int divide) {
  while (!start) {
    std::this_thread::yield();
  }

  int n = maxElements / divide;
  for (int i = 0; i < n; ++i) {
    if (list.Insert(rand() % n)) {
      ++cnt;
    }
  }
}

void onDelete(int divide) {
  while (!start) {
    std::this_thread::yield();
  }

  int n = maxElements / divide;
  for (int i = 0; i < n; ++i) {
    if (list.Delete(rand() % n)) {
      --cnt;
    }
  }
}

void TestConcurrentInsert() {
  int old_size = list.size();
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

  assert(cnt + old_size == static_cast<int>(list.size()));
  int ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t2_ - t1_).count();
  elements2timespan[maxElements][0] += ms;
  std::cout << maxElements << " elements insert concurrently, timespan=" << ms
            << "ms"
            << "\n";
  start = false;
}

void TestConcurrentDelete() {
  int old_size = list.size();
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

  assert(cnt + old_size == static_cast<int>(list.size()));
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
  int old_size = list.size();

  int divide = kMaxThreads / 2;
  std::vector<std::thread> insert_threads;
  for (int i = 0; i < divide; ++i) {
    insert_threads.push_back(std::thread(onInsert, divide));
  }

  std::vector<std::thread> delete_threads;
  for (int i = 0; i < divide; ++i) {
    delete_threads.push_back(std::thread(onDelete, divide));
  }

  cnt = 0;
  start = true;
  auto t1_ = std::chrono::steady_clock::now();
  for (int i = 0; i < divide; ++i) {
    insert_threads[i].join();
  }

  for (int i = 0; i < divide; ++i) {
    delete_threads[i].join();
  }
  auto t2_ = std::chrono::steady_clock::now();

  assert(cnt + old_size == static_cast<int>(list.size()));
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

const int kElements1 = 1000;
const int kElements2 = 10000;
const int kElements3 = 100000;

int main(int argc, char const* argv[]) {
  (void)argc;
  (void)argv;

  srand(std::time(0));

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
    float avg = static_cast<float>(elements2timespan[maxElements][0])
    / 10.0f; std::cout << maxElements
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