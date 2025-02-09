#ifndef _VMMCACHE_H
#define _VMMCACHE_H

#include <kernel/pmm.h>
#include <kernel/vfs.h>

extern size_t vmmcache_cachedpages;

void vmmcache_init();
int vmmcache_getpage(vnode_t *vnode, uintmax_t offset, page_t **res);
int vmmcache_takepage(page_t *page);
int vmmcache_makedirty(page_t *page);
int vmmcache_truncate(vnode_t *vnode, uintmax_t offset);
int vmmcache_syncvnode(vnode_t *vnode, uintmax_t startoffset, size_t size);
int vmmcache_pushpage(vnode_t *vnode, uintmax_t offset, page_t *page);
int vmmcache_sync();
int vmmcache_evict(page_t *page);

#endif
