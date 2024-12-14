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

#define INITIAL_POOL_SIZE 1024 * 1024
#define MMAP_THRESHOLD 1024 * 1024
#define FLAG_INUSE 2
#define SIZE_ALIGN 16
#define MEMHEADER_SIZE sizeof(size_t)
#define SIZE_MASKED(s) ((s) & ~(SIZE_ALIGN - 1))
#define SIZE_ALIGNED(s) SIZE_MASKED((s) + SIZE_ALIGN - 1)

// Memory allocation functions

void *kmalloc(size_t size) { return GC_malloc(size); }
void *kmalloc_atomic(size_t size) { return GC_malloc_atomic(size); }
void *krealloc(void *ptr, size_t size) { return GC_realloc(ptr, size); }
void kfree(void *ptr) { (void)ptr; }
char *kstrdup(const char *s) { return GC_strdup(s); }

#ifdef REPLACE_SYSTEM_ALLOC
// REPLACE SYSTEM ALLOC
void *malloc(size_t size) { return kmalloc(size); }
void *realloc(void *ptr, size_t size) { return krealloc(ptr, size); }
void free(void *ptr) { (void)ptr; }
void *calloc(size_t nmemb, size_t size) { return kmalloc(nmemb * size); }
#else
// USE SYSTEM ALLOC
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
#endif

// Memory allocation functions

void *ksmalloc(size_t size) { return malloc(size); }
void *ksmalloc_atomic(size_t size) { return malloc(size); }
void *ksrealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void ksfree(void *ptr) { free(ptr); }
char *ksstrdup(const char *s) { return strdup(s); }

static void *g_main_arena = NULL;
static void *g_main_arena_end = NULL;
static pthread_mutex_t g_main_arena_lock = PTHREAD_MUTEX_INITIALIZER;

static void kalloc_internal_init_main_arena(size_t size) {
  if (g_main_arena == NULL) {
    g_main_arena = sbrk(0);
    if (g_main_arena == (void *)-1) {
      abort();
    }
    g_main_arena_end = g_main_arena + size;
    int ret = brk(g_main_arena_end);
    if (ret == -1) {
      abort();
    }
  }
}

static void *kmalloc_internal_by_mmap(size_t size) {
  size_t size_to_alloc = SIZE_ALIGNED(size + MEMHEADER_SIZE);
  void *memhdr = mmap(NULL, size_to_alloc, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memhdr == (void *)-1) {
    return NULL;
  }
  size_t memhdr_data = size_to_alloc;
  *(size_t *)memhdr = memhdr_data;
  void *ptr_new = memhdr + MEMHEADER_SIZE;
  return ptr_new;
}

static void kfree_internal_by_mmap(void *ptr) {
  void *memhdr = ptr - MEMHEADER_SIZE;
  size_t memhdr_data = *(size_t *)memhdr;
  size_t alloc_size = SIZE_MASKED(memhdr_data);
  munmap(memhdr, alloc_size);
}

static void *kmalloc_internal_from_main_arena(size_t size) {
  // - - - - - - - - - - - - -
  // ALLOCATE FROM MAIN ALENA
  // - - - - - - - - - - - - -
  void *p = g_main_arena;
  void *p_prev = NULL;
  size_t size_of_blk_prev = 0;
  size_t size_to_alloc = SIZE_ALIGNED(size + MEMHEADER_SIZE);

  while (p + MEMHEADER_SIZE < g_main_arena_end) {

    size_t memhdr_data = *(size_t *)p;
    size_t size_of_blk = SIZE_MASKED(memhdr_data);

    if (memhdr_data & FLAG_INUSE) {

      // READ NEXT BLOCK WHEN INUSE
      p += size_of_blk;
      p_prev = NULL;
      continue;
    }

    if (p_prev != NULL) {

      // MERGE WITH PREVIOUS BLOCK
      *(size_t *)p_prev = size_of_blk_prev + size_of_blk;
      p = p_prev;
      p_prev = NULL;
      continue;
    }

    if (size_of_blk >= size_to_alloc) {

      // ALLOCATE EXISTING BLOCK
      if (size_of_blk < size_to_alloc + MEMHEADER_SIZE) {

        // ALLOCATE WHOLE BLOCK
        size_to_alloc = size_of_blk;
        *(size_t *)p = size_to_alloc | FLAG_INUSE;

      } else {

        // SPLIT BLOCK
        *(size_t *)p = size_to_alloc | FLAG_INUSE;
        *(size_t *)(p + size_to_alloc) = size_of_blk - size_to_alloc;
      }

      void *ptr_new = p + MEMHEADER_SIZE;
      return ptr_new;
    }

    // MOVE TO NEXT BLOCK
    p_prev = p;
    size_of_blk_prev = size_of_blk;
    p = p + size_of_blk;
  }

  // ALLOCATE NEW BLOCK

  if (p_prev == NULL) {
    p_prev = g_main_arena_end;
    size_of_blk_prev = 0;
  }

  // EXPAND MAIN ALENA 1.5x
  void *main_arena_end_new =
      g_main_arena + (g_main_arena_end - g_main_arena) * 3 / 2;

  if (main_arena_end_new < p_prev + size_to_alloc) {

    // EXPAND MAIN ALENA TO FIT NEW BLOCK
    main_arena_end_new = p_prev + size_to_alloc;
  }

  int ret = brk(main_arena_end_new);
  if (ret == -1) {
    return NULL;
  }

  // NEW ALLOCATION
  size_t size_of_blk = main_arena_end_new - p_prev - MEMHEADER_SIZE;
  if (size_of_blk < size_to_alloc + MEMHEADER_SIZE) {

    // ALLOCATE WHOLE BLOCK
    size_to_alloc = size_of_blk;
    size_t memhdr_data = size_to_alloc | FLAG_INUSE;
    *(size_t *)p_prev = memhdr_data;
  } else {

    // SPLIT BLOCK
    size_t memhdr_data = size_to_alloc | FLAG_INUSE;
    *(size_t *)p_prev = memhdr_data;

    size_t memhdr_data_next = size_of_blk - size_to_alloc;
    *(size_t *)(p_prev + size_to_alloc) = memhdr_data_next;
  }
  void *ptr_new = p_prev + MEMHEADER_SIZE;
  return ptr_new;
}

static void *kmalloc_internal(size_t size) {

  // - - - - - - - - - - - - -
  // ALLOCATE BY MMAP
  // - - - - - - - - - - - - -
  if (size >= MMAP_THRESHOLD) {
    void *ptr_new = kmalloc_internal_by_mmap(size);
    return ptr_new;
  }

  return kmalloc_internal_from_main_arena(size);
}

void *krealloc_internal_by_mmap(void *ptr, size_t size) {
  // REALLOC
  void *memhdr = ptr - MEMHEADER_SIZE;
  size_t memhdr_data = *(size_t *)memhdr;
  size_t size_to_alloc = SIZE_ALIGNED(size + MEMHEADER_SIZE);

  size_t alloc_size = SIZE_MASKED(memhdr_data);
  if (size >= MMAP_THRESHOLD) {
    // ALLOCATE BY MMAP
    void *memhdr_new =
        mremap(memhdr, alloc_size, size_to_alloc, MREMAP_MAYMOVE);
    if (memhdr_new == (void *)-1) {
      return NULL;
    }
    size_t memhdr_data_new = size_to_alloc;
    *(size_t *)memhdr_new = memhdr_data_new;
    void *ptr_new = memhdr_new + MEMHEADER_SIZE;
    return ptr_new;
  }

  // ALLOCATE
  void *ptr_new = kmalloc_internal(size);
  if (ptr_new == NULL) {
    return NULL;
  }
  memcpy(ptr_new, ptr, size);
  munmap(memhdr, alloc_size);
  return ptr_new;
}


static void kfree_internal_from_main_arena(void *ptr) {
  void *memhdr = ptr - MEMHEADER_SIZE;
  size_t memhdr_data = *(size_t *)memhdr;

  size_t size_to_free = SIZE_MASKED(memhdr_data);
  void *main_arena_end = g_main_arena_end;
  if (ptr + size_to_free < main_arena_end) {
    void *memhdr_next = ptr + size_to_free;
    size_t memhdr_next_data = *(size_t *)memhdr_next;
    if (!(memhdr_next_data & FLAG_INUSE)) {
      // MERGE NEXT BLOCK
      size_t size_to_merge = memhdr_next_data & ~(SIZE_ALIGN - 1);
      size_t size_to_merge_total = size_to_free + size_to_merge;
      *(size_t *)memhdr = size_to_merge_total;
      size_to_free = size_to_merge_total;
    }
  }
  if (main_arena_end == ptr + size_to_free) {
    // SHRINK MAIN ALENA
    int ret = brk(ptr);
    if (ret != -1) {
      g_main_arena_end = ptr;
      return;
    }
  }
  *(size_t *)memhdr = size_to_free;
}

static void kfree_internal_by_mmap(void *ptr) {
  void *memhdr = ptr - MEMHEADER_SIZE;
  size_t memhdr_data = *(size_t *)memhdr;
  size_t alloc_size = SIZE_MASKED(memhdr_data);
  munmap(memhdr, alloc_size);
}

static void *krealloc_internal_from_main_arena(void *ptr, size_t size) {
  if (size == 0) {
    kfree_internal_from_main_arena(ptr);
    return NULL;
  }
  if (ptr < g_main_arena || g_main_arena_end <= ptr) {

    // REALLOC BY MMAP
    void *memhdr = ptr - MEMHEADER_SIZE;
    size_t memhdr_data = *(size_t *)memhdr;
    size_t size_to_alloc = SIZE_ALIGNED(size + MEMHEADER_SIZE);

    size_t alloc_size = SIZE_MASKED(memhdr_data);
    if (size >= MMAP_THRESHOLD) {
      // ALLOCATE BY MMAP
      void *memhdr_new =
          mremap(memhdr, alloc_size, size_to_alloc, MREMAP_MAYMOVE);
      if (memhdr_new == (void *)-1) {
        return NULL;
      }
      size_t memhdr_data_new = size_to_alloc;
      *(size_t *)memhdr_new = memhdr_data_new;
      void *ptr_new = memhdr_new + MEMHEADER_SIZE;
      return ptr_new;
    }
  }

  void *p = ptr;

  while (1) {
    void *memhdr = p - MEMHEADER_SIZE;
    size_t memhdr_data = *(size_t *)memhdr;
    size_t size_to_alloc = SIZE_ALIGNED(size + MEMHEADER_SIZE);

    size_t size_of_blk = SIZE_MASKED(memhdr_data);
    if (size_of_blk >= size_to_alloc) {
      // ALLOCATE EXISTING BLOCK
      if (size_of_blk < size_to_alloc + MEMHEADER_SIZE) {
        // ALLOCATE WHOLE BLOCK
        size_to_alloc = size_of_blk;
        *(size_t *)memhdr = size_to_alloc | FLAG_INUSE;
      } else {
        // SPLIT BLOCK
        *(size_t *)memhdr = size_to_alloc | FLAG_INUSE;
        *(size_t *)(memhdr + size_to_alloc) = size_of_blk - size_to_alloc;
      }
      void *ptr_new = memhdr + MEMHEADER_SIZE;
      return ptr_new;
    }

    void *p = ptr + size_of_blk;
    if (p < g_main_arena_end) {
      size_t memhdr_data_next = *(size_t *)p;
      size_t size_of_blk_next = memhdr_data_next & ~(SIZE_ALIGN - 1);

      if (!(memhdr_data_next & FLAG_INUSE)) {
        // MERGE NEXT BLOCK
        size_t size_to_merge = size_of_blk_next;
        size_t size_to_merge_total = size_of_blk + size_to_merge;
        *(size_t *)memhdr = size_to_merge_total;
        size_of_blk = size_to_merge_total;
        continue;
      }
    }

    // ALLOCATE NEW BLOCK
    void *ptr_new = kmalloc_internal(size);
    if (ptr_new == NULL) {
      return NULL;
    }
    memcpy(ptr_new, ptr, size_of_blk);
    kfree_internal_from_main_arena(ptr);
    return ptr_new;
  }
}

void *kalloc_internal(void *ptr, size_t size) {

  // --------------------------------------------------
  // Initialize MAIN ALENA
  // --------------------------------------------------

  pthread_mutex_lock(&g_main_arena_lock);
  kalloc_internal_init_main_arena(INITIAL_POOL_SIZE);
  pthread_mutex_unlock(&g_main_arena_lock);

  // --------------------------------------------------
  // Memory allocation
  // --------------------------------------------------

  assert(g_main_arena != NULL);
  assert(g_main_arena_end != NULL);

  if (ptr == NULL) {

    // ALLOCATE
    pthread_mutex_lock(&g_main_arena_lock);
    void *ptr_new = kmalloc_internal(size);
    pthread_mutex_unlock(&g_main_arena_lock);
    return ptr_new;
  }

  pthread_mutex_lock(&g_main_arena_lock);
  void *main_arena_end = g_main_arena_end;
  pthread_mutex_unlock(&g_main_arena_lock);

  if (g_main_arena <= ptr && ptr < main_arena_end) {
    return krealloc_internal_from_main_arena(ptr, size);
  }

  return krealloc_internal_by_mmap(ptr, size);
}
