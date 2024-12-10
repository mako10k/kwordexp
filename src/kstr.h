#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>

typedef struct kstr kstr_t;

struct kstr {
  char *buf;
  size_t len;
  size_t cap;
};

/**
 * Initialize a kstr_t object.
 *
 * @param kstr The kstr_t object.
 * @param buf The buffer to initialize the object with.
 * @param len The length of the buffer.
 * @return 0 on success, -1 on error.
 */
int kstr_init(kstr_t *kstr, const char *buf, size_t len)
    __attribute__((nonnull(1), warn_unused_result));

/**
 * Put a string into a kstr_t object.
 *
 * @param kstr The kstr_t object.
 * @param str The string to put.
 * @param len The length of the string.
 * @return 0 on success, -1 on error.
 */
int kstr_put(kstr_t *kstr, const char *str, size_t len)
    __attribute__((nonnull(1, 2), warn_unused_result));

/**
 * Destroy a kstr_t object.
 *
 * @param kstr The kstr_t object.
 */
void kstr_destroy(kstr_t kstr);