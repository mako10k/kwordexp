#pragma once

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../include/kmalloc.h"

void *kmalloc(size_t size) __attribute__((warn_unused_result));
void *kmalloc_atomic(size_t size) __attribute__((warn_unused_result));
void *krealloc(void *ptr, size_t size)
    __attribute__((warn_unused_result));
void kfree(void *ptr) __attribute__((nonnull(1)));
char *kstrdup(const char *s) __attribute__((warn_unused_result));

void *ksmalloc(size_t size) __attribute__((warn_unused_result));
void *ksmalloc_atomic(size_t size) __attribute__((warn_unused_result));
void *ksrealloc(void *ptr, size_t size)
    __attribute__((warn_unused_result));
void ksfree(void *ptr) __attribute__((nonnull(1)));
char *ksstrdup(const char *s) __attribute__((warn_unused_result));