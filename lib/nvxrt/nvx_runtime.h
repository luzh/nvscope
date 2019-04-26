#ifndef _NVX_RUNTIME_H
#define _NVX_RUNTIME_H

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "debug.h"
#include "nvx/config.h"

namespace __nvx {

using byte_t = uint8_t; /* TODO: Consider std::byte? */
using DirtyRanges = std::vector<std::pair<uintptr_t, uintptr_t>>;

static const int INIT_PRIO{0}; // __nvx_init priority (runs before main)
static const int FINI_PRIO{0}; // __nvx_fini priority (runs after main)
/**
 * If a store's data size is less than or equal to STBUF_INTERNAL_SIZE, it
 * resides inside StoreInfo. Otherwise, StoreInfo allocates a StoreData to hold
 * the store's data.
 */
static const uint32_t STBUF_INTERNAL_SIZE{32};
/**
 * Content saved in StoreData becomes persistent due to cache line flushes or
 * write-backs. But incomplete flushes may break a StoreData into small pieces.
 * A dirty store splits off from a StoreData if
 * StoreData._size >= STBUF_SPLIT_THRESHOLD and the dirty store size is less
 * than or equal to (StoreData._size >> STBUF_SPTH_RATIO_SHIFT).
 *
 * A split store may become a new StoreData or internalized by StoreInfo,
 * depending on the store's size.
 */
static const uint32_t STBUF_SPLIT_THRESHOLD{4096};
static const uint32_t STBUF_SPTH_SHIFT{3}; // one eigth of STBUF_SPLIT_THRESHOLD

static uintptr_t cache_addr_of(const uintptr_t addr) { return addr & ~63UL; }

struct RangeInfo {
  RangeInfo(uintptr_t start, uintptr_t end, char *func, char *file, int linenr)
      : _func(func), _file(file), _linenr(linenr), _start(start), _end(end) {}
  char *_func;
  char *_file;
  int _linenr;
  uintptr_t _start; // user's mmap start address
  uintptr_t _end;   // one byte after user's mmap end address
};

struct StoreData {
  size_t _size;
  size_t _spth;
  byte_t *_data;

  StoreData(uintptr_t start, uintptr_t end)
      : _size(start < end ? end - start : 0),
        /* A zero value of _spth prevents splitting. */
        _spth(_size < STBUF_SPLIT_THRESHOLD ? 0 : _size >> STBUF_SPTH_SHIFT),
        _data(_size ? new byte_t[_size] : nullptr) {

    static_assert(sizeof(byte_t) == 1);

    if (!_size) {
      ERRF("NVX-RT: Invalid data size!");
      exit(EXIT_FAILURE);
    }
    if (!_data) {
      ERRF("NVX-RT: Invalid data buffer!");
      exit(EXIT_FAILURE);
    }

    /* TODO: Consider std::copy()? */
    std::memcpy(_data, reinterpret_cast<void *>(start), _size);
    DBGF(cBRN "NVX-RT: StoreData for size %zu constructed, split threshold "
              "%zu" cRST,
         _size, _spth);
  }

  /* no copy construction */
  StoreData(const StoreData &other) = delete;

  ~StoreData() {
    delete[] _data;
    DBGF(cGRN "NVX-RT: StoreData for size %zu destructed" cRST, _size);
  }
};

struct StoreInfo {
  StoreInfo(uint64_t time, uintptr_t start, uintptr_t end, char *func,
            char *file, int linenr)
      : _func(func), _file(file), _linenr(linenr), _time(time), _start(start),
        _end(end), _offset(0), _extbuf(make_snapshot(start, end)) {

    static_assert(STBUF_INTERNAL_SIZE < CACHELINE_SIZE);
    static_assert(STBUF_INTERNAL_SIZE < STBUF_SPLIT_THRESHOLD);
  }

  char *_func;
  char *_file;
  int _linenr;
  uint64_t _time;   // logical timestamp
  uintptr_t _start; // pmem address starting this store
  uintptr_t _end;   // pmem address ending this store (one byte off)
  size_t _offset;   // byte offset relative to _intbuf or _extbuf._data
  byte_t _intbuf[STBUF_INTERNAL_SIZE];
  std::shared_ptr<StoreData> _extbuf;

  std::shared_ptr<StoreData> make_snapshot(uintptr_t start, uintptr_t end) {
    if (STBUF_INTERNAL_SIZE < end - start) {
      return std::make_shared<StoreData>(start, end);
    }

    std::memcpy(_intbuf, reinterpret_cast<void *>(start), end - start);
    return nullptr;
  }

  /* Print byte content. */
  void PrintStoreData(size_t bytes);
  /* Swap data between [_start, _end) and this store's data buffer. */
  void SwapData();
  /* Resize this store's data buffer (consequence of partial flushing). */
  void ResizeStoreData(uintptr_t start, uintptr_t end);
};

struct CLfwbInfo {
  CLfwbInfo(uint64_t time, uintptr_t addr, char *func, char *file, int linenr)
      : _func(func), _file(file), _linenr(linenr), _time(time), _addr(addr),
        _start(cache_addr_of(addr)),
        _end(cache_addr_of(addr) + CACHELINE_SIZE) {}
  char *_func;
  char *_file;
  int _linenr;
  uint64_t _time;
  uintptr_t _addr;  // user-provided address of this cache line op
  uintptr_t _start; // pmem cache-line address for _addr
  uintptr_t _end;   // pmem cache-line address + CACHELINE_SIZE for _addr

  // bool operator<(const CLfwbInfo &other) {
  //  if (_claddr != other._claddr)
  //    return _claddr < other._claddr;
  //  return _time < other._time;
  //};
};

class NVXRuntime {
  /* TODO: Many methods are not safe, e.g. _tgconfig may be nullptr. */
public:
  /* Check if NVX-RT is enabled. */
  bool Enabled() const { return _tgconfig->enabled; }

  /* Set target process PID. */
  void SetTargetPid(pid_t pid) { _tgconfig->pid = pid; }
  /* Set target process status. */
  void SetTargetStatus(int st) { _tgconfig->status = st; }

  /* Communication methods. */
  enum nvx_message ReadMessage() const;
  void SendMessage(enum nvx_message msg) const;
  void SendAnyData(void *data, ssize_t len) const;
  void CloseChannels() const;

  uint64_t CurrentEpoch() { return ++_epoch; };

  /* Add one mmaped range. */
  void SaveRange(uintptr_t addr, size_t size, char *func, char *file, int line);
  /* Check if the stored data falls into mmaped ranges. */
  bool StoreInRange(void *ptr, size_t size);
  /* Save store information. */
  void SaveStore(uintptr_t addr, size_t size, char *func, char *file, int line);
  /* Save CLFLUSH(OPT) or CLWB operations. */
  void SaveCLfwb(uintptr_t addr, char *func, char *file, int line);

  /* Fill unflushed store ranges and save them in dirty_ranges. */
  void FindDirtyRanges(StoreInfo &store, DirtyRanges &dirty_ranges);

  /* Generate the next test case. */
  bool NextReorder();
  /* Perform analysis for insights. */
  void CheckReorder(uint64_t epoch, char *func, char *file, int line);
  void CheckDirtyStores(uint64_t epoch, char *func, char *file, int line);
  void CheckMissingFence(uint64_t epoch, char *func, char *file, int line);

  /* Print content of a StoreInfo vector (up to limit entries). */
  void PrintStoreInfoVec(std::vector<StoreInfo> &stores, size_t nstores,
                         size_t bytes) const;

  NVXRuntime(void *_shm, struct nvx_target_config *tgconf)
      : _shm_base(_shm), _tgconfig(tgconf), _time(0), _epoch(0) {
    if (_shm_base) {
      OKF("NVX-RT: NVX runtime constructed");
    } else {
      ERRF("NVX-RT: invalid shared memory address");
      exit(NVX_EXIT_BAD_SHM);
    }
  }

  ~NVXRuntime() {
    if (Enabled()) {
      int linenr = 0;
      char *file = const_cast<char *>("Program");
      char *func = const_cast<char *>("Program exit");
      uint64_t epoch = _epoch;
      /*
       * TODO: It may not be safe to perform reordering tests at this point
       * because the mapped memory could be already unmapped. We may instrument
       * munmap() calls, or make reordering tests independent of the previous
       * mmaped region.
       *
       * CheckReorder(epoch, func, file, linenr);
       */
      CheckMissingFence(epoch, func, file, linenr);
      CheckDirtyStores(epoch, func, file, linenr);
    }

    OKF("NVX-RT: NVX runtime destructed");
  }

private:
  void *_shm_base;
  struct nvx_target_config *_tgconfig;

  /* timestamp, shared between threads */
  std::atomic_uint64_t _time;
  /* epoch id, shared between threads */
  std::atomic_uint64_t _epoch;

  /**
   * TODO: Using a vector for _nvranges assumes there are only few mappings
   * (less than 10), where a linear search is good enough. But if there are tens
   * or hundreds of mappings we should use a hash map.
   */
  std::vector<RangeInfo> _nvranges;
  std::vector<StoreInfo> _nvstores;
  std::vector<CLfwbInfo> _nvclfwbs;
  std::vector<StoreInfo> _dirty_stores;
}; // class NVXRuntime

} // namespace __nvx

#endif // _NVX_RUNTIME_H
