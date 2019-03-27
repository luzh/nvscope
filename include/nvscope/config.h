#ifndef _NVS_CONFIG_H
#define _NVS_CONFIG_H

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

#define NVS_ENV_SHM "__NVS_SHM_ID"

enum nvs_message {
  MSG_INVALID = 0,

  /* Control commands: nvscope telling target */
  MSG_FORK_AND_RUN,
  MSG_CONTINUE_TO_RUN,
  MSG_SHOW_BUG_AND_EXIT,
  MSG_EXIT_FORKSERVER,

  /* Information: target telling nvscope */
  MSG_FORKSERVER_HELLO,
  MSG_FORKSERVER_READY,
  MSG_AWAITING_CHECK,
  /* Information with payload */
  MSG_TARGET_STARTED,
  MSG_TARGET_EXITED,

  NVS_PIPE_MSG_MAX
};

enum nvs_excode {
  NVS_EXIT_SUCCESS = 0,
  NVS_EXIT_BAD_SHM,
  NVS_EXIT_BAD_CONFIG,
  NVS_EXIT_RUNQ_FULL,
  NVS_EXIT_FOUNDBUG
};

enum nvs_target_stage { ST_NONE, ST_DONTCARE, ST_MAINPROC, ST_RECOVERY };

enum nvs_target_type { TYPE_MAINPROC = 0, TYPE_RECOVERY };

struct nvs_target_config {
  int enabled;                  // if nvscope run-time is enabled
  pid_t pid;                    // target process pid
  int status;                   // target process status
  pid_t fksv_pid;               // target forkserver pid
  int read_fd;                  // pipe endpoint to read from nvscope
  int write_fd;                 // pipe endpoint to write to nvscope
  enum nvs_target_stage stage;  // TODO: may remove
  int reserved[9];              // pack to whole cache lines
} __attribute__((packed));

struct nvs_config {
  int initialized;
  enum nvs_target_type target_type;
  int reserved[14];  // pack to whole cache lines
  struct nvs_target_config mainproc;
  struct nvs_target_config recovery;
} __attribute__((packed));

#define NVS_SHM_CONFIG_SIZE ALIGN_UP(sizeof(struct nvs_config), CLSIZE)

struct nvs_runq_entry {
  union {
    uint8_t *ptr8;
    uint16_t *ptr16;
    uint32_t *ptr32;
    uint64_t *ptr64;
  };
  union {
    uint8_t val8;
    uint16_t val16;
    uint32_t val32;
    uint64_t val64;
  };
};

struct nvs_runq {
  size_t len;
  struct nvs_runq_entry entries[];
};

#define NVS_SHM_RUNQ_OFF (NVS_SHM_CONFIG_SIZE)
#define NVS_SHM_RUNQ_SIZE (500000)
#define NVS_SHM_RUNQ_META_SIZE ALIGN_UP(sizeof(struct nvs_runq), CLSIZE)
#define NVS_SHM_RUNQ_MAX_LEN \
  ((NVS_SHM_RUNQ_SIZE - NVS_SHM_RUNQ_META_SIZE) / sizeof(struct nvs_runq_entry))

#endif
