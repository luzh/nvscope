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

int main(int argc, char** argv) {
  if (argc < 2) FATAL("Usage: %s <target>", argv[0]);

  char** target_argv = argv + 1;  // skip the fuzzer program

  check_binary(argv[1]);
  ACTF("Preparing to test program %s", target_path);

  setup_shm();
  struct nvart_info* info = (struct nvart_info*)(trace_bits);
  memset(info, 0, NVART_SHM_INFO_SIZE);

  info->probing = 1;

  int tstatus, rstatus;
  pid_t tpid, tpidw, rpid, rpidw;

  if ((tpid = fork()) == -1) {
    PFATAL("NVFuzz: fork() to run the target program failed");
    exit(EXIT_FAILURE);
  } else if (tpid == 0) {  // target program (1st child)
    OKF("NVFuzz: fork() succeeds, target process %u parent %u", getpid(),
        getppid());
    // Note: target_argv should contain target_path
    execv(target_path, target_argv);

    // exit(0);
  } else {  // fuzzer (parent)
    OKF("NVFuzz: fork() succeeds, fuzzer process %u parent %u", getpid(),
        getppid());
    do {  // wait for the target program to finish
      tpidw = waitpid(tpid, &tstatus, WNOHANG);
      if (tpidw == -1) {
        ERRF("NVFuzz: waitpid() target %u failed", tpid);
      } else if (tpidw != 0) {
        if (WIFEXITED(tstatus)) {
          int exst = WEXITSTATUS(tstatus);
          if (exst == 0) {
            OKF("NVFuzz: target %u exited normally", tpid);
          } else if (exst == 127) {
            ERRF("NVFuzz: execv() target %u failed", tpid);
          } else {
            WARNF("NVFuzz: target %u exited with code %d", tpid, exst);
          }
        } else {  // perhaps killed by a signal?
          WARNF("NVFuzz: target died without a normal exit");
        }
      } else {  // target is still running, may request to run recovery
        volatile uint32_t reqcheck = info->reqcheck;
        if (reqcheck) {
          ACTF("NVFuzz: target requested to run recovery and checking");

          info->probing = 0;

          if ((rpid = fork()) == -1) {
            PFATAL("NVFuzz: fork() to run the recovery program failed");
            exit(EXIT_FAILURE);
          } else if (rpid == 0) {  // recovery program (2nd child)
            OKF("NVFuzz: fork() succeeds, recvry process %u parent %u",
                getpid(), getppid());

            // Note: target_argv should contain target_path
            char* args[] = {target_path, "stackfile", "check", NULL};
            execv(target_path, args);

            // exit(0);
          } else {  // fuzzer (parent)
            OKF("NVFuzz: fork() succeeds, fuzzer process %u parent %u",
                getpid(), getppid());
            do {  // wait for the recovery process to finish
              rpidw = waitpid(rpid, &rstatus, WNOHANG);
              if (rpidw == -1) {
                ERRF("NVFuzz: waitpid() recovery %u failed", rpid);
              } else if (rpidw != 0) {
                if (WIFEXITED(rstatus)) {
                  int exst = WEXITSTATUS(rstatus);
                  if (exst == 0) {  // pmem data looks good!
                    OKF("NVFuzz: recovery %u exited normally", rpid);
                  } else if (exst == 127) {
                    ERRF("NVFuzz: execv() recovery %u failed", rpid);
                  } else {  // pmem data caused abnormal recovery exit
                    info->foundbug = 1;
                    WARNF("NVFuzz: recovery %u exited, status %d", rpid, exst);
                  }
                } else {  // perhaps killed by a signal?
                  info->foundbug = 1;
                  WARNF("NVFuzz: recovery %u died without a normal exit", rpid);
                }
              } else {
                /* recovery is still running, fuzzer can do something else */
              }
            } while (rpidw == 0);  // recovery program still running
          }
          info->probing = 1;
          info->reqcheck = 0;
        }
      }
    } while (tpidw == 0);  // target program still running
  }

  return 0;
}
