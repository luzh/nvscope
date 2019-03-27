#include <algorithm>
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

#define NVS_INIT_PRIO 0  // __nvs_init priority (runs before a target's main)

/**
 * Globals needed by the injected instrumentation.
 */
struct nvs_runq *runq;

class NVScopeRT {
  /* TODO: Many methods are not safe, e.g. _tgconfig may be nullptr. */
 public:
  /* Set shared memory address for information exchange. */
  void set_shm_base(void *shm) { _shm_base = shm; }
  /* Set config region for target control. */
  void set_tgconfig(struct nvs_target_config *conf) { _tgconfig = conf; }
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
  /* Set target stage. */
  void set_target_stage(enum nvs_target_stage stge) { _tgconfig->stage = stge; }
  /* Set target process status. */
  void set_target_status(int st) { _tgconfig->status = st; }

  /* Get target stage. */
  enum nvs_target_stage get_target_stage() { return _tgconfig->stage; }

  /* Communication methods. */
  enum nvs_message read_message() const;
  void send_message(enum nvs_message msg) const;
  void send_anydata(void *data, ssize_t len) const;
  void close_channels() const;

  /* Add one mmaped range. */
  void add_nvrange(void *pmap, size_t size);
  /* Check if the stored data falls into mmaped ranges. */
  bool in_nvranges(void *ptr, size_t size) const;
  /* Save store information. */
  void save_store(void *ptr, size_t size, char *func, char *file, int line);
  /* Save CLFLUSHOPT or CLWB operations. */
  void save_clop_nofence(void *ptr, char *func, char *file, int line);
  /* Perform analysis for insights. */
  void analyze(uint64_t sfid, char *func, char *file, int line);

  NVScopeRT(void *_shm, struct nvs_target_config *tgconf)
      : _shm_base(_shm), _tgconfig(tgconf) {
    OKF("NVS-RT: nvscope run-time constructed");
  }

 private:
  void *_shm_base;
  struct nvs_target_config *_tgconfig;

  struct StoreInfo {
    StoreInfo(uintptr_t start, uintptr_t last, char *func, char *file, int line)
        : _start(start), _last(last), _func(func), _file(file), _line(line) {}
    uintptr_t _start;  // start address of this store
    uintptr_t _last;   // one byte after the stored range
    char *_func;
    char *_file;
    int _line;
  };

  uintptr_t get_cache_line_addr(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr) & (~uintptr_t(0) << 6);
  }

  std::vector<std::pair<uintptr_t, uintptr_t>> _nvranges;

  /**
   * A collection of store operations. Each operation is placed in a vector
   * that corresponds to a cache line this store is writing to.
   */
  std::unordered_map<uintptr_t, std::vector<std::shared_ptr<StoreInfo>>>
      _nvstores;
};

void NVScopeRT::add_nvrange(void *pmap, size_t size) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(pmap);
  _nvranges.emplace_back(addr, addr + size);
}

bool NVScopeRT::in_nvranges(void *ptr, size_t size) const {
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  /* TODO: May also check if the store overflows the mapped region. */
  return std::any_of(_nvranges.begin(), _nvranges.end(),
                     [addr, size](auto &rg) {
                       return rg.first <= addr && addr + size < rg.second;
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
  if (_tgconfig->read_fd > 0) close(_tgconfig->read_fd);
  if (_tgconfig->write_fd > 0) close(_tgconfig->write_fd);
}

void NVScopeRT::save_store(void *ptr, size_t size, char *func, char *file,
                           int line) {
  auto start = reinterpret_cast<uintptr_t>(ptr);
  auto last = static_cast<uintptr_t>(start + size);
  auto cline = get_cache_line_addr(ptr);
  auto store = std::make_shared<StoreInfo>(start, last, func, file, line);
  do {
    auto it = _nvstores.find(cline);
    if (it == _nvstores.end()) {
      /**
       * emplace returns a pair where `first` is an iterator pointing to the
       * new element of the container.
       */
      it = _nvstores.emplace(cline, std::vector<std::shared_ptr<StoreInfo>>())
               .first;
    }
    it->second.emplace_back(store);
    cline += CACHELINE_SIZE;
  } while (last > cline);
}

void NVScopeRT::save_clop_nofence(void *ptr, char *func, char *file, int line) {
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  void *pcl = reinterpret_cast<void *>(ALIGN_DOWN(addr, CACHELINE_SIZE));

  (void)ptr;
  (void)pcl;
  (void)func;
  (void)file;
  (void)line;
}

void NVScopeRT::analyze(uint64_t sfid, char *func, char *file, int line) {
  if (_nvstores.empty()) return;

  (void)sfid;
  (void)func;
  (void)file;
  (void)line;
}

static NVScopeRT *nvsrt;

/*--------------------- End of NVScopeRT Implementation ---------------------*/

/**
 * Debug functions
 */
static void __nvs_print_runq() {
  DBGF(cCYA "--- NVS-RT run queue (...) ---" cRST);
  struct nvs_runq_entry *e = runq->entries;
  for (size_t i = 0; i < runq->len; i++, e++) {
    DBGF("Entry[%zu]: i64 [%p] 0x%lx -> 0x%lx", i, e->ptr64, e->val64,
         *e->ptr64);
  }
  DBGF(cCYA "--- NVS-RT run queue (***) ---" cRST);
}

/**
 * Shared memory setup
 */
static void __nvs_setup_shm(void) {
  char *shmid_str = getenv(NVS_ENV_SHM);

  if (shmid_str) {
    uint32_t shmid = atoi(shmid_str);

    void *shm_base = shmat(shmid, NULL, 0);
    if (shm_base == reinterpret_cast<void *>(-1)) _exit(NVS_EXIT_BAD_SHM);

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

    nvsrt = new NVScopeRT(nullptr, nullptr);
    if (!nvsrt) {
      ERRF("NVS-RT: creating nvscope run-time failed");
      _exit(NVS_EXIT_BAD_CONFIG);
    }

    runq = (struct nvs_runq *)((char *)shm_base + NVS_SHM_RUNQ_OFF);

    nvsrt->set_shm_base(shm_base);
    nvsrt->set_tgconfig(tgconf);

    if (nvsrt->get_target_stage() == ST_NONE)
      nvsrt->set_target_stage(ST_DONTCARE);
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

  while (1) {
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
      _exit(EXIT_FAILURE);
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
       */

      nvsrt->set_target_pid(getpid());
      nvsrt->send_message(MSG_TARGET_STARTED);

      return;  // execute the target progrm, e.g. from main().
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
    } else if (cpidw == cpid) {  // child process reaped
      DBGF("NVS-RT: target process %u finished", cpid);
    } else {
      ERRF("NVS-RT: unexpected waitpid() return value %u", cpidw);
    }

    nvsrt->set_target_status(status);
    nvsrt->send_message(MSG_TARGET_EXITED);
  }
}

/*
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
  if (!nvsrt) return;

  __start_forkserver();
}

static inline int __runq_push_back_store64(uint64_t *ptr) {
  if (runq->len == NVS_SHM_RUNQ_MAX_LEN) {
    ERRF("NVS-RT: run queue is full (%lu entries)!", runq->len);
    _exit(NVS_EXIT_RUNQ_FULL);
  }

  runq->entries[runq->len].ptr64 = ptr;
  runq->entries[runq->len].val64 = *ptr;
  runq->len += 1;

  return 0;
}

static inline void __runq_flush() {
  /*
   * Todo: Remove flushed (clflushopt, clwb) stores from the runq, since they
   * should be persistent after the sfence and not be affected by reordering.
   * Now assume clflush(opt) or clwb is complete, and we simply set the queue
   * length to zero to flush it.
   */
  runq->len = 0;
}

static int __next_test_case(uint64_t sfid) {
  static size_t caseid = 0;
  uint64_t oldval, newval;

  if (caseid == 0) {
    TESTC("NVS-RT: make test case #%zu: crash after sfence #%zu", caseid, sfid);
    caseid++;
    return 1;
  }

  if (caseid > 1) {
    struct nvs_runq_entry *e = &runq->entries[caseid - 2];
    oldval = *e->ptr64;
    newval = e->val64;
    *e->ptr64 = newval;
    TESTC(
        "NVS-RT: pass over test case #%zu: redo store i64 [%p] 0x%lx -> 0x%lx",
        caseid - 1, e->ptr64, oldval, newval);
  }

  if (runq->len < caseid) {
    caseid = 0;
    return 0;
  }

  struct nvs_runq_entry *e = &runq->entries[caseid - 1];

  oldval = e->val64;
  newval = *e->ptr64;

  e->val64 = newval;
  *e->ptr64 = oldval;

  TESTC("NVS-RT: make test case #%zu: undo store i64 [%p] 0x%lx <- 0x%lx",
        caseid, e->ptr64, oldval, newval);
  caseid++;

  (void)sfid;

  return 1;
}

static void __emulate_crash(uint64_t sfid) {
  while (__next_test_case(sfid)) {
    nvsrt->send_message(MSG_AWAITING_CHECK);

    enum nvs_message command = nvsrt->read_message();

    if (command == MSG_SHOW_BUG_AND_EXIT) {
      ERRF("NVS-RT: found bug at sfence #%zu test case #?", sfid);
      _exit(NVS_EXIT_FOUNDBUG);
    }

    if (command != MSG_CONTINUE_TO_RUN) {
      ERRF("NVS-RT: received inappropriate message %d", command);
      _exit(EXIT_FAILURE);
    }
  }
}

void __nvs_store64(void *ptr) {
  DBGF("NVS-RT: store i64 to %p", ptr);

  /* TODO-PERF: Perhaps using likely/unlikely can improve performance. */

  if (nvsrt->get_target_stage() == ST_MAINPROC)
    __runq_push_back_store64((uint64_t *)ptr);
}

/**
 * The following functions are injected into target programs for testing.
 * They should be exposed with C linkage (declared as extern "C") if target
 * programs are written in C because C++ names are usually mangled.
 */

extern "C" void __nvs_store(void *ptr, size_t size, char *func, char *file,
                            int line) {
  if (!nvsrt || !nvsrt->is_enabled() || !nvsrt->in_nvranges(ptr, size)) return;

  DBGF("NVS-RT: [%s() at %s:%4d]: STORE to %p size %lu", func, file, line, ptr,
       size);

  nvsrt->save_store(ptr, size, func, file, line);

  if (size == 8)  // TODO: handle other sizes
    __nvs_store64(ptr);
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

  if (!nvsrt || !nvsrt->is_enabled()) return pmap;

  (void)func;
  (void)file;
  (void)line;

  nvsrt->add_nvrange(pmap, size);

  nvsrt->set_target_stage(ST_MAINPROC);

  return pmap;
}

extern "C" void __nvs_clwb(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled()) return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLWB addr %p cache line %p", func, file, line,
       ptr, (void *)ALIGN_DOWN((uintptr_t)ptr, CACHELINE_SIZE));

  nvsrt->save_clop_nofence(ptr, func, file, line);
}

extern "C" void __nvs_clflushopt(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled()) return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLFLUSHOPT addr %p cache line %p", func, file,
       line, ptr, (void *)ALIGN_DOWN((uintptr_t)ptr, CACHELINE_SIZE));

  nvsrt->save_clop_nofence(ptr, func, file, line);
}

extern "C" void __nvs_clflush(void *ptr, char *func, char *file, int line) {
  if (!nvsrt || !nvsrt->is_enabled()) return;

  DBGF("NVS-RT: [%s() at %s:%4d]: CLFLUSH addr %p cache line %p", func, file,
       line, ptr, (void *)ALIGN_DOWN((uintptr_t)ptr, CACHELINE_SIZE));

  nvsrt->save_clop_nofence(ptr, func, file, line);

  /* TODO: should also handle sfence here. */
}

extern "C" void __nvs_sfence(uint64_t sfid, char *func, char *file, int line) {
  DBGF("NVS-RT: [%s() at %s:%4d]: SFENCE #%lu", func, file, line, sfid);

  if (!nvsrt || !nvsrt->is_enabled()) return;

  __nvs_print_runq();
  __emulate_crash(sfid);
  __runq_flush();

  (void)func;
  (void)file;
  (void)line;

  TESTC("NVS-RT: pass over epoch [sfence] #%zu", sfid);
}
