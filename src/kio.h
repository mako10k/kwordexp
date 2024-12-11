#pragma once

#include <stdio.h>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct kin kin_t;
typedef struct kout kout_t;

struct kin {
  FILE *kin_ifp;
  const char *kin_ibuf;
  size_t kin_ibufsize;
  size_t kin_ibufpos;
  int kin_ch;
};

struct kout {
  FILE *kout_ofp;
  char *kout_obuf;
  size_t kout_obufsize;
};

kin_t kin_init(FILE *fp, const char *ibuf, size_t ibufsize)
    __attribute__((warn_unused_result));
int kin_getc(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
int kin_ungetc(kin_t *pkin, int ch)
    __attribute__((warn_unused_result, nonnull(1)));
int kin_close(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
int kin_destroy(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
kout_t kout_init(FILE *fp, char *obuf, size_t obufsize)
    __attribute__((warn_unused_result));
FILE *kout_getfp(kout_t *pkout) __attribute__((warn_unused_result, nonnull(1)));
int kout_putc(kout_t *pkout, int ch)
    __attribute__((warn_unused_result, nonnull(1)));
int kout_printf(kout_t *pkout, const char *format, ...)
    __attribute__((warn_unused_result, nonnull(1, 2), format(printf, 2, 3)));
int kout_close(kout_t *pkout) __attribute__((warn_unused_result, nonnull(1)));
int kout_destroy(kout_t *pkout) __attribute__((warn_unused_result, nonnull(1)));
