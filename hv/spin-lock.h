#pragma once

#include <intrin.h>

namespace hv {

// minimalistic spin lock class
struct spin_lock {
  void initialize() {
    lock = 0;
  }

  void acquire() {
    while (1 == _InterlockedCompareExchange(&lock, 1, 0))
      _mm_pause();
  }

  void release() {
    lock = 0;
  }

  volatile long lock;
};

class scoped_spin_lock {
public:
  scoped_spin_lock(spin_lock& lock)
      : lock_(lock) {
    lock.acquire();
  }

  ~scoped_spin_lock() {
    lock_.release();
  }

  // no copying
  scoped_spin_lock(scoped_spin_lock const&) = delete;
  scoped_spin_lock& operator=(scoped_spin_lock const&) = delete;

private:
  spin_lock& lock_;
};

} // namespace hv

