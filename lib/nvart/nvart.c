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
enum prog_state *pstate;

/* Debug functions */
void __nvart_print_runq() {
  SAYF("--- NVArt run queue data ---\n");
  struct nvart_runq_entry *e = runq->entries;
  for (size_t i = 0; i < runq->len; i++, e++) {
    SAYF("Entry[%zu]: i64 [%p] 0x%lx -> 0x%lx\n", i, e->ptr64, e->old64,
         e->new64);
  }
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

    if (__nvart_area_ptr == (void *)-1) _exit(1);

    /*
     * Write something into the bitmap so that even with low NVART_INST_RATIO,
     * our parent doesn't give up on us.
     */
    __nvart_area_ptr[0] = 1;

    __nvart_testing = 1;
    info = (struct nvart_info *)(__nvart_area_ptr);

    pstate = &info->pstate;
    *pstate = DONTCARE;

    runq = (struct nvart_runq *)(__nvart_area_ptr + NVART_SHM_RUNQ_OFF);
    memset(runq, 0, NVART_SHM_RUNQ_SIZE);

    OKF("NVArt SHM attached");
  } else {
    __nvart_testing = 0;

    WARNF("NVArt SHM NOT found");
  }
}

/* Fork server logic */
static void __nvart_start_forkserver(void) {
  /* setup NVART forkserver */
  WARNF("NVArt forkserver logic not implemented");
}

/*
 * Initialize NVArt run-time data structures. Runs before target's main() with
 * the constructor attribute.
 */
__attribute__((constructor(CONST_PRIO))) void __nvart_init(void) {
  static uint32_t init_done;

  if (!init_done) {
    __nvart_map_shm();
    __nvart_start_forkserver();
    init_done = 1;

    OKF("NVArt analysis runtime initialized");
  }
}

static inline int __store64_in_pmem(uint64_t *ptr) {
  (void)ptr;

  return 1;
}

static inline int __runq_push_back_store64(uint64_t *ptr, uint64_t val) {
  if (runq->len == NVART_SHM_RUNQ_MAX_LEN) {
    ERRF("Run queue is full (%lu entries)!\n", runq->len);
    _exit(EXIT_RUNQ_FULL);
  }

  runq->entries[runq->len].ptr64 = ptr;
  runq->entries[runq->len].old64 = *ptr;
  runq->entries[runq->len].new64 = val;
  runq->len += 1;

  __nvart_print_runq();

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

static inline void __runq_evict() {
  if (runq->len == 0) return;

  size_t stid = 0;
  struct nvart_runq_entry *e = &runq->entries[stid];

  *e->ptr64 = e->old64;

  TESTF("Revert store i64 [%p] 0x%lx <- 0x%lx", e->ptr64, e->old64, e->new64);
}

static inline int __recoverq_push_back_store64(uint64_t *ptr, uint64_t val) {
  (void)ptr;
  (void)val;

  return 0;
}

#ifdef NDEBUG
void __nvart_probe_store64(uint64_t *ptr, uint64_t val) {
  TESTF("store i64 [%p] 0x%lx -> 0x%lx", (void *)ptr, *ptr, val);
#else
void __nvart_probe_store64(uint64_t *ptr, uint64_t val, char *file, char *func,
                           int line) {
  TESTF("[%s, %s(), line %d]: store i64 [%p] 0x%lx -> 0x%lx", file, func, line,
        (void *)ptr, *ptr, val);
#endif
  /* PERF: Perhaps using likely/unlikely can improve performance. */
  if (!__nvart_testing) return;

  if (!__store64_in_pmem(ptr)) return;

  if (*pstate == NORMAL) __runq_push_back_store64(ptr, val);

  if (*pstate == RECOVERY) __recoverq_push_back_store64(ptr, val);
}

#ifdef NDEBUG
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize) {
  TESTF("mmap addr %p size %lu", (void *)mapaddr, mapsize);
#else
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize, char *file,
                        char *func, int line) {
  TESTF("[%s, %s(), line %d]: mmap addr %p size %lu", file, func, line,
        (void *)mapaddr, mapsize);
#endif
  if (!__nvart_testing) return;

  /* Implementation */
  *pstate = NORMAL;
}

void __nvart_probe_clflush(uint64_t *ptr) {
  TESTF("seeing a CLFLUSH on %p", (void *)ptr);

  if (!__nvart_testing) return;
}

#ifdef NDEBUG
void __nvart_probe_sfence(uint64_t sfid) {
  TESTF("sfence #%lu", sfid);
#else
void __nvart_probe_sfence(uint64_t sfid, char *file, char *func, int line) {
  TESTF("[%s, %s(), line %d]: sfence #%lu", file, func, line, sfid);
#endif
  if (!__nvart_testing) return;

  int crash = (sfid == 1);

  if (crash) {
    __runq_evict();
    _exit(EXIT_CRASH);
  } else {
    __runq_flush();
  }
}
