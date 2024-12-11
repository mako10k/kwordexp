#include "kio.h"
#include <stdio.h>
#include <stdlib.h>

kin_t kin_init(FILE *fp, const char *ibuf, size_t ibufsize) {
  kin_t kin;
  kin.kin_ifp = fp;
  kin.kin_ibuf = ibuf;
  kin.kin_ibufsize = ibufsize;
  kin.kin_ibufpos = 0;
  return kin;
}

int kin_getc(kin_t *pkin) {
  if (pkin->kin_ifp != NULL) {
    return fgetc(pkin->kin_ifp);
  }
  if (pkin->kin_ch != EOF) {
    int ch = pkin->kin_ch;
    pkin->kin_ch = EOF;
    return ch;
  }
  if (pkin->kin_ibufpos >= pkin->kin_ibufsize) {
    return EOF;
  }
  return pkin->kin_ibuf[pkin->kin_ibufpos++] & 0xff;
}

int kin_ungetc(kin_t *pkin, int ch) {
  if (pkin->kin_ifp != NULL) {
    return ungetc(ch, pkin->kin_ifp);
  }
  if (pkin->kin_ch != EOF) {
    return EOF;
  }
  if (pkin->kin_ibufpos == 0) {
    return EOF;
  }
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
      return -1;
    }
    pkin->kin_ifp = NULL;
  }
  return 0;
}

int kin_destroy(kin_t *pkin) { return kin_close(pkin); }

kout_t kout_init(FILE *fp, char *obuf, size_t obufsize) {
  kout_t kout;
  kout.kout_ofp = fp;
  kout.kout_obuf = obuf;
  kout.kout_obufsize = obufsize;
  return kout;
}

FILE *kout_getfp(kout_t *pkout) {
  if (pkout->kout_ofp == NULL) {
    FILE *ofp =
        open_memstream(&pkout->kout_obuf, &pkout->kout_obufsize);
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

int kout_close(kout_t *pkout) {
  if (pkout->kout_ofp != NULL) {
    int ret = fclose(pkout->kout_ofp);
    if (ret == EOF) {
      return -1;
    }
    pkout->kout_ofp = NULL;
  }
  return 0;
}

int kout_destroy(kout_t *pkout) {
  int ret = kout_close(pkout);
  if (ret == -1) {
    return -1;
  }
  if (pkout->kout_obuf != NULL) {
    free(pkout->kout_obuf);
    pkout->kout_obuf = NULL;
  }
  return 0;
}
