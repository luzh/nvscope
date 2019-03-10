#ifndef _NVART_CONFIG_H
#define _NVART_CONFIG_H

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

#define BINARY_PATH_LEN_MAX (512)  // buffer length to store binary paths

/* Environment variable used to pass SHM ID to the target programs. */

#define NVART_ENV_SHM "__NVART_SHM_ID"

/*
 * Designated file descriptors for forkserver commands.
 * The mainproc process reads FD_MAINPROC_CTRL and writes to FD_MAINPROC_INFO.
 * The recovery process reads FD_RECOVERY_CTRL and writes to FD_RECOVERY_INFO.
 */
enum nvart_pipe_fd {
  FD_MAINPROC_CTRL = 198,
  FD_MAINPROC_INFO,

  FD_RECOVERY_CTRL,
  FD_RECOVERY_INFO,

  NVART_PIPE_FD_MAX
};

enum nvart_message {
  MSG_INVALID = 0,

  /* Control commands: fuzzer telling target */
  MSG_FORK_AND_RUN,
  MSG_CONTINUE_TO_RUN,
  MSG_SHOW_BUG_AND_EXIT,
  MSG_EXIT_FORKSERVER,

  /* Information: target telling fuzzer */
  MSG_AWAITING_CHECK,
  MSG_FORKSERVER_HELLO,
  MSG_FORKSERVER_READY,
  /* Information with payload */
  MSG_TARGET_STARTED,
  MSG_TARGET_EXITED,

  NVART_PIPE_MSG_MAX
};

struct message_pid {
  enum nvart_message msg;
  pid_t pid;
};

struct message_status {
  enum nvart_message msg;
  int status;
};

enum nvart_excode {
  NVART_EXIT_SUCCESS = 0,
  NVART_EXIT_BAD_SHM,
  NVART_EXIT_BAD_CONFIG,
  NVART_EXIT_RUNQ_FULL,
  NVART_EXIT_FOUNDBUG
};

enum target_stage { NONE, DONTCARE, MAINPROC, RECOVERY };

enum nvart_target_type { TYPE_MAINPROC = 0, TYPE_RECOVERY }; // FIX: remove TYPE_

struct nvart_target_config {
  // pid_t fksv_pid;
  // pid_t proc_pid;
  int tracing;
  int info_fd;
  int ctrl_fd;
  enum target_stage stage; // FIX: remove
  int reserved[12];  // pack to whole cache lines
} __attribute__((packed));

struct nvart_config {
  int initialized;
  int tracing; // FIX: remove
  enum nvart_target_type target_type;
  int reserved[13];  // pack to whole cache lines
  struct nvart_target_config mainproc;
  struct nvart_target_config recovery;
} __attribute__((packed));

#define NVART_SHM_CONFIG_SIZE ALIGN_UP(sizeof(struct nvart_config), CLSIZE)

struct nvart_runq_entry {
  union {
    uint8_t *ptr8;
    uint16_t *ptr16;
    uint32_t *ptr32;
    uint64_t *ptr64;
  };
  union {
    uint8_t old8;
    uint16_t old16;
    uint32_t old32;
    uint64_t old64;
  };
  union {
    uint8_t new8;
    uint16_t new16;
    uint32_t new32;
    uint64_t new64;
  };
};

struct nvart_runq {
  size_t len;
  struct nvart_runq_entry entries[];
};

#define NVART_SHM_RUNQ_OFF (NVART_SHM_CONFIG_SIZE)
#define NVART_SHM_RUNQ_SIZE (4096)
#define NVART_SHM_RUNQ_META_SIZE ALIGN_UP(sizeof(struct nvart_runq), CLSIZE)
#define NVART_SHM_RUNQ_MAX_LEN                        \
  ((NVART_SHM_RUNQ_SIZE - NVART_SHM_RUNQ_META_SIZE) / \
   sizeof(struct nvart_runq_entry))

#endif
