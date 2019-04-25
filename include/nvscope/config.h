#ifndef _NVX_CONFIG_H
#define _NVX_CONFIG_H

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

enum nvx_message {
  MSG_INVALID = 0,

  /* Control commands: nvscope telling target */
  MSG_FORK_AND_RUN,
  MSG_CONTINUE_TO_RUN,
  MSG_SHOW_BUG_AND_EXIT,
  MSG_SHOW_BUG_AND_CONTINUE,
  MSG_EXIT_FORKSERVER,

  /* Information: target telling nvscope */
  MSG_FORKSERVER_HELLO,
  MSG_FORKSERVER_READY,
  MSG_AWAITING_CHECK,
  /* Information with payload */
  MSG_TARGET_STARTED,
  MSG_TARGET_EXITED,

  NVX_PIPE_MSG_MAX
};

enum nvx_excode {
  NVX_EXIT_SUCCESS = 0,
  NVX_EXIT_BAD_SHM,
  NVX_EXIT_BAD_MSG,
  NVX_EXIT_BAD_CONFIG,
  NVX_EXIT_FOUNDBUG
};

enum nvx_target_type { TYPE_MAINPROC = 0, TYPE_RECOVERY };

struct nvx_target_config {
  int enabled;      // if nvx run-time is enabled
  pid_t pid;        // target process pid
  int status;       // target process status
  pid_t fksv_pid;   // target forkserver pid
  int read_fd;      // pipe endpoint to read from nvscope
  int write_fd;     // pipe endpoint to write to nvscope
  int reserved[10]; // pack to whole cache lines
} __attribute__((packed));

struct nvx_config {
  int initialized;
  enum nvx_target_type target_type;
  int reserved[14]; // pack to whole cache lines
  struct nvx_target_config mainproc;
  struct nvx_target_config recovery;
} __attribute__((packed));

#endif
