#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../include/kio.h"

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

#define kin_getc_while(pkin, p, ...)                                           \
  ({                                                                           \
    __auto_type _pkin = (pkin);                                                \
    int _ch;                                                                   \
    do {                                                                       \
      _ch = kin_getc(_pkin);                                                   \
    } while (_ch != EOF && p(_ch, ##__VA_ARGS__));                             \
    _ch;                                                                       \
  })

#define kin_getc_until(pkin, p, ...)                                           \
  ({                                                                           \
    __auto_type _pkin = (pkin);                                                \
    int _ch;                                                                   \
    do {                                                                       \
      _ch = kin_getc(_pkin);                                                   \
    } while (_ch != EOF && !p(_ch, ##__VA_ARGS__));                            \
    _ch;                                                                       \
  })

kin_t *kin_open(FILE *fp, const char *ibuf, size_t ibufsize)
    __attribute__((warn_unused_result, malloc));

int kin_getc(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));

int kin_ungetc(kin_t *pkin, int ch)
    __attribute__((warn_unused_result, nonnull(1)));

int kin_close(kin_t *pkin) __attribute__((nonnull(1)));

int kin_error(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));

int kin_eof(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));

kout_t *kout_open(FILE *fp, char *obuf, size_t obufsize)
    __attribute__((warn_unused_result, malloc));

FILE *kout_getfp(kout_t *pkout) __attribute__((warn_unused_result, nonnull(1)));

int kout_putc(kout_t *pkout, int ch)
    __attribute__((warn_unused_result, nonnull(1)));

int kout_printf(kout_t *pkout, const char *format, ...)
    __attribute__((warn_unused_result, nonnull(1, 2), format(printf, 2, 3)));

int kout_close(kout_t *pkout, char **pbuf, size_t *psize)
    __attribute__((warn_unused_result, nonnull(1)));