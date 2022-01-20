#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <syscall.h>
#include <assert.h>

#include "refun.h"

/* Write NLONG 4 byte words from BUF into PID starting
   at address POS.  Calling process must be attached to PID. */
static int
write_mem(pid_t pid, unsigned long *buf, int nlong, unsigned long pos)
{
	unsigned long *p;
	int i;

	for (p = buf, i = 0; i < nlong; p++, i++)
		if (0 > ptrace(PTRACE_POKEDATA, pid, pos+(i*4), *p))
			return -1;
	return 0;
}

struct mmapargs {
	void *start;
	size_t length;
	int prot;
	int flags;
	int fd;
	off_t offset;
};

static char code[] = {
	0xcd, 0x80,      /* int 0x80 */
	0xcc,            /* trap */
	0x00,            /* pad */
};

static void
irpc(int pid, struct user_regs_struct *regs)
{
	int status, rv;

	ptrace(PTRACE_SETREGS, pid, 0, regs);
	rv = ptrace(PTRACE_CONT, pid, 0, 0);
	if (0 > rv) {
		perror("PTRACE_CONT");
		exit(1);
	}
	while (1) {
		if (0 > waitpid(pid, &status, 0)) {
			perror("waitpid");
			exit(1);
		}
		if (WIFSTOPPED(status)) {
			if (WSTOPSIG(status) == SIGTRAP)
				break;
			else if (WSTOPSIG(status) == SIGSEGV
				 || WSTOPSIG(status) == SIGBUS
				 || WSTOPSIG(status) == SIGILL) {
				fprintf(stderr, "got signal %d\n",
					WSTOPSIG(status));
				if (0 > ptrace(PTRACE_CONT, pid, 0,
					       WSTOPSIG(status))) {
					perror("PTRACE_CONT");
					exit(1);
				}
			} else if (0 > ptrace(PTRACE_CONT, pid, 0, 0)) {
				/* FIXME: Remember these signals */
				perror("PTRACE_CONT");
				exit(1);
			}

		} else {
			fprintf(stderr, "status is %x\n", status);
			assert(0);
		}
	}
	ptrace(PTRACE_GETREGS, pid, 0, regs);
}

static int
immap(int pid, struct user_regs_struct *regs)
{
	struct mmapargs args;
	unsigned long esp;

	args.start = 0;
	args.length = PAGE_SIZE;
	args.prot = PROT_READ|PROT_WRITE|PROT_EXEC;
	args.flags = MAP_PRIVATE|MAP_ANONYMOUS;
	args.fd = 0;
	args.offset = 0;

	esp = regs->esp;
	esp -= sizeof(args);
	assert(!(esp%4));
	if (0 > write_mem(pid, (unsigned long*)&args, sizeof(args)>>2, esp)) {
		fprintf(stderr, "failed to write to process %d\n", pid);
		return -1;
	}
	regs->ebx = esp;
	esp -= sizeof(code);
	assert(!(esp%4));
	if (0 > write_mem(pid, (unsigned long*)code, sizeof(code)>>2, esp)) {
		fprintf(stderr, "failed to write to process %d\n", pid);
		return -1;
	}
	regs->esp = esp;
	regs->eip = esp;
	regs->eax = SYS_mmap;
	irpc(pid, regs);
	return 0;
}

static int
xpad(int len)
{
	return len%4 ? 4-len%4 : 0;
}

/* code.c */
void loaddl(char *ldso, char *dummy, unsigned long entry);
void trap();
void marker();

/* stub.c */
extern char sinjstub[];
extern unsigned long sinjstublen;

static char *
mkstub()
{
	int fd, rv;
	char tmp[] = "/tmp/sinjstubXXXXXX";

	fd = mkstemp(tmp);
	if (0 > fd) {
		perror("mkstemp");
		return NULL;
	}
	if (0 > fchmod(fd, S_IRUSR|S_IWUSR|S_IXUSR)) {
		perror("fchmod");
		return NULL;
	}
	rv = write(fd, sinjstub, sinjstublen);
	if (0 > rv) {
		perror("write");
		return NULL;
	}
	close(fd);
	return strdup(tmp);
}


static int
ildso(int pid, unsigned long addr, struct user_regs_struct *regs)
{
	static char *ldso = "/lib/ld-linux.so.2";
	static char *dummy;
	unsigned long start, arg1, arg2, arg3;
	int len;
	unsigned long args[4];

	dummy = mkstub();
	if (!dummy) {
		fprintf(stderr, "cannot make sinj stub\n");
		return -1;
	}

	len = strlen(ldso)+1;
	len += xpad(len);
	arg1 = addr;
	if (0 > write_mem(pid, (unsigned long*)ldso, len>>2, arg1))
		return -1;
	arg2 = arg1 + len;
	len = strlen(dummy)+1;
	len += xpad(len);
	if (0 > write_mem(pid, (unsigned long*)dummy, len>>2, arg2))
		return -1;
	start = arg2 + len;
	len = (unsigned long)&marker - (unsigned long)&loaddl;
	len += xpad(len);
	if (0 > write_mem(pid, (unsigned long*)&loaddl, len>>2, start))
		return -1;
	len = (unsigned long)&trap - (unsigned long)&loaddl;
	arg3 = start + len;
	
	args[0] = 0; /* return address - not used */
	args[1] = arg1;
	args[2] = arg2;
	args[3] = arg3;

	regs->esp = regs->esp - sizeof(args);
	if (0 > write_mem(pid, (unsigned long*)args, 
			  sizeof(args)>>2, regs->esp))
		return -1;
	regs->eip = start;

	irpc(pid, regs);
	unlink(dummy);
	return 0;
}

int
sinj(int pid)
{
	struct user_regs_struct regs, oregs;
	int rv;

	/* Attach */
	if (0 > ptrace(PTRACE_ATTACH, pid, 0, 0)) {
		fprintf(stderr, "cannot attach to %d\n", pid);
		return -1;
	}
	waitpid(pid, NULL, 0);
	ptrace(PTRACE_GETREGS, pid, 0, &oregs);
	memcpy(&regs, &oregs, sizeof(regs));
	
	if (0 > immap(pid, &regs)) {
		fprintf(stderr, "cannot do immap\n");
		return -1;
	}

	/* The libc mmap stub makes this comparison
	   after the system call returns */
	if (regs.eax > 0xfffff000) {
		fprintf(stderr, "failed to mmap\n");
		return -1;
	}

	if (0 > ildso(pid, regs.eax, &regs)) {
		fprintf(stderr, "cannot do immap\n");
		return -1;
	}

	if (0 > ptrace(PTRACE_SETREGS, pid, 0, &oregs)) {
		perror("PTRACE_SETREGS");
		exit(1);
	}
	rv = ptrace(PTRACE_DETACH, pid, 0, 0);
	if (0 > rv) {
		perror("PTRACE_DETACH");
		exit(1);
	}
	return 0;
}
