#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "afl/config.h"
#include "debug.h"
#include "headers.h"
#include "nvscope/config.h"

static const int NVS_INIT_PRIO{0}; // __nvs_init priority (runs before main)

static uintptr_t cache_addr_of(const void *ptr) {
  return reinterpret_cast<uintptr_t>(ptr) & (~uintptr_t(0) << 6);
}

class NVScopeRT {
  /* TODO: Many methods are not safe, e.g. _tgconfig may be nullptr. */
public:
  /* Check if NVS-RT is enabled. */
  bool is_enabled() const { return _tgconfig->enabled; }

  /**
   * Initialize the store queue. TODO: Currently it's a pre-allocated region in
   * the shared memory. We need a more flexible data structure to save the
   * stored data.
   */
  // void init_store_queue(void *stq) { _store_queue = stq; }

  /* Set target process PID. */
  void set_target_pid(pid_t pid) { _tgconfig->pid = pid; }
  /* Set target process status. */
  void set_target_status(int st) { _tgconfig->status = st; }

  /* Communication methods. */
  enum nvs_message read_message() const;
  void send_message(enum nvs_message msg) const;
  void send_anydata(void *data, ssize_t len) const;
  void close_channels() const;

  /* Add one mmaped range. */
  void save_nvrange(void *pmap, size_t size, char *func, char *file, int line);
  /* Check if the stored data falls into mmaped ranges. */
  bool store_in_range(void *ptr, size_t size);
  /* Save store information. */
  void save_store(void *ptr, size_t size, char *func, char *file, int line);
  /* Save CLFLUSHOPT or CLWB operations. */
  void save_clop_nofence(void *ptr, char *func, char *file, int line);

  /* Generate the next test case. */
  bool next_reorder();
  /* Perform analysis for insights. */
  void analyze(uint64_t sfid, char *func, char *file, int line);

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

private:
  struct StoreInfo {
    StoreInfo(uintptr_t start, uintptr_t end, const char *func, const char *file,
              const int linenr)
        : _func(func), _file(file), _linenr(linenr), _start(start), _end(end),
          _snapshot(reinterpret_cast<std::byte *>(start),
                    reinterpret_cast<std::byte *>(end)) {}
    const char *_func;
    const char *_file;
    const int _linenr;
    uintptr_t _start; // start address of this store
    uintptr_t _end;   // one byte after the stored range
    std::vector<std::byte> _snapshot;
  };

  struct RangeInfo {
    RangeInfo(uintptr_t start, uintptr_t end, const char *func, const char *file,
              const int linenr)
        : _func(func), _file(file), _linenr(linenr), _start(start), _end(end) {}
    const char *_func;
    const char *_file;
    const int _linenr;
    uintptr_t _start; // user's mmap start address
    uintptr_t _end;   // user's mmap end address
  };

  void *_shm_base;
  struct nvs_target_config *_tgconfig;

  /**
   * TODO: Using a vector assumes there are only few mappings (less than 10),
   * where a linear search is good enough. But if there are tens or hundreds of
   * mappings we should use a hash map.
   */
  std::vector<RangeInfo> _nvranges;

  std::vector<StoreInfo> _nvstores;

  /**
   * A collection of store operations. Each operation is placed in a vector
   * that corresponds to a cache line this store is writing to.
   * std::unordered_map<uintptr_t, std::vector<std::shared_ptr<StoreInfo>>>
   * _clstores;
   */
};

void NVScopeRT::save_nvrange(void *pmap, size_t size, char *func, char *file,
                             int line) {
  auto addr = reinterpret_cast<uintptr_t>(pmap);
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

void NVScopeRT::save_store(void *ptr, size_t size, char *func, char *file,
                           int line) {
  auto start = reinterpret_cast<uintptr_t>(ptr);
  auto end = static_cast<uintptr_t>(start + size);

  _nvstores.emplace_back(start, end, func, file, line);
}

void NVScopeRT::save_clop_nofence(void *ptr, char *func, char *file, int line) {
  // uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  void *pcl = reinterpret_cast<void *>(cache_addr_of(ptr));

  (void)ptr;
  (void)pcl;
  (void)func;
  (void)file;
  (void)line;
}

#ifdef NVS_DEBUG
void NVScopeRT::print_nvstores(size_t limit) const {
  size_t n = 0;

  DBGF(cCYA "--- NVS-RT collected stores (...) ---" cRST);
  for (auto &store : _nvstores) {
    DBGF("Entry[%zu]: [%s() at %s:%4d], store to %p size %zu", n, store._func,
         store._file, store._linenr, reinterpret_cast<void *>(store._start),
         store._end - store._start);
    if (0 < limit && limit <= ++n)
      break;
  }
  DBGF(cCYA "--- NVS-RT collected stores (***) ---" cRST);
}
#endif

bool NVScopeRT::next_reorder() {
  static size_t caseid = 0;

  if (caseid == 0) {
    TESTC("NVS-RT: make test case #%zu: crash after sfence", caseid);
    caseid++;
    return true;
  }

  if (caseid > 1) {
    StoreInfo &store = _nvstores[caseid - 2];
    auto *start = reinterpret_cast<std::byte *>(store._start);
    auto *end = reinterpret_cast<std::byte *>(store._end);
    auto *newdata = store._snapshot.data();

    std::swap_ranges(start, end, newdata);

    TESTC("NVS-RT: pass over test case #%zu: redo store to %p size %zu",
          caseid - 1, start, end - start);
  }

  if (_nvstores.size() < caseid) {
    caseid = 0;
    return false;
  }

  StoreInfo &store = _nvstores[caseid - 1];
  auto *start = reinterpret_cast<std::byte *>(store._start);
  auto *end = reinterpret_cast<std::byte *>(store._end);
  auto *olddata = store._snapshot.data();

  /**
   * TODO: std::swap_ranges() seems to work at granularity determined by the
   * iterator, so for std::byte* iterators it swaps byte-by-byte. We will need
   * a more efficient swapping method at certain point.
   */
  std::swap_ranges(start, end, olddata);

  TESTC("NVS-RT: make test case #%zu: undo store to %p size %zu", caseid, start,
        end - start);
  caseid++;

  return true;
}

void NVScopeRT::analyze(uint64_t sfid, char *func, char *file, int line) {
  if (_nvstores.empty())
    return;

  DBGF("NVS-RT: reordering stores at sfence #%zu [%s() at %s:%4d]", sfid, func,
       file, line);

  while (next_reorder()) {
    send_message(MSG_AWAITING_CHECK);

    enum nvs_message command = read_message();

    if (command == MSG_SHOW_BUG_AND_EXIT) {
      ERRF("NVS-RT: found bug at sfence #%zu [%s() at %s:%4d], test case #?",
           sfid, func, file, line);
      _exit(NVS_EXIT_FOUNDBUG);
    }

    if (command != MSG_CONTINUE_TO_RUN) {
      ERRF("NVS-RT: received inappropriate message %d", command);
      _exit(NVS_EXIT_BAD_MSG);
    }
  }

  /*
   * TODO: Only remove flushed (clflushopt, clwb) stores, since they should be
   * persistent after the sfence and not be affected by reordering. Now assume
   * clflush(opt) or clwb is complete, and we simply set the queue length to
   * zero to flush it.
   */
  _nvstores.clear();
}

static NVScopeRT *nvsrt;

/*--------------------- End of NVScopeRT Implementation ---------------------*/

/**
 * Shared memory setup
 */
static void __nvs_setup_shm(void) {
  char *shmid_str = getenv(NVS_ENV_SHM);

  if (shmid_str) {
    uint32_t shmid = atoi(shmid_str);

    void *shm_base = shmat(shmid, NULL, 0);
    if (shm_base == reinterpret_cast<void *>(-1))
      _exit(NVS_EXIT_BAD_SHM);

    struct nvs_config *config = (struct nvs_config *)(shm_base);

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

    nvsrt = new NVScopeRT(shm_base, tgconf);
    if (!nvsrt) {
      ERRF("NVS-RT: creating nvscope run-time failed");
      _exit(NVS_EXIT_BAD_CONFIG);
    }
  } else {
    WARNF("NVS-RT: shared memory not found, nvscope run-time disabled");
  }
}

/**
 * Forkserver logic (see nvscope.c for the other part)
 */
static void __start_forkserver(void) {
  /* initial communication with nvscope */
  nvsrt->send_message(MSG_FORKSERVER_HELLO);

  while (true) {
    nvsrt->send_message(MSG_FORKSERVER_READY);

    enum nvs_message command = nvsrt->read_message();

    if (command == MSG_EXIT_FORKSERVER) {
      ACTF("NVS-RT: forkserver received command to exit");
      nvsrt->close_channels();
      delete nvsrt;
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
__attribute__((constructor(NVS_INIT_PRIO))) void __nvs_init(void) {
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
 * The destructor will be called with a child process exits.
 * __attribute__((destructor(NVS_FINI_PRIO))) void __nvs_fini(void) {
 * }
 */

/**
 * The following functions are injected into target programs for testing. They
 * should be exposed with C linkage (declared as extern "C") if target programs
 * are written in C because C++ names are usually mangled.
 */

extern "C" void __nvs_store(void *ptr, size_t size, char *func, char *file,
                            int line) {
  if (!nvsrt || !nvsrt->is_enabled() || !nvsrt->store_in_range(ptr, size))
    return;

  DBGF("NVS-RT: [%s() at %s:%4d]: STORE to %p size %lu", func, file, line, ptr,
       size);

  nvsrt->save_store(ptr, size, func, file, line);
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

  nvsrt->save_nvrange(pmap, size, func, file, line);

  return pmap;
}

extern "C" void __nvs_clwb(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled())
    return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLWB addr %p cache line %p", func, file, line,
       ptr, reinterpret_cast<void *>(cache_addr_of(ptr)));

  nvsrt->save_clop_nofence(ptr, func, file, line);
}

extern "C" void __nvs_clflushopt(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled())
    return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLFLUSHOPT addr %p cache line %p", func, file,
       line, ptr, reinterpret_cast<void *>(cache_addr_of(ptr)));

  nvsrt->save_clop_nofence(ptr, func, file, line);
}

extern "C" void __nvs_clflush(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled())
    return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLFLUSH addr %p cache line %p", func, file,
       line, ptr, reinterpret_cast<void *>(cache_addr_of(ptr)));

  nvsrt->save_clop_nofence(ptr, func, file, line);

  /* TODO: should also handle sfence here. */
}

extern "C" void __nvs_sfence(uint64_t sfid, char *func, char *file, int line) {
  DBGF("NVS-RT: [%s() at %s:%4d]: SFENCE #%lu", func, file, line, sfid);

  if (!nvsrt || !nvsrt->is_enabled())
    return;

#ifdef NVS_DEBUG
  nvsrt->print_nvstores(0);
#endif
  nvsrt->analyze(sfid, func, file, line);

  TESTC("NVS-RT: pass over epoch [sfence] #%zu", sfid);
}
