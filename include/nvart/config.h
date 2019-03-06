#ifndef _NVART_CONFIG_H
#define _NVART_CONFIG_H

#define CLSIZE (64)

#define ALIGN_UP(size, align) (((size) + (align)-1) & ~((align)-1))
#define ALIGN_DOWN(size, align) ((size) & ~((align)-1))

/* Environment variable used to pass SHM ID to the target program. */

#define NVART_SHM_ENV_VAR "__NVART_SHM_ID"

/*
 * Designated file descriptors for forkserver commands.
 * The target process writes to TGT_WR_FD and reads TGT_RD_FD.
 * The recovery process writes to RCY_WR_FD and reads RCY_RD_FD.
 */
enum nvart_pipe_fd {
  TGT_RD_FD = 198,
  TGT_WR_FD,
  RCY_RD_FD,
  RCY_WR_FD,

  NVART_PIPE_FD_MAX
};

enum nvart_pipe_msg {
  NVART_PIPE_MSG_NONE = 0,

  /* Control commands */
  NVART_RUN_TARGET,
  NVART_CHECK_PASS,
  NVART_CHECK_FAIL,
  NVART_EXIT_FORKSRV,

  /* Status */
  NVART_REQ_CHECK,
  NVART_FORKSRV_READY,
  NVART_TARGET_EXITED,

  NVART_PIPE_MSG_MAX
};

enum nvart_excode {
  NVART_EXIT_SUCCESS = 0,
  NVART_EXIT_NOSHM,
  NVART_EXIT_RUNQ_FULL,
  NVART_EXIT_FOUNDBUG
};

enum prog_state { NONE, DONTCARE, NORMAL, RECOVERY };

struct nvart_info {
  uint8_t reserved[64];
  uint32_t probing;
  uint32_t foundbug;
  enum prog_state pstate;
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
