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
struct nvart_info *nvai;
enum prog_state *pstate;

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
    nvai = (struct nvart_info *)(__nvart_area_ptr);
    pstate = &nvai->pstate;
    *pstate = DONTCARE;

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

static inline int __store64_in_pmem(void *ptr) {
  (void)ptr;

  return 1;
}

static inline int __runq_push_back_store64(void *ptr, uint64_t val) {
  (void)ptr;
  (void)val;

  return 0;
}

static inline int __recoverq_push_back_store64(void *ptr, uint64_t val) {
  (void)ptr;
  (void)val;

  return 0;
}

#ifdef NDEBUG
void __nvart_probe_store64(uint64_t *ptr, uint64_t val) {
  TESTF("Store64 [%p] <- %zu", (void *)ptr, val);
#else
void __nvart_probe_store64(uint64_t *ptr, uint64_t val, char *file, char *func,
                           int line) {
  TESTF("[%s, %s(), line %d]: Store64 [%p] <- %zu", file, func, line,
        (void *)ptr, val);
#endif
  /* PERF: Perhaps using likely/unlikely can improve performance. */
  if (!__nvart_testing) return;

  if (__store64_in_pmem(ptr)) return;

  if (*pstate == NORMAL) __runq_push_back_store64(ptr, val);

  if (*pstate == RECOVERY) __recoverq_push_back_store64(ptr, val);

  // srand(time(0));
  // if (rand() & 1) {
  //  *ptr = val;
  //  ACTF("Performing store %zu to %p", val, (void *)ptr);
  //} else {
  //  TESTF("Skipping store %zu to %p", val, (void *)ptr);
  //}
}

#ifdef NDEBUG
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize) {
  TESTF("Mmap addr %p size %lu", (void *)mapaddr, mapsize);
#else
void __nvart_probe_mmap(uint64_t mapaddr, uint64_t mapsize, char *file,
                        char *func, int line) {
  TESTF("[%s, %s(), line %d]: Mmap addr %p size %lu", file, func, line,
        (void *)mapaddr, mapsize);
#endif
  if (!__nvart_testing) return;

  /* Implementation */
  *pstate = NORMAL;
}

void __nvart_probe_clflush(uint64_t *ptr) {
  TESTF("Seeing a CLFLUSH on %p", (void *)ptr);

  if (!__nvart_testing) return;
}

#ifdef NDEBUG
void __nvart_probe_sfence(uint64_t sfid) {
  TESTF("SFence #%lu", sfid);
#else
void __nvart_probe_sfence(uint64_t sfid, char *file, char *func, int line) {
  TESTF("[%s, %s(), line %d]: SFence #%lu", file, func, line, sfid);
#endif
  if (!__nvart_testing) return;

  /* Implementation */
}
