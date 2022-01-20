#include "ext.h"


struct atomrec {
	unsigned long atom;
	char *val;
	unsigned long vallen;
	struct atomrec *next;
};

struct proprec {
	struct atomrec *patom; /* property atom */
	struct atomrec *tatom; /* type atom */
	unsigned long len;
	char *bytes;
};

struct windowrec {
	Window id, new;
	XWindowAttributes attr;  /* not all fields are used */
	
	int backgroundState;   /* None, Relative, Pixel, Pixmap */
	unsigned long bg;
	unsigned long bgpm;
	int mapped;

	unsigned long visual;

	int toplevel;
	int x, y;
	int width, height;
	int border_width;
	int depth;
	int parent, *child;  /* ids */
	int num_children;

	struct proprec *prop;
	int num_prop;
};

struct pixmaprec {
	Pixmap id, new;
	int width, height;
	int bwidth;
	int depth;
	int nbytes;
	char *bytes;
};

struct fontrec {
	XFontStruct fs;
	char *name;
	unsigned long id;
};

struct cmap {
	unsigned long id;
	unsigned long alloc;
	unsigned long vis;
};

struct guickpt {
	unsigned long seq;       /* sequence number */
	char *msg;               /* xsync message buffer */
	unsigned long nmsg;      /* how many bytes in message buffer */
	struct windowrec *win;
	int num_win;
	struct atomrec *atom;
	struct pixmaprec *pm;
	int num_pixmap;
	struct extGC *gc;
	int num_gc;
	struct fontrec *font;
	int num_font;
	struct cursor *cursor;
	int num_cursor;
	struct cmap *cmap;
	int num_cmap;
};
