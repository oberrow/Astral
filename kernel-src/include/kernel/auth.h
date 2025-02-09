#ifndef _AUTH_H
#define _AUTH_H

#include <kernel/cred.h>
#include <kernel/vfs.h>
#include <kernel/scheduler.h>
#include <kernel/sock.h>

#define AUTH_SCOPE_COUNT 5
#define AUTH_SCOPE_SYSTEM 0
#define AUTH_SCOPE_FILESYSTEM 1
#define AUTH_SCOPE_CRED 2
#define AUTH_SCOPE_PROCESS 3
#define AUTH_SCOPE_NETWORK 4

#define AUTH_LISTENER_LIMIT 8

#define AUTH_DECISION_ALLOW 1
#define AUTH_DECISION_DEFER 0
#define AUTH_DECISION_DENY -1000

#define AUTH_ACTIONS_SYSTEM_CHROOT	1
#define AUTH_ACTIONS_SYSTEM_SETHOSTNAME 2

#define AUTH_ACTIONS_FILESYSTEM_EXECUTE 1
#define AUTH_ACTIONS_FILESYSTEM_WRITE 	2
#define AUTH_ACTIONS_FILESYSTEM_READ 	4
#define AUTH_ACTIONS_FILESYSTEM_SETATTR 8
#define AUTH_ACTIONS_FILESYSTEM_MOUNT 	16
#define AUTH_ACTIONS_FILESYSTEM_UNLINK	32
#define AUTH_ACTIONS_FILESYSTEM_RENAME	64

#define AUTH_ACTIONS_CRED_SETRESUID	1
#define AUTH_ACTIONS_CRED_SETRESGID	2
#define AUTH_ACTIONS_CRED_SETUID	4
#define AUTH_ACTIONS_CRED_SETGID	8
#define AUTH_ACTIONS_CRED_SETEUID	16
#define AUTH_ACTIONS_CRED_SETEGID	32

#define AUTH_ACTIONS_PROCESS_SIGNAL	1

#define AUTH_ACTIONS_NETWORK_CONFIGURE		1
#define AUTH_ACTIONS_NETWORK_BINDRESERVED	2

typedef int (*authlistener_t)(cred_t *cred, int actions, void *arg0, void *arg1, void *arg2);

typedef struct {
	authlistener_t listeners[AUTH_LISTENER_LIMIT];
} authscope_t;

void auth_registerlistener(int scope, authlistener_t listener);
int auth_check(int scope, cred_t *cred, int actions, void *arg0, void *arg1, void *arg2);

static inline int auth_filesystem_check(cred_t *cred, int actions, vnode_t *vnode, vnode_t *dirvnode) {
	return auth_check(AUTH_SCOPE_FILESYSTEM, cred, actions, vnode, dirvnode, NULL);
}

static inline int auth_system_check(cred_t *cred, int actions) {
	return auth_check(AUTH_SCOPE_SYSTEM, cred, actions, NULL, NULL, NULL);
}

static inline int auth_cred_check(cred_t *cred, int actions, int id, int eid, int sid) {
	return auth_check(AUTH_SCOPE_CRED, cred, actions, &id, &eid, &sid);
}

static inline int auth_process_check(cred_t *cred, int actions, proc_t *proc) {
	return auth_check(AUTH_SCOPE_PROCESS, cred, actions, proc, NULL, NULL);
}

static inline int auth_network_check(cred_t *cred, int actions, socket_t *socket, netdev_t *netdev) {
	return auth_check(AUTH_SCOPE_NETWORK, cred, actions, socket, netdev, NULL);
}

static inline int auth_filesystem_convertaccess(int access) {
	int r = 0;

	if (access & V_ACCESS_EXECUTE)
		r |= AUTH_ACTIONS_FILESYSTEM_EXECUTE;
	if (access & V_ACCESS_WRITE)
		r |= AUTH_ACTIONS_FILESYSTEM_WRITE;
	if (access & V_ACCESS_READ)
		r |= AUTH_ACTIONS_FILESYSTEM_READ;

	return r;
}

#endif
