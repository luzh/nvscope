#include "afl/config.h"
#include "headers.h"
#include "nvart/config.h"
#include "pprint.h"

#define CONST_PRIO 0  // constructor priority

/*
 * Globals needed by the injected instrumentation. The __nvart_area_initial
 * region is used for instrumentation output before __nvart_map_shm() has a
 * chance to run. It will end up as .comm, so it shouldn't be too wasteful.
 */
uint8_t __nvart_area_initial[MAP_SIZE];
uint8_t *__nvart_area_ptr = __nvart_area_initial;

__thread uint32_t __nvart_prev_loc;

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

    PPISTR("NVArt SHM attached");
  } else {
    PPWARN("NVArt SHM NOT found");
  }
}

/* Fork server logic */
static void __nvart_start_forkserver(void) {
  /* setup NVART forkserver */
  PPWARN("NVArt forkserver logic not implemented");
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

    PPISTR("NVArt runtime initialized");
  }
}

void condstore(uint64_t *ptr, uint64_t val) {
  srand(time(0));
  if (rand() & 1) {
    *ptr = val;
    PPISTR("Performing store %zu to %p", val, (void *)ptr);
  } else {
    PPISTR("Skipping store %zu to %p", val, (void *)ptr);
  }
}
