#include "nvx_runtime.h"

namespace __nvx {

void StoreInfo::PrintStoreData(size_t bytes) {
  if (!_extbuf)
    assert(_end - _start <= STBUF_INTERNAL_SIZE);
  else
    assert(_end - _start > STBUF_INTERNAL_SIZE);

  auto lineaddr = _start & ~(16UL - 1);
  auto skip = _start - lineaddr;

  auto size = bytes > 0 && bytes < _end - _start ? bytes : _end - _start;

  auto print_bytes = [size, skip](uintptr_t lineaddr, const byte_t *data,
                                  const std::string &title) {
    SAYF(cBLU " %15s | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n" cRST,
         title.c_str());
    if (skip > 0) {
      SAYF("  %p |", reinterpret_cast<void *>(lineaddr));
      for (size_t i = 0; i < skip; ++i) {
        SAYF("   ");
      }
      lineaddr += 16;
    }
    for (size_t i = skip; i < size + skip; ++i) {
      if (i % 16 == 0) {
        SAYF("  %p |", reinterpret_cast<void *>(lineaddr));
        lineaddr += 16;
      }
      SAYF(" %02X%s", data[i - skip] & 0xFFU, ((i + 1) % 16 ? "" : "\n"));
    }

    if ((size + skip) % 16 != 0)
      SAYF("\n");
  };

  auto olddata = _extbuf ? _extbuf->_data + _offset : _intbuf + _offset;
  print_bytes(lineaddr, olddata, "Snapshot Bytes");

  auto newdata = reinterpret_cast<byte_t *>(_start);
  print_bytes(lineaddr, newdata, "Current Bytes");

  SAYF("-----------------------------------------------------------------\n");
}

void StoreInfo::SwapData() {
  auto *start = reinterpret_cast<byte_t *>(_start); // pmem start address
  auto *end = reinterpret_cast<byte_t *>(_end);     // pmem end address
  auto *bufdata = _extbuf ? _extbuf->_data + _offset : _intbuf + _offset;
  /**
   * TODO: std::swap_ranges() seems to work at granularity determined by the
   * iterator, so for byte_t* iterators it swaps byte-by-byte. We will need
   * a more efficient swapping method.
   */
  std::swap_ranges(start, end, bufdata);
}

void StoreInfo::ResizeStoreData(uintptr_t start, uintptr_t end) {
  assert(_start <= start && start < end && end <= _end);

  if (_extbuf) {
    size_t dirty_size = end - start;
    if (dirty_size <= STBUF_INTERNAL_SIZE) {
      void *src = _extbuf->_data + _offset + (start - _start);
      std::memcpy(_intbuf, src, end - start);
      _offset = 0;
      _extbuf = nullptr;
      DBGF(cBRN
           "NVX-RT: [%p +: %02zu) Internalize an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    } else if (dirty_size <= _extbuf->_spth) {
      void *src = _extbuf->_data + _offset + (start - _start);
      auto xstart = reinterpret_cast<uintptr_t>(src);
      auto xend = xstart + end - start;
      _offset = 0;
      _extbuf = make_snapshot(xstart, xend);
      DBGF(cBRN
           "NVX-RT: [%p +: %02zu) Splits from an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    } else {
      /* Update offset into the existing store buffer without splitting. */
      _offset += start - _start;
      DBGF(cBRN "NVX-RT: [%p +: %02zu) Reuse an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    }
  } else {
    _offset += start - _start;
    DBGF(cBRN "NVX-RT: [%p +: %02zu) Reuse an internal store buffer" cRST,
         reinterpret_cast<void *>(start), end - start);
  }

  _start = start;
  _end = end;
}

/*--------------------- End of StoreInfo Implementation ---------------------*/

uint64_t NVXRuntime::GetThreadID() { // This function should be lock-free.
  // tid equals MAX_THREADS means the current thread is not registered.
  // tid > MAX_THREADS means the current thread is registered but it's beyond
  // our predefined thread count threshold.
  static thread_local uint64_t tid{MAX_THREADS};

  if (tid == MAX_THREADS) {
    if (_nthreads > MAX_THREADS) {
      // TODO: Now we just ignore threads beyond MAX_THREADS. Perhaps NVX can
      // make an educated guess about what other thread to evict at this time.
      tid = _nthreads;
    } else {
      // Value of _nthreads can go over MAX_THREADS if more than that amount of
      // threads concurrently reach here.
      tid = _nthreads++;
    }
  }

  return tid;
}

void NVXRuntime::SaveRange(uintptr_t addr, size_t size, char *func, char *file,
                           int line) {
  _nvranges.emplace_back(addr, addr + size, func, file, line);
}

bool NVXRuntime::StoreInRange(void *ptr, size_t size) {
  auto start = reinterpret_cast<uintptr_t>(ptr);
  auto end = start + size;

  return std::any_of(_nvranges.begin(), _nvranges.end(),
                     [start, end](auto &range) {
                       return range._start <= start && end < range._end;
                     });
}

enum nvx_message NVXRuntime::ReadMessage() const {
  enum nvx_message msg;
  if (read(_tgconfig->read_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVX-RT: read() from fd %d failed", _tgconfig->read_fd);
    exit(EXIT_FAILURE);
  }
  return msg;
}

void NVXRuntime::SendMessage(enum nvx_message msg) const {
  if (write(_tgconfig->write_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVX-RT: write() to fd %d failed", _tgconfig->write_fd);
    exit(EXIT_FAILURE);
  }
}

void NVXRuntime::SendAnyData(void *data, ssize_t len) const {
  if (write(_tgconfig->write_fd, data, len) != len) {
    ERRF("NVX-RT: write() to fd %d failed", _tgconfig->write_fd);
    exit(EXIT_FAILURE);
  }
}

void NVXRuntime::CloseChannels() const {
  if (_tgconfig->read_fd > 0)
    close(_tgconfig->read_fd);
  if (_tgconfig->write_fd > 0)
    close(_tgconfig->write_fd);
}

void NVXRuntime::SaveStore(uintptr_t addr, size_t size, char *func, char *file,
                           int line) {
  uint64_t tid = GetThreadID();
  if (tid >= MAX_THREADS)
    return;

  uint64_t time = ++_time;

  assert(tid < MAX_THREADS);
  std::shared_lock savelock(_nvxlock);
  auto &nvstore = _nvstores[tid];
  nvstore.emplace_back(tid, time, addr, addr + size, func, file, line);
}

void NVXRuntime::SaveCLfwb(uintptr_t addr, char *func, char *file, int line) {
  uint64_t tid = GetThreadID();
  if (tid >= MAX_THREADS)
    return;

  uint64_t time = ++_time;

  assert(tid < MAX_THREADS);
  std::shared_lock savelock(_nvxlock);
  auto &nvclfwb = _nvclfwbs[tid];
  nvclfwb.emplace_back(tid, time, addr, func, file, line);
}

void NVXRuntime::PrintStoreInfoVec(std::vector<StoreInfo> &stores,
                                   size_t nstores, size_t bytes = 0) const {
  size_t count = 0;

  for (auto &store : stores) {
    DBGF("StoreInfo[%zu]: [%s() at %s: %d] thread #%zu start from %p size %zu",
         count, store._func, store._file, store._linenr, store._tid,
         reinterpret_cast<void *>(store._start), store._end - store._start);
    store.PrintStoreData(bytes);
    if (0 < nstores && nstores <= ++count)
      break;
  }
}

void NVXRuntime::FindDirtyRanges(StoreInfo &store,
                                 std::vector<CLfwbInfo> &nvclfwbs,
                                 DirtyRanges &dirty_ranges) {
  /* initially the full range is dirty */
  uintptr_t dirty_start = store._start;
  /**
   * NOTE: This algorithm works only if _nvclfwbs is already sorted by _start
   * or by _time if _start equals.
   */
  for (auto &clfwb : nvclfwbs) {
    assert(clfwb._time != store._time);
    if (store._end <= clfwb._start)
      break; // remaining clfwbs can be skipped
    if (clfwb._time < store._time || clfwb._end <= store._start)
      continue;
    if (dirty_start < clfwb._start) {
      dirty_ranges.emplace_back(dirty_start, clfwb._start);
    }
    dirty_start = clfwb._end;
  }
  if (dirty_start < store._end) {
    dirty_ranges.emplace_back(dirty_start, store._end);
  }
}

bool NVXRuntime::NextReorder(std::vector<StoreInfo> &nvstores) {
  static size_t caseid = 0;

  if (caseid == 0) {
    TESTC("NVX-RT: make test case #%zu: crash after sfence", caseid);
    caseid++;
    return true;
  }

  if (caseid > 1) {
    StoreInfo &store = nvstores[caseid - 2];
    store.SwapData();

    TESTC("NVX-RT: pass over test case #%zu: redo store to %p size %zu",
          caseid - 1, reinterpret_cast<void *>(store._start),
          store._end - store._start);
  }

  if (nvstores.size() < caseid) {
    caseid = 0;
    return false;
  }

  StoreInfo &store = nvstores[caseid - 1];
  store.SwapData();

  TESTC("NVX-RT: make test case #%zu: undo store to %p size %zu", caseid,
        reinterpret_cast<void *>(store._start), store._end - store._start);
  caseid++;

  return true;
}

void NVXRuntime::CheckReorder(uint64_t epoch, std::vector<StoreInfo> &nvstores,
                              char *func, char *file, int line) {
  /* TODO: Consider reverting all _dirty_stores. */
  if (nvstores.empty())
    return;

#ifdef NVX_DEBUG
  DBGF(cCYA "NVX-RT: collected stores in epoch #%zu >>>>>" cRST, epoch);
  PrintStoreInfoVec(nvstores, nvstores.size(), 32);
  DBGF(cCYA "NVX-RT: collected stores in epoch #%zu <<<<<" cRST, epoch);
#endif

  DBGF("NVX-RT: reordering stores at sfence #%zu [%s() at %s: %d]", epoch, func,
       file, line);

  while (NextReorder(nvstores)) {
    SendMessage(MSG_AWAITING_CHECK);

    enum nvx_message command = ReadMessage();

    if (command == MSG_SHOW_BUG_AND_CONTINUE ||
        command == MSG_SHOW_BUG_AND_EXIT) {
      SAYF("\n" cLRD "[-] Store Races:" cRST
           " in epoch #%zu [%s() at %s: %d]\n",
           epoch, func, file, line);
      ERRF("NVX-RT needs a patch to report details of this store race due to "
           "possible missing sfences.\n");

      if (command == MSG_SHOW_BUG_AND_EXIT)
        exit(NVX_EXIT_FOUNDBUG);

    } else if (command != MSG_CONTINUE_TO_RUN) {
      ERRF("NVX-RT: received inappropriate message %d", command);
      exit(NVX_EXIT_BAD_MSG);
    }
  }
}

void NVXRuntime::CheckDirtyStores(uint64_t epoch,
                                  std::vector<StoreInfo> &nvstores,
                                  std::vector<CLfwbInfo> &nvclfwbs, char *func,
                                  char *file, int line) {
  if (nvstores.empty() && _dirty_stores.empty()) {
    // TODO: Should report redundant flushes before clearing _nvclfwbs.
    // _nvclfwbs.clear();
    return;
  }

  /* Sort by cache-line address, or timestamp if that equals. */
  std::sort(nvclfwbs.begin(), nvclfwbs.end(),
            [](const CLfwbInfo &lhs, const CLfwbInfo &rhs) {
              if (lhs._start != rhs._start)
                return lhs._start < rhs._start;
              return lhs._time > rhs._time;
            });

  DirtyRanges dirty_ranges;

  std::vector<StoreInfo> new_dirty_stores;
  for (auto sti = _dirty_stores.begin(); sti != _dirty_stores.end();) {
    dirty_ranges.clear();
    FindDirtyRanges(*sti, nvclfwbs, dirty_ranges);
    /**
     * TODO: Previous calls of check_dirty_stores() should have reported
     * StoreInfo stored in _dirty_stores, so do not report it again here. But
     * probably we can report changed dirty ranges.
     */
    for (auto dti = dirty_ranges.begin(); dti != dirty_ranges.end(); ++dti) {
      /**
       * If store data is internally saved, its size must be no larger than
       * STBUF_INTERNAL_SIZE (by definition no more than a cache line size).
       * Thus, if a cache flush/write-back (at lease a cache line size) does not
       * clear this internal store buffer, only ONE part of it can remain dirty
       * (either at head or tail). It should never happen that a cache operation
       * can break an internal store buffer into more than one dirty ranges.
       */
      assert(sti->_extbuf || dirty_ranges.size() == 1);

      /**
       * If a StoreInfo of _dirty_stores is fragmented due to partial flushing,
       * we inplace-update the existing StoreInfo object in _dirty_stores for
       * the last member of dirty_ranges, and append new StoreInfo objects to
       * the end of _dirty_stores.
       */
      if (dti == dirty_ranges.end() - 1) {
        sti->ResizeStoreData(dti->first, dti->second);
      } else {
        new_dirty_stores.push_back(*sti);
        new_dirty_stores.back().ResizeStoreData(dti->first, dti->second);
      }
    }

    sti = dirty_ranges.empty() ? _dirty_stores.erase(sti) : sti + 1;
  }

  /**
   * TODO: Create StoreInfo objects in new_dirty_stores and then insert them
   * into _dirty_stores cause extra copy of these objects. Perhaps there is a
   * more efficient way to do this task.
   */
  _dirty_stores.insert(_dirty_stores.end(), new_dirty_stores.begin(),
                       new_dirty_stores.end());

  bool report = false;
  for (auto sti = nvstores.begin(); sti != nvstores.end(); ++sti) {
    dirty_ranges.clear();
    FindDirtyRanges(*sti, nvclfwbs, dirty_ranges);

    if (!dirty_ranges.empty()) {
      report = true;
      SAYF("\n" cLRD "[-] Dirty Stores:" cRST
           " in epoch #%zu [%s() at %s: %d]\n",
           epoch, func, file, line);
      ERRF("store size %zu made by [%s() at %s: %d] has unflushed ranges:",
           sti->_end - sti->_start, sti->_func, sti->_file, sti->_linenr);
    }

    for (auto dti = dirty_ranges.begin(); dti != dirty_ranges.end(); ++dti) {
      assert(sti->_extbuf || dirty_ranges.size() == 1);

      SAYF("    [%p, %p)\n", reinterpret_cast<void *>(dti->first),
           reinterpret_cast<void *>(dti->second));

      _dirty_stores.push_back(*sti);
      StoreInfo &stx = _dirty_stores.back();
      stx.ResizeStoreData(dti->first, dti->second);
    }
  }
  SAYF("%s", report ? "\n" : "");

  // Vector _dirty_stores will track any unflushed stored ranges.
  // _nvclfwbs.clear();
  // _nvstores.clear();
}

void NVXRuntime::CheckMissingFence(uint64_t epoch, char *func, char *file,
                                   int line) {
  // If _dirty_stores is not empty, its content should have been reported so we
  // we do not warn it again.
  // TODO: This logic assumes this function only runs when the target program
  // exits. But this may not be accurate if this function is called before
  // the target program exits.

  bool missing = false;
  size_t nthreads = _nthreads;
  nthreads = (nthreads < MAX_THREADS) ? nthreads : MAX_THREADS;
  for (size_t tid = 0; tid < nthreads; ++tid) {
    if (!_nvstores[tid].empty()) {
      missing = true;
      break;
    }
  }

  if (!missing)
    return;

  SAYF("\n" cLRD "[-] Missing SFence:" cRST " in epoch #%zu [%s() at %s: %d]\n",
       epoch, func, file, line);

  ERRF("Probably an sfence is missing because there are pending stores that "
       "cannot be guaranteed persistent.\n");

  /* Do not print dirty stores here. Let the caller call CheckDirtyStores. */
}

void NVXRuntime::Check(uint64_t epoch, uint32_t flags, char *func, char *file,
                       int line) {
  // We do not check thread id here. Any thread encounters a checkpoint (sfence)
  // can trigger a check action.

  std::unique_lock checklock(_nvxlock);

  if (flags & kCheckMissingFence) {
    CheckMissingFence(epoch, func, file, line);
  }

  if (flags & (kCheckReorder | kCheckDirtyStores)) {
    std::vector<StoreInfo> nvstores;

    // TODO: This copying method may become very expensive. We may use _nvstores
    // and _nvclfwbs without copying them.
    size_t nthreads = _nthreads;
    nthreads = (nthreads < MAX_THREADS) ? nthreads : MAX_THREADS;
    DBGF(cBRN "NVX-RT: collected information from %zu thread(s)" cRST,
         nthreads);

    for (size_t tid = 0; tid < nthreads; ++tid) {
      auto &stores = _nvstores[tid];
      nvstores.insert(nvstores.end(), stores.begin(), stores.end());
      stores.clear();
    }

    if (flags & kCheckReorder) {
      CheckReorder(epoch, nvstores, func, file, line);
    }

    if (flags & kCheckDirtyStores) {
      std::vector<CLfwbInfo> nvclfwbs;

      for (size_t tid = 0; tid < nthreads; ++tid) {
        auto &clfwbs = _nvclfwbs[tid];
        nvclfwbs.insert(nvclfwbs.end(), clfwbs.begin(), clfwbs.end());
        clfwbs.clear();
      }

      CheckDirtyStores(epoch, nvstores, nvclfwbs, func, file, line);
    }
  }
}

} // namespace __nvx
