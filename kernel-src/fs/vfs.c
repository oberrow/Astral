#include <kernel/vfs.h>
#include <hashtable.h>
#include <string.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <arch/cpu.h>

#define PATHNAME_MAX 512
#define MAXLINKDEPTH 64

static hashtable_t fstable;
vnode_t *vfsroot;

static spinlock_t listlock;
static vfs_t *vfslist;

static cred_t kernelcred = {
	.gid = 0,
	.uid = 0
};

static cred_t *getcred() {
	if (_cpu()->thread == NULL || _cpu()->thread->proc == NULL)
		return &kernelcred;
	else
		return &_cpu()->thread->proc->cred;
}

void vfs_init() {
	__assert(hashtable_init(&fstable, 20) == 0);
	vfsroot = alloc(sizeof(vnode_t));
	__assert(vfsroot);
	SPINLOCK_INIT(listlock);
	vfsroot->type = V_TYPE_DIR;
	vfsroot->refcount = 1;
}

int vfs_register(vfsops_t *ops, char *name) {
	return hashtable_set(&fstable, ops, name, strlen(name), true);
}

int vfs_mount(vnode_t *backing, vnode_t *pathref, char *path, char *name, void *data) {
	vfsops_t *ops;
	int err = hashtable_get(&fstable, (void **)(&ops), name, strlen(name));
	if (err)
		return err;

	vnode_t *mounton;
	err = vfs_lookup(&mounton, pathref, path, NULL, 0);

	if (err)
		return err;

	if (mounton->type != V_TYPE_DIR) {
		VOP_RELEASE(mounton);
		return ENOTDIR;
	}

	vfs_t *vfs;

	err = ops->mount(&vfs, mounton, backing, data);

	if (err) {
		VOP_RELEASE(mounton);
		return err;
	}

	spinlock_acquire(&listlock);

	vfs->next = vfslist;
	vfslist = vfs;

	spinlock_release(&listlock);

	mounton->vfsmounted = vfs;
	vfs->nodecovered = mounton;

	return 0;
}

int vfs_open(vnode_t *ref, char *path, int flags, vnode_t **res) {
	int err = vfs_lookup(res, ref, path, NULL, 0);
	if (err)
		return err;

	VOP_LOCK(*res);
	err = VOP_OPEN(res, flags, getcred());
	VOP_UNLOCK(*res);
	if (err)
		VOP_RELEASE(*res);

	return err;
}

int vfs_create(vnode_t *ref, char *path, vattr_t *attr, int type, vnode_t **node) {
	vnode_t *parent;
	char *component = alloc(strlen(path) + 1);
	int err = vfs_lookup(&parent, ref, path, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	VOP_LOCK(parent);
	err = VOP_CREATE(parent, component, attr, type, node, getcred());
	VOP_UNLOCK(parent);
	VOP_RELEASE(parent);

	cleanup:
	free(component);
	return err;
}

int vfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *written, int flags) {
	VOP_LOCK(node);
	int err = VOP_WRITE(node, buffer, size, offset, flags, written, getcred());
	VOP_UNLOCK(node);
	return err;
}

int vfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *bytesread, int flags) {
	VOP_LOCK(node);
	int err = VOP_READ(node, buffer, size, offset, flags, bytesread, getcred());
	VOP_UNLOCK(node);
	return err;
}

// if type is V_TYPE_LINK, a symlink is made
// in that case, destref is ignored, destpath is the link value and attr points to the attributes of the symlink
// if type is V_TYPE_REGULAR, a hardlink is made.
// in that case, destref and destpath point to the the node that will be linked and attr is ignored
// in both cases, linkref and linkpath describe the location of where to create the new link
int vfs_link(vnode_t *destref, char *destpath, vnode_t *linkref, char *linkpath, int type, vattr_t *attr) {
	__assert(type == V_TYPE_LINK || type == V_TYPE_REGULAR);
	char *component = alloc(strlen(linkpath) + 1);
	vnode_t *parent = NULL;
	int err = vfs_lookup(&parent, linkref, linkpath, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	if (type == V_TYPE_REGULAR) {
		vnode_t *targetnode;
		err = vfs_lookup(&targetnode, destref, destpath, NULL, 0);
		if (err) {
			VOP_RELEASE(parent);
			goto cleanup;
		}
		VOP_LOCK(parent);
		err = VOP_LINK(targetnode, parent, component, getcred());
		VOP_UNLOCK(parent);
		VOP_RELEASE(targetnode);
	} else {
		VOP_LOCK(parent);
		err = VOP_SYMLINK(parent, component, attr, linkpath, getcred());
		VOP_UNLOCK(parent);
	}

	VOP_RELEASE(parent);

	cleanup:
	free(component);
	return err;
}

int vfs_unlink(vnode_t *ref, char *path) {
	char *component = alloc(strlen(path) + 1);
	vnode_t *parent = NULL;
	int err = vfs_lookup(&parent, ref, path, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	VOP_LOCK(parent);
	VOP_UNLINK(parent, component, getcred());
	VOP_UNLOCK(parent);
	VOP_RELEASE(parent);

	cleanup:
	free(component);
	return err;
}

// returns the highest node in a mount point
static int highestnodeinmp(vnode_t *node, vnode_t **ret) {
	int e = 0;
	while (node->vfsmounted && e == 0)
		e = VFS_ROOT(node->vfsmounted, &node);

	*ret = node;

	return e;
}

// returns the lowest node in a mount point
static vnode_t *lowestnodeinmp(vnode_t *node) {
	while (node->flags & V_FLAGS_ROOT)
		node = node->vfs->nodecovered;

	return node;
}

// looks up a pathname
// if flags & VFS_LOOKUP_PARENT, the parent of the resulting node will be put in
// the result pointer and lastcomp will contain the last component name
// if flags & VFS_LOOKUP_NOLINK, the last component will not be dereferenced if its a symbolic link
// if flags & VFS_LOOKUP_INTERNAL, the first byte of flags will be the current count of symlinks transversed
// this is not an external flag and is meant to be called from inside the function itself
int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp, int flags) {
	if ((flags & VFS_LOOKUP_INTERNAL) && (flags & 0xff) > MAXLINKDEPTH)
		return ELOOP;

	size_t pathlen = strlen(path);

	if (pathlen == 0) {
		__assert((flags & VFS_LOOKUP_PARENT) == 0);
		vnode_t *r = start; 
		int e = highestnodeinmp(start, &r);
		if (e)
			return e;
		VOP_HOLD(r);
		*result = r;
		return 0;
	}

	if (pathlen > PATHNAME_MAX)
		return ENAMETOOLONG;

	vnode_t *current = start;
	int error = highestnodeinmp(start, &current);
	if (error)
		return error;

	char *compbuffer = alloc(pathlen + 1);
	if (compbuffer == NULL)
		return ENOMEM;

	strcpy(compbuffer, path);

	for (int i = 0; i < pathlen; ++i) {
		if (compbuffer[i] == '/')
			compbuffer[i] = '\0';
	}

	vnode_t *next;
	error = 0;
	VOP_HOLD(current);

	for (int i = 0; i < pathlen; ++i) {
		if (compbuffer[i] == '\0')
			continue;

		if (current->type != V_TYPE_DIR) {
			error = ENOTDIR;
			break;
		}

		char *component = &compbuffer[i];
		size_t complen = strlen(component);
		bool islast = i + complen == pathlen;

		if (islast && (flags & VFS_LOOKUP_PARENT)) {
			strcpy(lastcomp, component);
			break;
		}

		if (strcmp(component, "..") == 0) {
			// if the tree root, skip to next component
			vnode_t *root = NULL;
			__assert(highestnodeinmp(vfsroot, &root) == 0);
			if (root == current) {
				i += complen;
				continue;
			}
			// if the root of a mounted fs, go to
			if (current->flags & V_FLAGS_ROOT) {
				vnode_t *n = lowestnodeinmp(current);
				if (n != current) {
					VOP_HOLD(n);
					VOP_RELEASE(current);
					current = n;
				}
			}
		}

		VOP_LOCK(current);
		error = VOP_LOOKUP(current, component, &next, getcred());
		VOP_UNLOCK(current);
		if (error)
			break;

		vnode_t *r = next;
		error = highestnodeinmp(next, &r);
		if (error) {
			VOP_RELEASE(next);
			break;
		}

		if (r != next) {
			VOP_HOLD(r);
			VOP_RELEASE(next);
			next = r;
		}

		// dereference symlink
		if (next->type == V_TYPE_LINK && (islast == false || (islast == true && (flags & VFS_LOOKUP_NOLINK) == 0))) {
			// get path
			char *linkderef;
			VOP_LOCK(next);
			error = VOP_READLINK(next, &linkderef, getcred());
			VOP_UNLOCK(next);
			if (error) {
				VOP_RELEASE(next);
				break;
			}

			// lookup new path with recursion
			int pass = flags & VFS_LOOKUP_INTERNAL ? flags + 1 : VFS_LOOKUP_INTERNAL;
			vnode_t *derefresult = NULL;

			// get the start node of the dereference
			vnode_t *derefstart = next;

			if (*linkderef == '/' && _cpu()->thread && _cpu()->thread->proc) {
				spinlock_acquire(&_cpu()->thread->proc->lock);
				derefstart = _cpu()->thread->proc->root;
				VOP_HOLD(derefstart);
				spinlock_release(&_cpu()->thread->proc->lock);
			} else if (*linkderef == '/') {
				derefstart = vfsroot;
				VOP_HOLD(derefstart);
			}

			error = vfs_lookup(&derefresult, derefstart, linkderef, NULL, pass);

			if (*linkderef == '/')
				VOP_RELEASE(derefstart);

			free(linkderef);

			VOP_RELEASE(next);
			if (error)
				break;

			next = derefresult;
		}

		VOP_RELEASE(current);
		current = next;
		i += complen;
	}

	if (error) {
		VOP_RELEASE(current);
	} else {
		*result = current;
	}

	free(compbuffer);
	return error;
}
