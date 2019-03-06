#ifndef _NVART_CONFIG_H
#define _NVART_CONFIG_H

#define CLSIZE (64)

#define ALIGN_UP(size, align) (((size) + (align)-1) & ~((align)-1))
#define ALIGN_DOWN(size, align) ((size) & ~((align)-1))

/* Environment variable used to pass SHM ID to the target programs. */

#define NVART_SHM_ENV_VAR "__NVART_SHM_ID"

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

enum nvart_pipe_msg {
  MSG_INVALID = 0,

  /* Control commands: fuzzer telling target */
  MSG_CONTINUE_TO_RUN,
  MSG_SHOW_BUG_AND_EXIT,
  MSG_EXIT_FORKSERVER,

  /* Information: target telling fuzzer */
  MSG_AWAITING_CHECK,
  MSG_FORKSERVER_READY,
  MSG_MAINPROC_EXITED,

  NVART_PIPE_MSG_MAX
};

enum nvart_excode {
  NVART_EXIT_SUCCESS = 0,
  NVART_EXIT_NOSHM,
  NVART_EXIT_RUNQ_FULL,
  NVART_EXIT_FOUNDBUG
};

enum target_stage { NONE, DONTCARE, MAINPROC, RECOVERY };

struct nvart_info {
  uint8_t reserved[64];
  uint32_t probing;
  uint32_t foundbug;
  enum target_stage stage;
};

#define NVART_SHM_INFO_SIZE ALIGN_UP(sizeof(struct nvart_info), CLSIZE)

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

#define NVART_SHM_RUNQ_OFF (NVART_SHM_INFO_SIZE)
#define NVART_SHM_RUNQ_SIZE (4096)
#define NVART_SHM_RUNQ_META_SIZE ALIGN_UP(sizeof(struct nvart_runq), CLSIZE)
#define NVART_SHM_RUNQ_MAX_LEN                        \
  ((NVART_SHM_RUNQ_SIZE - NVART_SHM_RUNQ_META_SIZE) / \
   sizeof(struct nvart_runq_entry))

#endif
