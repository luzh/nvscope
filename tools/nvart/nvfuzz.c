#define AFL_MAIN
#define MESSAGES_TO_STDOUT
#define _FILE_OFFSET_BITS 64

#include "afl/alloc-inl.h"
#include "headers.h"
#include "nvart/config.h"

#define HAVE_AFFINITY 1

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
    OKF(cPIN "Persistent mode binary detected.");
    setenv(PERSIST_ENV_VAR, "1", 1);
    persistent_mode = 1;

  } else if (getenv("AFL_PERSISTENT")) {
    WARNF("AFL_PERSISTENT is no longer supported and may misbehave!");
  }

  if (memmem(f_data, f_len, DEFER_SIG, strlen(DEFER_SIG) + 1)) {
    OKF(cPIN "Deferred forkserver binary detected.");
    setenv(DEFER_ENV_VAR, "1", 1);
    deferred_mode = 1;

  } else if (getenv("AFL_DEFER_FORKSRV")) {
    WARNF("AFL_DEFER_FORKSRV is no longer supported and may misbehave!");
  }

  if (munmap(f_data, f_len)) PFATAL("unmap() failed");
}

int main(int argc, char** argv) {
  if (argc < 2) FATAL("Usage: %s <target>", argv[0]);

  char** target_argv = argv + 1;  // skip the fuzzer program

  check_binary(argv[1]);
  ACTF("Preparing to test program %s", target_path);

  setup_shm();
  struct nvart_info* info = (struct nvart_info*)(trace_bits);
  memset(info, 0, NVART_SHM_INFO_SIZE);

  volatile uint32_t* runcheck = &info->runcheck;
  info->probing = 1;

  int status1, status2;
  pid_t pid1 = fork();
  if (pid1 == -1) {
    PFATAL("fork() failed");
    exit(EXIT_FAILURE);
  } else if (pid1 == 0) {
    OKF("Forked, normal process pid %u", getpid());

    // Note: target_argv should contain target_path
    execv(target_path, target_argv);

    // exit(0);
  } else {
    OKF("Forked, fuzzer process pid %u child process pid %u", getppid(), pid1);
    while (1) {
      if (*runcheck) {
        TESTF("--- Running check ---");
        info->probing = 0;
        pid_t pid2 = fork();
        if (pid2 == -1) {
          PFATAL("Recovery fork() failed");
          exit(EXIT_FAILURE);
        } else if (pid2 == 0) {
          OKF("Forked, recovery process pid %u", getpid());

          // Note: target_argv should contain target_path
          char* args[] = {target_path, "stackfile", "check", NULL};
          execv(target_path, args);

          // exit(0);
        } else {
          if (waitpid(pid2, &status2, 0) > 0) {
            if (WIFEXITED(status2) && !WEXITSTATUS(status2)) {
              OKF("Recovery program finished normally.");
            } else if (WIFEXITED(status2) && WEXITSTATUS(status2)) {
              if (WEXITSTATUS(status2) == 127)
                ERRF("Recovery execv() failed");
              else {
                info->foundbug = 1;
                WARNF("Recovery program exits with non-zero code");
              }
            } else {
              info->foundbug = 1;
              ERRF("Recovery program did not exit normally");
            }
          } else
            ERRF("waitpid() failed");
        }

        info->probing = 1;
        info->runcheck = 0;
      }
    }

    if (waitpid(pid1, &status1, 0) > 0) {
      if (WIFEXITED(status1) && !WEXITSTATUS(status1)) {
        OKF("Target program finished normally.");
      } else if (WIFEXITED(status1) && WEXITSTATUS(status1)) {
        int excode = WIFEXITED(status1);
        if (excode == 127)
          ERRF("execv() failed");
        else
          WARNF("Target program exits with status %d.", excode);
      } else
        ERRF("Target program did not exit normally");
    } else
      ERRF("waitpid() failed");

    // exit(0);
  }

  return 0;
}
