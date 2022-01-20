#include <sys/types.h>
#include <elf.h>

/* symbol table */
typedef struct symtab *symtab_t;
struct symlist {
	Elf32_Sym *sym;       /* symbols */
	char *str;            /* symbol strings */
	unsigned num;         /* number of symbols */
};
struct symtab {
	struct symlist *st;    /* "static" symbols */
	struct symlist *dyn;   /* dynamic symbols */
};

/* process memory map */
enum {
	RF_MAXLEN = 1024
};
#define MEMORY_ONLY  "[memory]"
struct mm {
	char name[RF_MAXLEN];
	unsigned long start, end; /* where it is mapped */
	unsigned long base;       /* where it thinks it is mapped,
				     according to in-process dl data */
	struct symtab *st;
};

struct modulelist {
	int pid;
	int is_dynamic;
	char exe[RF_MAXLEN];
	int num_mm;
	struct mm *mm;
	int exe_mm; /* index into mm */
};

struct modulelist *rf_parse(int pid);
void rf_free_modulelist(struct modulelist *ml);
unsigned long rf_find_libc_function(struct modulelist *ml, char *name);
int rf_replace_libc_function(struct modulelist *ml, char *name, void *to);
unsigned long rf_find_function(struct modulelist *ml, char *name);
unsigned char *rf_find_address(struct modulelist *ml, unsigned long addr);
