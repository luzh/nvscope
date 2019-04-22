#ifndef _NVSRT_H
#define _NVSRT_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "afl/config.h"
#include "debug.h"
#include "headers.h"
#include "nvscope/config.h"

/* TODO: Consider std::byte? */
using byte_t = uint8_t;
using DirtyRanges = std::vector<std::pair<uintptr_t, uintptr_t>>;

enum CLOPType { CLFLUSH = 0, CLFLUSHOPT, CLWB };

static const int NVS_INIT_PRIO{0}; // __nvs_init priority (runs before main)
static const int NVS_FINI_PRIO{0}; // __nvs_init priority (runs after main)
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
static const uint32_t STBUF_SPTH_SHIFT{8};

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
      ERRF("NVS-RT: Invalid data size!");
      _exit(EXIT_FAILURE);
    }
    if (!_data) {
      ERRF("NVS-RT: Invalid data buffer!");
      _exit(EXIT_FAILURE);
    }

    /* TODO: Consider std::copy()? */
    std::memcpy(_data, reinterpret_cast<void *>(start), _size);
    DBGF(cBRN "NVS-RT: StoreData for size %zu constructed, split threshold "
              "%zu" cRST,
         _size, _spth);
  }

  /* no copy construction */
  StoreData(const StoreData &other) = delete;

  ~StoreData() {
    delete[] _data;
    DBGF(cGRN "NVS-RT: StoreData for size %zu destructed" cRST, _size);
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

  void resize_store_data(uintptr_t start, uintptr_t end);

  void swap_data();
};

struct CLOPInfo {
  CLOPInfo(uint64_t time, uintptr_t addr, CLOPType type, char *func, char *file,
           int linenr)
      : _func(func), _file(file), _linenr(linenr), _time(time), _addr(addr),
        _start(cache_addr_of(addr)), _end(cache_addr_of(addr) + CACHELINE_SIZE),
        _type(type) {}
  char *_func;
  char *_file;
  int _linenr;
  uint64_t _time;
  uintptr_t _addr;  // user-provided address of this cache line op
  uintptr_t _start; // pmem cache-line address for _addr
  uintptr_t _end;   // pmem cache-line address + CACHELINE_SIZE for _addr
  CLOPType _type;

  // bool operator<(const CLOPInfo &other) {
  //  if (_claddr != other._claddr)
  //    return _claddr < other._claddr;
  //  return _time < other._time;
  //};
};

class NVScopeRT {
  /* TODO: Many methods are not safe, e.g. _tgconfig may be nullptr. */
public:
  /* Check if NVS-RT is enabled. */
  bool is_enabled() const { return _tgconfig->enabled; }

  /* Set target process PID. */
  void set_target_pid(pid_t pid) { _tgconfig->pid = pid; }
  /* Set target process status. */
  void set_target_status(int st) { _tgconfig->status = st; }

  /* Communication methods. */
  enum nvs_message read_message() const;
  void send_message(enum nvs_message msg) const;
  // void send_anydata(void *data, ssize_t len) const;
  void close_channels() const;

  /* Add one mmaped range. */
  void save_range(uintptr_t addr, size_t size, char *func, char *file,
                  int line);
  /* Check if the stored data falls into mmaped ranges. */
  bool store_in_range(void *ptr, size_t size);
  /* Save store information. */
  void save_store(uintptr_t addr, size_t size, char *func, char *file,
                  int line);
  /* Save CLFLUSHOPT or CLWB operations. */
  void save_clop(uintptr_t addr, CLOPType type, char *func, char *file,
                 int line);

  /* Fill unflushed store ranges and save them in dirty_ranges. */
  void find_dirty_ranges(StoreInfo &store, DirtyRanges &dirty_ranges);

  /* Generate the next test case. */
  bool next_reorder();
  /* Perform analysis for insights. */
  void check_reorder(uint64_t epoch, char *func, char *file, int line);
  void check_dirty_stores(uint64_t epoch, char *func, char *file, int line);
  void check_missing_fence(uint64_t epoch, char *func, char *file, int line);

#ifdef NVS_DEBUG
  /* Print content of nvstores (up to limit entries). */
  void print_nvstores(size_t limit) const;
#endif

  NVScopeRT(void *_shm, struct nvs_target_config *tgconf)
      : _shm_base(_shm), _tgconfig(tgconf) {
    if (_shm_base) {
      OKF("NVS-RT: nvscope run-time constructed");
    } else {
      ERRF("NVS-RT: invalid shared memory address");
      _exit(NVS_EXIT_BAD_SHM);
    }
  }

  /**
   * If NVScopeRT is constructed in the forkserver process, its destructor will
   * only run when the forkserver exits.
   */
  ~NVScopeRT() = default;

private:
  void *_shm_base;
  struct nvs_target_config *_tgconfig;

  /**
   * TODO: Using a vector for _nvranges assumes there are only few mappings
   * (less than 10), where a linear search is good enough. But if there are tens
   * or hundreds of mappings we should use a hash map.
   */
  std::vector<RangeInfo> _nvranges;
  std::vector<StoreInfo> _nvstores;
  std::vector<CLOPInfo> _nvclops;
  std::vector<StoreInfo> _dirty_stores;
};

#endif // _NVSRT_H
