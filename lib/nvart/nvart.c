#include "afl/config.h"
#include "debug.h"
#include "headers.h"
#include "nvart/config.h"

#define CONST_PRIO 0  // constructor priority

/*
 * Globals needed by the injected instrumentation. The __nvart_area_initial
 * region is used for instrumentation output before __nvart_map_shm() has a
 * chance to run. It will end up as .comm, so it shouldn't be too wasteful.
 */

uint8_t __nvart_area_initial[MAP_SIZE];
uint8_t *__nvart_area_ptr = __nvart_area_initial;

__thread uint32_t __nvart_prev_loc;

/* NVArt run-time setup */
int __nvart_testing;
struct nvart_info *info;
struct nvart_runq *runq;
enum target_stage *stage;

/* Debug functions */
void __nvart_print_runq() {
  NOTEF("--- NVArt run queue (...) ---");
  struct nvart_runq_entry *e = runq->entries;
  for (size_t i = 0; i < runq->len; i++, e++) {
    NOTEF("Entry[%zu]: i64 [%p] 0x%lx -> 0x%lx", i, e->ptr64, e->old64,
          e->new64);
  }
  NOTEF("--- NVArt run queue (***) ---");
}

/* SHM setup */
static void __nvart_map_shm(void) {
  uint8_t *id_str = getenv(NVART_SHM_ENV_VAR);

  /*
   * If we're running under NVArt, attach to the appropriate region, replacing
   * the early-stage __nvart_area_initial region that is needed to allow some
   * really hacky .init code to work correctly in projects such as OpenSSL.
   */
  if (id_str) {
    uint32_t shm_id = atoi(id_str);

    __nvart_area_ptr = shmat(shm_id, NULL, 0);

    /* Whooooops. */

    if (__nvart_area_ptr == (void *)-1) _exit(NVART_EXIT_NOSHM);

    /*
     * Write something into the bitmap so that even with low NVART_INST_RATIO,
     * our parent doesn't give up on us.
     */
    __nvart_area_ptr[0] = 1;

    __nvart_testing = 1;
    info = (struct nvart_info *)(__nvart_area_ptr);  // zeroed from parent

    stage = &info->stage;
    if (*stage == NONE) *stage = DONTCARE;

    runq = (struct nvart_runq *)(__nvart_area_ptr + NVART_SHM_RUNQ_OFF);
    if (info->probing) memset(runq, 0, NVART_SHM_RUNQ_SIZE);

    OKF("NVArt: attached to shared memory");
  } else {
    __nvart_testing = 0;

    WARNF("NVArt: shared memory not found");
  }
}

/* Forkserver logic (see nvfuzz.c for the other part) */
static void __nvart_start_forkserver(void) {
  pid_t tpid;
  enum nvart_pipe_msg ctrl, stat;

  /*
   * Phone home and tell the parent that we're OK. If parent isn't there,
   * assume we're not running in forkserver mode and just execute program.
   */
  stat = MSG_FORKSERVER_READY;
  if (write(MAINPROC_INFO, &stat, sizeof(stat)) != sizeof(stat)) {
    WARNF("NVArt: contact fuzzer failed; mainproc will run without testing");
    return;
  }

  while (1) {
    /* Wait for parent by reading from the pipe. Abort if read fails. */
    if (read(MAINPROC_CTRL, &ctrl, sizeof(ctrl)) != sizeof(ctrl)) {
      ERRF("NVArt: read() from MAINPROC_CTRL %d failed", MAINPROC_CTRL);
      _exit(EXIT_FAILURE);
    }

    if (ctrl == MSG_EXIT_FORKSERVER) {
      ACTF("NVArt: forkserver received command to exit");
      close(MAINPROC_CTRL);
      close(MAINPROC_INFO);
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
     * because the mainproc is running and it may write to MAINPROC_INFO too.
     */

    /* In parent (forkserver): write PID to pipe, then wait for mainproc. */
    // if (write(MAINPROC_INFO, &tpid, sizeof(tpid)) != sizeof(tpid)) {
    //   ERRF("NVArt: write() tpid to MAINPROC_INFO %d failed", MAINPROC_INFO);
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
      if (write(MAINPROC_INFO, &stat, sizeof(stat)) != sizeof(stat)) {
        ERRF("NVArt: write() tstatus to MAINPROC_INFO %d failed",
             MAINPROC_INFO);
        _exit(EXIT_FAILURE);
      }
    } else {
      ERRF("NVArt: unexpected waitpid() return value %u", tpidw);
    }

    /* Relay waitpid status to pipe, then loop back to restart. */
    if (write(MAINPROC_INFO, &tstatus, sizeof(tstatus)) != sizeof(tstatus)) {
      ERRF("NVArt: write() tstatus to MAINPROC_INFO %d failed", MAINPROC_INFO);
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
    __nvart_start_forkserver();
    init_done = 1;

    OKF("NVArt: analysis runtime initialized");
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

static inline void __runq_flush(uint64_t sfid) {
  /*
   * Todo: Remove flushed (clflushopt, clwb) stores from the runq, since they
   * should be persistent after the sfence and not be affected by reordering.
   * Now assume clflush(opt) or clwb is complete, and we simply set the queue
   * length to zero to flush it.
   */
  runq->len = 0;
  TESTC("NVArt: pass over sfence #%zu", sfid);
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
    if (write(MAINPROC_INFO, &req, sizeof(req)) != sizeof(req)) {
      ERRF("NVArt: write() to MAINPROC_INFO %d failed", MAINPROC_INFO);
      _exit(EXIT_FAILURE);
    }

    if (read(MAINPROC_CTRL, &result, sizeof(result)) != sizeof(result)) {
      ERRF("NVArt: read() from MAINPROC_CTRL %d failed", MAINPROC_CTRL);
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
  NOTEF("NVArt: store i64 [%p] 0x%lx -> 0x%lx", (void *)ptr, *ptr, val);
#else
void __nvart_probe_store64(uint64_t *ptr, uint64_t val, char *file, char *func,
                           int line) {
  NOTEF("NVArt: [%s, %s(), line %d]: store i64 [%p] 0x%lx -> 0x%lx", file, func,
        line, (void *)ptr, *ptr, val);
#endif
  /* PERF: Perhaps using likely/unlikely can improve performance. */

  if (!__nvart_testing || !info->probing) return;

  if (!__store64_in_pmem(ptr)) return;

  if (*stage == MAINPROC) __runq_push_back_store64(ptr, val);

  if (*stage == RECOVERY) __recoverq_push_back_store64(ptr, val);
}

#ifdef NDEBUG
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize) {
  WARNF("NVArt: mmap addr %p size %lu", (void *)mapaddr, mapsize);
#else
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize, char *file,
                        char *func, int line) {
  WARNF("NVArt: [%s, %s(), line %d]: mmap addr %p size %lu", file, func, line,
        (void *)mapaddr, mapsize);
#endif
  if (!__nvart_testing || !info->probing) return;

  /* Implementation */
  *stage = MAINPROC;
}

void __nvart_probe_clflush(uint64_t *ptr) {
  NOTEF("NVArt: seeing a CLFLUSH on %p", (void *)ptr);

  if (!__nvart_testing || !info->probing) return;
}

#ifdef NDEBUG
void __nvart_probe_sfence(uint64_t sfid) {
  NOTEF("NVArt: sfence #%lu", sfid);
#else
void __nvart_probe_sfence(uint64_t sfid, char *file, char *func, int line) {
  NOTEF("NVArt: [%s, %s(), line %d]: sfence #%lu", file, func, line, sfid);
#endif
  if (!__nvart_testing || !info->probing) return;

  __nvart_print_runq();
  __emulate_crash(sfid);
  __runq_flush(sfid);
}
