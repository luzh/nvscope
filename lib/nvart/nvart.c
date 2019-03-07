#include "afl/config.h"
#include "debug.h"
#include "headers.h"
#include "nvart/config.h"

#define CONST_PRIO 0  // constructor priority (runs before a target's main)

/*
 * Globals needed by the injected instrumentation.
 */
int __nvart_active;
uint8_t *__nvart_shm;
struct nvart_config *config;
struct nvart_runq *runq;

/* Debug functions */
void __nvart_print_runq() {
  DEBUGF("--- NVArt run queue (...) ---");
  struct nvart_runq_entry *e = runq->entries;
  for (size_t i = 0; i < runq->len; i++, e++) {
    DEBUGF("Entry[%zu]: i64 [%p] 0x%lx -> 0x%lx", i, e->ptr64, e->old64,
           e->new64);
  }
  DEBUGF("--- NVArt run queue (***) ---");
}

/* Shared memory setup */
static void __nvart_map_shm(void) {
  uint8_t *shmid_str = getenv(NVART_SHM_ENV_VAR);

  if (shmid_str) {
    uint32_t shmid = atoi(shmid_str);

    __nvart_shm = shmat(shmid, NULL, 0);

    /* Whooooops. */

    if (__nvart_shm == (void *)-1) _exit(NVART_EXIT_BAD_SHM);

    config = (struct nvart_config *)(__nvart_shm);

    /* should be initialized by parent (fuzzer) */
    if (!config->ready) {
      ERRF("NVArt: config region not ready");
      _exit(NVART_EXIT_BAD_SHM);
    }
    if (config->stage == NONE) config->stage = DONTCARE;

    runq = (struct nvart_runq *)(__nvart_shm + NVART_SHM_RUNQ_OFF);

    __nvart_active = 1;

    OKF("NVArt: target attached to shared memory");
  } else {
    __nvart_active = 0;

    WARNF("NVArt: shared memory not found, target will run without tracing");
  }
}

/* Forkserver logic (see nvfuzz.c for the other part) */
static void __nvart_start_forkserver(void) {
  pid_t tpid;
  enum nvart_pipe_msg ctrl, stat;

  if (!__nvart_active) return;

  /*
   * Phone home and tell the parent that we're OK. If parent isn't there,
   * assume we're not running in forkserver mode and just execute program.
   */
  stat = MSG_FORKSERVER_READY;
  if (write(FD_MAINPROC_INFO, &stat, sizeof(stat)) != sizeof(stat)) {
    WARNF("NVArt: contact fuzzer failed, mainproc will run without testing");
    return;
  }

  while (1) {
    /* Wait for parent by reading from the pipe. Abort if read fails. */
    if (read(FD_MAINPROC_CTRL, &ctrl, sizeof(ctrl)) != sizeof(ctrl)) {
      ERRF("NVArt: read() from FD_MAINPROC_CTRL %d failed", FD_MAINPROC_CTRL);
      _exit(EXIT_FAILURE);
    }

    if (ctrl == MSG_EXIT_FORKSERVER) {
      ACTF("NVArt: forkserver received command to exit");
      close(FD_MAINPROC_CTRL);
      close(FD_MAINPROC_INFO);
      _exit(EXIT_SUCCESS);
    }

    if (ctrl != MSG_CONTINUE_TO_RUN) {
      ERRF("NVArt: inappropriate pipe message %d", ctrl);
      _exit(EXIT_FAILURE);
    }

    /* Check afl-llvm-rt.o.c for persistent mode and using SIGCONT. */
    if ((tpid = fork()) < 0) {
      ERRF("NVArt: fork() to run the mainproc program failed");
      _exit(EXIT_FAILURE);
    }

    if (tpid == 0) {
      /*
       * In the child process (mainproc): start execution, e.g. from
       * main(). It inherits pipes from the forkserver to communicate with the
       * fuzzer. Thus, when the mainproc program runs, there are two writers to
       * the state pipe: the forkserver and the mainproc program. Linux pipes
       * guarantee write atomicity for message sizes no larger than PIPE_BUF.
       * When the mainproc program exits, its pipe ends automatically close.
       *
       * In afl-llvm-rt.o.c, AFL closes the pipe fds because they are not used
       * anymore. But we still need them to relay testing requests to the
       * fuzzer.
       */
      return;
    }

    OKF("NVArt: mainproc program started, pid %d", tpid);

    /*
     * DO NOT write to pipe before waitpid() returns. Otherwise races can occur
     * because the mainproc is running and it may write to FD_MAINPROC_INFO too.
     */

    /* In parent (forkserver): write PID to pipe, then wait for mainproc. */
    // if (write(FD_MAINPROC_INFO, &tpid, sizeof(tpid)) != sizeof(tpid)) {
    //   ERRF("NVArt: write() tpid to FD_MAINPROC_INFO %d failed", FD_MAINPROC_INFO);
    //   _exit(EXIT_FAILURE);
    // }

    int tstatus;
    pid_t tpidw = waitpid(tpid, &tstatus, 0);
    if (tpidw < 0) {
      ERRF("NVArt: waitpid() for %u failed", tpid);
      _exit(EXIT_FAILURE);
    } else if (tpidw == tpid) {  // mainproc process reaped
      ACTF("NVArt: mainproc process %u finished", tpid);
      stat = MSG_MAINPROC_EXITED;
      if (write(FD_MAINPROC_INFO, &stat, sizeof(stat)) != sizeof(stat)) {
        ERRF("NVArt: write() tstatus to FD_MAINPROC_INFO %d failed",
             FD_MAINPROC_INFO);
        _exit(EXIT_FAILURE);
      }
    } else {
      ERRF("NVArt: unexpected waitpid() return value %u", tpidw);
    }

    /* Relay waitpid status to pipe, then loop back to restart. */
    if (write(FD_MAINPROC_INFO, &tstatus, sizeof(tstatus)) != sizeof(tstatus)) {
      ERRF("NVArt: write() tstatus to FD_MAINPROC_INFO %d failed", FD_MAINPROC_INFO);
      _exit(EXIT_FAILURE);
    }
  }
}

/*
 * Initialize NVArt run-time data structures. Runs before mainproc's main() with
 * the constructor attribute.
 */
__attribute__((constructor(CONST_PRIO))) void __nvart_init(void) {
  static uint32_t init_done;

  if (!init_done) {
    __nvart_map_shm();

    if (!__nvart_active) return;

    __nvart_start_forkserver();
    init_done = 1;

    OKF("NVArt: analysis runtime initialized");
  } else {
    assert(__nvart_shm != NULL);
    assert(__nvart_active == 1);
  }
}

static inline int __store64_in_pmem(uint64_t *ptr) {
  (void)ptr;

  return 1;
}

static inline int __runq_push_back_store64(uint64_t *ptr, uint64_t val) {
  if (runq->len == NVART_SHM_RUNQ_MAX_LEN) {
    ERRF("NVArt: run queue is full (%lu entries)!", runq->len);
    _exit(NVART_EXIT_RUNQ_FULL);
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
    TESTC("NVArt: make test case #%zu: crash after sfence #%zu", caseid, sfid);
    caseid++;
    return 1;
  }

  if (caseid > 1) {
    struct nvart_runq_entry *e = &runq->entries[caseid - 2];
    *e->ptr64 = e->new64;
    TESTC("NVArt: pass over test case #%zu: redo store i64 [%p] 0x%lx -> 0x%lx",
          caseid - 1, e->ptr64, e->old64, e->new64);
  }

  if (runq->len < caseid) {
    caseid = 0;
    return 0;
  }

  struct nvart_runq_entry *e = &runq->entries[caseid - 1];

  *e->ptr64 = e->old64;

  TESTC("NVArt: make test case #%zu: undo store i64 [%p] 0x%lx <- 0x%lx",
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
  enum nvart_pipe_msg req, result;
  while (__next_test_case(sfid)) {
    req = MSG_AWAITING_CHECK;
    if (write(FD_MAINPROC_INFO, &req, sizeof(req)) != sizeof(req)) {
      ERRF("NVArt: write() to FD_MAINPROC_INFO %d failed", FD_MAINPROC_INFO);
      _exit(EXIT_FAILURE);
    }

    if (read(FD_MAINPROC_CTRL, &result, sizeof(result)) != sizeof(result)) {
      ERRF("NVArt: read() from FD_MAINPROC_CTRL %d failed", FD_MAINPROC_CTRL);
      _exit(EXIT_FAILURE);
    }

    if (result == MSG_SHOW_BUG_AND_EXIT) {
      ERRF("NVArt: found bug at sfence #%zu test case #?", sfid);
      _exit(NVART_EXIT_FOUNDBUG);
    }
  }
}

#ifdef NDEBUG
void __nvart_probe_store64(uint64_t *ptr, uint64_t val) {
  DEBUGF("NVArt: store i64 [%p] 0x%lx -> 0x%lx", (void *)ptr, *ptr, val);
#else
void __nvart_probe_store64(uint64_t *ptr, uint64_t val, char *file, char *func,
                           int line) {
  DEBUGF("NVArt: [%s, %s(), line %d]: store i64 [%p] 0x%lx -> 0x%lx", file,
         func, line, (void *)ptr, *ptr, val);
#endif
  /* PERF: Perhaps using likely/unlikely can improve performance. */

  if (!__nvart_active || !config->tracing) return;

  if (!__store64_in_pmem(ptr)) return;

  if (config->stage == MAINPROC) __runq_push_back_store64(ptr, val);

  if (config->stage == RECOVERY) __recoverq_push_back_store64(ptr, val);
}

#ifdef NDEBUG
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize) {
  DEBUGF("NVArt: mmap addr %p size %lu", (void *)mapaddr, mapsize);
#else
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize, char *file,
                        char *func, int line) {
  DEBUGF("NVArt: [%s, %s(), line %d]: mmap addr %p size %lu", file, func, line,
         (void *)mapaddr, mapsize);
#endif
  if (!__nvart_active || !config->tracing) return;

  (void)mapaddr;
  (void)mapsize;

  /* Implementation */
  config->stage = MAINPROC;
}

void __nvart_probe_clflush(uint64_t *ptr) {
  DEBUGF("NVArt: seeing a CLFLUSH on %p", (void *)ptr);

  (void)ptr;

  if (!__nvart_active || !config->tracing) return;
}

#ifdef NDEBUG
void __nvart_probe_sfence(uint64_t sfid) {
  DEBUGF("NVArt: sfence #%lu", sfid);
#else
void __nvart_probe_sfence(uint64_t sfid, char *file, char *func, int line) {
  DEBUGF("NVArt: [%s, %s(), line %d]: sfence #%lu", file, func, line, sfid);
#endif
  if (!__nvart_active || !config->tracing) return;

  __nvart_print_runq();
  __emulate_crash(sfid);
  __runq_flush();

  TESTC("NVArt: pass over sfence #%zu", sfid);
}
