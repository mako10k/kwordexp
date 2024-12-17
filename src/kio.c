#include "kio_internal.h"
#include "kmalloc_internal.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int kprintf_flushbuf(int fd, char *buf, size_t *pbufpos) {
  size_t bufpos = *pbufpos;
  size_t bufidx = 0;
  while (bufidx < bufpos) {
    ssize_t w = write(fd, buf + bufidx, bufpos - bufidx);
    if (w == -1)
      return -1;
    bufidx += w;
  }
  *pbufpos -= bufidx;
  return 0;
}

static int kprintf_putbuf(int fd, char *buf, size_t buflen, size_t *pbufpos,
                          const char *data, size_t datalen) {
  size_t bufpos = *pbufpos;
  while (datalen > 0) {
    if (bufpos == 0 && datalen >= buflen) {
      ssize_t w = write(fd, data, buflen);
      if (w < 0)
        return -1;
      data += w;
      datalen -= w;
      continue;
    }

    if (bufpos + datalen < buflen) {
      memcpy(buf + bufpos, data, datalen);
      bufpos += datalen;
      *pbufpos = bufpos;
      return 0;
    }

    memcpy(buf + bufpos, data, buflen - bufpos);
    data += buflen - bufpos;
    bufpos -= buflen - bufpos;
    ssize_t w = write(fd, buf, buflen);
    if (w == -1)
      return -1;
    bufpos -= w;
    memmove(buf, buf + w, bufpos);
  }
  *pbufpos = bufpos;
  return 0;
}

#define kprintf_puto(fd, buf, buflen, pbufpos, type, n)                        \
  ({                                                                           \
    type _n = (n);                                                             \
    char _buf[(sizeof(n) * 2 + 2) / 3 + 1];                                    \
    _buf[sizeof(_buf) - 1] = '\0';                                             \
    int _i = sizeof(_buf) - 2;                                                 \
    do {                                                                       \
      _buf[_i--] = '0' + (_n & 7);                                             \
      _n >>= 3;                                                                \
    } while (_n > 0);                                                          \
    kprintf_putbuf((fd), (buf), (buflen), (pbufpos), _buf + _i + 1,            \
                   sizeof(_buf) - 2 - _i);                                     \
  })

#define kprintf_putx(fd, buf, buflen, pbufpos, type, n)                        \
  ({                                                                           \
    type _n = (n);                                                             \
    char _buf[sizeof(n) * 2 + 1];                                              \
    _buf[sizeof(_buf) - 1] = '\0';                                             \
    int _i = sizeof(_buf) - 2;                                                 \
    do {                                                                       \
      _buf[_i--] = "0123456789abcdef"[_n & 15];                                \
      _n >>= 4;                                                                \
    } while (_n > 0);                                                          \
    kprintf_putbuf((fd), (buf), (buflen), (pbufpos), _buf + _i + 1,            \
                   sizeof(_buf) - 2 - _i);                                     \
  })

#define kprintf_putX(fd, buf, buflen, pbufpos, type, n)                        \
  ({                                                                           \
    type _n = (n);                                                             \
    char _buf[sizeof(n) * 2 + 1];                                              \
    _buf[sizeof(_buf) - 1] = '\0';                                             \
    int _i = sizeof(_buf) - 2;                                                 \
    do {                                                                       \
      _buf[_i--] = "0123456789ABCDEF"[_n & 15];                                \
      _n >>= 4;                                                                \
    } while (_n > 0);                                                          \
    kprintf_putbuf((fd), (buf), (buflen), (pbufpos), _buf + _i + 1,            \
                   sizeof(_buf) - 2 - _i);                                     \
  })

#define kprintf_putnum(fd, buf, buflen, pbufpos, sign, type, n)                \
  ({                                                                           \
    type _n = (n);                                                             \
    char _sign = (sign);                                                       \
    char _buf[sizeof(n) * 4 + 2];                                              \
    _buf[sizeof(_buf) - 1] = '\0';                                             \
    int _i = sizeof(_buf) - 2;                                                 \
    do {                                                                       \
      _buf[_i--] = '0' + _n % 10;                                              \
      _n /= 10;                                                                \
    } while (_n > 0);                                                          \
    if (_sign != '\0')                                                         \
      buf[_i--] = _sign;                                                       \
    kprintf_putbuf((fd), (buf), (buflen), (pbufpos), _buf + _i + 1,            \
                   sizeof(_buf) - 2 - _i);                                     \
  })

#define kprintf_putu(fd, buf, buflen, pbufpos, type, n)                        \
  kprintf_putnum((fd), (buf), (buflen), (pbufpos), '\0', type, (n));

#define kprintf_putd(fd, buf, buflen, pbufpos, type, n)                        \
  ({                                                                           \
    type _m = (n);                                                             \
    _m < 0 ? kprintf_putnum((fd), (buf), (buflen), (pbufpos), '-', type, -_m)  \
           : kprintf_putnum((fd), (buf), (buflen), (pbufpos), '\0', type, _m); \
  })

#define KPLN_DEFAULT 0
#define KPLN_CHAR 1
#define KPLN_SHORT 2
#define KPLN_LONG 3
#define KPLN_LONGLONG 4
#define KPLN_INTMAX 5
#define KPLN_SIZE 6
#define KPLN_PTR 7

static void kprintf_length(const char **pp, int *plength) {
  while (1) {
    switch (**pp) {

    case 'h':
      switch (*plength) {
      case KPLN_DEFAULT:
        *plength = KPLN_SHORT;
        (*pp)++;
        continue;
      case KPLN_SHORT:
        *plength = KPLN_CHAR;
        (*pp)++;
        continue;
      }
      break;

    case 'l':
      switch (*plength) {
      case KPLN_DEFAULT:
        *plength = KPLN_LONG;
        (*pp)++;
        continue;
      case KPLN_LONG:
        *plength = KPLN_LONGLONG;
        (*pp)++;
        continue;
      }
      break;

    case 'j':
      switch (*plength) {
      case KPLN_DEFAULT:
        *plength = KPLN_INTMAX;
        (*pp)++;
        continue;
      }
      break;

    case 'z':
      switch (*plength) {
      case KPLN_DEFAULT:
        *plength = KPLN_SIZE;
        (*pp)++;
        continue;
      }

    default:
      return;
    }
  }
}

static int kprintf_parcentd(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  switch (length) {
  case KPLN_DEFAULT:
    return kprintf_putd(fd, buf, buflen, pbufpos, int, va_arg(ap, int));

  case KPLN_CHAR:
    return kprintf_putd(fd, buf, buflen, pbufpos, char, va_arg(ap, int));

  case KPLN_SHORT:
    return kprintf_putd(fd, buf, buflen, pbufpos, short, va_arg(ap, int));

  case KPLN_LONG:
    return kprintf_putd(fd, buf, buflen, pbufpos, long, va_arg(ap, long));

  case KPLN_LONGLONG:
    return kprintf_putd(fd, buf, buflen, pbufpos, long long,
                        va_arg(ap, long long));

  case KPLN_INTMAX:
    return kprintf_putd(fd, buf, buflen, pbufpos, intmax_t,
                        va_arg(ap, intmax_t));

  case KPLN_SIZE:
    return kprintf_putd(fd, buf, buflen, pbufpos, ssize_t, va_arg(ap, ssize_t));
  }
  abort();
}

static int kprintf_parcentu(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  switch (length) {
  case KPLN_DEFAULT:
    return kprintf_putu(fd, buf, buflen, pbufpos, int, va_arg(ap, int));

  case KPLN_CHAR:
    return kprintf_putu(fd, buf, buflen, pbufpos, char, va_arg(ap, int));

  case KPLN_SHORT:
    return kprintf_putu(fd, buf, buflen, pbufpos, short, va_arg(ap, int));

  case KPLN_LONG:
    return kprintf_putu(fd, buf, buflen, pbufpos, long, va_arg(ap, long));

  case KPLN_LONGLONG:
    return kprintf_putu(fd, buf, buflen, pbufpos, long long,
                        va_arg(ap, long long));

  case KPLN_INTMAX:
    return kprintf_putu(fd, buf, buflen, pbufpos, intmax_t,
                        va_arg(ap, intmax_t));

  case KPLN_SIZE:
    return kprintf_putu(fd, buf, buflen, pbufpos, ssize_t, va_arg(ap, ssize_t));
  }
  abort();
}

static int kprintf_parcento(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  switch (length) {
  case KPLN_DEFAULT:
    return kprintf_puto(fd, buf, buflen, pbufpos, int, va_arg(ap, int));

  case KPLN_CHAR:
    return kprintf_puto(fd, buf, buflen, pbufpos, char, va_arg(ap, int));

  case KPLN_SHORT:
    return kprintf_puto(fd, buf, buflen, pbufpos, short, va_arg(ap, int));

  case KPLN_LONG:
    return kprintf_puto(fd, buf, buflen, pbufpos, long, va_arg(ap, long));

  case KPLN_LONGLONG:
    return kprintf_puto(fd, buf, buflen, pbufpos, long long,
                        va_arg(ap, long long));

  case KPLN_INTMAX:
    return kprintf_puto(fd, buf, buflen, pbufpos, intmax_t,
                        va_arg(ap, intmax_t));

  case KPLN_SIZE:
    return kprintf_puto(fd, buf, buflen, pbufpos, ssize_t, va_arg(ap, ssize_t));
  }
  abort();
}

static int kprintf_parcentx(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  switch (length) {
  case KPLN_DEFAULT:
    return kprintf_putx(fd, buf, buflen, pbufpos, int, va_arg(ap, int));

  case KPLN_CHAR:
    return kprintf_putx(fd, buf, buflen, pbufpos, char, va_arg(ap, int));

  case KPLN_SHORT:
    return kprintf_putx(fd, buf, buflen, pbufpos, short, va_arg(ap, int));

  case KPLN_LONG:
    return kprintf_putx(fd, buf, buflen, pbufpos, long, va_arg(ap, long));

  case KPLN_LONGLONG:
    return kprintf_putx(fd, buf, buflen, pbufpos, long long,
                        va_arg(ap, long long));

  case KPLN_INTMAX:
    return kprintf_putx(fd, buf, buflen, pbufpos, intmax_t,
                        va_arg(ap, intmax_t));

  case KPLN_SIZE:
    return kprintf_putx(fd, buf, buflen, pbufpos, ssize_t, va_arg(ap, ssize_t));

  case KPLN_PTR:
    return kprintf_putx(fd, buf, buflen, pbufpos, intptr_t,
                        (intptr_t)va_arg(ap, void *));
  }
  abort();
}

static int kprintf_parcentX(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  switch (length) {
  case KPLN_DEFAULT:
    return kprintf_putX(fd, buf, buflen, pbufpos, int, va_arg(ap, int));

  case KPLN_CHAR:
    return kprintf_putX(fd, buf, buflen, pbufpos, char, va_arg(ap, int));

  case KPLN_SHORT:
    return kprintf_putX(fd, buf, buflen, pbufpos, short, va_arg(ap, int));

  case KPLN_LONG:
    return kprintf_putX(fd, buf, buflen, pbufpos, long, va_arg(ap, long));

  case KPLN_LONGLONG:
    return kprintf_putX(fd, buf, buflen, pbufpos, long long,
                        va_arg(ap, long long));

  case KPLN_INTMAX:
    return kprintf_putX(fd, buf, buflen, pbufpos, intmax_t,
                        va_arg(ap, intmax_t));

  case KPLN_SIZE:
    return kprintf_putX(fd, buf, buflen, pbufpos, ssize_t, va_arg(ap, ssize_t));
  }
  abort();
}

static int kprintf_parcentp(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  if (length != KPLN_DEFAULT)
    return -1;
  int ret = kprintf_putbuf(fd, buf, buflen, pbufpos, "0x", 2);
  if (ret < 0)
    return -1;
  return kprintf_parcentx(fd, buf, buflen, pbufpos, KPLN_PTR, ap);
}

static int kprintf_parcentc(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  if (length != KPLN_DEFAULT)
    return -1;
  char ch = va_arg(ap, int);
  return kprintf_putbuf(fd, buf, buflen, pbufpos, &ch, 1);
}

static int kprintf_parcents(int fd, char *buf, size_t buflen, size_t *pbufpos,
                            int length, va_list ap) {
  if (length != KPLN_DEFAULT)
    return -1;
  const char *str = va_arg(ap, const char *);
  if (str == NULL)
    str = "(null)";
  return kprintf_putbuf(fd, buf, buflen, pbufpos, str, strlen(str));
}

static int kprintf_parcent(int fd, char *buf, size_t buflen, size_t *pbufpos,
                           const char **pp, va_list ap) {
  int length = KPLN_DEFAULT;
  kprintf_length(pp, &length);
  switch (**pp) {
  case 'd':
  case 'i':
    return kprintf_parcentd(fd, buf, buflen, pbufpos, length, ap);

  case 'u':
    return kprintf_parcentu(fd, buf, buflen, pbufpos, length, ap);

  case 'p':
    return kprintf_parcentp(fd, buf, buflen, pbufpos, length, ap);

  case 'o':
    return kprintf_parcento(fd, buf, buflen, pbufpos, length, ap);

  case 'x':
    return kprintf_parcentx(fd, buf, buflen, pbufpos, length, ap);

  case 'X':
    return kprintf_parcentX(fd, buf, buflen, pbufpos, length, ap);

  case 's':
    return kprintf_parcents(fd, buf, buflen, pbufpos, length, ap);

  case 'c':
    return kprintf_parcentc(fd, buf, buflen, pbufpos, length, ap);

  case '%':
    return kprintf_putbuf(fd, buf, buflen, pbufpos, "%", 1);
  }
  return -1;
}

int kprintf(int fd, const char *fmt, ...) {
  size_t buflen = sysconf(_SC_PAGESIZE);
  char buf[buflen];
  size_t bufpos = 0;

  int n = 0;
  va_list ap;
  va_start(ap, fmt);

  const char *p = fmt;
  while (1) {
    int ch = *p;
    int ret;

    switch (ch) {
    case '\0':
      ret = kprintf_flushbuf(fd, buf, &bufpos);
      if (ret == -1)
        return -1;
      return n;

    case '%':
      p++;
      ret = kprintf_parcent(fd, buf, buflen, &bufpos, &p, ap);
      if (ret == -1)
        return -1;
      n++;
      break;

    default:
      ret = kprintf_putbuf(fd, buf, buflen, &bufpos, p, 1);
      break;
    }
    p++;
  }
}