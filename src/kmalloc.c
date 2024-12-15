#define _GNU_SOURCE
#include "kmalloc.h"
#include "kmalloc_internal.h"
#include <assert.h>
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

typedef struct memhdr {
  size_t mh;
} memhdr_t;

/** Align of size */
#define KM_ALIGNSIZE KM_MHSIZE

/** Get size of memhdr */
#define KM_MHSIZE (sizeof(memhdr_t))

/** Memhdr flag mask of in-use */
#define KM_MHFLMASK_INUSE 1

/** Memhdr flag mask of mmap */
#define KM_MHFLMASK_MMAP 2

/** Memhdr flag of in-use */
#define KM_MHFL_INUSE 1

/** Memhdr flag of free */
#define KM_MHFL_FREE 0

/** Memhdr flag of mmap */
#define KM_MHFL_MMAP 2

/** Memhdr flag of main arena */
#define KM_MHFL_MA 0

/** Check if memhdr is in-use */
#define KM_MHISINUSE(s) (((s).mh & KM_MHFLMASK_INUSE) == KM_MHFL_INUSE)

/** Check if memhdr is free */
#define KM_MHISFREE(s) (((s).mh & KM_MHFLMASK_INUSE) == KM_MHFL_FREE)

/** Check if memhdr is mmap */
#define KM_MHISMMAP(s) (((s).mh & KM_MHFLMASK_MMAP) == KM_MHFL_MMAP)

/** Check if memhdr is main arena */
#define KM_MHISMA(s) (((s).mh & KM_MHFLMASK_MMAP) == KM_MHFL_MA)

/** size mask of memhdr */
#define KM_MHFLMASK_SIZE (~(KM_ALIGNSIZE - 1))

/** Construct memhdr */
#define KM_MH(s, f) ((memhdr_t){.mh = (s) | (f)})

/** Get memhdr from pointer */
#define KH_PTRGETMH(p) (*(memhdr_t *)(p))

/** Set memhdr to pointer */
#define KM_PTRSETMH(p, m) (*(memhdr_t *)(p) = (m))

/** Get size from memhdr */
#define KM_MHGETSIZE(s) ((s).mh & KM_MHFLMASK_SIZE)

/** Align allocation size */
#define KM_SIZEALGIN(s) (((s) + KM_ALIGNSIZE - 1) & ~(KM_ALIGNSIZE - 1))

/** GET dataptr from memhdr */
#define KM_PTRGETDPTR(p) ((void *)((memhdr_t *)(p) + 1))

/** GET pointer from dataptr */
#define KM_DPTRGETPTR(p) ((void *)((memhdr_t *)(p) - 1))

/** Initial size of main arena */
#define KM_MASIZE_INITIAL 1048576

/** Maximum fragment size for main alena */
#define KM_FRSIZEMAXMA 1048576

/** main arena starting pointer */
static void *g_main_arena_start = NULL;
/** main arena ending pointer */
static void *g_main_arena_end = NULL;
/** main arena search pointer */
static void *g_main_arena_ptr = NULL;
/** main arena lock */
static pthread_mutex_t g_main_arena_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize main arena
 *
 * must lock g_main_arena_lock before calling this function
 *
 * @param size size of main arena
 */
static void km_mainit(size_t size) {
  // ALREADY INITIALIZED
  if (g_main_arena_start != NULL)
    return;

  // SET HEAP START POINTER to g_main_arena
  g_main_arena_start = sbrk(0);
  if (g_main_arena_start == (void *)-1)
    abort();

  // SET HEAP END POINTER to g_main_arena_end
  g_main_arena_end = g_main_arena_start + size;
  int ret = brk(g_main_arena_end);
  if (ret == -1)
    abort();

  // SET MAIN ARENA POINTER to g_main_arena
  g_main_arena_ptr = g_main_arena_start;

  // SET MAIN ARENA FLAGMENT to g_main_arena
  KM_PTRSETMH(g_main_arena_ptr, KM_MH(size, KM_MHFL_FREE));
}

/**
 * Allocate memory from main arena
 *
 * @param dsize_req size of memory to allocate
 * @return pointer to allocated memory
 */
static void *km_mamalloc_internal(size_t dsize_req) {
  // - - - - - - - - - - - - -
  // ALLOCATE FROM MAIN ALENA
  // - - - - - - - - - - - - -
  void *main_arena_start = g_main_arena_start;
  void *main_arena_end = g_main_arena_end;
  void *main_arena_ptr = g_main_arena_ptr;

  if (main_arena_ptr >= main_arena_end)
    main_arena_ptr = main_arena_start;

  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);

  void *ptr = main_arena_ptr;
  size_t frsize;
  memhdr_t mh;

  void *ptr_lastfree = main_arena_end;

  while (1) {
    mh = KH_PTRGETMH(ptr);
    frsize = KM_MHGETSIZE(mh);

    if (KM_MHISFREE(mh) && frsize >= frsize_req)
      break;

    if (ptr + frsize < main_arena_end) {
      ptr += frsize;
      continue;
    }

    if (KM_MHISFREE(mh))
      ptr_lastfree = ptr;

    ptr = main_arena_start;

    while (1) {
      mh = KH_PTRGETMH(ptr);
      frsize = KM_MHGETSIZE(mh);
      if (KM_MHISFREE(mh) && frsize >= frsize_req)
        break;
      if (ptr + frsize >= main_arena_ptr)
        break;
      ptr += frsize;
    }
    break;
  }

  if (frsize >= frsize_req) {

    size_t frsize_remain = frsize - frsize_req;
    // allocate flagment
    if (frsize_remain <= KM_MHSIZE) {

      // allocate whole flagment
      frsize_req = frsize;
      KM_PTRSETMH(ptr, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));

    } else {

      // split flagment
      KM_PTRSETMH(ptr, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));
      KM_PTRSETMH(ptr + frsize_req,
                  KM_MH(frsize_remain, KM_MHFL_FREE | KM_MHFL_MA));
    }

    g_main_arena_ptr = ptr + frsize_req;
    return KM_PTRGETDPTR(ptr);
  }

  // expand main arena
  void *main_arena_end_new =
      main_arena_start + (main_arena_end - main_arena_start) * 3 / 2;

  if (main_arena_end_new < ptr_lastfree + frsize_req)
    main_arena_end_new = ptr_lastfree + frsize_req;

  int ret = brk(main_arena_end_new);
  if (ret == -1)
    return NULL;

  main_arena_end = main_arena_end_new;
  g_main_arena_end = main_arena_end;

  // allocate new flagment
  frsize = main_arena_end - ptr_lastfree - KM_MHSIZE;
  if (frsize < frsize_req + KM_MHSIZE) {

    // allocate whole block
    frsize_req = frsize;
    KM_PTRSETMH(ptr_lastfree, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));
  } else {

    // SPLIT BLOCK
    KM_PTRSETMH(ptr_lastfree, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));
    KM_PTRSETMH(ptr_lastfree + frsize_req,
                KM_MH(frsize - frsize_req, KM_MHFL_FREE | KM_MHFL_MA));
  }

  return KM_PTRGETDPTR(ptr_lastfree);
}

static void *km_mamalloc(size_t dsize_req) {
  pthread_mutex_lock(&g_main_arena_lock);
  km_mainit(KM_MASIZE_INITIAL);
  void *ptr = km_mamalloc_internal(dsize_req);
  pthread_mutex_unlock(&g_main_arena_lock);
  return ptr;
}

static void km_mafree_internal(void *dptr) {
  void *main_arena_end = g_main_arena_end;
  void *ptr = KM_DPTRGETPTR(dptr);
  memhdr_t mh = KH_PTRGETMH(ptr);
  size_t frsize = KM_MHGETSIZE(mh);

  // check next flagment is free
  while (ptr + frsize + KM_MHSIZE < main_arena_end) {
    void *ptr_next = ptr + frsize;
    memhdr_t mh_next = KH_PTRGETMH(ptr_next);
    if (KM_MHISINUSE(mh_next))
      break;
    // merge next flagment
    frsize += KM_MHGETSIZE(mh_next);
  }

  // freeing last flagment
  if (main_arena_end == ptr + frsize) {
    // shrink main arena
    int ret = brk(ptr);
    if (ret != -1) {
      g_main_arena_end = ptr;
      g_main_arena_ptr = ptr;
      return;
    }
  }

  // free flagment
  KM_PTRSETMH(ptr, KM_MH(frsize, KM_MHFL_FREE | KM_MHFL_MA));
}

static void km_mafree(void *dptr) {
  pthread_mutex_lock(&g_main_arena_lock);
  km_mafree_internal(dptr);
  pthread_mutex_unlock(&g_main_arena_lock);
}

static void *km_mmmalloc(size_t dsize_req) {
  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);
  void *ptr = mmap(NULL, frsize_req, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == (void *)-1)
    return NULL;
  KM_PTRSETMH(ptr, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MMAP));
  return KM_PTRGETDPTR(ptr);
}

static void *km_malloc(size_t dsize_req) {
  if (dsize_req == 0)
    return NULL;

  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);
  if (frsize_req > KM_FRSIZEMAXMA)
    return km_mmmalloc(dsize_req);

  return km_mamalloc(dsize_req);
}

static void *km_marealloc_internal(void *dptr, size_t dsize_req) {
  void *main_arena_end = g_main_arena_end;
  void *ptr = KM_DPTRGETPTR(dptr);
  memhdr_t mh = KH_PTRGETMH(ptr);
  size_t frsize = KM_MHGETSIZE(mh);
  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);

  if (frsize >= frsize_req) {
    // SHRINK BLOCK
    if (frsize < frsize_req + KM_MHSIZE) {
      // ALLOCATE WHOLE BLOCK
      frsize_req = frsize;
      // NOP (DO NOTHING)
    } else {
      // SPLIT BLOCK
      KM_PTRSETMH(ptr, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));
      KM_PTRSETMH(ptr + frsize_req,
                  KM_MH(frsize - frsize_req, KM_MHFL_FREE | KM_MHFL_MA));
    }
    g_main_arena_ptr = ptr + frsize_req;
    return KM_PTRGETDPTR(ptr);
  }

  void *ptr_next = ptr + frsize;
  size_t frsize_free = frsize;
  while (1) {
    memhdr_t mh_next = KH_PTRGETMH(ptr_next);
    if (KM_MHISINUSE(mh_next) || ptr_next >= main_arena_end)
      break;
    size_t frsize_next = KM_MHGETSIZE(mh_next);
    frsize_free += frsize_next;
    if (ptr + frsize_free >= main_arena_end)
      break;
    ptr_next += frsize_next;
  }

  if (ptr + frsize >= main_arena_end) {
    // expand main arena
    void *main_arena_end_new = main_arena_end + frsize_req;
    int ret = brk(main_arena_end_new);
    if (ret == -1)
      return NULL;
    g_main_arena_end = main_arena_end_new;
    KM_PTRSETMH(ptr, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MA));
    g_main_arena_ptr = ptr + frsize_req;
    return KM_PTRGETDPTR(ptr);
  }

  if (frsize != frsize_free) {
    // MERGE BLOCK
    KM_PTRSETMH(ptr, KM_MH(frsize_free, KM_MHFL_FREE | KM_MHFL_MA));
  }

  if (frsize_free < frsize_req) {
    // ALLOCATE NEW BLOCK
    void *ptr_new = km_mamalloc_internal(dsize_req);
    if (ptr_new == NULL)
      return NULL;
    memcpy(ptr_new, dptr, frsize - KM_MHSIZE);
    km_mafree_internal(dptr);
    return ptr_new;
  }

  return KM_PTRGETDPTR(ptr);
}

static void *km_marealloc(void *dptr, size_t dsize_req) {
  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);
  if (frsize_req > KM_FRSIZEMAXMA) {
    void *dptr_new = km_mmmalloc(dsize_req);
    if (dptr_new == NULL)
      return NULL;
    memcpy(dptr_new, dptr, KM_MHGETSIZE(KH_PTRGETMH(KM_DPTRGETPTR(dptr))));
    km_mafree(dptr);
    return dptr_new;
  }

  pthread_mutex_lock(&g_main_arena_lock);
  km_mainit(KM_MASIZE_INITIAL);
  void *dptr_new = km_marealloc_internal(dptr, dsize_req);
  pthread_mutex_unlock(&g_main_arena_lock);
  return dptr_new;
}

static void km_mmfree(void *dptr) {
  void *ptr = KM_DPTRGETPTR(dptr);
  int ret = munmap(ptr, KM_MHGETSIZE(KH_PTRGETMH(ptr)));
  if (ret == -1)
    abort();
}

void *km_mmrealloc(void *dptr, size_t dsize_req) {
  size_t frsize_req = KM_SIZEALGIN(dsize_req + KM_MHSIZE);
  if (frsize_req <= KM_FRSIZEMAXMA) {
    void *dptr_new = km_mamalloc(dsize_req);
    if (dptr_new == NULL)
      return NULL;
    memcpy(dptr_new, dptr, dsize_req);
    km_mmfree(dptr);
    return dptr_new;
  }

  // REALLOC
  void *ptr = KM_DPTRGETPTR(dptr);
  memhdr_t mh = KH_PTRGETMH(ptr);
  size_t frsize = KM_MHGETSIZE(mh);

  // ALLOCATE BY MMAP
  void *ptr_new = mremap(ptr, frsize, frsize_req, MREMAP_MAYMOVE);
  if (ptr_new == (void *)-1)
    return NULL;
  KM_PTRSETMH(ptr_new, KM_MH(frsize_req, KM_MHFL_INUSE | KM_MHFL_MMAP));
  return KM_PTRGETDPTR(ptr_new);
}

static void km_free(void *dptr) {
  if (dptr == NULL)
    return;

  void *ptr = KM_DPTRGETPTR(dptr);
  memhdr_t mh = KH_PTRGETMH(ptr);
  if (KM_MHISMMAP(mh))
    km_mmfree(dptr);
  else
    km_mafree(dptr);
}

static void *km_realloc(void *dptr, size_t dsize_req) {
  if (dptr == NULL)
    return km_malloc(dsize_req);

  if (dsize_req == 0) {
    km_free(dptr);
    return NULL;
  }

  void *ptr = KM_DPTRGETPTR(dptr);
  memhdr_t mh = KH_PTRGETMH(ptr);
  if (KM_MHISMMAP(mh))
    return km_mmrealloc(dptr, dsize_req);
  else
    return km_marealloc(dptr, dsize_req);
}

void *km_alloc(void *ptr, size_t size) {

  // --------------------------------------------------
  // Initialize MAIN ALENA
  // --------------------------------------------------

  km_mainit(KM_MASIZE_INITIAL);

  // --------------------------------------------------
  // Memory allocation
  // --------------------------------------------------

  assert(g_main_arena_start != NULL);
  assert(g_main_arena_end != NULL);

  if (ptr == NULL) {

    // ALLOCATE
    pthread_mutex_lock(&g_main_arena_lock);
    void *ptr_new = km_malloc(size);
    pthread_mutex_unlock(&g_main_arena_lock);
    return ptr_new;
  } else if (size == 0) {

    // FREE
    km_free(ptr);
    return NULL;
  }

  return km_realloc(ptr, size);
}

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
