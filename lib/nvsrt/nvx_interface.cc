#include "nvx_runtime.h"

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

  uint64_t time = ++timestamp;
  nvsrt->save_store(time, reinterpret_cast<uint64_t>(ptr), size, func, file,
                    line);
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

  uint64_t time = ++timestamp;
  nvsrt->save_clfwb(time, addr, func, file, line);
}

extern "C" void __nvs_clflushopt(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVS-RT: [%s() at %s:%4d]: CLFLUSHOPT addr %p cache line %p", func,
        file, line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  uint64_t time = ++timestamp;
  nvsrt->save_clfwb(time, addr, func, file, line);
}

extern "C" void __nvs_clflush(void *ptr, char *func, char *file, int line) {
  uint64_t epoch = ++epochid;
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVS-RT: epoch %zu [%s() at %s:%4d]: CLFLUSH addr %p cache line %p",
        epoch, func, file, line, ptr,
        reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvsrt || !nvsrt->is_enabled())
    return;

  uint64_t time = ++timestamp;
  nvsrt->save_clfwb(time, addr, func, file, line);

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
