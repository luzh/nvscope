#include "nvx_runtime.h"

namespace __nvx {

/**
 * NVXRuntime handle. When the forserver is enabled, the forkserver process will
 * construct nvxrt, and a child process inherits nvxrt via forking. If nvxrt is
 * constructed by new, it does not automatically destruct when the child process
 * exits (unless using delete). If nvxrt is constructed using make_unique, it
 * automatically destructs before the child calls __nvs_fini(). No matter how
 * nvxrt is created, changes to nvxrt made by a child process all disappear when
 * the child process exits due to the copy-on-write nature of fork().
 */
static std::unique_ptr<NVXRuntime> nvxrt;

/**
 * Shared memory setup
 */
static void __nvx_setup_shm() {
  char *shmid_str = getenv(NVX_ENV_SHM);

  if (shmid_str) {
    uint32_t shmid = strtoul(shmid_str, nullptr, 0);

    void *shm_base = shmat(shmid, nullptr, 0);
    if (shm_base == reinterpret_cast<void *>(-1))
      exit(NVX_EXIT_BAD_SHM);

    auto *config = (struct nvx_config *)(shm_base);

    /* should be initialized by parent (nvscope) */
    if (!config->initialized) {
      ERRF("NVX-RT: config region not initialized");
      exit(NVX_EXIT_BAD_SHM);
    }

    struct nvx_target_config *tgconf = nullptr;
    if (config->target_type == TYPE_MAINPROC) {
      tgconf = &config->mainproc;
      OKF("NVX-RT: target mainproc attached to shared memory");
    } else if (config->target_type == TYPE_RECOVERY) {
      tgconf = &config->recovery;
      OKF("NVX-RT: target recovery attached to shared memory");
    } else {
      ERRF("NVX-RT: invalid target type");
      exit(NVX_EXIT_BAD_CONFIG);
    }

    nvxrt = std::make_unique<NVXRuntime>(shm_base, tgconf);
    if (!nvxrt) {
      ERRF("NVX-RT: creating NVX runtime failed");
      exit(NVX_EXIT_BAD_CONFIG);
    }
  } else {
    WARNF("NVX-RT: running instrumented binary but NVX runtime disabled");
  }
}

/**
 * Forkserver logic (see nvscope.c for the other part)
 */
static void __start_forkserver() {
  /* initial communication with nvscope */
  nvxrt->send_message(MSG_FORKSERVER_HELLO);

  while (true) {
    nvxrt->send_message(MSG_FORKSERVER_READY);

    enum nvx_message command = nvxrt->read_message();

    if (command == MSG_EXIT_FORKSERVER) {
      ACTF("NVX-RT: forkserver received command to exit");
      nvxrt->close_channels();
      /* If nvxrt was created by new: delete nvxrt; */
      exit(EXIT_SUCCESS);
    }

    if (command != MSG_FORK_AND_RUN) {
      ERRF("NVX-RT: received inappropriate message %d", command);
      exit(NVX_EXIT_BAD_MSG);
    }

    pid_t cpid = fork();

    /* Check afl-llvm-rt.o.c for persistent mode and using SIGCONT. */
    if (cpid < 0) {
      ERRF("NVX-RT: fork() to run the target program failed");
      exit(EXIT_FAILURE);
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
       * anymore. But nvxrt still needs them to communicate with nvscope for
       * testing requests and results.
       *
       * The child process also inherits nvxrt and all modifications to it will
       * be in copy-on-write manner. When the child exits, the forkserver
       * (parent) does not see changes that the child made to nvxrt.
       */

      nvxrt->set_target_pid(getpid());
      nvxrt->send_message(MSG_TARGET_STARTED);

      return; // execute the target progrm, e.g. from main().
    }

    DBGF("NVX-RT: target process started, pid %d", cpid);

    /*
     * DO NOT write to pipe before waitpid() returns. Otherwise races can occur
     * because the target is running and it may write to the same channel.
     */

    int status;
    pid_t cpidw = waitpid(cpid, &status, 0);
    if (cpidw < 0) {
      ERRF("NVX-RT: waitpid() for %u failed", cpid);
      exit(EXIT_FAILURE);
    } else if (cpidw == cpid) { // child process reaped
      DBGF("NVX-RT: target process %u finished", cpid);
    } else {
      ERRF("NVX-RT: unexpected waitpid() return value %u", cpidw);
    }

    nvxrt->set_target_status(status);
    nvxrt->send_message(MSG_TARGET_EXITED);
  }
}

/**
 * Initialize NVX-RT runtime data structures. Runs before the target's main()
 * with the constructor attribute.
 */
__attribute__((constructor(INIT_PRIO))) void __nvx_init() {
  if (nvxrt) {
    /**
     * Because we use forkservers, this function should not fire more than once
     * if NVX-RT is already enabled.
     */
    ERRF("NVX-RT: instrumented program already started");
    exit(EXIT_FAILURE);
  }

  __nvx_setup_shm();

  /* If not testing, return to execute the target program, e.g. from main(). */
  if (!nvxrt)
    return;

  __start_forkserver();
}

/**
 * The following functions are injected into target programs for testing. They
 * should be exposed with C linkage (declared as extern "C") if target programs
 * are written in C because C++ names are usually mangled.
 */

extern "C" void __nvx_store(void *ptr, size_t size, char *func, char *file,
                            int line) {
  MUTEF("NVX-RT: [%s() at %s:%4d]: STORE to %p size %lu", func, file, line, ptr,
        size);

  if (!nvxrt || !nvxrt->is_enabled() || !nvxrt->store_in_range(ptr, size))
    return;

  nvxrt->save_store(reinterpret_cast<uint64_t>(ptr), size, func, file, line);
}

/**
 * Replaces the standard mmap() call with this wrapped version.
 * void
 * *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
 *
 * The targeted range is determined by the program's call to mmap(). We ignore
 * stores that occur before the mmap() call.
 */
extern "C" void *__nvx_mmap(void *addr, size_t size, int prot, int flags,
                            int fd, off_t offset, char *func, char *file,
                            int line) {
  /**
   * TODO: If necessary, we can change how mmap() is called, for example, using
   * provate mapping other than shared.
   */
  void *pmap = mmap(addr, size, prot, flags, fd, offset);

  /* TODO: Save the mapped address and size for store range checking. */

  DBGF("NVX-RT: [%s() at %s:%4d]: MMAP addr %p size %lu", func, file, line,
       pmap, size);

  if (!nvxrt || !nvxrt->is_enabled())
    return pmap;

  nvxrt->save_range(reinterpret_cast<uint64_t>(pmap), size, func, file, line);

  return pmap;
}

extern "C" void __nvx_clwb(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVX-RT: [%s() at %s:%4d]: CLWB addr %p cache line %p", func, file,
        line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvxrt || !nvxrt->is_enabled())
    return;

  nvxrt->save_clfwb(addr, func, file, line);
}

extern "C" void __nvx_clflushopt(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVX-RT: [%s() at %s:%4d]: CLFLUSHOPT addr %p cache line %p", func,
        file, line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvxrt || !nvxrt->is_enabled())
    return;

  nvxrt->save_clfwb(addr, func, file, line);
}

extern "C" void __nvx_clflush(void *ptr, char *func, char *file, int line) {
  auto addr = reinterpret_cast<uintptr_t>(ptr);

  MUTEF("NVX-RT: [%s() at %s:%4d]: CLFLUSH addr %p cache line %p", epoch, func,
        file, line, ptr, reinterpret_cast<void *>(cache_addr_of(addr)));

  if (!nvxrt || !nvxrt->is_enabled())
    return;

  uint64_t epoch = nvxrt->current_epoch();

  nvxrt->save_clfwb(addr, func, file, line);

  nvxrt->check_reorder(epoch, func, file, line);
  nvxrt->check_dirty_stores(epoch, func, file, line);

  TESTC("NVX-RT: pass over epoch #%zu [clflush]", epoch);
}

extern "C" void __nvx_sfence(char *func, char *file, int line) {
  MUTEF("NVX-RT: [%s() at %s:%4d]: SFENCE", func, file, line);

  if (!nvxrt || !nvxrt->is_enabled())
    return;

  uint64_t epoch = nvxrt->current_epoch();

  nvxrt->check_reorder(epoch, func, file, line);
  nvxrt->check_dirty_stores(epoch, func, file, line);

  TESTC("NVX-RT: pass over epoch #%zu [sfence]", epoch);
}

/**
 * The destructor will run when a child process finishes or a forkserver process
 * finishes if they call exit(..) or normally terminate. If they finish via
 * calling _exit(..), this destructor will not run.
 */
__attribute__((destructor(FINI_PRIO))) void __nvx_fini() {
  DBGF("NVX-RT: process %d exit", getpid());
  /* If nvxrt was created by new: delete nvxrt; */
}

} // namespace __nvx
