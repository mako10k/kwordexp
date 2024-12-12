#ifndef __KIO_H__
#define __KIO_H__

#include <stdio.h>

typedef struct kin kin_t;
typedef struct kout kout_t;

int kin_init(kin_t **ppkin, FILE *fp, const char *ibuf, size_t ibufsize)
    __attribute__((warn_unused_result, nonnull(1)));
int kin_getc(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
int kin_ungetc(kin_t *pkin, int ch)
    __attribute__((warn_unused_result, nonnull(1)));
int kin_close(kin_t *pkin) __attribute__((nonnull(1)));
int kin_error(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
int kin_eof(kin_t *pkin) __attribute__((warn_unused_result, nonnull(1)));
void kin_destroy(kin_t *pkin) __attribute__((nonnull(1)));
int kout_init(kout_t **ppkout, FILE *fp, char *obuf, size_t obufsize)
    __attribute__((warn_unused_result, nonnull(1)));
FILE *kout_getfp(kout_t *pkout) __attribute__((warn_unused_result, nonnull(1)));
int kout_putc(kout_t *pkout, int ch)
    __attribute__((warn_unused_result, nonnull(1)));
int kout_printf(kout_t *pkout, const char *format, ...)
    __attribute__((warn_unused_result, nonnull(1, 2), format(printf, 2, 3)));
int kout_close(kout_t *pkout, char **pbuf, size_t *psize)
    __attribute__((warn_unused_result, nonnull(1)));
void kout_destroy(kout_t *pkout) __attribute__((nonnull(1)));

#endif // __KIO_H__