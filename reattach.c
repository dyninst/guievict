#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <execinfo.h>
#include <sys/un.h>
#define __USE_GNU   /* fcntl's F_SETSIG */
#include <fcntl.h>
#undef __USE_GNU

#include "rt.h"

/* see xc/programs/Xserver/include/servermd.h */
#define XBitmapBytePad(w)  ((((w)+31)>>5)<<2)

unsigned long
nextid(struct sctl *sctl)
{
	return sctl->nextid++;
}

/* create a 1x1 window of depth 0 from the root window */
static void
nullwindow(struct sctl *sctl, unsigned long id, int depth)
{
	int rv;
	xCreateWindowReq req;
	req.reqType = X_CreateWindow;
	req.length = sizeof(req)>>2;
	req.depth = 0;
	req.wid = id;
	req.parent = sctl->xcon->rootid;
	req.class = InputOutput;
	req.x = 0;
	req.y = 0;
	req.width = 1;
	req.height = 1;
	req.borderWidth = 1;
	req.visual = CopyFromParent;
	req.mask = 0;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}

static void
freepixmap(struct sctl *sctl, unsigned long id)
{
	xResourceReq req;
	int rv;

	req.reqType = X_FreePixmap;
	req.length = sizeof(req)>>2;
	req.id = id;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}

static void
freewindow(struct sctl *sctl, unsigned long id)
{
	xResourceReq req;
	int rv;

	req.reqType = X_DestroyWindow;
	req.length = sizeof(req)>>2;
	req.id = id;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}


static void
tmppixmap(struct sctl *sctl, unsigned long id, int depth)
{
	xCreatePixmapReq creq;
	int rv;
	unsigned long tid;

	tid = nextid(sctl);
	nullwindow(sctl, tid, 0);
	creq.reqType = X_CreatePixmap;
	creq.depth = depth;
	creq.pid = id;
	creq.drawable = tid;
	creq.width = 1;
	creq.height = 1;
	creq.length = sizeof(creq)>>2;
	rv = xwrite(sctl->xsock, &creq, sizeof(creq));
	if (0 >= rv)
		assert(0);
	freewindow(sctl, tid);
}

static void
tmpgc(struct sctl *sctl, unsigned long id, unsigned long drawable)
{
	xCreateGCReq req;
	int rv;

	req.reqType = X_CreateGC;
	req.drawable = drawable;
	req.length = sizeof(req)>>2;
	req.gc = id;
	req.mask = 0;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}

static void
freegc(struct sctl *sctl, unsigned long id)
{
	xResourceReq req;
	int rv;

	req.reqType = X_FreeGC;
	req.length = sizeof(req)>>2;
	req.id = id;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}

/* send an image as a bigrequest */
#if 0
static void
bigreqputimage(struct sctl *sctl, unsigned long id,
	       int width, int height, int depth, int nbytes, char *bytes)
{
	char *p;
	xPutImageReq preq;
	int rv;
	unsigned long tid;
	unsigned long len;

	tmpgc(sctl, tid, id);
	preq.reqType = X_PutImage;
	preq.drawable = id;
	preq.dstX = 0;
	preq.dstY = 0;
	preq.width = width;
	preq.height = height;
	preq.length = 0;   /* use big request */
	len = (sizeof(preq)+sizeof(len)+nbytes)>>2;
	preq.gc = tid;
	preq.format = XYPixmap;
	preq.depth = depth;
	preq.leftPad = 0;

	/* send as big request */
	p = (char*)&preq;
	rv = xwrite(sctl->xsock, p, sizeof(xReq));
	if (0 >= rv)
		assert(0);
	rv = xwrite(sctl->xsock, &len, sizeof(len));
	if (0 >= rv)
		assert(0);
	rv = xwrite(sctl->xsock, p+sizeof(xReq), sizeof(preq)-sizeof(xReq));
	if (0 >= rv)
		assert(0);
	rv = xwrite(sctl->xsock, bytes, nbytes);
	if (0 >= rv)
		assert(0);
	freegc(sctl, tid);
}
#endif

static void
splitputimage(struct sctl *sctl, unsigned long id,
	      int width, int height, int depth, int nbytes, char *bytes)
{
	xPutImageReq preq;
	char *p;
	int i, j, rv;
	unsigned long tid;
	unsigned long limit = 4*sctl->xcon->setup.maxRequestSize-sizeof(preq);
	unsigned long numlines, linelen;

	linelen = width;
	linelen += linelen%32 ? 32-linelen%32 : 0;
	linelen /= 8;      /* bytes */

	/* lines per slurp */
	if (linelen*height*depth <= limit)
		numlines = height;
	else
		numlines = (linelen*height*depth/limit
			    + (linelen*height*depth%limit ? 1 : 0));

	tid = nextid(sctl);
	tmpgc(sctl, tid, id);

	preq.reqType = X_PutImage;
	preq.drawable = id;
	preq.dstX = 0;
	preq.width = width;
	preq.gc = tid;
	preq.format = XYPixmap;
	preq.depth = depth;
	preq.leftPad = 0;

	if (geverbose)
		rs_log("pixmap(w=%ld,h=%ld,d=%ld) linelen=%ld, numlines=%ld, limit=%ld\n",
		       width, height, depth, linelen, numlines, limit);

	for (i = 0; i < height; i += numlines) {
		if (numlines < height-i)
			preq.height = numlines;
		else
			preq.height = height-i;
		preq.dstY = i;
		preq.length = (sizeof(preq)+linelen*preq.height*depth)>>2;
		rv = xwrite(sctl->xsock, &preq, sizeof(preq));
		if (0 >= rv)
			assert(0);
		for (j = 0, p = bytes+i*linelen;
		     j < depth;
		     j++, p += linelen*height) {
			rv = xwrite(sctl->xsock, p, preq.height*linelen);
			if (0 >= rv)
				assert(0);
		}
	}
	freegc(sctl, tid);
}

static void
putpixmap(struct sctl *sctl, unsigned long id,
	  int width, int height, int depth, int nbytes, char *bytes)
{
	xCreatePixmapReq creq;
	int rv;
	unsigned long tid;

	tid = nextid(sctl);
	nullwindow(sctl, tid, depth);
	creq.reqType = X_CreatePixmap;
	creq.depth = depth;
	creq.pid = id;
	creq.drawable = tid;
	creq.width = width;
	creq.height = height;
	creq.length = sizeof(creq)>>2;
	rv = xwrite(sctl->xsock, &creq, sizeof(creq));
	if (0 >= rv)
		assert(0);
	freewindow(sctl, tid);

	if (nbytes <= 0)
		return;
	splitputimage(sctl, id, width, height, depth, nbytes, bytes);
}

static void
putcursor(struct sctl *sctl, struct cursor *c)
{
	xCreateCursorReq req;
	unsigned long rv, sid, mid;
	
	sid = nextid(sctl);
	putpixmap(sctl, sid, c->width, c->height, 1,
		  c->nbytes, c->src);
	if (!c->emptymsk) {
		mid = nextid(sctl);
		putpixmap(sctl, mid, c->width, c->height, 1,
			  c->nbytes, c->msk);
	}

	req.reqType = X_CreateCursor;
	req.length = sizeof(req)>>2;
	req.cid = c->id;
	req.x = c->xhot;
	req.y = c->yhot;
	req.source = sid;
	req.mask = c->emptymsk ? None : mid;
	req.foreRed = c->fore_red;
	req.foreGreen = c->fore_green;
	req.foreBlue = c->fore_blue;
	req.backRed = c->back_red;
	req.backGreen = c->back_green;
	req.backBlue = c->back_blue;

	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);

	freepixmap(sctl, sid);
	if (!c->emptymsk)
		freepixmap(sctl, mid);
}

static void
putcursors(struct sctl *sctl)
{
	int i;
	for (i = 0; i < sctl->ckpt->num_cursor; i++)
		putcursor(sctl, &sctl->ckpt->cursor[i]);
}

static void
putpixmaps(struct sctl *sctl)
{
	int i;
	struct pixmaprec *pm;
	for (i = 0; i < sctl->ckpt->num_pixmap; i++) {
		pm = &sctl->ckpt->pm[i];
		putpixmap(sctl, pm->id, pm->width, pm->height,
			  pm->depth, pm->nbytes, pm->bytes);
	}
}

static void
checkpixmaps(struct sctl *sctl, unsigned long id)
{
	int i;
	struct pixmaprec *pm;
	if (!id)
		return;
	for (i = 0; i < sctl->ckpt->num_pixmap; i++) {
		pm = &sctl->ckpt->pm[i];
		if (pm->id == id)
			return;
	}
	assert(0);
}

static void
putfont(struct sctl *sctl, struct fontrec *f)
{
	xOpenFontReq req;
	char pad[3];
	int rv, slen;

	slen = strlen(f->name);
	req.reqType = X_OpenFont;
	req.length = (sizeof(req)+slen+xpad(slen))>>2;
	req.nbytes = slen;
	req.fid = f->fs.fid;
	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
	rv = xwrite(sctl->xsock, f->name, slen);
	if (0 >= rv)
		assert(0);
	if (xpad(slen)) {
		rv = xwrite(sctl->xsock, pad, xpad(slen));
		if (0 >= rv)
			assert(0);
	}
}

static void
putgc(struct sctl *sctl, struct extGC *gc)
{
	char buf[1024];
	unsigned long mask, *x;
	xCreateGCReq *req;
	int rv;
	unsigned long tid;

	/* the order of these values is significant */
	x = (unsigned long*)(buf+sizeof(*req));
	mask = 0;
	mask |= GCFunction;
	*x++ = gc->values.function;
	mask |= GCPlaneMask;
	*x++ = gc->values.plane_mask;
	mask |= GCForeground;
	*x++ = gc->values.foreground;
	mask |= GCBackground;
	*x++ = gc->values.background;
	mask |= GCLineWidth;
	*x++ = gc->values.line_width;
	mask |= GCLineStyle;
	*x++ = gc->values.line_style;
	mask |= GCCapStyle;
	*x++ = gc->values.cap_style;
	mask |= GCJoinStyle;
	*x++ = gc->values.join_style;
	mask |= GCFillStyle;
	*x++ = gc->values.fill_style;
	mask |= GCFillRule;
	*x++ = gc->values.fill_rule;
	if (gc->values.tile_is_pixel) {
		/* i think the server will use automatically 
		   the foreground color for the tile 
		   assert that the tile pixel value and fg 
		   are the same?*/
		/* rs_log("warning: tile is pixel\n"); */
	} else if (gc->values.tilepm != None) {
		mask |= GCTile;
		checkpixmaps(sctl, gc->values.tilepm);
		*x++ = gc->values.tilepm;
	}
	if (gc->values.stipple != None) {
		mask |= GCStipple;
		checkpixmaps(sctl, gc->values.stipple);
		*x++ = gc->values.stipple;
	}
	mask |= GCTileStipXOrigin;
	*x++ = gc->values.ts_x_origin;
	mask |= GCTileStipYOrigin;
	*x++ = gc->values.ts_y_origin;
	if (gc->values.font) {
		mask |= GCFont;
		*x++ = gc->values.font;
	}
	mask |= GCSubwindowMode;
	*x++ = gc->values.subwindow_mode;
	mask |= GCGraphicsExposures;
	*x++ = gc->values.graphics_exposures;
#if 0
	mask |= GCClipXOrigin;
	*x++ = gc->values.clip_x_origin;
	mask |= GCClipYOrigin;
	*x++ = gc->values.clip_y_origin;
	/* FIXME: ClipMask */
#endif
	/* FIXME: DashOffset, DashList */
	mask |= GCArcMode;
	*x++ = gc->values.arc_mode;

	tid = nextid(sctl);
	tmppixmap(sctl, tid, gc->depth);

	req = (xCreateGCReq *)buf;
	req->reqType = X_CreateGC;
	req->drawable = tid;
	req->gc = gc->id;
	req->mask = mask;
	req->length = x-(unsigned long*)buf;
	assert(req->length*4 <= sizeof(buf));
	rv = xwrite(sctl->xsock, buf, req->length*4);
	if (0 >= rv)
		assert(0);
	freepixmap(sctl, tid);
}

static void
putgcs(struct sctl *sctl)
{
	int i;
	for (i = 0; i < sctl->ckpt->num_gc; i++)
		putgc(sctl, &sctl->ckpt->gc[i]);
}

static void
putfonts(struct sctl *sctl)
{
	int i;
	for (i = 0; i < sctl->ckpt->num_font; i++)
		putfont(sctl, &sctl->ckpt->font[i]);
}

static void
putwindow(struct sctl *sctl, struct windowrec *w)
{
	char buf[1024];
	xCreateWindowReq *req;
	int rv;
	unsigned long mask, *x;
	
	req = (xCreateWindowReq *)buf;
	req->reqType = X_CreateWindow;
	req->depth = w->depth;
	req->wid = w->id;
	req->parent = w->toplevel ? sctl->xcon->rootid : w->parent;
	req->x = w->x;
	req->y = w->y;
	req->width = w->width;
	req->height = w->height;
	req->borderWidth = w->border_width;
	req->class = w->attr.class;
	req->visual = w->visual;

	x = (unsigned long *)(buf+sizeof(*req));
	mask = 0;
	if (w->backgroundState == 2L) {  /* the BackgroundPixel constant */
		mask |= CWBackPixel;
		*x++ = w->bg;
	} else if (w->backgroundState == 3L) { /* the BackgroundPixmap constant */
		mask |= CWBackPixmap;
		*x++ = w->bgpm;
	}

	mask |= CWBitGravity;
	*x++ = w->attr.bit_gravity;
	mask |= CWWinGravity;
	*x++ = w->attr.win_gravity;
	mask |= CWBackingStore;
	*x++ = w->attr.backing_store;
	mask |= CWBackingPlanes;
	*x++ = w->attr.backing_planes;
	mask |= CWBackingPixel;
	*x++ = w->attr.backing_pixel;
	mask |= CWOverrideRedirect;
	*x++ = w->attr.override_redirect;
	mask |= CWSaveUnder;
	*x++ = w->attr.save_under;
	mask |= CWEventMask;
	*x++ = w->attr.your_event_mask;
	mask |= CWDontPropagate;
	*x++ = w->attr.do_not_propagate_mask;
	mask |= CWColormap;
	*x++ = w->attr.colormap;
	req->mask = mask;
	req->length = x-(unsigned long *)buf;
	assert(req->length*4 <= sizeof(buf));

	rv = xwrite(sctl->xsock, buf, req->length*4);
	if (0 >= rv)
		assert(0);
}

static void
putwindows(struct sctl *sctl)
{
	int i;
	for (i = 0; i < sctl->ckpt->num_win; i++)
		putwindow(sctl, &sctl->ckpt->win[i]);
}

static void
putcolormap(struct sctl *sctl, struct cmap *cmap)
{
	xCreateColormapReq req;
	unsigned long rv;
	
	req.reqType = X_CreateColormap;
	req.length = sizeof(req)>>2;
	req.mid = cmap->id;
	req.window = sctl->xcon->rootid;
	req.visual = cmap->vis;
	req.alloc = cmap->alloc;

	rv = xwrite(sctl->xsock, &req, sizeof(req));
	if (0 >= rv)
		assert(0);
}

static void
putcolormaps(struct sctl *sctl)
{
	int i;
	for (i = 0; i < sctl->ckpt->num_cmap; i++)
		putcolormap(sctl, &sctl->ckpt->cmap[i]);
}

static void
regenerate(struct sctl *sctl)
{
	putcolormaps(sctl);
	putcursors(sctl);
	putfonts(sctl);
	putpixmaps(sctl);
	putgcs(sctl);
	putwindows(sctl);
}

/* regeneration xsync */
static int
xsync2(int s, unsigned long *seqp)
{
	int rv;
	xError *err;
	xGenericReply *rep;
	xCirculateWindowReq req;
	unsigned char *p, *m;
	const int msgsize = 32;
	char buf[msgsize];

	req.reqType = X_CirculateWindow;
	req.direction = 0;
	req.length = sizeof(req)>>2;
	req.window = 0xffffffff;
	if (-1 == xwrite(s, &req, sizeof(req)))
		assert(0);

	p = buf;
	while (1) {
		rv = xread(s, p, msgsize);
		if (0 >= rv)
			assert(0);

		switch (p[0]) {
		case 0: /* error */
			err = (xError *) p;
			if (geverbose)
				rs_log("got error %d on resource %x\n",
				       err->errorCode, (unsigned int)err->resourceID);
			if (err->resourceID == req.window) {
				*seqp = err->sequenceNumber;
				return 0;
			} else {
				rs_log("we're probably fucked\n");
			}
			break;
		case 1: /* reply */
			rep = (xGenericReply *) p;
			rs_log("tossing a reply\n");
			if (rep->length > 0) {
				m = xmalloc(rep->length*4);
				rv = xread(s, p, rep->length*4);
				if (0 >= rv)
					assert(0);
				free(m);
			}
			break;
		default: /* event */
			rs_log("how come we get events?\n");
			break;
		}
	}
	assert(0);
}

/*
  guimux has two stages.
  First, we read regeneration requests from
  A1, xlate them, and forward them to S.  We read from
  S and print any messages.  When A1 sends EOF (end of
  regeneration), we dump A1 and switch to A2.

  Second, on A2 we write sctl->msg buffer, if nonempty, on A2.
  Then we xlate on A2 as before, but messages from S
  are now reverse xlated and sent back through A2.
*/

static struct timeval start, end;

extern int xsigaction(int sig, struct sigaction *sa, struct sigaction *old);

static int
guimux(int s, int a1, int a2, struct sctl *sctl)
{
	unsigned long curseq;
	int i, nummap, rv;
	xResourceReq req;
	struct sigaction sa;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = SIG_DFL;
	rv = xsigaction(SIGSEGV, &sa, NULL);
	if (0 > rv)
		assert(0);

	/* first stage: resource regeneration */
	doxlate1(a1, s, sctl);
	close(a1);

	xsync2(s, &curseq);
	nummap = 0;
	for (i = sctl->ckpt->num_win-1; i >= 0; i--)
		if (sctl->ckpt->win[i].mapped) {
			req.reqType = X_MapWindow;
			req.id = sctl->ckpt->win[i].id;
			req.length = sizeof(req)>>2;
			xlate_xResourceReq((char*)&req);
			rv = xwrite(s, &req, sizeof(req));
			if (0 >= rv)
				assert(0);
			nummap++;
		}

	if (sctl->ckpt->nmsg > 0) {
		rv = xwrite(a2, sctl->ckpt->msg, sctl->ckpt->nmsg);
		if (0 >= rv)
			assert(0);
	}
	/* sequence number mapping:
	   1st arg: next sequence number expected by application
	   2nd arg: next sequence number server will emit
	            (FIXME: or, has just emitted)
	   3rd arg: number of messages that we have sent from
	            guimux to the server
		    (NUMMAP map window requests, 1 xsync request,
		    2 bigreq requests, 1 vicext)
	*/
	/* FIXME: Here we need to add message counts
	   from interning atoms */
	initseq(sctl->ckpt->seq, curseq+nummap+1, nummap+1+2+1);
	gettimeofday(&end, NULL);
	tv_diff(&end, &start, &end);
	rs_log("total reattach time: %ld.%06ld sec\n",
		end.tv_sec, end.tv_usec);
	doxlate2(a2, s, sctl);
	return 0;
}

void
do_reattach(struct sctl *sctl, char *arg)
{
	int rv, pid, s;
	int fda[2], fdb[2];
	char *disp;
	unsigned long flags;

	gettimeofday(&start, NULL);
	if (arg && *arg)
		disp = arg;
	else {
		disp = getenv("DISPLAY");
		if (!disp)
			disp = "localhost:0";
	}
	sctl->xcon = xmalloc(sizeof(*sctl->xcon));
	s = dial_xserver(disp, sctl->xcon);
	if (0 > s) {
		rs_log("cannot dial server %s\n", disp);
		assert(0);
	}

	if (0 > socketpair(PF_UNIX, SOCK_STREAM, 0, fda)) {
		perror("socketpair");
		assert(0);
	}
	if (0 > socketpair(PF_UNIX, SOCK_STREAM, 0, fdb)) {
		perror("socketpair");
		assert(0);
	}

	pid = fork();
	if (0 > pid) {
		perror("fork");
		exit(1);
	}
	if (!pid) {
		close(fda[0]);
		close(fdb[0]);
		exit(guimux(s, fda[1], fdb[1], sctl));
	}
	close(fda[1]);
	close(fdb[1]);
	close(s);
	
	if (fda[0] != sctl->xsock) {
		if (0 > dup2(fda[0], sctl->xsock)) {
			perror("dup2");
			assert(0);
		}
		close(fda[0]);
	}
	regenerate(sctl);
	close(sctl->xsock);
	if (fdb[0] != sctl->xsock) {
		if (0 > dup2(fdb[0], sctl->xsock)) {
			perror("dup2");
			assert(0);
		}
		close(fdb[0]);
	}
	/* here we hope for the best */
	rv = fcntl(sctl->xsock, F_SETFL, sctl->fcntl.flags);
	rv = fcntl(sctl->xsock, F_SETOWN, sctl->fcntl.owner);
	rv = fcntl(sctl->xsock, F_SETSIG, sctl->fcntl.sig);

	/* Re-enable the async we disabled in rtioready */
	flags = fcntl(sctl->lfd, F_GETFL);
	assert(flags != -1);
	rv = fcntl(sctl->lfd, F_SETFL, flags|O_ASYNC);
	assert(rv != -1);
}

void
waitforreattach(struct sctl *sctl)
{
	char buf[1024];
	int rv, len;
	struct sockaddr_un from;
	char *arg;
	
retry:
	len = sizeof(from);
	rs_log("waiting for reattach\n");
	rv = recvfrom(sctl->lfd, buf, sizeof(buf),
		      0, (struct sockaddr*)&from, &len);
	if (0 > rv && errno == EINTR)
		goto retry;
	if (0 > rv) {
		perror("recvfrom");
		assert(0);
	}
	if (rv >= sizeof(buf)) {
		rs_log("buffer overflow\n");
		assert(0);
	}
	buf[rv] = '\0';
	if (strncmp("reattach", buf, strlen("reattach")))
		rs_log("unexpected command: %s\n", buf);
	arg = buf+strlen("reattach");
	while (*arg == ' ')
		arg++;
	do_reattach(sctl, arg);
}
