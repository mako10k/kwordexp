#include "kio_internal.h"
#include "kmalloc_internal.h"
#include <stdarg.h>
#include <stdio.h>

kin_t *kin_open(FILE *fp, const char *ibuf, size_t ibufsize) {
  kin_t *pkin = (kin_t *)kmalloc(sizeof(kin_t));
  if (pkin == NULL)
    return NULL;
  pkin->kin_ifp = fp;
  pkin->kin_ibuf = ibuf;
  pkin->kin_ibufsize = ibufsize;
  pkin->kin_ibufpos = 0;
  pkin->kin_ch = EOF;
  return pkin;
}

int kin_getc(kin_t *pkin) {
  if (pkin->kin_ifp != NULL)
    return fgetc(pkin->kin_ifp);

  if (pkin->kin_ch != EOF) {
    int ch = pkin->kin_ch;
    pkin->kin_ch = EOF;
    return ch;
  }
  if (pkin->kin_ibufpos >= pkin->kin_ibufsize)
    return EOF;

  return pkin->kin_ibuf[pkin->kin_ibufpos++] & 0xff;
}

int kin_ungetc(kin_t *pkin, int ch) {
  if (pkin->kin_ifp != NULL)
    return ungetc(ch, pkin->kin_ifp);

  if (pkin->kin_ch != EOF)
    return EOF;

  if (pkin->kin_ibufpos == 0)
    return EOF;

  if (pkin->kin_ibuf[pkin->kin_ibufpos - 1] == ch)
    pkin->kin_ibufpos--;
  else
    pkin->kin_ch = ch;

  return ch;
}

int kin_close(kin_t *pkin) {
  if (pkin->kin_ifp != NULL) {
    int ret = fclose(pkin->kin_ifp);
    if (ret == EOF)
      return EOF;
  }
  kfree(pkin);
  return 0;
}

int kin_error(kin_t *pkin) {
  if (pkin->kin_ifp != NULL) {
    return ferror(pkin->kin_ifp);
  }
  return 0;
}

int kin_eof(kin_t *pkin) {
  if (pkin->kin_ifp != NULL)
    return feof(pkin->kin_ifp);
  return pkin->kin_ibufpos >= pkin->kin_ibufsize;
}

void kin_destroy(kin_t *pkin) {
  kin_close(pkin);
  kfree(pkin);
}

kout_t *kout_open(FILE *fp, char *obuf, size_t obufsize) {
  kout_t *pkout = (kout_t *)kmalloc(sizeof(kout_t));
  if (pkout == NULL)
    return NULL;

  pkout->kout_ofp = fp;
  pkout->kout_obuf = obuf;
  pkout->kout_obufsize = obufsize;
  return pkout;
}

FILE *kout_getfp(kout_t *pkout) {
  if (pkout->kout_ofp == NULL) {
    FILE *ofp = open_memstream(&pkout->kout_obuf, &pkout->kout_obufsize);
    if (ofp == NULL) {
      return NULL;
    }
    pkout->kout_ofp = ofp;
  }
  return pkout->kout_ofp;
}

int kout_putc(kout_t *pkout, int ch) {
  if (pkout->kout_ofp == NULL) {
    FILE *ofp = kout_getfp(pkout);
    if (ofp == NULL) {
      return EOF;
    }
  }
  return fputc(ch, pkout->kout_ofp);
}

int kout_printf(kout_t *pkout, const char *format, ...) {
  if (pkout->kout_ofp == NULL) {
    FILE *ofp = kout_getfp(pkout);
    if (ofp == NULL) {
      return EOF;
    }
    pkout->kout_ofp = ofp;
  }
  va_list ap;
  va_start(ap, format);
  int ret = vfprintf(pkout->kout_ofp, format, ap);
  va_end(ap);
  return ret;
}

int kout_close(kout_t *pkout, char **pbuf, size_t *psize) {
  if (pkout->kout_ofp != NULL) {
    int ret = fclose(pkout->kout_ofp);
    if (ret == EOF)
      return EOF;
    pkout->kout_ofp = NULL;
    if (psize != NULL)
      *psize = pkout->kout_obufsize;

    if (pbuf != NULL)
      *pbuf = pkout->kout_obuf;
    else
      ksfree(pkout->kout_obuf);
  }
  pkout->kout_obuf = NULL;
  pkout->kout_obufsize = 0;
  return 0;
}