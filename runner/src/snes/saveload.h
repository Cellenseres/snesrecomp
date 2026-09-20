#pragma once

#include <stddef.h>

typedef struct SaveLoadInfo SaveLoadInfo;
typedef void SaveLoadInfoFunc(SaveLoadInfo *info, void *data, size_t data_size);
typedef int SaveLoadPeekFunc(SaveLoadInfo *info, size_t offset, void *data, size_t size);
struct SaveLoadInfo {
  SaveLoadInfoFunc *func;
  /* Optional read-only lookahead; returns zero for writers or unavailable data.
   * Initialize to NULL for streams that do not support format detection. */
  SaveLoadPeekFunc *peek;
};

//#define SL(x) sli->func(sli, &x, sizeof(x))
