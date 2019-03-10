#define AFL_MAIN
#define MESSAGES_TO_STDOUT
#define _FILE_OFFSET_BITS 64

#include "afl/alloc-inl.h"
#include "headers.h"
#include "nvart/config.h"

#define HAVE_AFFINITY 1

static char mainproc[BINARY_PATH_LEN_MAX];
// static char recovery[BINARY_PATH_LEN_MAX];

static int32_t shm_id; /* ID of the SHM region */
static char* shm_base; /* pointer to the SHM region */

/**
 * Communication functions
 *
 * Now we use pipes. It is possible to change them to use other mechanisms.
 */
static inline void send_message(int channel, enum nvart_message msg) {
  if (write(channel, &msg, sizeof(msg)) != sizeof(msg))
    PFATAL("NVFuzz: write() to channel %d failed", channel);
}

static inline enum nvart_message read_message(int channel) {
  enum nvart_message msg;
  if (read(channel, &msg, sizeof(msg)) != sizeof(msg))
    PFATAL("NVFuzz: read() from channel %d failed", channel);
  return msg;
}

#if 0
static inline void read_data(int channel, void* data, ssize_t len) {
  if (read(channel, data, len) != len)
    PFATAL("NVFuzz: read() from channel %d failed", channel);
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
      FATAL("NVFuzz: '%s' not found or not executable", fname);
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

    if (!bin_path) FATAL("NVFuzz: '%s' not found or not executable", fname);
  }

  if (getenv("NVART_SKIP_BIN_CHECK")) return;

  if (target == NULL) FATAL("NVFuzz: invalid buffer to store target's path");
  size_t bin_path_len = strlen(bin_path);
  if (BINARY_PATH_LEN_MAX <= bin_path_len) {
    ck_free(bin_path);
    SAYF("\n" cLRD "[-] " cRST
         "Oops, the target buffer length is not large enough to store the\n"
         "    target binary's path. Try to increase BINARY_PATH_MLEN_MAX.\n");
    FATAL("NVFuzz: BINARY_PATH_LEN_LEN %zu <= bin_path_len %zu",
          BINARY_PATH_LEN_MAX, bin_path_len);
  }

  memcpy(target, bin_path, bin_path_len);
  target[bin_path_len] = 0;
  ck_free(bin_path);

  /* Check for blatant user errors. */

  if ((!strncmp(target, "/tmp/", 5) && !strchr(target + 5, '/')) ||
      (!strncmp(target, "/var/tmp/", 9) && !strchr(target + 9, '/')))
    FATAL("NVFuzz: please don't keep binaries in /tmp or /var/tmp");

  fd = open(target, O_RDONLY);

  if (fd < 0) PFATAL("NVFuzz: unable to open '%s'", target);

  f_data = mmap(0, f_len, PROT_READ, MAP_PRIVATE, fd, 0);

  if (f_data == MAP_FAILED) PFATAL("NVFuzz: unable to mmap file '%s'", target);

  close(fd);

  if (f_data[0] == '#' && f_data[1] == '!') {
    SAYF("\n" cLRD "[-] " cRST
         "Oops, the target binary looks like a shell script.\n");
    FATAL("NVFuzz: '%s' is a shell script", target);
  }

  if (f_data[0] != 0x7f || memcmp(f_data + 1, "ELF", 3))
    FATAL("NVFuzz: '%s' is not an ELF binary", target);

  if (!memmem(f_data, f_len, NVART_ENV_SHM, strlen(NVART_ENV_SHM) + 1)) {
    SAYF("\n" cLRD "[-] " cRST
         "Looks like the target binary is not instrumented!\n");
    FATAL("NVFuzz: no instrumentation detected - '%s' not found",
          NVART_ENV_SHM);
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

  if (munmap(f_data, f_len)) PFATAL("NVFuzz: unmap() failed");
}

static int check_status(int status, pid_t pid, char* pname) {
  int err = 1;

  if (pname == NULL) pname = "(unnamed)";

  if (WIFEXITED(status)) {
    int exstatus = WEXITSTATUS(status);
    if (exstatus == 0) {
      err = 0;
      OKF("NVFuzz: %s process %u exited normally", pname, pid);
    } else if (exstatus == 127) {
      ERRF("NVFuzz: execv() for %s process %u failed", pname, pid);
    } else {
      ERRF("NVFuzz: %s process %u exited, status %d", pname, pid, exstatus);
    }
  } else if (WIFSTOPPED(status)) {
    ERRF("NVFuzz: %s process %u has stopped %u", pname, pid, WSTOPSIG(status));
  } else if (WIFSIGNALED(status)) {
    ERRF("NVFuzz: %s process %u killed by signal %u", pname, pid,
         WTERMSIG(status));
  } else {
    ERRF("NVFuzz: %s process %u died for unknown reasons", pname, pid);
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

  /*
   * If somebody is asking us to fuzz instrumented binaries in dumb mode, we
   * don't want them to detect instrumentation, since we won't be sending fork
   * server commands. This should be replaced with better auto-detection later
   * on, perhaps?
   */

  setenv(NVART_ENV_SHM, shm_str, 1);

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
 * through a pipe. The other part of this logic is in lib/nvart/nvart.c.
 */
static pid_t start_forkserver(char* target, char** target_argv,
                              struct nvart_target_config *target_conf,
                              int* parent_read_fd, int* parent_write_fd) {
  int info_fds[2], ctrl_fds[2];

  ACTF("NVFuzz: spinning up the fork server for '%s'...", target);

  assert(target_conf != NULL && parent_read_fd != NULL &&
         parent_write_fd != NULL);

  if (pipe(info_fds) || pipe(ctrl_fds))
    PFATAL("NVFuzz: pipe() for the target program failed");

  pid_t fksv_pid = fork();
  if (fksv_pid < 0)
    PFATAL("NVFuzz: fork() to run the target program's forkserver failed");

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
    FATAL("NVFuzz: unable to execute the target program '%s'", target);
  }

  /* Close the unneeded endpoints. */
  close(ctrl_fds[0]);
  close(info_fds[1]);

  *parent_read_fd = info_fds[0];
  *parent_write_fd = ctrl_fds[1];

  /* Check afl-fuzz.c for using setitimer() and SIGALARM to kill. */
  ACTF("NVFuzz: waiting for the forkserver to come up...");

  enum nvart_message info;
  /* This call blocks if no data comes though the pipe. */
  ssize_t rlen = read(*parent_read_fd, &info, sizeof(info));

  /*
   * If we have ready message from the forkserver, we're all set. Otherwise,
   * try to figure out what went wrong with waitpid().
   */
  if (rlen == sizeof(info) && info == MSG_FORKSERVER_HELLO) {
    OKF("NVFuzz: target program's forkserver is up, pid %u", fksv_pid);
    target_conf->fksv_pid = fksv_pid;
    return fksv_pid;
  }

  int status;
  pid_t pidw = waitpid(fksv_pid, &status, 0);

  if (pidw < 0) {
    ERRF("NVFuzz: waitpid(%u) failed", fksv_pid);
  } else if (pidw == fksv_pid) {  // target's forkserver reaped
    check_status(status, fksv_pid, "target's forkserver");
  } else {
    ERRF("NVFuzz: unexpected waitpid() return value %u", pidw);
  }

  /* Check afl-fuzz.c for more detailed parsing of failure status. */
  ERRF("Fork server handshake failed");

  return -1;
}

int main(int argc, char** argv) {
  COMPILE_ERROR_ON(sizeof(struct nvart_target_config) != CLSIZE);
  COMPILE_ERROR_ON(!ALIGNED_CL(OFFSETOF(struct nvart_config, mainproc)));

  if (argc < 2) FATAL("Usage: %s <mainproc>", argv[0]);

  char** mainproc_argv = argv + 1;  // skip the fuzzer program

  check_binary(argv[1], mainproc);
  ACTF("Preparing to test program %s", mainproc);

  setup_shm();

  struct nvart_config* config = (struct nvart_config*)(shm_base);

  config->initialized = 1;
  config->tracing = 1;
  config->target_type = TYPE_MAINPROC;

  struct nvart_target_config* tgconf_main = &config->mainproc;

  int main_ctrl_fd, main_info_fd; /* forkserver control pipes */

  /* must set fds in SHM before starting the corresponding forkserver */
  tgconf_main->tracing = 1;
  pid_t main_fksv_pid =
      start_forkserver(mainproc, mainproc_argv, tgconf_main,
                       &main_info_fd, &main_ctrl_fd);
  if (main_fksv_pid < 0)
    FATAL("NVFuzz: initialize the mainproc's forkserver failed");

  int fatal = 0, stop = 0;
  int status, foundbug = 0;
  pid_t rpid, rpidw; // FIX: remove
  enum nvart_message info, command;

  while (!fatal && !stop) {
    /* wait for requests from targets */
    info = read_message(main_info_fd);

    switch (info) {
      case MSG_FORKSERVER_READY:
        send_message(main_ctrl_fd, MSG_FORK_AND_RUN);
        break;
      case MSG_TARGET_STARTED:
        DBGF("NVFuzz: target process started, pid %d", tgconf_main->pid);
        break;
      case MSG_AWAITING_CHECK:
        DBGF("NVFuzz: mainproc requested to run recovery and checking");

        config->tracing = 0;

        if ((rpid = fork()) == -1) {
          FATAL("NVFuzz: fork() to run the recovery program failed");
          fatal = 1;
        } else if (rpid == 0) {  // recovery program (2nd child)
          DBGF("NVFuzz: fork() succeeds, recovery process %u parent %u",
               getpid(), getppid());

          // Note: mainproc_argv should contain mainproc
          char* args[] = {mainproc, "stackfile", "check", NULL};
          execv(mainproc, args);

          /* If execv() succeeds, it should not return (getting here). */
          FATAL("NVFuzz: unable to execute the recovery program");
        } else {  // fuzzer (parent)
          DBGF("NVFuzz: fork() succeeds, fuzzer process %u", getpid());
          do {  // wait for the recovery process to finish
            rpidw = waitpid(rpid, &status, WNOHANG);
            if (rpidw == -1) {
              ERRF("NVFuzz: waitpid(%u) failed", rpid);
            } else if (rpidw == 0) {
              /* recovery is still running; fuzzer can do something else */
            } else if (rpidw == rpid) {  // recovery process reaped
              foundbug = check_status(status, rpid, "recovery");
            } else {
              ERRF("NVFuzz: unexpected waitpid() return value %u", rpidw);
            }
          } while (rpidw == 0);  // recovery program still running
        }

        config->tracing = 1; // FIX: remove
        command = foundbug ? MSG_SHOW_BUG_AND_EXIT : MSG_CONTINUE_TO_RUN;
        send_message(main_ctrl_fd, command);
        break;
      case MSG_TARGET_EXITED:
        check_status(tgconf_main->status, tgconf_main->pid, "mainproc");
        send_message(main_ctrl_fd, MSG_EXIT_FORKSERVER);
        stop = 1;  // can restart the mainproc process
        break;
      default:
        ERRF("NVFuzz: received inappropriate message %d", info);
        fatal = 1;
        break;
    }
  }

  if (fatal) {
    /* Todo: Should kill forked processes: forkservers, mainproc & recovery. */
    exit(EXIT_FAILURE);
  }

  /* wait for the forkserver to exit */
  pid_t main_fksv_pidw = waitpid(main_fksv_pid, &status, 0);

  if (main_fksv_pidw < 0) {
    ERRF("NVFuzz: waitpid(%u) failed", main_fksv_pid);
  } else if (main_fksv_pidw == main_fksv_pid) {
    check_status(status, main_fksv_pid, "mainproc's forkserver");
  } else {
    ERRF("NVFuzz: unexpected waitpid() return value %u", main_fksv_pidw);
  }

  return 0;
}
