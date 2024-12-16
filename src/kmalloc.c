#define _GNU_SOURCE
#include "kmalloc.h"
#include "kmalloc_internal.h"
#include <assert.h>
#include <errno.h>
#include <gc.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Memory allocation functions
void *kmalloc(size_t size) { return GC_malloc(size); }
void *kmalloc_atomic(size_t size) { return GC_malloc_atomic(size); }
void *krealloc(void *ptr, size_t size) { return GC_realloc(ptr, size); }
void kfree(void *ptr) { (void)ptr; }
char *kstrdup(const char *s) { return GC_strdup(s); }

#ifdef KM_USE_SYSTEM_ALLOC

// USE SYSTEM ALLOC
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);

#else

#ifdef DEBUG
static int km_puts(int fd, const char *s) {
  size_t len = strlen(s);
  ssize_t ret = write(fd, s, len);
  if (ret == -1)
    return -1;
  if ((size_t)ret != len)
    return -1;
  return 0;
}

static int km_putptr(int fd, const void *ptr) {
  char buf[sizeof(intptr_t) * 2 + 3];
  intptr_t p = (intptr_t)ptr;
  for (size_t i = 0; i < sizeof(intptr_t) * 2; i++) {
    buf[sizeof(intptr_t) * 2 - i + 1] = "0123456789abcdef"[p & 0xf];
    p >>= 4;
  }
  buf[0] = '0';
  buf[1] = 'x';
  buf[sizeof(intptr_t) * 2 + 2] = '\0';
  return km_puts(fd, buf);
}

#define km_putuint(fd, n)                                                      \
  ({                                                                           \
    __auto_type _n = (n);                                                      \
    char _buf[sizeof(n) * 4 + 1];                                              \
    _buf[sizeof(_buf) - 1] = '\0';                                             \
    int _i = sizeof(_buf) - 2;                                                 \
    do {                                                                       \
      _buf[_i--] = '0' + _n % 10;                                              \
      _n /= 10;                                                                \
    } while (_n > 0);                                                          \
    km_puts((fd), _buf + _i + 1);                                              \
  })

#endif

typedef struct km_arena km_arena_t;
typedef struct km_frag km_frag_t;
typedef int (*km_expand_arena_func_t)(km_arena_t *, size_t);

/** Align of size */
#define KM_ALIGNSIZE 8

struct km_frag {
  size_t data;
};

struct km_arena {
  size_t size;
  size_t index;
  pthread_mutex_t lock;
  km_expand_arena_func_t expand_arena_func;
  km_frag_t pfr[0];
};

/** Memhdr flag mask of in-use */
#define KM_FHFLMASK_USAGE 1

/** Memhdr flag mask of mmap */
#define KM_FHFLMASK_METHOD 2

/** Memhdr flag of in-use */
#define KM_MHFL_INUSE 1

/** Memhdr flag of free */
#define KM_MHFL_FREE 0

/** Memhdr flag of mmap */
#define KM_MHFL_MMAP 2

/** Memhdr flag of main arena */
#define KM_MHFL_MA 0

/** Initial size of main arena */
#define KM_MASIZE_INITIAL 1048576

/** Maximum fragment size for main alena */
#define KM_FRSIZEMAXMA 1048576

// ************************************************************************************************
// GENERIC ARENA
// ************************************************************************************************

#define km_assert(x) assert(x)
// #define km_assert(x) ((void)0)

static km_frag_t *km_arget_frfirst(km_arena_t *par) {
  km_assert(par != NULL);
  return par->pfr;
}

static km_frag_t *km_arget_frlast(km_arena_t *par) {
  km_assert(par != NULL);
  km_assert(par->size >= sizeof(km_arena_t));
  km_assert((par->size & (KM_ALIGNSIZE - 1)) == 0);
  return (km_frag_t *)((void *)par + par->size);
}

static void km_arset_frlast(km_arena_t *par, km_frag_t *pfr) {
  km_assert(par != NULL);
  km_assert(pfr != NULL);
  par->size = (void *)pfr - (void *)par;
  km_assert(par->size >= sizeof(km_arena_t));
  km_assert((par->size & (KM_ALIGNSIZE - 1)) == 0);
}

static km_frag_t *km_arget_frsearch(km_arena_t *par) {
  km_assert(par != NULL);
  km_assert(par->index >= sizeof(km_arena_t));
  km_assert(par->index <= par->size);
  km_frag_t *pfr_search = (void *)par + par->index;
  return pfr_search;
}

static void km_arset_frsearch(km_arena_t *par, km_frag_t *pfr) {
  km_assert(par != NULL);
  km_assert(pfr != NULL);
  km_assert(pfr >= (km_frag_t *)(par + 1));
  km_assert(pfr <= km_arget_frlast(par));
  par->index = (void *)pfr - (void *)par;
}

static size_t km_frget_size(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return pfr->data & ~(KM_ALIGNSIZE - 1);
}

static km_frag_t *km_frget_next_by_size(km_frag_t *pfr, size_t frsize) {
  km_assert(pfr != NULL);
  km_assert(frsize >= sizeof(km_frag_t));
  km_assert((frsize & (KM_ALIGNSIZE - 1)) == 0);
  km_frag_t *fr_next = (void *)pfr + frsize;
  return fr_next;
}

static km_frag_t *km_frget_next(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return km_frget_next_by_size(pfr, km_frget_size(pfr));
}

static void *km_frget_dptr(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return (void *)(pfr + 1);
}

static int km_frisfree(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return (pfr->data & KM_FHFLMASK_USAGE) == KM_MHFL_FREE;
}

static int km_frisinuse(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return (pfr->data & KM_FHFLMASK_USAGE) == KM_MHFL_INUSE;
}

static int km_frismmap(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return (pfr->data & KM_FHFLMASK_METHOD) == KM_MHFL_MMAP;
}

static int km_frisma(km_frag_t *pfr) {
  km_assert(pfr != NULL);
  return (pfr->data & KM_FHFLMASK_METHOD) == KM_MHFL_MA;
}

static void km_frsetflags(km_frag_t *pfr, int flags) {
  km_assert(pfr != NULL);
  km_assert((flags & ~KM_FHFLMASK_USAGE) == 0);
  pfr->data = (pfr->data & ~KM_FHFLMASK_USAGE) | flags;
}

static void km_frset(km_frag_t *pfr, size_t frsize, int flags) {
  km_assert(pfr != NULL);
  km_assert(frsize >= sizeof(km_frag_t));
  km_assert((frsize & (KM_ALIGNSIZE - 1)) == 0);
  km_assert((flags & ~KM_FHFLMASK_USAGE) == 0);
  pfr->data = frsize | flags;
}

static size_t km_dsizeget_frsize(size_t dsize_req) {
  size_t frsize_req = (dsize_req + KM_ALIGNSIZE - 1) & ~(KM_ALIGNSIZE - 1);
  if (frsize_req < 16)
    frsize_req = 16;
  return frsize_req;
}

static km_frag_t *km_dptrget_fr(void *dptr) {
  km_assert(dptr != NULL);
  return (km_frag_t *)((void *)dptr - sizeof(km_frag_t));
}

static km_frag_t *km_arena_init(km_arena_t *par, size_t size,
                                km_expand_arena_func_t expand_arena_func) {
  km_assert(par != NULL);
  km_assert(size >= sizeof(km_arena_t));
  km_assert((size & (KM_ALIGNSIZE - 1)) == 0);
  par->size = size;
  par->index = sizeof(km_arena_t);
  par->expand_arena_func = expand_arena_func;
  par->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  km_frag_t *pfr = km_arget_frfirst(par);
  km_frset(pfr, size - sizeof(km_arena_t), KM_MHFL_FREE);
  return pfr;
}

static void *km_arena_alloc(km_arena_t *par, int flags, size_t dsize_req) {
  km_assert(par != NULL);
  km_assert((flags & ~KM_FHFLMASK_USAGE) == 0);
  km_assert(dsize_req >= 1);
  km_frag_t *pfr_first = km_arget_frfirst(par);
  km_frag_t *pfr_last = km_arget_frlast(par);
  km_frag_t *pfr_search = km_arget_frsearch(par);
  km_expand_arena_func_t expand_arena_func = par->expand_arena_func;

  if (pfr_search >= pfr_last)
    pfr_search = pfr_first;

  size_t frsize_req = km_dsizeget_frsize(dsize_req);

  km_frag_t *pfr = pfr_search;
  size_t frsize;

  km_frag_t *pfr_lastfree = pfr_last;
  size_t frsize_lastfree = 0;

  km_frag_t *pfr_loopend = pfr_last;

  while (1) {
    frsize = km_frget_size(pfr);

    if (km_frisfree(pfr)) {
      if (frsize >= frsize_req)
        break;
    }

    km_frag_t *pfr_next = km_frget_next(pfr);
    if (pfr_next < pfr_loopend) {
      if (km_frisfree(pfr_next)) {
        frsize += km_frget_size(pfr_next);
        if (frsize >= frsize_req)
          break;
        km_frset(pfr, frsize, KM_MHFL_FREE | flags);
        continue;
      }
      pfr = pfr_next;
      continue;
    }

    if (pfr_loopend < pfr_last)
      break;

    if (km_frisfree(pfr)) {
      pfr_lastfree = pfr;
      frsize_lastfree = frsize;
    }

    pfr = pfr_first;
  }

  if (km_frisfree(pfr) && frsize >= frsize_req) {
    if (frsize_req + sizeof(km_frag_t) >= frsize)
      frsize_req = frsize;
    km_frset(pfr, frsize_req, KM_MHFL_INUSE | flags);
    pfr = km_frget_next(pfr);
    frsize_req = frsize - frsize_req;
    if (frsize_req > 0)
      km_frset(pfr, frsize_req, KM_MHFL_FREE | flags);
    km_arset_frsearch(par, km_frget_next(pfr));
    return km_frget_dptr(pfr);
  }

  if (expand_arena_func == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  int ret = expand_arena_func(par, frsize_req - frsize_lastfree);
  if (ret == -1) {
    errno = ENOMEM;
    return NULL;
  }

  pfr_last = km_arget_frlast(par);
  frsize = (void *)pfr_last - (void *)pfr_lastfree;
  if (frsize < frsize_req + sizeof(km_frag_t))
    frsize_req = frsize;
  km_frset(pfr_lastfree, frsize_req, KM_MHFL_INUSE | flags);
  pfr = km_frget_next(pfr_lastfree);
  frsize_req = frsize - frsize_req;
  if (frsize_req > 0)
    km_frset(pfr, frsize_req, KM_MHFL_FREE | flags);

  km_arset_frsearch(par, km_frget_next(pfr_lastfree));
  return km_frget_dptr(pfr_lastfree);
}

static void km_arena_free(void *dptr) {
  km_assert(dptr != NULL);
  km_frag_t *pfr = km_dptrget_fr(dptr);
  km_frsetflags(pfr, KM_MHFL_FREE);
}

static void *km_arena_realloc_internal(km_arena_t *par, void *dptr,
                                       size_t dsize_req, int flags) {
  km_assert(par != NULL);
  km_assert(dptr != NULL);
  km_assert((flags & ~KM_FHFLMASK_USAGE) == 0);
  km_assert(dsize_req >= 1);

  km_frag_t *pfr_last = km_arget_frlast(par);
  km_frag_t *pfr = km_dptrget_fr(dptr);
  km_frag_t *pfr_next;

  size_t frsize_req = km_dsizeget_frsize(dsize_req);
  size_t frsize = km_frget_size(pfr);

  while (1) {
    if (frsize >= frsize_req)
      break;

    pfr_next = km_frget_next_by_size(pfr, frsize);
    if (pfr_next >= pfr_last)
      break;

    if (km_frisinuse(pfr_next))
      break;

    frsize += km_frget_size(pfr_next);
  }

  if (frsize >= frsize_req) {
    if (frsize < frsize_req + sizeof(km_frag_t))
      frsize_req = frsize;
    km_frset(pfr, frsize_req, KM_MHFL_INUSE | flags);
    pfr = km_frget_next(pfr);
    frsize -= frsize_req;
    if (frsize > 0)
      km_frset(pfr, frsize, KM_MHFL_FREE | flags);
    return km_frget_dptr(pfr);
  }

  if (pfr_next < pfr_last) {
    void *dptr = km_arena_alloc(par, flags, dsize_req);
    if (dptr == NULL)
      return NULL;
    memcpy(dptr, km_frget_dptr(pfr), km_frget_size(pfr));
    km_arena_free(km_frget_dptr(pfr));
    return dptr;
  }

  if (par->expand_arena_func == NULL) {
    errno = ENOMEM;
    return NULL;
  }

  int ret = par->expand_arena_func(par, frsize_req - frsize);
  if (ret == -1) {
    errno = ENOMEM;
    return NULL;
  }

  pfr_last = km_arget_frlast(par);
  frsize = (void *)pfr_last - (void *)pfr;
  if (frsize < frsize_req + sizeof(km_frag_t))
    frsize_req = frsize;
  km_frset(pfr, frsize_req, KM_MHFL_INUSE | flags);
  pfr = km_frget_next(pfr);
  frsize -= frsize_req;
  if (frsize > 0)
    km_frset(pfr, frsize, KM_MHFL_FREE | flags);
  return km_frget_dptr(pfr);
}

static void *km_arena_realloc(km_arena_t *par, void *dptr, size_t dsize_req,
                              int flags) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, par);
  km_puts(2, ", ");
  km_putptr(2, dptr);
  km_puts(2, ", ");
  km_putuint(2, dsize_req);
  km_puts(2, ", ");
  km_putuint(2, flags);
  km_puts(2, ")\n");
#endif
  void *dptr_new = km_arena_realloc_internal(par, dptr, dsize_req, flags);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr_new);
  km_puts(2, "\n");
#endif
  return dptr_new;
}

// ************************************************************************************************
// MAIN ARENA
// ************************************************************************************************

/** main arena starting pointer */
static km_arena_t *g_main_arena = NULL;

static int km_maexpand(km_arena_t *par, size_t size) {
  km_assert(par != NULL);
  km_assert(size >= sizeof(km_arena_t));
  km_assert((size & (KM_ALIGNSIZE - 1)) == 0);
  void *ptr = sbrk(size);
  if (ptr == (void *)-1)
    return -1;

  km_arset_frlast(par, (km_frag_t *)((void *)ptr + size));
  return 0;
}

static void km_mainit(size_t size) {
  km_assert(size >= sizeof(km_arena_t));

  if (g_main_arena != NULL)
    return;

  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&lock);
  if (g_main_arena != NULL)
    return;

  g_main_arena = (km_arena_t *)sbrk(size);
  if (g_main_arena == (void *)-1)
    return;

  km_arena_init(g_main_arena, size, km_maexpand);
  pthread_mutex_unlock(&lock);
}

static void *km_mamalloc_internal(size_t dsize_req) {
  km_assert(dsize_req >= 1);
  km_mainit(KM_MASIZE_INITIAL);
  pthread_mutex_lock(&g_main_arena->lock);
  void *dptr = km_arena_alloc(g_main_arena, KM_MHFL_MA, dsize_req);
  pthread_mutex_unlock(&g_main_arena->lock);
  return dptr;
}

static void *km_mamalloc(size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr = km_mamalloc_internal(dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr);
  km_puts(2, "\n");
#endif
  return dptr;
}

static void km_mafree(void *dptr) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ")\n");
#endif
  km_assert(dptr != NULL);
  km_assert(g_main_arena != NULL);
  km_assert(km_arget_frfirst(g_main_arena) <= km_dptrget_fr(dptr));
  km_assert(km_dptrget_fr(dptr) < km_arget_frlast(g_main_arena));
  pthread_mutex_lock(&g_main_arena->lock);
  km_arena_free(dptr);
  pthread_mutex_unlock(&g_main_arena->lock);
}

static void *km_marealloc_internal(void *dptr, size_t dsize_req) {
  km_assert(dptr != NULL || dsize_req == 0);
  km_mainit(KM_MASIZE_INITIAL);
  if (dptr == NULL)
    return km_mamalloc(dsize_req);
  if (dsize_req == 0) {
    km_mafree(dptr);
    return NULL;
  }
  pthread_mutex_lock(&g_main_arena->lock);
  void *dptr_new = km_arena_realloc(g_main_arena, dptr, dsize_req, KM_MHFL_MA);
  pthread_mutex_unlock(&g_main_arena->lock);
  return dptr_new;
}

static void *km_marealloc(void *dptr, size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ", ");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr_new = km_marealloc_internal(dptr, dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr_new);
  km_puts(2, "\n");
#endif
  return dptr_new;
}

// ************************************************************************************************
// MMAP
// ************************************************************************************************

static void *km_mmmalloc_internal(size_t dsize_req) {
  km_assert(dsize_req >= 1);
  size_t frsize_req = km_dsizeget_frsize(dsize_req);
  km_frag_t *pfr = mmap(NULL, frsize_req, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pfr == (void *)-1)
    return NULL;
  km_frset(pfr, frsize_req, KM_MHFL_INUSE | KM_MHFL_MMAP);
  return km_frget_dptr(pfr);
}

static void *km_mmmalloc(size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr = km_mmmalloc_internal(dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr);
  km_puts(2, "\n");
#endif
  return dptr;
}

static void km_mmfree(void *dptr) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ")\n");
#endif
  km_assert(dptr != NULL);
  km_frag_t *pfr = km_dptrget_fr(dptr);
  int ret = munmap(pfr, km_frget_size(pfr));
  if (ret == -1)
    abort();
}

void *km_mmrealloc_internal(void *dptr, size_t dsize_req) {
  km_assert(dptr != NULL || dsize_req == 0);
  km_frag_t *ptr = km_dptrget_fr(dptr);
  size_t frsize = km_frget_size(ptr);
  size_t frsize_req = km_dsizeget_frsize(dsize_req);

  // ALLOCATE BY MMAP
  void *ptr_new = mremap(ptr, frsize, frsize_req, MREMAP_MAYMOVE);
  if (ptr_new == (void *)-1)
    return NULL;
  km_frset(ptr_new, frsize_req, KM_MHFL_INUSE | KM_MHFL_MMAP);
  return km_frget_dptr(ptr_new);
}

static void *km_mmrealloc(void *dptr, size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ", ");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr_new = km_mmrealloc_internal(dptr, dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr_new);
  km_puts(2, "\n");
#endif
  return dptr_new;
}

// ************************************************************************************************
// MEMORY ALLOCATION
// ************************************************************************************************

static void *km_malloc_internal(size_t dsize_req) {
  if (dsize_req == 0)
    return NULL;

  size_t frsize_req = km_dsizeget_frsize(dsize_req);
  if (frsize_req > KM_FRSIZEMAXMA)
    return km_mmmalloc(dsize_req);

  return km_mamalloc(dsize_req);
}

static void *km_malloc(size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr = km_malloc_internal(dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr);
  km_puts(2, "\n");
#endif
  return dptr;
}

static void km_free(void *dptr) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ")\n");
#endif

  if (dptr == NULL)
    return;

  km_frag_t *pfr = km_dptrget_fr(dptr);
  if (km_frismmap(pfr))
    km_mmfree(dptr);
  else
    km_mafree(dptr);
}

static void *km_realloc_internal(void *dptr, size_t dsize_req) {
  if (dptr == NULL)
    return km_malloc(dsize_req);

  if (dsize_req == 0) {
    km_free(dptr);
    return NULL;
  }

  if (dsize_req == 0)
    return NULL;

  km_frag_t *pfr = km_dptrget_fr(dptr);
  size_t frsize_req = km_dsizeget_frsize(dsize_req);
  if (km_frismmap(pfr) && frsize_req > KM_FRSIZEMAXMA)
    return km_mmrealloc(dptr, dsize_req);

  if (km_frisma(pfr) && frsize_req <= KM_FRSIZEMAXMA)
    return km_marealloc(dptr, dsize_req);

  void *dptr_new = km_malloc(dsize_req);
  if (dptr_new == NULL)
    return NULL;
  memcpy(dptr_new, dptr, km_frget_size(pfr));
  km_free(dptr);
  return dptr_new;
}

static void *km_realloc(void *dptr, size_t dsize_req) {
#ifdef DEBUG
  km_puts(2, __func__);
  km_puts(2, "(");
  km_putptr(2, dptr);
  km_puts(2, ", ");
  km_putuint(2, dsize_req);
  km_puts(2, ")\n");
#endif
  void *dptr_new = km_realloc_internal(dptr, dsize_req);
#ifdef DEBUG
  km_puts(2, "  -> ");
  km_putptr(2, dptr_new);
  km_puts(2, "\n");
#endif
  return dptr_new;
}

void *km_alloc(void *ptr, size_t size) { return km_realloc(ptr, size); }

void *malloc(size_t size) { return km_malloc(size); }

void *realloc(void *ptr, size_t size) { return km_realloc(ptr, size); }

void free(void *ptr) { km_free(ptr); }

void *calloc(size_t nmemb, size_t size) {
  void *ptr = km_malloc(nmemb * size);
  if (ptr != NULL)
    memset(ptr, 0, nmemb * size);
  return ptr;
}

#endif

// Memory allocation functions
void *ksmalloc(size_t size) { return malloc(size); }
void *ksmalloc_atomic(size_t size) { return malloc(size); }
void *ksrealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void ksfree(void *ptr) { free(ptr); }
char *ksstrdup(const char *s) { return strdup(s); }
