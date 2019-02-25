#ifndef _NVART_CONFIG_H
#define _NVART_CONFIG_H

#define ALIGN_UP(size, align) (((size) + (align)-1) & ~((align)-1))
#define ALIGN_DOWN(size, align) ((size) & ~((align)-1))

/* Environment variable used to pass SHM ID to the target program. */

#define NVART_SHM_ENV_VAR "__NVART_SHM_ID"

enum prog_state { STOPPED, DONTCARE, NORMAL, RECOVERY };

struct nvart_info {
  uint8_t reserved[64];
  enum prog_state pstate;
};

#define NVART_SHM_INFO_SIZE ALIGN_UP(sizeof(struct nvart_info), 64)

struct nvart_runq_entry {
  uint64_t *ptr;
  uint64_t val;
};

struct nvart_runq {
  size_t len;
  struct nvart_runq_entry entries[];
};

#define NVART_SHM_RUNQ_OFF (NVART_SHM_INFO_SIZE)
#define NVART_SHM_RUNQ_SIZE (4096)
#define NVART_SHM_RUNQ_META_SIZE (sizeof(struct runq))
#define NVART_SHM_RUNQ_MAX_LEN                        \
  ((NVART_SHM_RUNQ_SIZE - NVART_SHM_RUNQ_META_SIZE) / \
   sizeof(struct nvart_runq_entry))

#endif
