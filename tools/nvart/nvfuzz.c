#define AFL_MAIN
#define MESSAGES_TO_STDOUT
#define _FILE_OFFSET_BITS 64

#include "afl/alloc-inl.h"
#include "headers.h"
#include "nvart/config.h"

#define HAVE_AFFINITY 1

static pid_t mainproc_frks_pid; /* PID of the mainproc's fork server */
// recovery_forksrv_pid;    /* PID of the recovery's forkserver */

static int mainproc_ctrl_fd, /* Fork server control pipe (write) */
    mainproc_info_fd;        /* Fork server status pipe (read)   */

static int32_t shm_id; /* ID of the SHM region */

static uint8_t
    // *in_dir, /* Input directory with test cases */
    // *out_file,          /* File to fuzz, if any            */
    // *out_dir,           /* Working & output directory      */
    // *sync_dir,          /* Synchronization directory       */
    // *sync_id,           /* Fuzzer ID                       */
    // *use_banner,        /* Display banner                  */
    *in_bitmap,   /* Input bitmap                    */
    *target_path; /* Path to target binary           */
//  *orig_cmdline;      /* Original command line           */

static uint8_t
    // skip_deterministic, /* Skip deterministic stages?     */
    // force_deterministic,           /* Force deterministic stages?    */
    // use_splicing,                  /* Recombine input files?         */
    dumb_mode, /* Run in non-instrumented mode?  */
    // score_changed,                 /* Scoring for favorites changed? */
    // kill_signal,                   /* Signal that killed the child   */
    // resuming_fuzz,                 /* Resuming an older fuzzing job? */
    // timeout_given,                 /* Specific timeout given?        */
    // not_on_tty,                    /* stdout is not a tty            */
    // term_too_small,                /* terminal dimensions too small  */
    uses_asan, /* Target uses ASAN?              */
    // no_forkserver,                 /* Disable forkserver?            */
    // crash_mode,                    /* Crash mode! Yeah!              */
    // in_place_resume,               /* Attempt in-place resume?       */
    // auto_changed,                  /* Auto-generated tokens changed? */
    // no_cpu_meter_red,              /* Feng shui on the status screen */
    // no_arith,                      /* Skip most arithmetic ops       */
    // shuffle_queue,                 /* Shuffle input queue?           */
    // bitmap_changed = 1,            /* Time to update bitmap?         */
    // skip_requested,                /* Skip request, via SIGUSR1      */
    // run_over10m,                   /* Run time over 10 minutes?      */
    persistent_mode, /* Running in persistent mode?    */
    deferred_mode;   /* Deferred forkserver mode?      */
//  fast_cal;                      /* Try to calibrate faster?       */

static uint8_t* trace_bits; /* SHM with instrumentation bitmap  */

static uint8_t virgin_bits[MAP_SIZE], /* Regions yet untouched by fuzzing */
    virgin_tmout[MAP_SIZE],           /* Bits we haven't seen in tmouts   */
    virgin_crash[MAP_SIZE];           /* Bits we haven't seen in crashes  */

/*
 * Do a PATH search and find target binary to see that it exists and isn't a
 * shell script - a common and painful mistake. We also check for a valid ELF
 * header and for evidence of AFL instrumentation.
 */
static void check_binary(uint8_t* fname) {
  u8* env_path = 0;
  struct stat st;

  s32 fd;
  u8* f_data;
  u32 f_len = 0;

  ACTF("Validating target binary...");

  if (strchr(fname, '/') || !(env_path = getenv("PATH"))) {
    target_path = ck_strdup(fname);
    if (stat(target_path, &st) || !S_ISREG(st.st_mode) ||
        !(st.st_mode & 0111) || (f_len = st.st_size) < 4)
      FATAL("Program '%s' not found or not executable", fname);

  } else {
    while (env_path) {
      u8 *cur_elem, *delim = strchr(env_path, ':');

      if (delim) {
        cur_elem = ck_alloc(delim - env_path + 1);
        memcpy(cur_elem, env_path, delim - env_path);
        delim++;

      } else
        cur_elem = ck_strdup(env_path);

      env_path = delim;

      if (cur_elem[0])
        target_path = alloc_printf("%s/%s", cur_elem, fname);
      else
        target_path = ck_strdup(fname);

      ck_free(cur_elem);

      if (!stat(target_path, &st) && S_ISREG(st.st_mode) &&
          (st.st_mode & 0111) && (f_len = st.st_size) >= 4)
        break;

      ck_free(target_path);
      target_path = 0;
    }

    if (!target_path) FATAL("Program '%s' not found or not executable", fname);
  }

  if (getenv("AFL_SKIP_BIN_CHECK")) return;

  /* Check for blatant user errors. */

  if ((!strncmp(target_path, "/tmp/", 5) && !strchr(target_path + 5, '/')) ||
      (!strncmp(target_path, "/var/tmp/", 9) && !strchr(target_path + 9, '/')))
    FATAL("Please don't keep binaries in /tmp or /var/tmp");

  fd = open(target_path, O_RDONLY);

  if (fd < 0) PFATAL("Unable to open '%s'", target_path);

  f_data = mmap(0, f_len, PROT_READ, MAP_PRIVATE, fd, 0);

  if (f_data == MAP_FAILED) PFATAL("Unable to mmap file '%s'", target_path);

  close(fd);

  if (f_data[0] == '#' && f_data[1] == '!') {
    SAYF("\n" cLRD "[-] " cRST
         "Oops, the target binary looks like a shell script.\n");
    FATAL("Program '%s' is a shell script", target_path);
  }

  if (f_data[0] != 0x7f || memcmp(f_data + 1, "ELF", 3))
    FATAL("Program '%s' is not an ELF binary", target_path);

  if (!dumb_mode && !memmem(f_data, f_len, NVART_SHM_ENV_VAR,
                            strlen(NVART_SHM_ENV_VAR) + 1)) {
    SAYF("\n" cLRD "[-] " cRST
         "Looks like the target binary is not instrumented!\n");
    FATAL("No instrumentation detected - '%s' not found", NVART_SHM_ENV_VAR);
  }

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

  if (munmap(f_data, f_len)) PFATAL("unmap() failed");
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
  u8* shm_str;

  if (!in_bitmap) memset(virgin_bits, 255, MAP_SIZE);

  memset(virgin_tmout, 255, MAP_SIZE);
  memset(virgin_crash, 255, MAP_SIZE);

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

  if (!dumb_mode) setenv(NVART_SHM_ENV_VAR, shm_str, 1);

  ck_free(shm_str);

  trace_bits = shmat(shm_id, NULL, 0);

  if (!trace_bits) PFATAL("shmat() failed");
}

/*
 * Spin up fork server. The idea is explained here:
 * http://lcamtuf.blogspot.com/2014/10/fuzzing-binaries-without-execve.html
 *
 * In essence, the instrumentation allows us to skip execve(), and just keep
 * cloning a stopped child. So, we just execute once, and then send commands
 * through a pipe. The other part of this logic is in lib/nvart/nvart.c.
 */
static void init_forkserver(char* target, char** target_argv) {
  int mainproc_info_fds[2], mainproc_ctrl_fds[2];

  ACTF("NVFuzz: spinning up the fork server...");

  if (pipe(mainproc_info_fds) || pipe(mainproc_ctrl_fds))
    PFATAL("NVFuzz: pipe() for the target program's forkserver failed");

  mainproc_frks_pid = fork();

  if (mainproc_frks_pid < 0)
    PFATAL("NVFuzz: fork() to run the target program's forkserver failed");

  if (mainproc_frks_pid == 0) {  // target program's forkserver process
    struct rlimit rlim;

    /*
     * Umpf. On OpenBSD, the default fd limit for root users is set to soft 128.
     * Let's try to fix that...
     */
    if (!getrlimit(RLIMIT_NOFILE, &rlim) && rlim.rlim_cur < NVART_PIPE_FD_MAX) {
      rlim.rlim_cur = NVART_PIPE_FD_MAX;
      setrlimit(RLIMIT_NOFILE, &rlim);
    }

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

    if (dup2(mainproc_ctrl_fds[0], MAINPROC_CTRL) < 0)
      PFATAL("NVFuzz: dup2() for MAINPROC_CTRL failed");
    if (dup2(mainproc_info_fds[1], MAINPROC_INFO) < 0)
      PFATAL("NVFuzz: dup2() for MAINPROC_INFO failed");

    close(mainproc_ctrl_fds[0]);
    close(mainproc_ctrl_fds[1]);
    close(mainproc_info_fds[0]);
    close(mainproc_info_fds[1]);

    execv(target, target_argv);

    /* If execv() succeeds, it should not return (getting here). */
    FATAL("NVFuzz: unable to execute the target program '%s'", target_path);
  }

  /* Close the unneeded endpoints. */
  close(mainproc_ctrl_fds[0]);
  close(mainproc_info_fds[1]);

  mainproc_ctrl_fd = mainproc_ctrl_fds[1];
  mainproc_info_fd = mainproc_info_fds[0];

  /* Check afl-fuzz.c for using setitimer() and SIGALARM to kill. */
  ACTF("NVFuzz: waiting for the forkserver to come up...");

  enum nvart_pipe_msg stat;
  /* This call blocks if no data comes though the pipe. */
  ssize_t rlen = read(mainproc_info_fd, &stat, sizeof(stat));

  /*
   * If we have ready message from the forkserver, we're all set. Otherwise,
   * try to figure out what went wrong with waitpid().
   */
  if (rlen == sizeof(stat) && stat == NVART_FORKSRV_READY) {
    OKF("NVFuzz: target program's forkserver is up, pid %u", mainproc_frks_pid);
    return;
  }

  int status;
  pid_t pidw = waitpid(mainproc_frks_pid, &status, 0);

  if (pidw < 0) {
    ERRF("NVFuzz: waitpid(%u) failed", mainproc_frks_pid);
  } else if (pidw == mainproc_frks_pid) {  // target's forkserver reaped
    check_status(status, mainproc_frks_pid, "target's forkserver");
  } else {
    ERRF("NVFuzz: unexpected waitpid() return value %u", pidw);
  }

  /* Check afl-fuzz.c for more detailed parsing of failure status. */

  FATAL("Fork server handshake failed");
}

int main(int argc, char** argv) {
  if (argc < 2) FATAL("Usage: %s <target>", argv[0]);

  char** target_argv = argv + 1;  // skip the fuzzer program

  check_binary(argv[1]);
  ACTF("Preparing to test program %s", target_path);

  setup_shm();
  struct nvart_info* info = (struct nvart_info*)(trace_bits);
  memset(info, 0, NVART_SHM_INFO_SIZE);

  info->probing = 1;

  pid_t rpid, rpidw;
  int status, foundbug = 0;

  init_forkserver(target_path, target_argv);

  enum nvart_pipe_msg ctrl, stat;

  /* tell the the target program's forkserver to run the target program */
  ctrl = NVART_RUN_TARGET;
  if (write(mainproc_ctrl_fd, &ctrl, sizeof(ctrl)) != sizeof(ctrl))
    PFATAL("NVFuzz: write() to mainproc_ctrl_fd failed");

  int fatal = 0, alldone = 0;
  while (!fatal && !alldone) {
    if (read(mainproc_info_fd, &stat, sizeof(stat)) != sizeof(stat))
      PFATAL("NVFuzz: read() from mainproc_info_fd failed");

    switch (stat) {
      case NVART_REQ_CHECK:
        ACTF("NVFuzz: target requested to run recovery and checking");

        info->probing = 0;

        if ((rpid = fork()) == -1) {
          PFATAL("NVFuzz: fork() to run the recovery program failed");
          // exit(EXIT_FAILURE);
        } else if (rpid == 0) {  // recovery program (2nd child)
          OKF("NVFuzz: fork() succeeds, recovery process %u parent %u",
              getpid(), getppid());

          // Note: target_argv should contain target_path
          char* args[] = {target_path, "stackfile", "check", NULL};
          execv(target_path, args);

          /* If execv() succeeds, it should not return (getting here). */
          FATAL("NVFuzz: unable to execute the recovery program");
        } else {  // fuzzer (parent)
          OKF("NVFuzz: fork() succeeds, fuzzer process %u", getpid());
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

        info->probing = 1;
        ctrl = foundbug ? NVART_CHECK_FAIL : NVART_CHECK_PASS;
        if (write(mainproc_ctrl_fd, &ctrl, sizeof(ctrl)) != sizeof(ctrl))
          PFATAL("NVFuzz: write() to mainproc_ctrl_fd failed");
        break;
      case NVART_TARGET_EXITED:
        OKF("NVFuzz: target program exited", stat);
        if (read(mainproc_info_fd, &status, sizeof(status)) != sizeof(status))
          PFATAL("NVFuzz: read() from mainproc_info_fd failed");
        check_status(status, 0, NULL);

        ctrl = NVART_EXIT_FORKSRV;
        if (write(mainproc_ctrl_fd, &ctrl, sizeof(ctrl)) != sizeof(ctrl))
          PFATAL("NVFuzz: write() to mainproc_ctrl_fd failed");

        alldone = 1;  // can restart the target process
        break;
      default:
        ERRF("NVFuzz: inappropriate pipe message %d", stat);
        fatal = 1;
        break;
    }
  }

  if (fatal) {
    /* Todo: Should kill forked processes: forkservers, target & recovery. */
    exit(EXIT_FAILURE);
  }

  /* wait for the forkserver to exit */
  pid_t mainproc_frks_pidw = waitpid(mainproc_frks_pid, &status, 0);

  if (mainproc_frks_pidw < 0) {
    ERRF("NVFuzz: waitpid(%u) failed", mainproc_frks_pid);
  } else if (mainproc_frks_pidw == mainproc_frks_pid) {
    check_status(status, mainproc_frks_pid, "target's forkserver");
  } else {
    ERRF("NVFuzz: unexpected waitpid() return value %u", mainproc_frks_pidw);
  }

  return 0;
}
