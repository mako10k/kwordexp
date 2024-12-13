#include "kmalloc_internal.h"
#include <gc.h>
#include <string.h>

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