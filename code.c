#include <sys/types.h>
#include <time.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <syscall.h>

#define PAGE_SIZE 4096  /* FIXME: Find this constant */

#if ELF_EXEC_PAGESIZE > PAGE_SIZE
# define ELF_MIN_ALIGN	ELF_EXEC_PAGESIZE
#else
# define ELF_MIN_ALIGN	PAGE_SIZE
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_MIN_ALIGN-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_MIN_ALIGN-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_MIN_ALIGN - 1) & ~(ELF_MIN_ALIGN - 1))

struct xmyelf {
	Elf32_Ehdr ehdr;
	Elf32_Phdr phdr[100];
	unsigned long loadaddr;
};

struct atts {
	unsigned long id, val;
};

struct params {
	unsigned long argc;
	char *argv[2];
	char *envv[1];
	struct atts atts[14];
	char arg[5]; /* "argv" */
};
static void xmemset(char *p, int s, int c);
static void foo(void *p, int len, void *a);
static void load_elf(char *filename, struct xmyelf *me);
static int xsyscall(int num, ...);
void trap();

void
loaddl(char *ldso, char *dummy, unsigned long entry)
{
	struct params par;
	struct xmyelf interp;
	struct xmyelf exe;

	load_elf(ldso, &interp);
	load_elf(dummy, &exe);

	par.argc = 1;
	par.argv[0] = &par.arg[0];
	par.argv[1] = NULL;
	par.envv[0] = NULL;
	par.atts[0].id = AT_HWCAP;
	par.atts[0].val = 0;
	par.atts[1].id = AT_PAGESZ;
	par.atts[1].val = PAGE_SIZE;
	par.atts[2].id = AT_CLKTCK;
	par.atts[2].val = CLOCKS_PER_SEC;
	par.atts[3].id = AT_PHDR;
	par.atts[3].val = exe.loadaddr + exe.ehdr.e_phoff;
	par.atts[4].id = AT_PHENT;
	par.atts[4].val = 0;
	par.atts[5].id = AT_PHNUM;
	par.atts[5].val = exe.ehdr.e_phnum;
	par.atts[6].id = AT_BASE;
	par.atts[6].val = interp.loadaddr;
	par.atts[7].id = AT_FLAGS;
	par.atts[7].val = 0;
	par.atts[8].id = AT_ENTRY;
	par.atts[8].val = entry;
	par.atts[9].id = AT_UID;
	par.atts[9].val = xsyscall(SYS_getuid);
	par.atts[10].id = AT_EUID;
	par.atts[10].val = xsyscall(SYS_geteuid);
	par.atts[11].id = AT_GID;
	par.atts[11].val = xsyscall(SYS_getgid);
	par.atts[12].id = AT_EGID;
	par.atts[12].val = xsyscall(SYS_getegid);
	par.atts[13].id = AT_NULL;
	par.atts[13].val = 0;
	par.arg[0] = 'a'; par.arg[1] = 'r'; par.arg[2] = 'g';
	par.arg[3] = 'v'; par.arg[4] = '\0';

	foo(&par, sizeof(par),
	    (void*)(interp.loadaddr+interp.ehdr.e_entry));	
}

void
trap()
{
	asm("int3" : :);
}

static int
xsyscall(int num, ...)
{
	/* assume we get a stack frame from the
	   C function prologue */
	asm("push   %%edi\n\t"
	    "push   %%esi\n\t"
	    "push   %%ebx\n\t"
	    "mov    0x1c(%%ebp,1),%%edi\n\t"
	    "mov    0x18(%%ebp,1),%%esi\n\t"
	    "mov    0x14(%%ebp,1),%%edx\n\t"
	    "mov    0x10(%%ebp,1),%%ecx\n\t"
	    "mov    0x0c(%%ebp,1),%%ebx\n\t"
	    "mov    0x08(%%ebp,1),%%eax\n\t"
	    "int    $0x80\n\t"
	    "pop    %%ebx\n\t"
	    "pop    %%esi\n\t"
	    "pop    %%edi\n\t"
	    "cmp    $0xfffff001,%%eax\n\t"
	    "jb     1f\n\t"
	    "mov    $0xffffffff,%%eax\n\t"
	    "1: pop    %%ebp\n\t"
	    "ret\n\t"
	    : : );
	return -1;
}

static void
xmemset(char *p, int s, int c)
{
	int i;
	for (i = 0; i < c; i++)
		p[i] = s;
}

static void
foo(void *p, int len, void *a)
{
	asm("sub %0,%%esp" : : "m" (len));
	asm("cld");
	asm("mov %0,%%ecx" : : "m" (len));
	asm("mov %%esp,%%edi" : : );
	asm("mov %0,%%esi" : : "m" (p));
	asm("repz movsb %%ds:(%%esi),%%es:(%%edi)" : :);
	asm("jmp *%0" : : "m" (a));
}

struct mmapargs {
	void *start;
	size_t length;
	int prot;
	int flags;
	int fd;
	off_t offset;
};

static void
load_elf(char *filename, struct xmyelf *me)
{
	int fd = -1, i, rv;
	struct mmapargs mmapargs;
	Elf32_Phdr *p;
	unsigned long addr, loadaddr = 0, loadbias = 0;
	unsigned long elfbss = 0, lastbss = 0;

	fd = xsyscall(SYS_open, filename, O_RDONLY);
	if (0 > fd)
		goto fail;

	if (0 > xsyscall(SYS_read, fd, &me->ehdr, sizeof(me->ehdr)))
		goto fail;
	if (me->ehdr.e_phentsize != sizeof(*me->phdr))
		goto fail;
	if (me->ehdr.e_phnum*me->ehdr.e_phentsize > sizeof(me->phdr))
		goto fail;

	/* the extra final 0 argument is the upper
	   bits of the 64 offset argument (i think) */
	rv = xsyscall(SYS_pread, fd, me->phdr,
		      me->ehdr.e_phnum*me->ehdr.e_phentsize,
		      me->ehdr.e_phoff, 0);
	if (0 > rv)
		goto fail;
	for (i = 0, p = me->phdr; i < me->ehdr.e_phnum; i++, p++) {
		int prot = PROT_WRITE;
		int type = MAP_PRIVATE;
		unsigned long k;

		if (p->p_type != PT_LOAD)
			continue;

		if (p->p_flags & PF_R)
			prot |= PROT_READ;
		if (p->p_flags & PF_W)
			prot |= PROT_WRITE;
		if (p->p_flags & PF_X)
			prot |= PROT_EXEC;
		if (loadaddr)
			type |= MAP_FIXED;

		mmapargs.start = (void*)ELF_PAGESTART(loadbias+p->p_vaddr);
		mmapargs.length = p->p_filesz+ELF_PAGEOFFSET(p->p_vaddr);
		mmapargs.prot = prot;
		mmapargs.flags = type;
		mmapargs.fd = fd;
		mmapargs.offset = p->p_offset-ELF_PAGEOFFSET(p->p_vaddr);
		addr = (unsigned long)xsyscall(SYS_mmap, &mmapargs);
		if (MAP_FAILED == (void*)addr)
			goto fail;
		if (!loadaddr) {
			loadbias = addr-ELF_PAGESTART(p->p_vaddr);
			loadaddr = loadbias+p->p_vaddr-p->p_offset;
		}

		/* determine the bss */
		k = loadbias + p->p_vaddr + p->p_filesz;
		if (k > elfbss)
			elfbss = k;
		k = loadbias + p->p_vaddr + p->p_memsz;
		if (k > lastbss)
			lastbss = k;
	}
	/* zero unused portion of fractional page */
	if (ELF_PAGEOFFSET(elfbss))
		xmemset((void*)elfbss, 0,
			ELF_MIN_ALIGN-ELF_PAGEOFFSET(elfbss));
	/* what we have mapped so far */
	elfbss = ELF_PAGESTART(elfbss+ELF_MIN_ALIGN-1);
	
	/* map the remainder of the bss, if necessary */
	if (lastbss > elfbss) {
		addr = (unsigned long)
			xsyscall(SYS_mmap, (void*)elfbss, lastbss-elfbss,
			     PROT_READ|PROT_WRITE,
			     MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS,
			     0, 0);
		if (MAP_FAILED == (void*)addr)
			goto fail;
	}
	xsyscall(SYS_close, fd);
	fd = -1;
	me->loadaddr = loadaddr;
	return;
fail:
	if (fd >= 0)
		xsyscall(SYS_close, fd);
	asm("int3" : :);
}

void
marker()
{}
