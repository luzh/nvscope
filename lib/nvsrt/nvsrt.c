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
    DBGF("Entry[%zu]: i64 [%p] 0x%lx -> 0x%lx", i, e->ptr64, e->old64,
         e->new64);
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

    /* Whooooops. */

    if (__shm_base == (void *)-1) _exit(NVS_EXIT_BAD_SHM);

    config = (struct nvs_config *)(__shm_base);

    /* should be initialized by parent (fuzzer) */
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
  /* initial communication with the fuzzer */
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
       * the fuzzer. Thus, when the target program runs, there are two writers
       * to the state pipe: the forkserver and the target program. Linux pipes
       * guarantee write atomicity for message sizes no larger than PIPE_BUF.
       * When the target program exits, its pipe ends automatically close.
       *
       * In afl-llvm-rt.o.c, AFL closes the pipe fds because they are not needed
       * anymore. But NVS-RT still needs them to communicate with the fuzzer for
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

static inline int __runq_push_back_store64(uint64_t *ptr, uint64_t val) {
  if (runq->len == NVS_SHM_RUNQ_MAX_LEN) {
    ERRF("NVS-RT: run queue is full (%lu entries)!", runq->len);
    _exit(NVS_EXIT_RUNQ_FULL);
  }

  runq->entries[runq->len].ptr64 = ptr;
  runq->entries[runq->len].old64 = *ptr;
  runq->entries[runq->len].new64 = val;
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

  if (caseid == 0) {
    TESTC("NVS-RT: make test case #%zu: crash after sfence #%zu", caseid, sfid);
    caseid++;
    return 1;
  }

  if (caseid > 1) {
    struct nvs_runq_entry *e = &runq->entries[caseid - 2];
    *e->ptr64 = e->new64;
    TESTC(
        "NVS-RT: pass over test case #%zu: redo store i64 [%p] 0x%lx -> 0x%lx",
        caseid - 1, e->ptr64, e->old64, e->new64);
  }

  if (runq->len < caseid) {
    caseid = 0;
    return 0;
  }

  struct nvs_runq_entry *e = &runq->entries[caseid - 1];

  *e->ptr64 = e->old64;

  TESTC("NVS-RT: make test case #%zu: undo store i64 [%p] 0x%lx <- 0x%lx",
        caseid, e->ptr64, e->old64, e->new64);
  caseid++;

  (void)sfid;

  return 1;
}

static inline int __recoverq_push_back_store64(uint64_t *ptr, uint64_t val) {
  (void)ptr;
  (void)val;

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

#ifdef NDEBUG
void __nvs_probe_store64(uint64_t *ptr, uint64_t val) {
  DBGF("NVS-RT: store i64 [%p] 0x%lx -> 0x%lx", (void *)ptr, *ptr, val);
#else
void __nvs_probe_store64(uint64_t *ptr, uint64_t val, char *file, char *func,
                         int line) {
  DBGF("NVS-RT: [%s, %s(), line %d]: store i64 [%p] 0x%lx -> 0x%lx", file, func,
       line, (void *)ptr, *ptr, val);
#endif
  /* PERF: Perhaps using likely/unlikely can improve performance. */

  if (!__nvs_enabled || !tgconf->tracing) return;

  if (!__store64_in_pmem(ptr)) return;

  if (tgconf->stage == MAINPROC) __runq_push_back_store64(ptr, val);

  if (tgconf->stage == RECOVERY) __recoverq_push_back_store64(ptr, val);
}

#ifdef NDEBUG
void __nvs_probe_mmap(uint64_t mapaddr, uint64_t mapsize) {
  DBGF("NVS-RT: mmap addr %p size %lu", (void *)mapaddr, mapsize);
#else
void __nvs_probe_mmap(uint64_t mapaddr, uint64_t mapsize, char *file,
                      char *func, int line) {
  DBGF("NVS-RT: [%s, %s(), line %d]: mmap addr %p size %lu", file, func, line,
       (void *)mapaddr, mapsize);
#endif
  if (!__nvs_enabled || !tgconf->tracing) return;

  (void)mapaddr;
  (void)mapsize;

  /* Implementation */
  tgconf->stage = MAINPROC;
}

void __nvs_probe_clflush(uint64_t *ptr) {
  DBGF("NVS-RT: seeing a CLFLUSH on %p", (void *)ptr);

  (void)ptr;

  if (!__nvs_enabled || !tgconf->tracing) return;
}

#ifdef NDEBUG
void __nvs_probe_sfence(uint64_t sfid) {
  DBGF("NVS-RT: epoch [sfence] #%lu", sfid);
#else
void __nvs_probe_sfence(uint64_t sfid, char *file, char *func, int line) {
  DBGF("NVS-RT: [%s, %s(), line %d]: sfence #%lu", file, func, line, sfid);
#endif
  if (!__nvs_enabled || !tgconf->tracing) return;

  __nvs_print_runq();
  __emulate_crash(sfid);
  __runq_flush();

  TESTC("NVS-RT: pass over epoch [sfence] #%zu", sfid);
}
