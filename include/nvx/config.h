#ifndef NVX_CONFIG_H_
#define NVX_CONFIG_H_

#define CACHELINE_SIZE (64)
#define CLSIZE (CACHELINE_SIZE)
#define CLMASK (CACHELINE_SIZE - 1)
#define CLSHIFT (6)

#define PAGESHIFT (12)
#define PAGESIZE (1 << PAGESHIFT)

#define ALIGN_UP(size, align) (((size) + (align)-1) & ~((align)-1))
#define ALIGN_DOWN(size, align) ((size) & ~((align)-1))

#define ALIGNED_16(x) (((uint64_t)(x) & (16 - 1)) == 0)
#define ALIGNED_32(x) (((uint64_t)(x) & (32 - 1)) == 0)
#define ALIGNED_64(x) (((uint64_t)(x) & (64 - 1)) == 0)
#define ALIGNED_CL(x) ALIGNED_64(x)
#define ALIGNED_4K(x) (((uint64_t)(x) & (4096 - 1)) == 0)

#define BINARY_PATH_LEN_MAX (512) // buffer length to store binary paths

/* Environment variable used to pass SHM ID to the target programs. */

#define NVX_ENV_SHM "__NVX_SHM_ID"

enum NvxMessage {
  kNvxMsgInvalid = 0,

  /* Control commands: nvscope telling target */
  kNvxMsgForkAndRun,
  kNvxMsgContinue,
  kNvxMsgShowBugAndExit,
  kNvxMsgShowBugAndContinue,
  kNvxMsgExitForkServer,

  /* Information: target telling nvscope */
  kNvxMsgForkServerHello,
  kNvxMsgForkServerReady,
  kNvxMsgAwaitChecking,
  /* Information with payload */
  kNvxMsgTargetStarted,
  kNvxMsgTargetExited,

  kNvxNumMessages
};

enum NvxExitCode {
  kNvxExitOK = 0,
  kNvxExitBadShm,
  kNvxExitBadMsg,
  kNvxExitBadConfig,
  kNvxExitFoundBug
};

enum NvxTargetType { kNvxTargetMainProc = 0, kNvxTargetRecovery };

struct NvxTargetConfig {
  int enabled;      // if nvx run-time is enabled
  pid_t pid;        // target process pid
  int status;       // target process status
  pid_t fksv_pid;   // target forkserver pid
  int read_fd;      // pipe endpoint to read from nvscope
  int write_fd;     // pipe endpoint to write to nvscope
  int reserved[10]; // pack to whole cache lines
} __attribute__((packed));

struct NvxConfig {
  int initialized;
  enum NvxTargetType target_type;
  int reserved[14]; // pack to whole cache lines
  struct NvxTargetConfig mainproc;
  struct NvxTargetConfig recovery;
} __attribute__((packed));

#endif // NVX_CONFIG_H_
