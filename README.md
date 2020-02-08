# LockFreeLinkedList
A lock free linked list implemented in c++20 based on Harris-SetBasedList and Michael-HazardPointer.
## Feature
  * C++20 implementation.
  * Full Thread-safe and Lock-free.
  * ABA safe.
  * Set implemented through single ordered linked list[1].
  * Use Hazard pointer to manage memory[2].
  * Support Multi-producer & Multi-consumer.
## Benchmark

  Magnitude     | Insert      | Delete      | Insert & Delete|
  :-----------  | :-----------| :-----------| :-----------------
  1K            | 1.2ms       | 0ms         | 3.6ms
  10K           | 147.1ms     | 18.9ms      | 293.5ms
  100K          | 15064.4ms   | 1647ms      | 27176ms
  
The above data was tested on my 2013 macbook-pro with Intel Core i7 4 cores 2.3 GHz.

The data of first and second column was obtained by starting 8 threads to insert concurrently and delete concurrently, the data of third column was obtained by starting 4 threads to insert and 4 threads to delete concurrently, each looped 10 times to calculate the average time consumption.
See also [test](test.cc).
## Build
```
make && ./test
```
## API
```C++
bool Insert(const T& data);
bool Insert(T&& data);
bool Delete(const T& data);
bool Find(const T& data);
size_t size() const;
```
## Reference
[1]A Pragmatic Implementation of Non-BlockingLinked-Lists. Timothy L.Harris\
[2]Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects. Maged M. Michael
