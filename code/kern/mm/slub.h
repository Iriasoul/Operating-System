#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/*
 * Simplified SLUB-style object allocator built on the buddy page allocator.
 *
 * kmalloc(size):
 *   size == 0         : returns NULL, no page is touched;
 *   1 <= size <= 2048 : one slot of the smallest size class that fits, taken
 *                       from a slab that is exactly one buddy page;
 *   size > 2048       : one contiguous page block from alloc_pages(), with a
 *                       private header in its first page.
 *
 * kfree(ptr) accepts only NULL or an original kmalloc() return value; the
 * caller never passes a size, the allocator recovers it. Returned memory is
 * not zeroed and only the requested size bytes are usable.
 *
 * slub_init must be called once, after the real physical memory has been
 * registered and checked; slub_check walks the allocator's structures
 * without changing any ownership.
 */
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_check(void);

#endif /* !__KERN_MM_SLUB_H__ */
