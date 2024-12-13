#ifndef __KMALLOC_H__
#define __KMALLOC_H__

#include <stddef.h>

void *kmalloc(size_t size);
void *kmalloc_atomic(size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
char *kstrdup(const char *s);

void *ksmalloc(size_t size);
void *ksmalloc_atomic(size_t size);
void *ksrealloc(void *ptr, size_t size);
void ksfree(void *ptr);
char *ksstrdup(const char *s);

#endif