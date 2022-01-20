#include <stdio.h>
#include <stdlib.h>
#include <X11/Xproto.h>
#define __USE_GNU   /* fcntl's F_SETSIG */
#include <fcntl.h>
#undef __USE_GNU
#include "rt.h"
#include "list.h"

const char *extname = "GUIEVICT";

static void
getresources(struct sctl *sctl, int res, int *num, union resu **u)
{
	xEvictResourceReply rep;
	xEvictResourceReq req;
	int rv, z;

	bzero(&req, sizeof(req));
	req.reqType = sctl->orig_xcon->vicmajor;
	req.evictType = X_EvictResource;
	req.res = res;
	req.length = sizeof(req)>>2;

	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
	readxreply(sctl->xsock, (char*)&rep);
	z = sizeof(**u);
	*num = rep.num;
	if (!rep.num)
		return;
	*u = xmalloc(rep.num*z);
	rv = xread(sctl->xsock, (char*)*u, rep.num*z);
	if (0 >= rv)
		assert(0);
}

static struct windowrec *
find_win(unsigned long id, struct windowrec *ws, int num)
{
	int i;
	for (i = 0; i < num; i++)
		if (ws[i].id == id)
			return &ws[i];
	return NULL;
}

static void
dfcopy(struct windowrec *root, struct windowrec *rec, int num,
       struct windowrec **next)
{
	int i;
	struct windowrec *p, *q;

	p = *next;
	for (i = 0; i < root->num_children; i++) {
		q = find_win(root->child[i], rec, num);
		assert(q);
		*p++ = *q;
	}

	for (i = 0; i < root->num_children; i++) {
		q = find_win(root->child[i], rec, num);
		assert(q);
		dfcopy(q, rec, num, &p);
	}
	*next = p;
}

static void
winsort(struct guickpt *ckpt)
{
	struct windowrec *sorted, *s;
	int i;

	sorted = xmalloc(sizeof(*sorted)*ckpt->num_win);
	s = sorted;
	for (i = 0; i < ckpt->num_win; i++)
		if (!find_win(ckpt->win[i].parent, ckpt->win, ckpt->num_win)) {
			/* toplevel window */
			ckpt->win[i].toplevel = 1;
			*s++ = ckpt->win[i];
			dfcopy(&ckpt->win[i], ckpt->win, ckpt->num_win, &s);
		}
	free(ckpt->win);
	ckpt->win = sorted;
}

static void
doquerytree(int xsock, struct windowrec *w)
{
	xResourceReq req;
	xQueryTreeReply rep;
	int rv;

	req.reqType = X_QueryTree;
	req.length = sizeof(req)>>2;
	req.id = w->id;
	if (-1 == xwrite(xsock, &req, sizeof(req)))
		assert(0);
	readxreply(xsock, (char*)&rep);
	w->parent = rep.parent;
	w->num_children = rep.nChildren;
	if (w->num_children > 0) {
		w->child = xmalloc(rep.nChildren*sizeof(*w->child));
		rv = xread(xsock, w->child, rep.nChildren*sizeof(*w->child));
		if (0 >= rv)
			assert(0);
	}
}

static void
dowinattrs(int xsock, struct windowrec *w)
{
	xResourceReq req;
	xGetWindowAttributesReply rep;
	int rv;
	char *p;

	req.reqType = X_GetWindowAttributes;
	req.length = sizeof(req)>>2;
	req.id = w->id;

	if (-1 == xwrite(xsock, &req, sizeof(req)))
		assert(0);
	p = (char*)&rep;
	readxreply(xsock, p);
	p += sizeof(xReply);
	rv = xread(xsock, p, sizeof(rep)-sizeof(xReply));
	if (0 >= rv)
		assert(0);

	w->visual = rep.visualID;
	w->attr.class = rep.class;
	w->attr.bit_gravity = rep.bitGravity;
	w->attr.win_gravity = rep.winGravity;
	w->attr.backing_planes = rep.backingBitPlanes;
	w->attr.backing_pixel = rep.backingPixel;
	w->attr.backing_store = rep.backingStore;
	w->attr.save_under = rep.saveUnder;
	w->attr.colormap = rep.colormap;
	w->attr.map_installed = rep.mapInstalled;
	w->attr.map_state = rep.mapState;
	w->attr.all_event_masks = rep.allEventMasks;
	w->attr.your_event_mask = rep.yourEventMask;
	w->attr.do_not_propagate_mask = rep.doNotPropagateMask;
	w->attr.override_redirect = rep.override;
	w->attr.colormap = rep.colormap;
}

static void
dowingeom(int xsock, struct windowrec *w)
{
	xResourceReq req;
	xGetGeometryReply rep;
	char *p;
	
	req.reqType = X_GetGeometry;
	req.length = sizeof(req)>>2;
	req.id = w->id;
	if (-1 == xwrite(xsock, &req, sizeof(req)))
		assert(0);
	p = (char*)&rep;
	readxreply(xsock, p);

	w->x = rep.x;
	w->y = rep.y;
	w->width = rep.width;
	w->height = rep.height;
	w->border_width = rep.borderWidth;
	w->depth = rep.depth;
}

static struct atomrec *
noteatom(struct sctl *sctl, unsigned long atom)
{
	int rv;
	struct atomrec *a;
	xResourceReq req;
	xGetAtomNameReply rep;

	a = sctl->ckpt->atom;
	while (a) {
		if (a->atom == atom)
			return a;
		a = a->next;
	}
	a = xmalloc(sizeof(*a));
	a->atom = atom;
	LIST_INSERT(sctl->ckpt->atom, a);
	
	req.reqType = X_GetAtomName;
	req.length = sizeof(req)>>2;
	req.id = atom;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
	readxreply(sctl->xsock, (char *)&rep);
	a->vallen = rep.nameLength;
	if (a->vallen == 0)
		return a;
	a->val = xmalloc(rep.nameLength);
	rv = xread(sctl->xsock, a->val, a->vallen);
	if (0 >= rv)
		assert(0);
	xreadpad(sctl->xsock, a->vallen);
	return a;
}

static void
getprop(struct sctl *sctl, unsigned long atom, unsigned long wid,
	struct proprec *pr)
{
	xGetPropertyReq req;
	xGetPropertyReply rep;
	char *p;
	int rv;

	req.reqType = X_GetProperty;
	req.length = sizeof(req)>>2;
	req.delete = 0;
	req.window = wid;
	req.property = atom;
	req.type = AnyPropertyType;
	req.longOffset = 0;
	req.longLength = 0;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
	p = (char*)&rep;
	readxreply(sctl->xsock, p);
	pr->len = rep.bytesAfter;
	pr->patom = noteatom(sctl, atom);
	pr->tatom = noteatom(sctl, rep.propertyType);
	if (pr->len == 0)
		return;
	pr->bytes = xmalloc(pr->len);

	/* do it again, now that we know the length */
	req.longLength = pr->len;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
	readxreply(sctl->xsock, p);
	rv = xread(sctl->xsock, pr->bytes, pr->len);
	xreadpad(sctl->xsock, pr->len);
}

static void
dowinprop(struct sctl *sctl, struct windowrec *w)
{
	int rv;
	xResourceReq lreq;
	xListPropertiesReply lrep;
	char *p;
	unsigned long *r;
	int i, num;

	lreq.reqType = X_ListProperties;
	lreq.length = sizeof(lreq)>>2;
	lreq.id = w->id;
	rv = xwrite(sctl->xsock, &lreq, sizeof(lreq));
	if (0 >= rv)
		assert(0);
	p = (char*)&lrep;
	readxreply(sctl->xsock, p);
	num = lrep.nProperties;
	if (num == 0)
		return;

	r = xmalloc(4*num);
	rv = xread(sctl->xsock, r, 4*num);
	if (0 >= rv)
		assert(0);
	w->prop = xmalloc(num*sizeof(*w->prop));
	w->num_prop = num;
	for (i = 0; i < num; i++) {
		getprop(sctl, r[i], w->id, &w->prop[i]);
#if 0
		rs_log("window %lx property %.*s(%.*s) is (%d bytes) %.*s\n",
		       w->id,
		       w->prop[i].patom->vallen, w->prop[i].patom->val,
		       w->prop[i].tatom->vallen, w->prop[i].tatom->val,
		       w->prop[i].len,
		       w->prop[i].len, w->prop[i].bytes);
#endif
	}
	free(r);
}

static void
getwindows(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;

	getresources(sctl, ExtRT_WINDOW, &n, &u);

	sctl->ckpt->win = xmalloc(n*sizeof(*sctl->ckpt->win));
	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_win; j++)
			if (sctl->ckpt->win[j].id == u[i].winx.id)
				break;
		if (j < sctl->ckpt->num_win)
			continue;
		sctl->ckpt->win[sctl->ckpt->num_win].id = u[i].winx.id;
		sctl->ckpt->win[sctl->ckpt->num_win].bg = u[i].winx.bgpixel;
		sctl->ckpt->win[sctl->ckpt->num_win].bgpm = u[i].winx.bgpm;
		sctl->ckpt->win[sctl->ckpt->num_win].mapped = u[i].winx.mapped;
		sctl->ckpt->win[sctl->ckpt->num_win].backgroundState = u[i].winx.backgroundState;
		sctl->ckpt->num_win++;
	}	
	if (n > 0)
		free(u);
	for (i = 0; i < sctl->ckpt->num_win; i++)
		doquerytree(sctl->xsock, &sctl->ckpt->win[i]);
	winsort(sctl->ckpt);
	for (i = 0; i < sctl->ckpt->num_win; i++) {
		dowinattrs(sctl->xsock, &sctl->ckpt->win[i]);
		dowingeom(sctl->xsock, &sctl->ckpt->win[i]);
#if 1
		dowinprop(sctl, &sctl->ckpt->win[i]);
#endif
	}
}

static void
getimage(struct sctl *sctl, struct pixmaprec *pm)
{
	xGetImageReq req;
	xGetImageReply rep;
	char *p;
	int rv;

	req.reqType = X_GetImage;
	req.x = 0;
	req.y = 0;
	req.width = pm->width;
	req.height = pm->height;
	req.planeMask = (1<<(pm->depth-1));
	req.planeMask |= req.planeMask-1;
	req.length = sizeof(req)>>2;
	req.drawable = pm->id;
	req.format = XYPixmap;

	if (-1 == xwrite(sctl->xsock, &req, sizeof(req)))
		assert(0);
	p = (char*)&rep;
	readxreply(sctl->xsock, p);
	pm->nbytes = rep.length*4;
	pm->bytes = xmalloc(pm->nbytes);
	rv = xread(sctl->xsock, pm->bytes, pm->nbytes);
	if (0 >= rv)
		assert(0);
#if 0
	rs_log("got pixmap %lx (w=%d,h=%d,d=%d) of %d(%x) bytes\n",
		(unsigned long)pm->id, pm->width, pm->height, pm->depth, pm->nbytes, pm->nbytes);
#endif
}

static void
getpixmap(struct sctl *sctl, struct pixmaprec *pm)
{
	xResourceReq req;
	xGetGeometryReply rep;
	char *p;
	
	req.reqType = X_GetGeometry;
	req.length = sizeof(req)>>2;
	req.id = pm->id;
	if (-1 == xwrite(sctl->xsock, &req, sizeof(req)))
		assert(0);
	p = (char*)&rep;
	readxreply(sctl->xsock, p);

	pm->width = rep.width;
	pm->height = rep.height;
	pm->bwidth = rep.borderWidth;
	pm->depth = rep.depth;
	getimage(sctl, pm);
}

static void
getpixmaps(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;
	struct pixmaprec *p;

	getresources(sctl, ExtRT_PIXMAP, &n, &u);
	sctl->ckpt->pm = xmalloc(n*sizeof(*sctl->ckpt->pm));
	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_pixmap; j++)
			if (sctl->ckpt->pm[j].id == u[i].xid)
				break;
		if (j < sctl->ckpt->num_pixmap)
			continue;

		p = &sctl->ckpt->pm[sctl->ckpt->num_pixmap++];
		p->id = u[i].xid;
	}		
	if (n > 0)
		free(u);

	for (i = 0; i < sctl->ckpt->num_pixmap; i++) {
		p = &sctl->ckpt->pm[i];
		getpixmap(sctl, p);
#if 0
		rs_log("pixmap[%d] = %lx (%d,%d,%d)\n",
			i, p->id, p->width, p->height, p->depth);
#endif
	}
}

static void
getfonts(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;
	struct fontrec *fr;
	
	getresources(sctl, ExtRT_FONT, &n, &u);
	sctl->ckpt->font = xmalloc(n*sizeof(*sctl->ckpt->font));
	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_font; j++)
			if (sctl->ckpt->font[j].id == u[i].xid)
				break;
		if (j < sctl->ckpt->num_font)
			continue;

		fr = &sctl->ckpt->font[sctl->ckpt->num_font++];
		fr->id = u[i].xid;
	}
	if (n > 0)
		free(u);
}

/* this cannot be called before we have determined
   sctl->nextid */
static void
fontfill(struct sctl *sctl)
{
	char *fn;
	int i;
	struct fontrec *fr;
	unsigned long nchar;

	for (i = 0; i < sctl->ckpt->num_font; i++) {
		fr = &sctl->ckpt->font[i];
		getfont(sctl, fr->id, &fr->fs, &nchar);
		fn = matchfont(sctl, &fr->fs);
		if (fn)
			fr->name = xstrdup(fn);
		else {
			rs_log("no match for font 0x%lx\n", fr->id);
			fr->name = NULL;
		}
	}
}

static void
getgcs(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;

	getresources(sctl, ExtRT_GC, &n, &u);
	sctl->ckpt->gc = xmalloc(n*sizeof(*sctl->ckpt->gc));

	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_gc; j++)
			if (sctl->ckpt->gc[j].id == u[i].gc.id)
				break;
		if (j < sctl->ckpt->num_gc)
			continue;

		sctl->ckpt->gc[sctl->ckpt->num_gc++] = u[i].gc;
	}
	if (n > 0)
		free(u);
}

static void
getcursors(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;
	struct cursor *c;
	getresources(sctl, ExtRT_CURSOR, &n, &u);
	sctl->ckpt->cursor = xmalloc(n*sizeof(*sctl->ckpt->cursor));
	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_cursor; j++)
			if (sctl->ckpt->cursor[j].id == u[i].xid)
				break;
		if (j < sctl->ckpt->num_cursor)
			continue;

		c = &sctl->ckpt->cursor[sctl->ckpt->num_cursor++];
		memcpy(c, &u[i].cursor, sizeof(*c));
	}
	if (n > 0)
		free(u);
}

static void
getcolormaps(struct sctl *sctl)
{
	union resu *u;
	int i, j, n;
	struct cmap *cmap;

	getresources(sctl, ExtRT_COLORMAP, &n, &u);
	sctl->ckpt->cmap = xmalloc(n*sizeof(*sctl->ckpt->cmap));

	for (i = 0; i < n; i++) {
		/* skip duplicate ids */
		for (j = 0; j < sctl->ckpt->num_cmap; j++)
			if (sctl->ckpt->cmap[j].id == u[i].xid)
				break;
		if (j < sctl->ckpt->num_cmap)
			continue;

		cmap = &sctl->ckpt->cmap[sctl->ckpt->num_cmap++];
		cmap->id = u[i].xid;
		cmap->alloc = 0;
		cmap->vis = 0x00000024;
	}
}

enum {
	T_WINDOWS = 0,
	T_PIXMAPS,
	T_GCS,
	T_FONTINIT,
	T_FONTS,
	T_FONTFILL,
	T_CURSORS,
	T_COLORMAPS,
	T_MAX
};

static char *intervalnames[] = {
	[T_WINDOWS]     "Windows",
	[T_PIXMAPS]     "Pixmaps",
	[T_GCS]         "GCs",
	[T_FONTINIT]    "Fontinit",
	[T_FONTS]       "Fonts",
	[T_FONTFILL]    "Fontfill",
	[T_CURSORS]     "Cursors",
	[T_COLORMAPS]   "Colormaps",
};

struct interval {
	struct timeval start;
	struct timeval end;
	struct timeval diff;
};
	
static struct interval intervals[T_MAX];

static void
interval_start(int n)
{
	gettimeofday(&intervals[n].start, NULL);
}

static void
interval_end(int n)
{
	gettimeofday(&intervals[n].end, NULL);
}

static void
interval_summary()
{
	int i;
	for (i = 0; i < T_MAX; i++) {
		tv_diff(&intervals[i].end, &intervals[i].start, &intervals[i].diff);
		rs_log("%15s: %3ld.%06ld sec\n", intervalnames[i],
			intervals[i].diff.tv_sec, intervals[i].diff.tv_usec);
	}
}

static void
get_session(struct sctl *sctl)
{
	interval_start(T_WINDOWS);
	getwindows(sctl);
	interval_end(T_WINDOWS);
	interval_start(T_PIXMAPS);
	getpixmaps(sctl);
	interval_end(T_PIXMAPS);
	interval_start(T_GCS);
	getgcs(sctl);
	interval_end(T_GCS);
	interval_start(T_FONTINIT);
	initfont(sctl);
	interval_end(T_FONTINIT);
	interval_start(T_FONTS);
	getfonts(sctl);
	interval_end(T_FONTS);
	interval_start(T_CURSORS);
	getcursors(sctl);
	interval_end(T_CURSORS);
	interval_start(T_COLORMAPS);
	getcolormaps(sctl);
	interval_end(T_COLORMAPS);
}

static int
xsync(struct sctl *sctl)
{
	int rv;
	xError *err;
	xGenericReply *rep;
	xCirculateWindowReq req;
	unsigned char *p, *m;
	const int msgsize = 32;
	unsigned long msgmax;

	msgmax = 1024;
	m = p = sctl->ckpt->msg = xmalloc(msgmax);

	req.reqType = X_CirculateWindow;
	req.direction = 0;
	req.length = sizeof(req)>>2;
	req.window = 0xffffffff;
	if (-1 == xwrite(sctl->xsock, &req, sizeof(req)))
		assert(0);
	
	while (1) {
		if (msgmax-(p-m) < msgsize) {
			int n = p-m;
			msgmax *= 2;
			m = xrealloc(m, msgmax);
			sctl->ckpt->msg = m;
			p = m+n;
		}
		rv = xread(sctl->xsock, p, msgsize);
		if (0 >= rv)
			assert(0);

		switch (p[0]) {
		case 0: /* error */
			err = (xError *) p;
#if 0
			rs_log("got error %d on resource %x\n",
				err->errorCode, (unsigned int)err->resourceID);
#endif
			if (err->resourceID == req.window) {
				sctl->ckpt->seq = err->sequenceNumber;
				sctl->ckpt->nmsg = p-m; /* don't count this message */
				return 0;
			}
			p += msgsize;
			break;
		case 1: /* reply */
			rep = (xGenericReply *) p;
			if (rep->length > 0) {
				if (msgmax-(p-m) < rep->length*4) {
					int n = p-m;
					msgmax *= 2;
					m = xrealloc(m, msgmax);
					sctl->ckpt->msg = m;
					p = m+n;
				}
				rv = xread(sctl->xsock, p, rep->length*4);
				if (0 >= rv)
					assert(0);
			}
			p += msgsize + rep->length*4;
			break;
		default: /* event */
			p += msgsize;
			break;
		}
	}
	assert(0);
}


static void
setunusedid(struct sctl *sctl)
{
	int i;
	struct guickpt *c;
	unsigned long max = 0;

	c = sctl->ckpt;
	for (i = 0; i < c->num_win; i++)
		if (max < c->win[i].id)
			max = c->win[i].id;
	for (i = 0; i < c->num_pixmap; i++)
		if (max < c->pm[i].id)
			max = c->pm[i].id;
	for (i = 0; i < c->num_gc; i++)
		if (max < c->gc[i].id)
			max = c->gc[i].id;
	for (i = 0; i < c->num_font; i++)
		if (max < c->font[i].fs.fid)
			max = c->font[i].fs.fid;
	for (i = 0; i < c->num_cursor; i++)
		if (max < c->cursor[i].id)
			max = c->cursor[i].id;
	sctl->nextid = max+1;
}

void
do_detach(struct sctl *sctl, char *arg)
{
	int rv, flags;
	struct timeval start, end;

	gettimeofday(&start, NULL);
	flags = fcntl(sctl->xsock, F_GETFL);
	assert(flags != -1);
	sctl->fcntl.flags = flags;
	sctl->fcntl.owner = fcntl(sctl->xsock, F_GETOWN);
	assert(sctl->fcntl.owner != -1);
	sctl->fcntl.sig = fcntl(sctl->xsock, F_GETSIG);
	assert(sctl->fcntl.sig != -1);

	/* cancel async i/o while we're in evict
	   so we do not compete with the application
	   to service i/o on xsock */
	rv = fcntl(sctl->xsock, F_SETFL, flags&(~O_ASYNC));
	assert(rv != -1);

	sctl->ckpt = xmalloc(sizeof(*sctl->ckpt));
	xsync(sctl);
	get_session(sctl);
	setunusedid(sctl);
	interval_start(T_FONTFILL);
	fontfill(sctl);
	getcolormaps(sctl);
	interval_end(T_FONTFILL);
	close(sctl->xsock);
	gettimeofday(&end, NULL);
	tv_diff(&end, &start, &end);
	interval_summary();
	rs_log("total detach time: %ld.%06ld sec\n",
		end.tv_sec, end.tv_usec);
	waitforreattach(sctl);
}
