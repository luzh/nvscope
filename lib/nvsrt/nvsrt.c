#include "afl/config.h"
#include "debug.h"
#include "headers.h"
#include "nvscope/config.h"

#define CONST_PRIO 0  // constructor priority (runs before a target's main)

/**
 * Globals needed by the injected instrumentation.
 */
int __nvs_enabled;  // __shm_base != NULL
uint8_t *__shm_base;
struct nvs_config *config;
struct nvs_target_config *tgconf;
struct nvs_runq *runq;

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
 * Communication functions
 *
 * Now we use pipes. It is possible to change them to use other mechanisms.
 */
static inline enum nvs_message __read_message() {
  enum nvs_message msg;
  if (read(tgconf->read_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVS-RT: read() from tgconf->read_fd %d failed", tgconf->read_fd);
    _exit(EXIT_FAILURE);
  }
  return msg;
}

static inline void __send_message(enum nvs_message msg) {
  if (write(tgconf->write_fd, &msg, sizeof(msg)) != sizeof(msg)) {
    ERRF("NVS-RT: write() to tgconf->write_fd %d failed", tgconf->write_fd);
    _exit(EXIT_FAILURE);
  }
}

#if 0
static inline void __send_data(void *data, ssize_t len) {
  if (write(tgconf->write_fd, data, len) != len) {
    ERRF("NVS-RT: write() to tgconf->write_fd %d failed", tgconf->write_fd);
    _exit(EXIT_FAILURE);
  }
}
#endif

/**
 * Shared memory setup
 */
static void __nvs_setup_shm(void) {
  char *shmid_str = getenv(NVS_ENV_SHM);

  if (shmid_str) {
    uint32_t shmid = atoi(shmid_str);

    __shm_base = shmat(shmid, NULL, 0);

    if (__shm_base == (void *)-1) _exit(NVS_EXIT_BAD_SHM);

    config = (struct nvs_config *)(__shm_base);

    /* should be initialized by parent (nvscope) */
    if (!config->initialized) {
      ERRF("NVS-RT: config region not initialized");
      _exit(NVS_EXIT_BAD_SHM);
    }
    // if (config->stage == NONE) config->stage = DONTCARE;

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

    runq = (struct nvs_runq *)(__shm_base + NVS_SHM_RUNQ_OFF);

    __nvs_enabled = 1;

  } else {
    __nvs_enabled = 0;

    WARNF("NVS-RT: shared memory not found, tracing functions disabled");
  }
}

/**
 * Forkserver logic (see nvscope.c for the other part)
 */
static void __start_forkserver(void) {
  /* initial communication with nvscope */
  __send_message(MSG_FORKSERVER_HELLO);

  while (1) {
    __send_message(MSG_FORKSERVER_READY);

    enum nvs_message command = __read_message();

    if (command == MSG_EXIT_FORKSERVER) {
      ACTF("NVS-RT: forkserver received command to exit");
      close(tgconf->read_fd);
      close(tgconf->write_fd);
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

      tgconf->pid = getpid();
      __send_message(MSG_TARGET_STARTED);

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

    tgconf->status = status;
    __send_message(MSG_TARGET_EXITED);
  }
}

/*
 * Initialize NVS-RT run-time data structures. Runs before the target's main()
 * with the constructor attribute.
 */
__attribute__((constructor(CONST_PRIO))) void __nvs_init(void) {
  if (__nvs_enabled) {
    /* This function should not fire more than once if __nvs_enabled. */
    ERRF("NVS-RT: instrumented program already started");
    _exit(EXIT_FAILURE);
  }

  __nvs_setup_shm();

  /* If not testing, return to execute the target program, e.g. from main(). */
  if (!__nvs_enabled) return;

  __start_forkserver();
}

static inline int __store64_in_pmem(uint64_t *ptr) {
  (void)ptr;

  return 1;
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

static inline int __recoverq_push_back_store64(uint64_t *ptr) {
  (void)ptr;

  return 0;
}

static void __emulate_crash(uint64_t sfid) {
  while (__next_test_case(sfid)) {
    __send_message(MSG_AWAITING_CHECK);

    enum nvs_message command = __read_message();

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

void __nvs_probe_store64(uint64_t *ptr) {
  DBGF("NVS-RT: store i64 to %p", ptr);

  /* PERF: Perhaps using likely/unlikely can improve performance. */

  if (!__nvs_enabled || !tgconf->tracing) return;

  if (!__store64_in_pmem(ptr)) return;

  if (tgconf->stage == MAINPROC) __runq_push_back_store64(ptr);

  if (tgconf->stage == RECOVERY) __recoverq_push_back_store64(ptr);
}

void __nvs_probe_store(void *ptr, uint64_t size, char *func, char *file,
                       int line) {
  DBGF("NVS-RT: [%s() at %s:%4d]: store to %p size %lu", func, file, line, ptr,
       size);

  (void)func;
  (void)file;
  (void)line;

  if (size == 8)  // Todo: handle other sizes
    __nvs_probe_store64(ptr);
}

void __nvs_probe_mapping(void *ptr, uint64_t size, char *func, char *file,
                         int line) {
  DBGF("NVS-RT: [%s() at %s:%4d]: mmap addr %p size %lu", func, file, line, ptr,
       size);

  if (!__nvs_enabled || !tgconf->tracing) return;

  (void)ptr;
  (void)size;
  (void)func;
  (void)file;
  (void)line;

  /* Implementation */
  tgconf->stage = MAINPROC;
}

void __nvs_probe_clflushopt(void *ptr, char *func, char *file, int line) {
  void *clptr = (void *)ALIGN_DOWN((uintptr_t)ptr, CACHELINE_SIZE);

  DBGF("NVS-RT: [%s() at %s:%4d]: clflushopt addr %p cache line %p", func, file,
       line, ptr, clptr);

  (void)ptr;
  (void)clptr;
  (void)func;
  (void)file;
  (void)line;

  if (!__nvs_enabled || !tgconf->tracing) return;
}

void __nvs_probe_clflush(void *ptr, char *func, char *file, int line) {
  void *clptr = (void *)ALIGN_DOWN((uintptr_t)ptr, CACHELINE_SIZE);

  DBGF("NVS-RT: [%s() at %s:%4d]: clflush addr %p cache line %p", func, file,
       line, ptr, clptr);

  (void)ptr;
  (void)clptr;
  (void)func;
  (void)file;
  (void)line;

  if (!__nvs_enabled || !tgconf->tracing) return;
}

void __nvs_probe_sfence(uint64_t sfid, char *func, char *file, int line) {
  DBGF("NVS-RT: [%s() at %s:%4d]: sfence #%lu", func, file, line, sfid);

  if (!__nvs_enabled || !tgconf->tracing) return;

  __nvs_print_runq();
  __emulate_crash(sfid);
  __runq_flush();

  (void)func;
  (void)file;
  (void)line;

  TESTC("NVS-RT: pass over epoch [sfence] #%zu", sfid);
}
