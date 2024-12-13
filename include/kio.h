#ifndef __KIO_H__
#define __KIO_H__

#include <stdio.h>

typedef struct kin kin_t;
typedef struct kout kout_t;

kin_t *kin_open(FILE *fp, const char *ibuf, size_t ibufsize);
int kin_getc(kin_t *pkin);
int kin_ungetc(kin_t *pkin, int ch);
int kin_close(kin_t *pkin);
int kin_error(kin_t *pkin);
int kin_eof(kin_t *pkin);

kout_t* kout_open(FILE *fp, char *obuf, size_t obufsize);
FILE *kout_getfp(kout_t *pkout);
int kout_putc(kout_t *pkout, int ch);
int kout_printf(kout_t *pkout, const char *format, ...);
int kout_close(kout_t *pkout, char **pbuf, size_t *psize);

#endif // __KIO_H__