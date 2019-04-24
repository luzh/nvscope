#include "nvsrt.h"

/* epoch id, shared between threads */
static std::atomic_uint64_t epochid{0};
/* event timestamp, shared between threads */
static std::atomic_uint64_t timestamp{0};
/**
 * NVScopeRT handle. If forserver is enabled, the forkserver process will
 * construct nvsrt, and destruct it when the forkserver process exits. Child
 * process will inherit nvsrt via forking, but nvsrt does not destruct when a
 * child process exits. Changes to nvsrt made by a child process all disappear
 * when the child process exits due to copy-on-write of fork().
 */
static std::unique_ptr<NVScopeRT> nvsrt;

void StoreInfo::print_bytes(size_t limit) {
  if (!_extbuf)
    assert(_end - _start <= STBUF_INTERNAL_SIZE);
  else
    assert(_end - _start > STBUF_INTERNAL_SIZE);

  auto data = _extbuf ? _extbuf->_data + _offset : _intbuf + _offset;
  auto size = limit > 0 && limit < _end - _start ? limit : _end - _start;

  auto lineaddr = _start & ~(16UL - 1);
  auto skip = _start - lineaddr;

  SAYF(cBLU "%17s 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n" cRST, "|");
  SAYF("-----------------------------------------------------------------\n");

  if (skip > 0) {
    SAYF(" %p |", reinterpret_cast<void *>(lineaddr));
    for (size_t i = 0; i < skip; ++i) {
      SAYF("   ");
    }
    lineaddr += 16;
  }

  for (size_t i = skip; i < size + skip; ++i) {
    if (i % 16 == 0) {
      SAYF(" %p |", reinterpret_cast<void *>(lineaddr));
      lineaddr += 16;
    }
    SAYF(" %02x%s", data[i - skip] & 0xFFU, ((i + 1) % 16 ? "" : "\n"));
  }

  if ((size + skip) % 16 != 0)
    SAYF("\n");
}

void StoreInfo::swap_data() {
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

void StoreInfo::resize_store_data(uintptr_t start, uintptr_t end) {
  assert(_start <= start && start < end && end <= _end);

  if (_extbuf) {
    size_t dirty_size = end - start;
    if (dirty_size <= STBUF_INTERNAL_SIZE) {
      void *src = _extbuf->_data + _offset + (start - _start);
      std::memcpy(_intbuf, src, end - start);
      _offset = 0;
      _extbuf = nullptr;
      DBGF(cBRN
           "NVS-RT: [%p +: %02zu) Internalize an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    } else if (dirty_size <= _extbuf->_spth) {
      void *src = _extbuf->_data + _offset + (start - _start);
      auto xstart = reinterpret_cast<uintptr_t>(src);
      auto xend = xstart + end - start;
      _offset = 0;
      _extbuf = make_snapshot(xstart, xend);
      DBGF(cBRN
           "NVS-RT: [%p +: %02zu) Splits from an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    } else {
      /* Update offset into the existing store buffer without splitting. */
      _offset += start - _start;
      DBGF(cBRN "NVS-RT: [%p +: %02zu) Reuse an external store buffer" cRST,
           reinterpret_cast<void *>(start), end - start);
    }
  } else {
    _offset += start - _start;
    DBGF(cBRN "NVS-RT: [%p +: %02zu) Reuse an internal store buffer" cRST,
         reinterpret_cast<void *>(start), end - start);
  }

  _start = start;
  _end = end;
}

/*--------------------- End of StoreInfo Implementation ---------------------*/

void NVScopeRT::save_range(uintptr_t addr, size_t size, char *func, char *file,
                           int line) {
  _nvranges.emplace_back(addr, addr + size, func, file, line);
}

bool NVScopeRT::store_in_range(void *ptr, size_t size) {
  auto start = reinterpret_cast<uintptr_t>(ptr);
  auto end = start + size;

  return std::any_of(_nvranges.begin(), _nvranges.end(),
                     [start, end](auto &range) {
                       return range._start <= start && end < range._end;
                     });
}

enum nvs_message NVScopeRT::read_message() const {
  enum nvs_message msg;
  if (read(_tgconfig->read_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVS-RT: read() from fd %d failed", _tgconfig->read_fd);
    _exit(EXIT_FAILURE);
  }
  return msg;
}

void NVScopeRT::send_message(enum nvs_message msg) const {
  if (write(_tgconfig->write_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVS-RT: write() to fd %d failed", _tgconfig->write_fd);
    _exit(EXIT_FAILURE);
  }
}

void NVScopeRT::send_anydata(void *data, ssize_t len) const {
  if (write(_tgconfig->write_fd, data, len) != len) {
    ERRF("NVS-RT: write() to fd %d failed", _tgconfig->write_fd);
    _exit(EXIT_FAILURE);
  }
}

void NVScopeRT::close_channels() const {
  if (_tgconfig->read_fd > 0)
    close(_tgconfig->read_fd);
  if (_tgconfig->write_fd > 0)
    close(_tgconfig->write_fd);
}

void NVScopeRT::save_store(uintptr_t addr, size_t size, char *func, char *file,
                           int line) {
  uint64_t time = ++timestamp;
  _nvstores.emplace_back(time, addr, addr + size, func, file, line);
}

void NVScopeRT::save_clfwb(uintptr_t addr, char *func, char *file, int line) {
  uint64_t time = ++timestamp;
  _nvclfwbs.emplace_back(time, addr, func, file, line);
}

void NVScopeRT::print_stores(std::vector<StoreInfo> &stores, size_t limit,
                             size_t byteslimit = 0) const {
  size_t count = 0;

  for (auto &store : stores) {
    DBGF("StoreInfo[%zu]: [%s() at %s:%4d], start from %p size %zu", count,
         store._func, store._file, store._linenr,
         reinterpret_cast<void *>(store._start), store._end - store._start);
    store.print_bytes(byteslimit);
    if (0 < limit && limit <= ++count)
      break;
  }
}

void NVScopeRT::find_dirty_ranges(StoreInfo &store, DirtyRanges &dirty_ranges) {
  /* initially the full range is dirty */
  uintptr_t dirty_start = store._start;
  /**
   * NOTE: This algorithm works only if _nvclfwbs is already sorted by _start
   * or by _time if _start equals.
   */
  for (auto &clfwb : _nvclfwbs) {
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

bool NVScopeRT::next_reorder() {
  static size_t caseid = 0;

  if (caseid == 0) {
    TESTC("NVS-RT: make test case #%zu: crash after sfence", caseid);
    caseid++;
    return true;
  }

  if (caseid > 1) {
    StoreInfo &store = _nvstores[caseid - 2];
    store.swap_data();

    TESTC("NVS-RT: pass over test case #%zu: redo store to %p size %zu",
          caseid - 1, reinterpret_cast<void *>(store._start),
          store._end - store._start);
  }

  if (_nvstores.size() < caseid) {
    caseid = 0;
    return false;
  }

  StoreInfo &store = _nvstores[caseid - 1];
  store.swap_data();

  TESTC("NVS-RT: make test case #%zu: undo store to %p size %zu", caseid,
        reinterpret_cast<void *>(store._start), store._end - store._start);
  caseid++;

  return true;
}

void NVScopeRT::check_reorder(uint64_t epoch, char *func, char *file,
                              int line) {
  if (_nvstores.empty())
    return;

#ifdef NVS_DEBUG
  DBGF(cCYA "--- NVS-RT collected stores (...) in epoch #%zu ---" cRST, epoch);
  print_stores(_nvstores, _nvstores.size(), 32);
  DBGF(cCYA "--- NVS-RT collected stores (***) in epoch #%zu ---" cRST, epoch);
#endif

  DBGF("NVS-RT: reordering stores at sfence #%zu [%s() at %s:%4d]", epoch, func,
       file, line);

  while (next_reorder()) {
    send_message(MSG_AWAITING_CHECK);

    enum nvs_message command = read_message();

    if (command == MSG_SHOW_BUG_AND_EXIT) {
      SAYF("\n" cLRD "[-] Store Races:" cRST
           " in epoch #%zu [%s() at %s:%4d]\n",
           epoch, func, file, line);
      ERRF("NVS-RT needs a patch to report details of this store race due to "
           "possible missing sfences. ");
      _exit(NVS_EXIT_FOUNDBUG);
    }

    if (command != MSG_CONTINUE_TO_RUN) {
      ERRF("NVS-RT: received inappropriate message %d", command);
      _exit(NVS_EXIT_BAD_MSG);
    }
  }
}

void NVScopeRT::check_dirty_stores(uint64_t epoch, char *func, char *file,
                                   int line) {
  if (_nvstores.empty() && _dirty_stores.empty()) {
    /* TODO: Should report redundant flushes before clearing _nvclfwbs. */
    _nvclfwbs.clear();
    return;
  }

  /* Sort by cache-line address, or timestamp if that equals. */
  std::sort(_nvclfwbs.begin(), _nvclfwbs.end(),
            [](const CLfwbInfo &lhs, const CLfwbInfo &rhs) {
              if (lhs._start != rhs._start)
                return lhs._start < rhs._start;
              return lhs._time > rhs._time;
            });

  DirtyRanges dirty_ranges;

  std::vector<StoreInfo> new_dirty_stores;
  for (auto sti = _dirty_stores.begin(); sti != _dirty_stores.end();) {
    dirty_ranges.clear();
    find_dirty_ranges(*sti, dirty_ranges);
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
        sti->resize_store_data(dti->first, dti->second);
      } else {
        new_dirty_stores.push_back(*sti);
        new_dirty_stores.back().resize_store_data(dti->first, dti->second);
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
  for (auto sti = _nvstores.begin(); sti != _nvstores.end(); ++sti) {
    dirty_ranges.clear();
    find_dirty_ranges(*sti, dirty_ranges);

    if (!dirty_ranges.empty()) {
      report = true;
      SAYF("\n" cLRD "[-] Dirty Stores:" cRST
           " in epoch #%zu [%s() at %s:%4d]\n",
           epoch, func, file, line);
      ERRF("store size %zu made by [%s() at %s:%4d] has unflushed ranges:",
           sti->_end - sti->_start, sti->_func, sti->_file, sti->_linenr);
    }

    for (auto dti = dirty_ranges.begin(); dti != dirty_ranges.end(); ++dti) {
      assert(sti->_extbuf || dirty_ranges.size() == 1);

      SAYF("    [%p, %p)\n", reinterpret_cast<void *>(dti->first),
           reinterpret_cast<void *>(dti->second));

      _dirty_stores.push_back(*sti);
      StoreInfo &stx = _dirty_stores.back();
      stx.resize_store_data(dti->first, dti->second);
    }
  }
  SAYF("%s", report ? "\n" : "");

  /* Vector _dirty_stores will track any unflushed stored ranges. */
  _nvclfwbs.clear();
  _nvstores.clear();
}

void NVScopeRT::check_missing_fence(uint64_t epoch, char *func, char *file,
                                    int line) {
  /* TODO: Also check _dirty_stores. */
  if (_nvstores.empty())
    return;

  SAYF("\n" cLRD "[-] Missing SFence:" cRST " in epoch #%zu [%s() at %s:%4d]\n",
       epoch, func, file, line);

  ERRF("Probably an sfence is missing because there are pending stores that "
       "cannot be guaranteed persistent.");

  /* Do not print dirty stores here. Let the caller call check_dirty_stores. */
}

/*--------------------- End of NVScopeRT Implementation ---------------------*/

/**
 * Shared memory setup
 */
static void __nvs_setup_shm() {
  char *shmid_str = getenv(NVS_ENV_SHM);

  if (shmid_str) {
    uint32_t shmid = strtoul(shmid_str, nullptr, 0);

    void *shm_base = shmat(shmid, nullptr, 0);
    if (shm_base == reinterpret_cast<void *>(-1))
      _exit(NVS_EXIT_BAD_SHM);

    auto *config = (struct nvs_config *)(shm_base);

    /* should be initialized by parent (nvscope) */
    if (!config->initialized) {
      ERRF("NVS-RT: config region not initialized");
      _exit(NVS_EXIT_BAD_SHM);
    }

    struct nvs_target_config *tgconf = nullptr;
    if (config->target_type == TYPE_MAINPROC) {
      tgconf = &config->mainproc;
      OKF("NVS-RT: target mainproc attached to shared memory");
    } else if (config->target_type == TYPE_RECOVERY) {
      tgconf = &config->recovery;
      OKF("NVS-RT: target recovery attached to shared memory");
    } else {
      ERRF("NVS-RT: invalid target type");
      _exit(NVS_EXIT_BAD_CONFIG);
    }

    nvsrt = std::make_unique<NVScopeRT>(shm_base, tgconf);
    if (!nvsrt) {
      ERRF("NVS-RT: creating nvscope run-time failed");
      _exit(NVS_EXIT_BAD_CONFIG);
    }
  } else {
    WARNF("NVS-RT: running instrumented binary but nvscope run-time disabled");
  }
}

/**
 * Forkserver logic (see nvscope.c for the other part)
 */
static void __start_forkserver() {
  /* initial communication with nvscope */
  nvsrt->send_message(MSG_FORKSERVER_HELLO);

  while (true) {
    nvsrt->send_message(MSG_FORKSERVER_READY);

    enum nvs_message command = nvsrt->read_message();

    if (command == MSG_EXIT_FORKSERVER) {
      ACTF("NVS-RT: forkserver received command to exit");
      nvsrt->close_channels();
      _exit(EXIT_SUCCESS);
    }

    if (command != MSG_FORK_AND_RUN) {
      ERRF("NVS-RT: received inappropriate message %d", command);
      _exit(NVS_EXIT_BAD_MSG);
    }

    pid_t cpid = fork();

    /* Check afl-llvm-rt.o.c for persistent mode and using SIGCONT. */
    if (cpid < 0) {
      ERRF("NVS-RT: fork() to run the target program failed");
      _exit(EXIT_FAILURE);
    }

    if (cpid == 0) {
      /**
       * The child process will execute the target program (mainproc, recovery,
       * or checker). It inherits pipes from the forkserver to communicate with
       * nvscope, when the target program runs, there are two writers to the
       * state pipe: the forkserver and the target program. Linux pipes
       * guarantee write atomicity for message sizes no larger than PIPE_BUF.
       * When the target program exits, its pipe ends automatically close.
       *
       * In afl-llvm-rt.o.c, AFL closes the pipe fds because they are not needed
       * anymore. But nvsrt still needs them to communicate with nvscope for
       * testing requests and results.
       *
       * The child process also inherits nvsrt and all modifications to it will
       * be in copy-on-write manner. When the child exits, the forkserver
       * (parent) does not see changes that the child made to nvsrt.
       */

      nvsrt->set_target_pid(getpid());
      nvsrt->send_message(MSG_TARGET_STARTED);

      return; // execute the target progrm, e.g. from main().
    }

    DBGF("NVS-RT: target process started, pid %d", cpid);

    /*
     * DO NOT write to pipe before waitpid() returns. Otherwise races can occur
     * because the target is running and it may write to the same channel.
     */

    int status;
    pid_t cpidw = waitpid(cpid, &status, 0);
    if (cpidw < 0) {
      ERRF("NVS-RT: waitpid() for %u failed", cpid);
      _exit(EXIT_FAILURE);
    } else if (cpidw == cpid) { // child process reaped
      DBGF("NVS-RT: target process %u finished", cpid);
    } else {
      ERRF("NVS-RT: unexpected waitpid() return value %u", cpidw);
    }

    nvsrt->set_target_status(status);
    nvsrt->send_message(MSG_TARGET_EXITED);
  }
}

/**
 * Initialize NVS-RT run-time data structures. Runs before the target's main()
 * with the constructor attribute.
 */
__attribute__((constructor(NVS_INIT_PRIO))) void __nvs_init() {
  if (nvsrt) {
    /**
     * Because we use forkservers, this function should not fire more than once
     * if NVS-RT is already enabled.
     */
    ERRF("NVS-RT: instrumented program already started");
    _exit(EXIT_FAILURE);
  }

  __nvs_setup_shm();

  /* If not testing, return to execute the target program, e.g. from main(). */
  if (!nvsrt)
    return;

  __start_forkserver();
}

/**
 * The following functions are injected into target programs for testing. They
 * should be exposed with C linkage (declared as extern "C") if target programs
 * are written in C because C++ names are usually mangled.
 */

extern "C" void __nvs_store(void *ptr, size_t size, char *func, char *file,
                            int line) {
  MUTEF("NVS-RT: [%s() at %s:%4d]: STORE to %p size %lu", func, file, line, ptr,
        size);

  if (!nvsrt || !nvsrt->is_enabled() || !nvsrt->store_in_range(ptr, size))
    return;

  nvsrt->save_store(reinterpret_cast<uint64_t>(ptr), size, func, file, line);
}

/**
 * Replaces the standard mmap() call with this wrapped version.
 * void
 * *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
 *
 * The targeted range is determined by the program's call to mmap(). We ignore
 * stores that occur before the mmap() call.
 */
extern "C" void *__nvs_mmap(void *addr, size_t size, int prot, int flags,
                            int fd, off_t offset, char *func, char *file,
                            int line) {
  /**
   * TODO: If necessary, we can change how mmap() is called, for example, using
   * provate mapping other than shared.
   */
  void *pmap = mmap(addr, size, prot, flags, fd, offset);

  /* TODO: Save the mapped address and size for store range checking. */

  DBGF("NVS-RT: [%s() at %s:%4d]: MMAP addr %p size %lu", func, file, line,
       pmap, size);

  if (!nvsrt || !nvsrt->is_enabled())
    return pmap;

  nvsrt->save_range(reinterpret_cast<uint64_t>(pmap), size, func, file, line);

  return pmap;
}

extern "C" void __nvs_clwb(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVS-RT: [%s() at %s:%4d]: CLWB addr %p cache line %p", func, file,
        line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  nvsrt->save_clfwb(addr, func, file, line);
}

extern "C" void __nvs_clflushopt(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVS-RT: [%s() at %s:%4d]: CLFLUSHOPT addr %p cache line %p", func,
        file, line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  nvsrt->save_clfwb(addr, func, file, line);
}

extern "C" void __nvs_clflush(void *ptr, char *func, char *file, int line) {
  uint64_t epoch = ++epochid;
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVS-RT: epoch %zu [%s() at %s:%4d]: CLFLUSH addr %p cache line %p",
        epoch, func, file, line, ptr,
        reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  nvsrt->save_clfwb(addr, func, file, line);
  nvsrt->check_reorder(epoch, func, file, line);
  nvsrt->check_dirty_stores(epoch, func, file, line);

  TESTC("NVS-RT: pass over epoch #%zu [clflush]", epoch);
}

extern "C" void __nvs_sfence(char *func, char *file, int line) {
  uint64_t epoch = ++epochid;

  MUTEF("NVS-RT: epoch %zu [%s() at %s:%4d]: SFENCE", epoch, func, file, line);

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  nvsrt->check_reorder(epoch, func, file, line);
  nvsrt->check_dirty_stores(epoch, func, file, line);

  TESTC("NVS-RT: pass over epoch #%zu [sfence]", epoch);
}

/**
 * The destructor will be called with a child process exits and also when a
 * forkserver exits.
 */
__attribute__((destructor(NVS_FINI_PRIO))) void __nvs_fini() {
  DBGF("NVS-RT: process %d exit", getpid());

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  uint64_t epoch = ++epochid;
  int linenr = 0;
  char *file = const_cast<char *>("Program");
  char *func = const_cast<char *>("Program exit");
  /*
   * TODO: It may not be safe to perform reordering tests at this point because
   * the mapped memory could be already unmapped. We should instrument program
   * munmap() calls, or make reordering tests independent of the previous mmaped
   * region.
   *
   * check_reorder(epoch, func, file, linenr);
   */
  nvsrt->check_missing_fence(epoch, func, file, linenr);
  nvsrt->check_dirty_stores(epoch, func, file, linenr);
}
