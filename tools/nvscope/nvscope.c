#define AFL_MAIN
#define MESSAGES_TO_STDOUT
#define _FILE_OFFSET_BITS 64

#include "afl/alloc-inl.h"
#include "headers.h"
#include "nvscope/config.h"
#include "utils.h"

#define HAVE_AFFINITY 1

static char mainproc[BINARY_PATH_LEN_MAX];
static char recovery[BINARY_PATH_LEN_MAX];

static int32_t shm_id; /* ID of the SHM region */
static char* shm_base; /* pointer to the SHM region */

/**
 * Communication functions
 *
 * Now we use pipes. It is possible to change them to use other mechanisms.
 */
static inline void send_message(int channel, enum nvs_message msg) {
  if (write(channel, &msg, sizeof(msg)) != sizeof(msg))
    PFATAL("NVScope: write() to channel %d failed", channel);
}

static inline enum nvs_message read_message(int channel) {
  enum nvs_message msg;
  if (read(channel, &msg, sizeof(msg)) != sizeof(msg))
    PFATAL("NVScope: read() from channel %d failed", channel);
  return msg;
}

#if 0
static inline void read_data(int channel, void* data, ssize_t len) {
  if (read(channel, data, len) != len)
    PFATAL("NVScope: read() from channel %d failed", channel);
}
#endif

/**
 * Do a PATH search and find target binaries to see that it exists and isn't a
 * shell script - a common and painful mistake. We also check for a valid ELF
 * header and for evidence of AFL instrumentation.
 */
static void check_binary(char* fname, char* target) {
  char* bin_path = NULL;
  char* env_path = NULL;
  struct stat st;

  int fd;
  char* f_data;
  uint32_t f_len = 0;

  ACTF("Validating target binary '%s'...", fname);

  if (strchr(fname, '/') || !(env_path = getenv("PATH"))) {
    bin_path = ck_strdup(fname);
    if (stat(bin_path, &st) || !S_ISREG(st.st_mode) || !(st.st_mode & 0111) ||
        (f_len = st.st_size) < 4) {
      ck_free(bin_path);
      FATAL("NVScope: '%s' not found or not executable", fname);
    }

  } else {
    while (env_path) {
      char *cur_elem, *delim = strchr(env_path, ':');

      if (delim) {
        cur_elem = ck_alloc(delim - env_path + 1);
        memcpy(cur_elem, env_path, delim - env_path);
        delim++;

      } else {
        cur_elem = ck_strdup(env_path);
      }

      env_path = delim;

      if (cur_elem[0])
        bin_path = alloc_printf("%s/%s", cur_elem, fname);
      else
        bin_path = ck_strdup(fname);

      ck_free(cur_elem);

      if (!stat(bin_path, &st) && S_ISREG(st.st_mode) && (st.st_mode & 0111) &&
          (f_len = st.st_size) >= 4)
        break;

      ck_free(bin_path);
      bin_path = NULL;
    }

    if (!bin_path) FATAL("NVScope: '%s' not found or not executable", fname);
  }

  if (getenv("NVS_SKIP_BIN_CHECK")) return;

  if (target == NULL) FATAL("NVScope: invalid buffer to store target's path");
  size_t bin_path_len = strlen(bin_path);
  if (BINARY_PATH_LEN_MAX <= bin_path_len) {
    ck_free(bin_path);
    SAYF("\n" cLRD "[-] " cRST
         "Oops, the target buffer length is not large enough to store the\n"
         "    target binary's path. Try to increase BINARY_PATH_MLEN_MAX.\n");
    FATAL("NVScope: BINARY_PATH_LEN_LEN %zu <= bin_path_len %zu",
          BINARY_PATH_LEN_MAX, bin_path_len);
  }

  memcpy(target, bin_path, bin_path_len);
  target[bin_path_len] = 0;
  ck_free(bin_path);

  /* Check for blatant user errors. */

  if ((!strncmp(target, "/tmp/", 5) && !strchr(target + 5, '/')) ||
      (!strncmp(target, "/var/tmp/", 9) && !strchr(target + 9, '/')))
    FATAL("NVScope: please don't keep binaries in /tmp or /var/tmp");

  fd = open(target, O_RDONLY);

  if (fd < 0) PFATAL("NVScope: unable to open '%s'", target);

  f_data = mmap(0, f_len, PROT_READ, MAP_PRIVATE, fd, 0);

  if (f_data == MAP_FAILED) PFATAL("NVScope: unable to mmap file '%s'", target);

  close(fd);

  if (f_data[0] == '#' && f_data[1] == '!') {
    SAYF("\n" cLRD "[-] " cRST
         "Oops, the target binary looks like a shell script.\n");
    FATAL("NVScope: '%s' is a shell script", target);
  }

  if (f_data[0] != 0x7f || memcmp(f_data + 1, "ELF", 3))
    FATAL("NVScope: '%s' is not an ELF binary", target);

  if (!memmem(f_data, f_len, NVS_ENV_SHM, strlen(NVS_ENV_SHM) + 1)) {
    SAYF("\n" cLRD "[-] " cRST
         "Looks like the target binary is not instrumented!\n");
    FATAL("NVScope: no instrumentation detected - '%s' not found", NVS_ENV_SHM);
  }

#if 0
  if (memmem(f_data, f_len, "libasan.so", 10) ||
      memmem(f_data, f_len, "__msan_init", 11))
    uses_asan = 1;

  /* Detect persistent & deferred init signatures in the binary. */

  if (memmem(f_data, f_len, PERSIST_SIG, strlen(PERSIST_SIG) + 1)) {
    OKF(cPIN "Persistent mode binary detected");
    setenv(PERSIST_ENV_VAR, "1", 1);
    persistent_mode = 1;
  } else if (getenv("AFL_PERSISTENT")) {
    WARNF("AFL_PERSISTENT is no longer supported and may misbehave!");
  }

  if (memmem(f_data, f_len, DEFER_SIG, strlen(DEFER_SIG) + 1)) {
    OKF(cPIN "Deferred forkserver binary detected");
    setenv(DEFER_ENV_VAR, "1", 1);
    deferred_mode = 1;
  } else if (getenv("AFL_DEFER_FORKSRV")) {
    WARNF("AFL_DEFER_FORKSRV is no longer supported and may misbehave!");
  }
#endif

  if (munmap(f_data, f_len)) PFATAL("NVScope: unmap() failed");
}

static int check_status(int status, pid_t pid, char* pname) {
  int err = 1;

  if (pname == NULL) pname = "(unnamed)";

  if (WIFEXITED(status)) {
    int exstatus = WEXITSTATUS(status);
    if (exstatus == 0) {
      err = 0;
      DBGF("NVScope: %s process %u exited normally", pname, pid);
    } else if (exstatus == 127) {
      ERRF("NVScope: execv() for %s process %u failed", pname, pid);
    } else {
      ERRF("NVScope: %s process %u exited, status %d", pname, pid, exstatus);
    }
  } else if (WIFSTOPPED(status)) {
    ERRF("NVScope: %s process %u has stopped %u", pname, pid, WSTOPSIG(status));
  } else if (WIFSIGNALED(status)) {
    ERRF("NVScope: %s process %u killed by signal %u", pname, pid,
         WTERMSIG(status));
  } else {
    ERRF("NVScope: %s process %u died for unknown reasons", pname, pid);
  }

  return err;
}

/*
 * Get rid of shared memory (atexit handler).
 */
static void remove_shm(void) {
  shmctl(shm_id, IPC_RMID, NULL);
  OKF("Shared memory removed");
}

/*
 * Configure shared memory and virgin_bits. This is called at startup.
 */
static void setup_shm(void) {
  char* shm_str;

#if 0
  if (!in_bitmap) memset(virgin_bits, 255, MAP_SIZE);

  memset(virgin_tmout, 255, MAP_SIZE);
  memset(virgin_crash, 255, MAP_SIZE);
#endif

  shm_id = shmget(IPC_PRIVATE, MAP_SIZE, IPC_CREAT | IPC_EXCL | 0600);
  if (shm_id < 0) PFATAL("shmget() failed");

  OKF("Shared memory created");

  atexit(remove_shm);

  shm_str = alloc_printf("%d", shm_id);
  setenv(NVS_ENV_SHM, shm_str, 1);

  ck_free(shm_str);

  shm_base = shmat(shm_id, NULL, 0);
  if (!shm_base) PFATAL("shmat() failed");

  memset(shm_base, 0, MAP_SIZE);
}

/*
 * Spin up a forkserver. The idea is explained here:
 * http://lcamtuf.blogspot.com/2014/10/fuzzing-binaries-without-execve.html
 *
 * In essence, the instrumentation allows us to skip execve(), and just keep
 * cloning a stopped child. So, we just execute once, and then send commands
 * through a pipe. The other part of this logic is in lib/nvsrt/nvsrt.c.
 */
static pid_t start_forkserver(char* target, char** target_argv,
                              struct nvs_target_config* target_conf,
                              int* parent_read_fd, int* parent_write_fd) {
  int info_fds[2], ctrl_fds[2];

  assert(target_conf != NULL && parent_read_fd != NULL &&
         parent_write_fd != NULL);

  if (pipe(info_fds) || pipe(ctrl_fds))
    PFATAL("NVScope: pipe() for the target program failed");

  pid_t fksv_pid = fork();
  if (fksv_pid < 0)
    PFATAL("NVScope: fork() to run the target program's forkserver failed");

  if (fksv_pid == 0) {  // target program's forkserver process
    struct rlimit rlim;

    /*
     * Dumping cores is slow and can lead to anomalies if SIGKILL is delivered
     * before the dump is complete.
     */
    rlim.rlim_max = rlim.rlim_cur = 0;
    setrlimit(RLIMIT_CORE, &rlim);

    /*
     * Isolate the process and configure standard descriptors. If out_file is
     * specified, stdin is /dev/null; otherwise, out_fd is cloned instead.
     */
    setsid();

    /* Set up control and status pipes, close the unneeded original fds. */
    target_conf->read_fd = ctrl_fds[0];
    target_conf->write_fd = info_fds[1];

    close(ctrl_fds[1]);
    close(info_fds[0]);

    execv(target, target_argv);

    /* If execv() succeeds, it should not return (getting here). */
    FATAL("NVScope: unable to execute the target program '%s'", target);
  }

  /* Close the unneeded endpoints. */
  close(ctrl_fds[0]);
  close(info_fds[1]);

  *parent_read_fd = info_fds[0];
  *parent_write_fd = ctrl_fds[1];

  /* Check afl-fuzz.c for using setitimer() and SIGALARM to kill. */
  ACTF("NVScope: waiting for the forkserver to come up...");

  /*
   * If we have ready message from the forkserver, we're all set. Otherwise,
   * try to figure out what went wrong with waitpid().
   */
  if (read_message(*parent_read_fd) == MSG_FORKSERVER_HELLO) {
    OKF("NVScope: target program's forkserver is up, pid %u", fksv_pid);
    target_conf->fksv_pid = fksv_pid;
    return fksv_pid;
  }

  int status;
  pid_t pidw = waitpid(fksv_pid, &status, 0);

  if (pidw < 0) {
    ERRF("NVScope: waitpid(%u) failed", fksv_pid);
  } else if (pidw == fksv_pid) {  // target's forkserver reaped
    check_status(status, fksv_pid, "target's forkserver");
  } else {
    ERRF("NVScope: unexpected waitpid() return value %u", pidw);
  }

  /* Check afl-fuzz.c for more detailed parsing of failure status. */
  ERRF("NVScope: forkserver handshake failed");

  return -1;
}

int main(int argc, char** argv) {
  COMPILE_ERROR_ON(MAP_SIZE < NVS_SHM_RUNQ_OFF + NVS_SHM_RUNQ_SIZE);
  COMPILE_ERROR_ON(sizeof(struct nvs_target_config) != CLSIZE);
  COMPILE_ERROR_ON(!ALIGNED_CL(OFFSETOF(struct nvs_config, mainproc)));

  if (argc < 2) FATAL("Usage: %s <mainproc>", argv[0]);

  char** mainproc_argv = argv + 1;  // skip the nvscope program

  check_binary(argv[1], mainproc);
  ACTF("Preparing to test program %s", mainproc);

  setup_shm();

  struct nvs_config* config = (struct nvs_config*)(shm_base);
  config->initialized = 1;

  struct nvs_target_config* tgconf_main = &config->mainproc;
  struct nvs_target_config* tgconf_reco = &config->recovery;

  int main_ctrl_fd, main_info_fd;  // mainproc control pipes
  int reco_ctrl_fd, reco_info_fd;  // recovery control pipes

  ACTF("NVScope: spinning up the forkserver for mainproc...");
  config->target_type = TYPE_MAINPROC;  // must set before start_forkserver()
  pid_t main_fksv_pid = start_forkserver(mainproc, mainproc_argv, tgconf_main,
                                         &main_info_fd, &main_ctrl_fd);
  if (main_fksv_pid < 0)
    FATAL("NVScope: mainproc's forkserver failed to start");

  // Todo: Get recovery process from command line options.
  memcpy(recovery, mainproc, BINARY_PATH_LEN_MAX);
  char* recovery_argv[] = {recovery, "stackfile", "check", NULL};

  ACTF("NVScope: spinning up the forkserver for recovery...");
  config->target_type = TYPE_RECOVERY;  // must set before start_forkserver()
  pid_t reco_fksv_pid = start_forkserver(recovery, recovery_argv, tgconf_reco,
                                         &reco_info_fd, &reco_ctrl_fd);
  if (reco_fksv_pid < 0)
    FATAL("NVScope: recovery's forkserver failed to start");

  int fatal = 0, stop = 0;
  int status, bug = 0;
  uint64_t testcases = 0;
  enum nvs_message main_info, main_ctrl, reco_info;

  /* testing mainproc but not recovery */
  tgconf_main->tracing = 1;
  tgconf_reco->tracing = 0;

  benchmark_time_t start, end;
  benchmark_time_get(&start);

  while (!fatal && !stop) {
    /* wait for requests from targets */
    main_info = read_message(main_info_fd);

    switch (main_info) {
      case MSG_FORKSERVER_READY:
        send_message(main_ctrl_fd, MSG_FORK_AND_RUN);
        break;
      case MSG_TARGET_STARTED:
        DBGF("NVScope: target process started, pid %d", tgconf_main->pid);
        break;
      case MSG_AWAITING_CHECK:
        DBGF("NVScope: mainproc requested to run recovery and checking");

        ++testcases;

        reco_info = read_message(reco_info_fd);
        if (reco_info != MSG_FORKSERVER_READY) {
          ERRF("NVScope: received inappropriate message %d", reco_info);
          fatal = 1;
          break;
        }

        send_message(reco_ctrl_fd, MSG_FORK_AND_RUN);

        reco_info = read_message(reco_info_fd);
        if (reco_info != MSG_TARGET_STARTED) {
          ERRF("NVScope: received inappropriate message %d", reco_info);
          fatal = 1;
          break;
        }
        DBGF("NVScope: recovery process started, pid %d", tgconf_reco->pid);

        reco_info = read_message(reco_info_fd);
        if (reco_info != MSG_TARGET_EXITED) {
          ERRF("NVScope: received inappropriate message %d", reco_info);
          fatal = 1;
          break;
        }

        bug = check_status(tgconf_reco->status, tgconf_reco->pid, "recovery");
        main_ctrl = bug ? MSG_SHOW_BUG_AND_EXIT : MSG_CONTINUE_TO_RUN;
        send_message(main_ctrl_fd, main_ctrl);
        break;
      case MSG_TARGET_EXITED:
        check_status(tgconf_main->status, tgconf_main->pid, "mainproc");
        send_message(main_ctrl_fd, MSG_EXIT_FORKSERVER);
        send_message(reco_ctrl_fd, MSG_EXIT_FORKSERVER);
        stop = 1;  // can restart the mainproc process
        break;
      default:
        ERRF("NVScope: received inappropriate message %d", main_info);
        fatal = 1;
        break;
    }
  }

  if (fatal) {
    /* Todo: Should kill forked processes: forkservers, mainproc & recovery. */
    exit(EXIT_FAILURE);
  }

  /* wait for forkservers to exit */
  pid_t main_fksv_pidw = waitpid(main_fksv_pid, &status, 0);

  if (main_fksv_pidw < 0) {
    ERRF("NVScope: waitpid(%u) failed", main_fksv_pid);
  } else if (main_fksv_pidw == main_fksv_pid) {
    check_status(status, main_fksv_pid, "mainproc's forkserver");
  } else {
    ERRF("NVScope: unexpected waitpid() return value %u", main_fksv_pidw);
  }

  pid_t reco_fksv_pidw = waitpid(reco_fksv_pid, &status, 0);

  if (reco_fksv_pidw < 0) {
    ERRF("NVScope: waitpid(%u) failed", reco_fksv_pid);
  } else if (reco_fksv_pidw == reco_fksv_pid) {
    check_status(status, reco_fksv_pid, "recovery's forkserver");
  } else {
    ERRF("NVScope: unexpected waitpid() return value %u", reco_fksv_pidw);
  }

  benchmark_time_t runtime;
  benchmark_time_get(&end);
  benchmark_time_diff(&runtime, &start, &end);
  unsigned long long runns = benchmark_time_get_nsecs(&runtime);
  unsigned long long avgns = runns / testcases;

  OKF("NVScope: total elapsed time to run %zu test cases is %lld ns, per test "
      "case time is %lld ns",
      testcases, runns, avgns);

  return 0;
}
