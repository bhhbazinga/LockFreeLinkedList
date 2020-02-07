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
std::unordered_map<int, int*> elements2timespan;

void onInsert(int divide) {
  while (!start) {
    std::this_thread::yield();
  }
  for (int i = 0; i < maxElements / divide; ++i) {
    list.Insert(i);
  }
}

void onDelete(int divide) {
  while (!start) {
    std::this_thread::yield();
  }

  while (cnt < maxElements) {
    for (int i = 0; i < maxElements / divide; ++i) {
      if (list.Delete(i)) {
        ++cnt;
      }
    }
  }
}

void TestConcurrentInsert() {
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

  // assert(static_cast<int>(list.size()) == maxElements);
  int ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t2_ - t1_).count();
  elements2timespan[maxElements][0] += ms;
  std::cout << maxElements << " elements insert concurrently, timespan=" << ms
            << "ms"
            << "\n";
  start = false;
}

void TestConcurrentDelete() {
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

  // assert(static_cast<int>(list.size()) == 0 && cnt == maxElements);
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
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    delete_threads[i].join();
  }
  auto t2_ = std::chrono::steady_clock::now();

  // assert(static_cast<int>(list.size()) == 0 && cnt == maxElements);
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

auto onDelete_with_count = [](std::unordered_map<int, int>& element2count) {
  while (!start) {
    std::this_thread::yield();
  }
  int x;
  for (; cnt < maxElements;) {
    if (list.Delete(x)) {
      ++cnt;
      ++element2count[x];
    }
  }
};

std::unordered_map<int, int> element2count[kMaxThreads / 2];

void TestCorrectness() {
  maxElements = 1000000;
  assert(maxElements % kMaxThreads == 0);

  for (int i = 0; i < maxElements / kMaxThreads; ++i) {
    for (int j = 0; j < kMaxThreads / 2; ++j) {
      element2count[j][i] = 0;
    }
  }

  std::vector<std::thread> insert_threads;
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    insert_threads.push_back(std::thread(onInsert, kMaxThreads / 2));
  }

  std::vector<std::thread> delete_threads;
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    delete_threads.push_back(
        std::thread(onDelete_with_count, std::ref(element2count[i])));
  }

  cnt = 0;
  start = true;
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    insert_threads[i].join();
  }
  for (int i = 0; i < kMaxThreads / 2; ++i) {
    delete_threads[i].join();
  }

  assert(static_cast<int>(list.size()) == 0 && cnt == maxElements);
  for (int i = 0; i < maxElements / kMaxThreads; ++i) {
    int sum = 0;
    for (int j = 0; j < kMaxThreads / 2; ++j) {
      sum += element2count[j][i];
    }
    assert(sum == kMaxThreads / 2);
  }
}

int main(int argc, char const* argv[]) {
  (void)argc;
  (void)argv;

  // std::cout << "Benchmark with " << kMaxThreads << " threads:"
  //           << "\n";

  // int elements[] = {80, 800, 8000};
  // int timespan1[] = {0, 0, 0};
  // int timespan2[] = {0, 0, 0};
  // int timespan3[] = {0, 0, 0};

  // elements2timespan[80] = timespan1;
  // elements2timespan[800] = timespan2;
  // elements2timespan[8000] = timespan3;

  // for (int i = 0; i < 10; ++i) {
  //   for (int j = 0; j < 3; ++j) {
  //     maxElements = elements[j];
  //     TestConcurrentInsert();
  //     TestConcurrentDelete();
  //     TestConcurrentInsertAndDequeue();
  //     std::cout << "\n";
  //   }
  // }

  // for (int i = 0; i < 3; ++i) {
  //   maxElements = elements[i];
  //   float avg = static_cast<float>(elements2timespan[maxElements][0])
  //   / 10.0f; std::cout << maxElements
  //             << " elements insert concurrently, average timespan=" << avg
  //             << "ms"
  //             << "\n";
  //   avg = static_cast<float>(elements2timespan[maxElements][1]) / 10.0f;
  //   std::cout << maxElements
  //             << " elements delete concurrently, average timespan=" << avg
  //             << "ms"
  //             << "\n";
  //   avg = static_cast<float>(elements2timespan[maxElements][2]) / 10.0f;
  //   std::cout << maxElements
  //             << " elements insert and delete concurrently, average
  //             timespan="
  //             << avg << "ms"
  //             << "\n";
  //   std::cout << "\n";
  // }

  // TestCorrectness();

  for (;;) {
    std::atomic<bool> sync = false;

    LockFreeLinkedList<int> ll;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&] {
        while (!sync) {
          std::this_thread::yield();
        }

        for (int i = 0; i < 10; ++i) {
          ll.Insert(i);
        }
      });
    }

    sync = true;

    for (auto& t : threads) {
      t.join();
    }

    threads.clear();
    assert(ll.size() == 10);

    sync = false;

    for (int i = 0; i < 8; ++i) {
      threads.emplace_back([&] {
        while (!sync) {
          std::this_thread::yield();
        }

        for (int i = 0; i < 10; ++i) {
          ll.Delete(i);
        }
      });
    }

    sync = true;

    for (auto& t : threads) {
      t.join();
    }

    ll.Dump();
    assert(ll.size() == 0);
  }

  return 0;
}