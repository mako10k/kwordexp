#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "kstr.h"
#include <stddef.h>

typedef struct kstrs kstrs_t;
struct kstrs {
  const char **strv;
  size_t strc;
  kstr_t kstr;
};

/**
 * Initialize a kstrs_t object.
 *
 * @param kstrs The kstrs_t object.
 * @return 0 on success, -1 on error.
 */
int kstrs_init(kstrs_t *kstrs) __attribute__((warn_unused_result));

/**
 * Push a string into a kstrs_t object.
 *
 * @param kstrs The kstrs_t object.
 * @return 0 on success, -1 on error.
 */
int kstrs_push(kstrs_t *kstrs) __attribute__((nonnull(1), warn_unused_result));

/**
 * Destroy a kstrs_t object.
 *
 * @param kstrs The kstrs_t object.
 */
void kstrs_destroy(kstrs_t *kstrs) __attribute__((nonnull(1)));