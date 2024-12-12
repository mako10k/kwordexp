#include "../include/kio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

int kin_init(kin_t **ppkin, FILE *fp, const char *ibuf, size_t ibufsize) {
  kin_t *pkin = (kin_t *)malloc(sizeof(kin_t));
  if (pkin == NULL) {
    return -1;
  }
  pkin->kin_ifp = fp;
  pkin->kin_ibuf = ibuf;
  pkin->kin_ibufsize = ibufsize;
  pkin->kin_ibufpos = 0;
  pkin->kin_ch = EOF;
  *ppkin = pkin;
  return 0;
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
    if (ret == EOF) {
      return EOF;
    }
    pkin->kin_ifp = NULL;
  }
  return 0;
}

int kin_error(kin_t *pkin) {
  if (pkin->kin_ifp != NULL) {
    return ferror(pkin->kin_ifp);
  }
  return 0;
}

int kin_eof(kin_t *pkin) {
  if (pkin->kin_ifp != NULL) {
    return feof(pkin->kin_ifp);
  }
  return pkin->kin_ibufpos >= pkin->kin_ibufsize;
}

void kin_destroy(kin_t *pkin) {
  kin_close(pkin);
  free(pkin);
}

int kout_init(kout_t **ppkout, FILE *fp, char *obuf, size_t obufsize) {
  kout_t *pkout = (kout_t *)malloc(sizeof(kout_t));
  if (pkout == NULL) {
    return -1;
  }
  pkout->kout_ofp = fp;
  pkout->kout_obuf = obuf;
  pkout->kout_obufsize = obufsize;
  *ppkout = pkout;
  return 0;
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
    pkout->kout_ofp = ofp;
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
    if (ret == EOF) {
      return EOF;
    }
    if (psize != NULL) {
      *psize = pkout->kout_obufsize;
    }
    if (*pbuf != NULL) {
      *pbuf = pkout->kout_obuf;
      pkout->kout_obuf = NULL;
      pkout->kout_obufsize = 0;
    }
    pkout->kout_ofp = NULL;
  }
  return 0;
}

void kout_destroy(kout_t *pkout) {
  char *buf = NULL;
  int ret = kout_close(pkout, &buf, NULL);
  (void)ret;
  if (buf != NULL)
    free(buf);
  free(pkout);
}
