#include <assert.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <X11/X.h>
#include <X11/Xproto.h>

#include "list.h"
#include "rt.h"

struct idmap {
	unsigned long client;
	unsigned long server;
	struct idmap *next;
};

static struct idmap *map = NULL;
static struct idmap *amap = NULL;

static unsigned long ridbase;
static unsigned long ridmask;
static unsigned long seqcbase;
static unsigned long seqsbase;
static unsigned xlateseq = 1;
static unsigned long cextmajoropcode;
static unsigned long sextmajoropcode;

int
hpxread(int sd, void *buf, size_t len)
{
	char *p = (char *)buf;
	size_t nrecv = 0;
	ssize_t rv;

	assert(len > 0); 	/* a common bug */
	while (nrecv < len) {
		rv = read(sd, p, len - nrecv);
		if (0 > rv && errno == EINTR)
			continue;
		if (0 > rv)
			return -1;
		if (0 == rv)
			return 0;
		nrecv += rv;
		p += rv;
	}
	return nrecv;
}

static void
addidmap(unsigned long cid, unsigned long sid)
{
	struct idmap *id;
	id = xmalloc(sizeof(*id));
	id->client = cid;
	id->server = sid;
	LIST_INSERT(map, id);
}

static void
addatmap(unsigned long cid, unsigned long sid)
{
	struct idmap *id;
	id = xmalloc(sizeof(*id));
	id->client = cid;
	id->server = sid;
	LIST_INSERT(amap, id);
}

static int
visis_are_compatible(struct xvisual *a, struct xvisual *b)
{
	return (a->class == b->class
		&& a->depth == b->depth
		&& a->red_mask == b->red_mask
		&& a->blue_mask == b->blue_mask
		&& a->green_mask == b->green_mask
		&& a->bits_per_rgb == b->bits_per_rgb
		&& a->entries == b->entries);
}


static void
internatoms(struct sctl *sctl)
{
	struct atomrec *a;
	xInternAtomReq req;
	xInternAtomReply rep;
	int rv;

	a = sctl->ckpt->atom;
	while (a) {
		req.reqType = X_InternAtom;
		req.length = (sizeof(req)+a->vallen+xpad(a->vallen))>>2;
		req.onlyIfExists = False;
		req.nbytes = a->vallen;
		rv = xwrite(sctl->xsock, &req, sizeof(req));
		if (0 >= rv)
			assert(0);
		if (a->vallen > 0) {
			rv = xwritepad(sctl->xsock, a->val, a->vallen);
			if (0 >= rv)
				assert(0);
		}
		readxreply(sctl->xsock, (char*)&rep);
		addatmap(a->atom, rep.atom);
		a = a->next;
	}
}

static void
initmap(struct sctl *sctl)
{
	struct xvisual *cv, *sv;
	int i, j;

	cextmajoropcode = sctl->orig_xcon->vicmajor;
	sextmajoropcode = sctl->xcon->vicmajor;
	ridbase = sctl->xcon->setup.ridBase;
	ridmask = sctl->xcon->setup.ridMask;
#if 0
	/* FIXME: be sure to add extra message
	   counts in reattach.c */
	internatoms(sctl);
#endif
	addidmap(sctl->orig_xcon->rootid, sctl->xcon->rootid);
	addidmap(sctl->orig_xcon->defaultcmap, sctl->xcon->defaultcmap);
	for (i = 0; i < sctl->orig_xcon->num_vis; i++) {
		cv = &sctl->orig_xcon->vis[i];
		for (j = 0; j < sctl->xcon->num_vis; j++) {
			sv = &sctl->xcon->vis[j];
			if (visis_are_compatible(cv, sv)) {
				addidmap(cv->id, sv->id);
				break;
			}
		}
		if (j < sctl->xcon->num_vis)
			break;
		rs_log("warning: cannot find compatible visual for visual %lx\n",
		       cv->id);
		addidmap(cv->id, sctl->xcon->rootvis);
	}
}

void
initseq(unsigned long cbase, unsigned long sbase, unsigned long diff)
{
	if (geverbose)
		rs_log("initseq: cbase=%ld (0x%lx), sbase=%ld (0x%lx), xl=%ld (0x%lx),"
		       "xl+=%ld (0x%lx)\n",
		       cbase, cbase, sbase, sbase, xlateseq, xlateseq, diff, diff);

	seqcbase = cbase;
	seqsbase = sbase;
	xlateseq += diff;
}

static unsigned long
idcreate(unsigned long id)
{
	static unsigned long nextid = 10;
	struct idmap *idmap;
	unsigned long thid;
	thid = ridbase|(nextid&ridmask);
	nextid++;
	idmap = xmalloc(sizeof(*idmap));
	idmap->client = id;
	idmap->server = thid;
	LIST_INSERT(map, idmap);
	if (geverbose)
		rs_log("mapping client %lx -> server %lx\n",
		       idmap->client, idmap->server);
	return thid;
}

static int
cmpcid(struct idmap *id, unsigned long client)
{
	return id->client != client;
}

static int
cmpsid(struct idmap *id, unsigned long server)
{
	return id->server != server;
}

unsigned long
atmap(unsigned long id)
{
	struct idmap *idmap;
	LIST_FIND(amap, cmpcid, id, &idmap);
	if (!idmap) {
		if (geverbose)
			rs_log("no mapping for client atom 0x%lx\n", id);
		return id;
	}
	return idmap->server;
}

unsigned long
idmap(unsigned long id)
{
	struct idmap *idmap;
	LIST_FIND(map, cmpcid, id, &idmap);
	if (!idmap) {
		if (geverbose)
			rs_log("no mapping for client identifier 0x%lx\n", id);
		return id;
	}
	if (geverbose)
		rs_log("mapping client 0x%lx to server 0x%lx\n",
		       idmap->client, idmap->server);
	return idmap->server;
}

unsigned long
atrmap(unsigned long id)
{
	struct idmap *idmap;
	LIST_FIND(amap, cmpsid, id, &idmap);
	if (!idmap) {
		if (geverbose)
			rs_log("no mapping for server atom 0x%lx\n", id);
		return id;
	}
	return idmap->client;
}

unsigned long
idrmap(unsigned long id)
{
	struct idmap *idmap;
	LIST_FIND(map, cmpsid, id, &idmap);
	if (!idmap) {
		if (geverbose)
			rs_log("no mapping for server identifier 0x%lx\n", id);
		return id;
	}
	if (geverbose)
		rs_log("reverse mapping server 0x%lx to client 0x%lx\n",
		       idmap->server, idmap->client);
	return idmap->client;
}

unsigned long
seqrmap(unsigned long seq)
{
	unsigned long rv;
		
	if (seq <= seqsbase)
		rv = seqcbase;
	else
		rv = seqcbase+(seq-seqsbase);
	if (geverbose)
		rs_log("mapping sequence number %ld to %ld\n", seq, rv);
	return rv;
}

enum {
	REVERSE,
	FORWARD
};


static char *events[] = {
[KeyPress] "KeyPress",
[KeyRelease] "KeyRelease",
[ButtonPress] "ButtonPress",
[ButtonRelease] "ButtonRelease",
[MotionNotify] "MotionNotify",
[EnterNotify] "EnterNotify",
[LeaveNotify] "LeaveNotify",
[FocusIn] "FocusIn",
[FocusOut] "FocusOut",
[KeymapNotify] "KeymapNotify",
[Expose] "Expose",
[GraphicsExpose] "GraphicsExpose",
[NoExpose] "NoExpose",
[VisibilityNotify] "VisibilityNotify",
[CreateNotify] "CreateNotify",
[DestroyNotify] "DestroyNotify",
[UnmapNotify] "UnmapNotify",
[MapNotify] "MapNotify",
[MapRequest] "MapRequest",
[ReparentNotify] "ReparentNotify",
[ConfigureNotify] "ConfigureNotify",
[ConfigureRequest] "ConfigureRequest",
[GravityNotify] "GravityNotify",
[ResizeRequest] "ResizeRequest",
[CirculateNotify] "CirculateNotify",
[CirculateRequest] "CirculateRequest",
[PropertyNotify] "PropertyNotify",
[SelectionClear] "SelectionClear",
[SelectionRequest] "SelectionRequest",
[SelectionNotify] "SelectionNotify",
[ColormapNotify] "ColormapNotify",
[ClientMessage] "ClientMessage",
[MappingNotify] "MappingNotify",
};

static char *
event2str(int event)
{
	if (event < KeyPress || event > MappingNotify)
		return "Unknown";
	else
		return events[event];
}

static void
xlate_xEvent(char *p, int dir, int *drop)
{
	xEvent *e = (xEvent *)p;
	unsigned long (*mp)(unsigned long);

	*drop = 0;

	/* note that in forward direction
	   sequence number map does not matter
	   - the server corrects it */
	mp = (dir == REVERSE) ? idrmap : idmap;

	if (geverbose)
		rs_log("forwarding event (%s)\n", event2str(e->u.u.type));

	/* keymap events do not have sequence numbers  */
	if (e->u.u.type != KeymapNotify)
		e->u.u.sequenceNumber = seqrmap(e->u.u.sequenceNumber);
	switch (e->u.u.type) {
	case KeyPress:
	case KeyRelease:
	case ButtonPress:
	case ButtonRelease:
	case MotionNotify:
		e->u.keyButtonPointer.root = mp(e->u.keyButtonPointer.root);
		e->u.keyButtonPointer.event = mp(e->u.keyButtonPointer.event);
		e->u.keyButtonPointer.child = mp(e->u.keyButtonPointer.child);
		break;
	case EnterNotify:
	case LeaveNotify:
		e->u.enterLeave.root  = mp(e->u.enterLeave.root);
		e->u.enterLeave.event = mp(e->u.enterLeave.event);
		e->u.enterLeave.child = mp(e->u.enterLeave.child);
		break;
	case FocusIn:
	case FocusOut:
		e->u.focus.window = mp(e->u.focus.window);
		break;
	case KeymapNotify:
		break;
	case Expose:
		e->u.expose.window = mp(e->u.expose.window);
		break;
	case GraphicsExpose:
		e->u.graphicsExposure.drawable = mp(e->u.graphicsExposure.drawable);
		break;
	case NoExpose:
		e->u.noExposure.drawable = mp(e->u.noExposure.drawable);
		break;
	case VisibilityNotify:
		e->u.visibility.window = mp(e->u.visibility.window);
		break;
	case CreateNotify:
		e->u.createNotify.parent = mp(e->u.createNotify.parent);
		e->u.createNotify.window = mp(e->u.createNotify.window);
		break;
	case DestroyNotify:
		e->u.destroyNotify.event  = mp(e->u.destroyNotify.event);
		e->u.destroyNotify.window = mp(e->u.destroyNotify.window);
		break;
	case UnmapNotify:
		e->u.unmapNotify.event  = mp(e->u.unmapNotify.event);
		e->u.unmapNotify.window = mp(e->u.unmapNotify.window);
		break;
	case MapNotify:
		e->u.mapNotify.event  = mp(e->u.mapNotify.event);
		e->u.mapNotify.window = mp(e->u.mapNotify.window);
		*drop = 1;
		break;
	case MapRequest:
		e->u.mapRequest.parent = mp(e->u.mapRequest.parent);
		e->u.mapRequest.window = mp(e->u.mapRequest.window);
		break;
	case ReparentNotify:
		e->u.reparent.parent = mp(e->u.reparent.parent);
		e->u.reparent.window = mp(e->u.reparent.window);
		e->u.reparent.event  = mp(e->u.reparent.event);
		*drop = 1;
		break;
	case ConfigureNotify:
		e->u.configureNotify.aboveSibling = mp(e->u.configureNotify.aboveSibling);
		e->u.configureNotify.window = mp(e->u.configureNotify.window);
		e->u.configureNotify.event  = mp(e->u.configureNotify.event);
		*drop = 1;
		break;
	case ConfigureRequest:
		e->u.configureRequest.sibling = mp(e->u.configureRequest.sibling);
		e->u.configureRequest.window = mp(e->u.configureRequest.window);
		e->u.configureRequest.parent  = mp(e->u.configureRequest.parent);
		break;
	case GravityNotify:
		e->u.gravity.event  = mp(e->u.gravity.event);
		e->u.gravity.window = mp(e->u.gravity.window);
		break;
	case ResizeRequest:
		e->u.resizeRequest.window = mp(e->u.resizeRequest.window);
		break;
	case CirculateNotify:
	case CirculateRequest:
		e->u.circulate.parent = mp(e->u.circulate.parent);
		e->u.circulate.window = mp(e->u.circulate.window);
		e->u.circulate.event  = mp(e->u.circulate.event);
		break;
	case PropertyNotify:
		e->u.property.window = mp(e->u.property.window);
		e->u.property.atom = mp(e->u.property.atom);
		break;
	case SelectionClear:
		e->u.selectionClear.window = mp(e->u.selectionClear.window);
		e->u.selectionClear.atom   = mp(e->u.selectionClear.atom);
		break;
	case SelectionRequest:
		e->u.selectionRequest.owner = mp(e->u.selectionRequest.owner);
		e->u.selectionRequest.requestor = mp(e->u.selectionRequest.requestor);
		e->u.selectionRequest.selection = mp(e->u.selectionRequest.selection);
		e->u.selectionRequest.target = mp(e->u.selectionRequest.target);
		e->u.selectionRequest.property = mp(e->u.selectionRequest.property);
		break;
	case SelectionNotify:
		e->u.selectionNotify.requestor = mp(e->u.selectionNotify.requestor);
		e->u.selectionNotify.selection = mp(e->u.selectionNotify.selection);
		e->u.selectionNotify.target = mp(e->u.selectionNotify.target);
		e->u.selectionNotify.property = mp(e->u.selectionNotify.property);
		break;
	case ColormapNotify:
		e->u.colormap.window = mp(e->u.colormap.window);
		e->u.colormap.colormap = mp(e->u.colormap.colormap);
		break;
	case MappingNotify:
		break;
	case ClientMessage:
		e->u.clientMessage.window = mp(e->u.clientMessage.window);
		e->u.clientMessage.u.b.type = mp(e->u.clientMessage.u.b.type);
		break;
	default:
		if (geverbose)
			rs_log("unknown event type 0x%x\n", e->u.u.type);
	}
}

/* REQUESTS */

/* Requests that need no translation */
static void
xlate_xPlainReq(char *p)
{
}

void
xlate_xResourceReq(char *p)
{
	xResourceReq *req = (xResourceReq *)p;
	req->id = idmap(req->id);
}

static void
xlate_xCreateWindowReq(char *p)
{
	xCreateWindowReq *req = (xCreateWindowReq *)p;
	unsigned long *x;

	req->wid = idcreate(req->wid);
	req->parent = idmap(req->parent);
	if (req->visual)
		/* 0 visual means CopyFromParent */
		req->visual = idmap(req->visual);
	x = (unsigned long *)(p+sizeof(*req));
	if (req->mask & CWBackPixmap) {
		if (!(*x)) rs_log("CWBackPixmap is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (req->mask & CWBackPixel) {
		x++;
	}
	if (req->mask & CWBorderPixmap) {
		if (!(*x)) rs_log("CWBorderPixmap is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (req->mask & CWBorderPixel)
		x++;
	if (req->mask & CWBitGravity)
		x++;
	if (req->mask & CWWinGravity)
		x++;
	if (req->mask & CWBackingStore)
		x++;
	if (req->mask & CWBackingPlanes)
		x++;
	if (req->mask & CWBackingPixel)
		x++;
	if (req->mask & CWOverrideRedirect)
		x++;
	if (req->mask & CWSaveUnder)
		x++;
	if (req->mask & CWEventMask)
		x++;
	if (req->mask & CWDontPropagate)
		x++;
	if (req->mask & CWColormap) {
		if (!(*x)) rs_log("CWColormap is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (req->mask & CWCursor) {
		if (!(*x)) rs_log("CWCursor is 0\n");
		*x = idmap(*x);
		x++;
	}
}

static void
xlate_xChangeWindowAttributesReq(char *p)
{
	xChangeWindowAttributesReq *req = (xChangeWindowAttributesReq *)p;
	unsigned long *x;

	req->window = idmap(req->window);
	x = (unsigned long *)(p+sizeof(*req));
	if (req->valueMask & CWBackPixmap) {
		*x = idmap(*x);
		x++;
	}
	if (req->valueMask & CWBackPixel) {
		x++;
	}
	if (req->valueMask & CWBorderPixmap) {
		*x = idmap(*x);
		x++;
	}
	if (req->valueMask & CWBorderPixel)
		x++;
	if (req->valueMask & CWBitGravity)
		x++;
	if (req->valueMask & CWWinGravity)
		x++;
	if (req->valueMask & CWBackingStore)
		x++;
	if (req->valueMask & CWBackingPlanes)
		x++;
	if (req->valueMask & CWBackingPixel)
		x++;
	if (req->valueMask & CWOverrideRedirect)
		x++;
	if (req->valueMask & CWSaveUnder)
		x++;
	if (req->valueMask & CWEventMask)
		x++;
	if (req->valueMask & CWDontPropagate)
		x++;
	if (req->valueMask & CWColormap) {
		*x = idmap(*x);
		x++;
	}
	if (req->valueMask & CWCursor) {
		*x = idmap(*x);
		x++;
	}
}

static void
xlate_xChangeSaveSetReq(char *p)
{
	xChangeSaveSetReq *req = (xChangeSaveSetReq *)p;
	req->window = idmap(req->window);
}

static void
xlate_xReparentWindowReq(char *p)
{
	xReparentWindowReq *req = (xReparentWindowReq *)p;
	req->window = idmap(req->window);
	req->parent = idmap(req->parent);
}

static void
xlate_xConfigureWindowReq(char *p)
{
	xConfigureWindowReq *req = (xConfigureWindowReq *)p;
	unsigned long *x, mask;
	req->window = idmap(req->window);
	x = (unsigned long *)p+sizeof(*req);
	mask = req->mask;
	if (mask & CWX)
		x++;
	if (mask & CWY)
		x++;
	if (mask & CWWidth)
		x++;
	if (mask & CWHeight)
		x++;
	if (mask & CWBorderWidth)
		x++;
	if (mask & CWSibling) {
		*x = idmap(*x);
		x++;
	}
	if (mask & CWStackMode)
		x++;
}

static void
xlate_xCirculateWindowReq(char *p)
{
	xCirculateWindowReq *req = (xCirculateWindowReq *)p;
	req->window = idmap(req->window);
}

static void
xlate_xInternAtomReq(char *p)
{
}

static void
xlate_xChangePropertyReq(char *p)
{
	xChangePropertyReq *req = (xChangePropertyReq *)p;
	req->window = idmap(req->window);
	req->property = idmap(req->property);
 	req->type = idmap(req->type);
	/* FIXME: Property Mapping */
}

static void
xlate_xDeletePropertyReq(char *p)
{
	xDeletePropertyReq *req = (xDeletePropertyReq *)p;
	req->window = idmap(req->window);
	req->property = idmap(req->property);
}

static void
xlate_xGetPropertyReq(char *p)
{
	xGetPropertyReq *req = (xGetPropertyReq *)p;
	req->window = idmap(req->window);
	req->property = idmap(req->property);
	req->type = idmap(req->type);
}

static void
xlate_xSetSelectionOwnerReq(char *p)
{
	xSetSelectionOwnerReq *req = (xSetSelectionOwnerReq *)p;
	req->window = idmap(req->window);
	req->selection = idmap(req->selection);
}

static void
xlate_xConvertSelectionReq(char *p)
{
	xConvertSelectionReq *req = (xConvertSelectionReq *)p;
	req->requestor = idmap(req->requestor);
	req->selection = idmap(req->selection);
	req->target = idmap(req->target);
	req->property = idmap(req->property);
}

static void
xlate_xSendEventReq(char *p)
{
	int notused;
	xSendEventReq *req = (xSendEventReq *)p;
	req->destination = idmap(req->destination);
	xlate_xEvent((char*)&req->event, FORWARD, &notused);
}

static void
xlate_xGrabPointerReq(char *p)
{
	xGrabPointerReq *req = (xGrabPointerReq *)p;
	req->grabWindow = idmap(req->grabWindow);
	req->confineTo = idmap(req->confineTo);
	req->cursor = idmap(req->cursor);
}

static void
xlate_xGrabButtonReq(char *p)
{
	xGrabButtonReq *req = (xGrabButtonReq *)p;
	req->grabWindow = idmap(req->grabWindow);
	req->confineTo = idmap(req->confineTo);
	req->cursor = idmap(req->cursor);
}

static void
xlate_xUngrabButtonReq(char *p)
{
	xUngrabButtonReq *req = (xUngrabButtonReq *)p;
	req->grabWindow = idmap(req->grabWindow);
}

static void
xlate_xChangeActivePointerGrabReq(char *p)
{
	xChangeActivePointerGrabReq *req = (xChangeActivePointerGrabReq *)p;
	req->cursor = idmap(req->cursor);
}

static void
xlate_xGrabKeyboardReq(char *p)
{
	xGrabKeyboardReq *req = (xGrabKeyboardReq *)p;
	req->grabWindow = idmap(req->grabWindow);
}

static void
xlate_xGrabKeyReq(char *p)
{
	xGrabKeyReq *req = (xGrabKeyReq *)p;
	req->grabWindow = idmap(req->grabWindow);
}

static void
xlate_xUngrabKeyReq(char *p)
{
	xUngrabKeyReq *req = (xUngrabKeyReq *)p;
	req->grabWindow = idmap(req->grabWindow);
}

static void
xlate_xAllowEventsReq(char *p)
{
}

static void
xlate_xGetMotionEventsReq(char *p)
{
	xGetMotionEventsReq *req = (xGetMotionEventsReq *)p;
	req->window = idmap(req->window);
}

static void
xlate_xTranslateCoordsReq(char *p)
{
	xTranslateCoordsReq *req = (xTranslateCoordsReq *)p;
	req->srcWid = idmap(req->srcWid);
	req->dstWid = idmap(req->dstWid);
}

static void
xlate_xWarpPointerReq(char *p)
{
	xWarpPointerReq *req = (xWarpPointerReq *)p;
	req->srcWid = idmap(req->srcWid);
	req->dstWid = idmap(req->dstWid);
}

static void
xlate_xSetInputFocusReq(char *p)
{
	xSetInputFocusReq *req = (xSetInputFocusReq *)p;
	req->focus = idmap(req->focus);
}

static void
xlate_xOpenFontReq(char *p)
{
	xOpenFontReq *req = (xOpenFontReq *)p;
	req->fid = idcreate(req->fid);
}

static void
xlate_xQueryTextExtentsReq(char *p)
{
	xQueryTextExtentsReq *req = (xQueryTextExtentsReq *)p;
	req->fid = idmap(req->fid);
}

static void
xlate_xListFontsReq(char *p)
{
}

static void
xlate_xSetFontPathReq(char *p)
{
}

static void
xlate_xCreatePixmapReq(char *p)
{
	xCreatePixmapReq *req = (xCreatePixmapReq *)p;
	req->pid = idcreate(req->pid);
	req->drawable = idmap(req->drawable);
}

static void
xlate_xCreateGCReq(char *p)
{
	xCreateGCReq *req = (xCreateGCReq *)p;
	unsigned long *x, mask;
	req->gc = idcreate(req->gc);
	req->drawable = idmap(req->drawable);

	x = (unsigned long *)(p+sizeof(*req));
	mask = req->mask;
	if (mask & GCFunction)
		x++;
	if (mask & GCPlaneMask)
		x++;
	if (mask & GCForeground)
		x++;
	if (mask & GCBackground)
		x++;
	if (mask & GCLineWidth)
		x++;
	if (mask & GCLineStyle)
		x++;
	if (mask & GCCapStyle)
		x++;
	if (mask & GCJoinStyle)
		x++;
	if (mask & GCFillStyle)
		x++;
	if (mask & GCFillRule)
		x++;
	if (mask & GCTile) {
		*x = idmap(*x);
		x++;
	}
	if (mask & GCStipple) {
		*x = idmap(*x);
		x++;
	}
	if (mask & GCTileStipXOrigin)
		x++;
	if (mask & GCTileStipYOrigin)
		x++;
	if (mask & GCFont) {
		*x = idmap(*x);
		x++;
	}
	if (mask & GCSubwindowMode)
		x++;
	if (mask & GCGraphicsExposures)
		x++;
	if (mask & GCClipXOrigin)
		x++;
	if (mask & GCClipYOrigin)
		x++;
	if (mask & GCClipMask) {
		if (*x)
			/* 0 means None */
			*x = idmap(*x);
		x++;
	}
	if (mask & GCDashOffset)
		x++;
	if (mask & GCDashList)
		x++;
	if (mask & GCArcMode)
		x++;
}

static void
xlate_xChangeGCReq(char *p)
{
	xChangeGCReq *req = (xChangeGCReq *)p;
	unsigned long *x, mask;
	req->gc = idmap(req->gc);

	x = (unsigned long *)(p+sizeof(*req));
	mask = req->mask;
	if (mask & GCFunction)
		x++;
	if (mask & GCPlaneMask)
		x++;
	if (mask & GCForeground)
		x++;
	if (mask & GCBackground)
		x++;
	if (mask & GCLineWidth)
		x++;
	if (mask & GCLineStyle)
		x++;
	if (mask & GCCapStyle)
		x++;
	if (mask & GCJoinStyle)
		x++;
	if (mask & GCFillStyle)
		x++;
	if (mask & GCFillRule)
		x++;
	if (mask & GCTile) {
		if (!(*x)) rs_log("GCTile is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (mask & GCStipple) {
		if (!(*x)) rs_log("GCStipple is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (mask & GCTileStipXOrigin)
		x++;
	if (mask & GCTileStipYOrigin)
		x++;
	if (mask & GCFont) {
		if (!(*x)) rs_log("GCFont is 0\n");
		*x = idmap(*x);
		x++;
	}
	if (mask & GCSubwindowMode)
		x++;
	if (mask & GCGraphicsExposures)
		x++;
	if (mask & GCClipXOrigin)
		x++;
	if (mask & GCClipYOrigin)
		x++;
	if (mask & GCClipMask) {
		if (*x)
			/* 0 means None */
			*x = idmap(*x);
		x++;
	}
	if (mask & GCDashOffset)
		x++;
	if (mask & GCDashList)
		x++;
	if (mask & GCArcMode)
		x++;
}

static void
xlate_xCopyGCReq(char *p)
{
	xCopyGCReq *req = (xCopyGCReq *)p;
	req->srcGC = idmap(req->srcGC);
	req->dstGC = idmap(req->dstGC);
}

static void
xlate_xSetDashesReq(char *p)
{
	xSetDashesReq *req = (xSetDashesReq *)p;
	req->gc = idmap(req->gc);
}

static void
xlate_xSetClipRectanglesReq(char *p)
{
	xSetClipRectanglesReq *req = (xSetClipRectanglesReq *)p;
	req->gc = idmap(req->gc);
}

static void
xlate_xClearAreaReq(char *p)
{
	xClearAreaReq *req = (xClearAreaReq *)p;
	req->window = idmap(req->window);
}

static void
xlate_xCopyAreaReq(char *p)
{
	xCopyAreaReq *req = (xCopyAreaReq *)p;
	req->srcDrawable = idmap(req->srcDrawable);
	req->dstDrawable = idmap(req->dstDrawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xCopyPlaneReq(char *p)
{
	xCopyPlaneReq *req = (xCopyPlaneReq *)p;
	req->srcDrawable = idmap(req->srcDrawable);
	req->dstDrawable = idmap(req->dstDrawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xPolyPointReq(char *p)
{
	xPolyPointReq *req = (xPolyPointReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xPolySegmentReq(char *p)
{
	xPolySegmentReq *req = (xPolySegmentReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xFillPolyReq(char *p)
{
	xFillPolyReq *req = (xFillPolyReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xPutImageReq(char *p)
{
	xPutImageReq *req = (xPutImageReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xGetImageReq(char *p)
{
	xGetImageReq *req = (xGetImageReq *)p;
	req->drawable = idmap(req->drawable);
}

static void
xlate_xPolyTextReq(char *p)
{
	xPolyTextReq *req = (xPolyTextReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xImageTextReq(char *p)
{
	xImageTextReq *req = (xImageTextReq *)p;
	req->drawable = idmap(req->drawable);
	req->gc = idmap(req->gc);
}

static void
xlate_xCreateColormapReq(char *p)
{
	xCreateColormapReq *req = (xCreateColormapReq *)p;
	req->window = idmap(req->window);
	req->mid = idcreate(req->mid);
	req->visual = idmap(req->visual);
}

static void
xlate_xCopyColormapAndFreeReq(char *p)
{
	xCopyColormapAndFreeReq *req = (xCopyColormapAndFreeReq *)p;
	req->mid = idcreate(req->mid);
	req->srcCmap = idmap(req->srcCmap);
}

static void
xlate_xAllocColorReq(char *p)
{
	xAllocColorReq *req = (xAllocColorReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xAllocNamedColorReq(char *p)
{
	xAllocNamedColorReq *req = (xAllocNamedColorReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xAllocColorCellsReq(char *p)
{
	xAllocColorCellsReq *req = (xAllocColorCellsReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xAllocColorPlanesReq(char *p)
{
	xAllocColorPlanesReq *req = (xAllocColorPlanesReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xFreeColorsReq(char *p)
{
	xFreeColorsReq *req = (xFreeColorsReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xStoreColorsReq(char *p)
{
	xStoreColorsReq *req = (xStoreColorsReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xStoreNamedColorReq(char *p)
{
	xStoreNamedColorReq *req = (xStoreNamedColorReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xQueryColorsReq(char *p)
{
	xQueryColorsReq *req = (xQueryColorsReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xLookupColorReq(char *p)
{
	xLookupColorReq *req = (xLookupColorReq *)p;
	req->cmap = idmap(req->cmap);
}

static void
xlate_xCreateCursorReq(char *p)
{
	xCreateCursorReq *req = (xCreateCursorReq *)p;
	req->cid = idcreate(req->cid);
	req->source = idmap(req->source);
	req->mask = idmap(req->mask);
}

static void
xlate_xCreateGlyphCursorReq(char *p)
{
	xCreateGlyphCursorReq *req = (xCreateGlyphCursorReq *)p;
	req->cid = idcreate(req->cid);
	req->source = idmap(req->source);
	req->mask = idmap(req->mask);
}

static void
xlate_xRecolorCursorReq(char *p)
{
	xRecolorCursorReq *req = (xRecolorCursorReq *)p;
	req->cursor = idmap(req->cursor);
}

static void
xlate_xQueryBestSizeReq(char *p)
{
	xQueryBestSizeReq *req = (xQueryBestSizeReq *)p;
	req->drawable = idmap(req->drawable);
}

static void
xlate_xQueryExtensionReq(char *p)
{
}

static void
xlate_xSetModifierMappingReq(char *p)
{
}

static void
xlate_xSetPointerMappingReq(char *p)
{
}

static void
xlate_xGetKeyboardMappingReq(char *p)
{
}

static void
xlate_xChangeKeyboardMappingReq(char *p)
{
}

static void
xlate_xChangeKeyboardControlReq(char *p)
{
}

static void
xlate_xBellReq(char *p)
{
}

static void
xlate_xChangePointerControlReq(char *p)
{
}

static void
xlate_xSetScreenSaverReq(char *p)
{
}

static void
xlate_xChangeHostsReq(char *p)
{
}

static void
xlate_xListHostsReq(char *p)
{
}

static void
xlate_xChangeModeReq(char *p)
{
}

static void
xlate_xRotatePropertiesReq(char *p)
{
	xRotatePropertiesReq *req = (xRotatePropertiesReq *)p;
	unsigned long *x;
	int i;
	req->window = idmap(req->window);
	x = (unsigned long*)(p+sizeof(*req));
	for (i = 0; i < req->nAtoms ; i++) {
		*x = idmap(*x);
		x++;
	}
}


/* REPLIES */

static void
xlate_xGetWindowAttributesReply(char *p)
{
	xGetWindowAttributesReply *rep = (xGetWindowAttributesReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);    
	rep->colormap = idrmap(rep->colormap);
	rep->visualID = idrmap(rep->visualID);
}

static void
xlate_xGetGeometryReply(char *p)
{
	xGetGeometryReply *rep = (xGetGeometryReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->root = idrmap(rep->root);
}

static void
xlate_xQueryTreeReply(char *p)
{
	xQueryTreeReply *rep = (xQueryTreeReply *)p;
	unsigned long *x = (unsigned long*)(p+sizeof(*rep));
	int i;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->root = idrmap(rep->root);
	rep->parent = idrmap(rep->parent);
	for (i = 0; i < rep->nChildren; i++) {
		*x = idrmap(*x);
		x++;
	}
}

static void
xlate_xInternAtomReply(char *p)
{
	xInternAtomReply *rep = (xInternAtomReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->atom = idrmap(rep->atom);
}

static void
xlate_xGetAtomNameReply(char *p)
{
	xGetAtomNameReply *rep = (xGetAtomNameReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetPropertyReply(char *p)
{
	xGetPropertyReply *rep = (xGetPropertyReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	/* FIXME: Property Mapping */
}

static void
xlate_xListPropertiesReply(char *p)
{
	int i;
	xListPropertiesReply *rep = (xListPropertiesReply *)p;
	unsigned long *x = (unsigned long*)(p+sizeof(*rep));
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	for (i = 0; i < rep->nProperties; i++) {
		*x = idrmap(*x);
		x++;
	}
}

static void
xlate_xGetSelectionOwnerReply(char *p)
{
	xGetSelectionOwnerReply *rep = (xGetSelectionOwnerReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->owner = idrmap(rep->owner);
}

static void
xlate_xGrabPointerReply(char *p)
{
	xGrabPointerReply *rep = (xGrabPointerReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xQueryPointerReply(char *p)
{
	xQueryPointerReply *rep = (xQueryPointerReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->root = idrmap(rep->root);
	rep->child = idrmap(rep->child);
}

static void
xlate_xGetMotionEventsReply(char *p)
{
	xGetMotionEventsReply *rep = (xGetMotionEventsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xTranslateCoordsReply(char *p)
{
	xTranslateCoordsReply *rep = (xTranslateCoordsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->child = idrmap(rep->child);
}

static void
xlate_xGetInputFocusReply(char *p)
{
	xGetInputFocusReply *rep = (xGetInputFocusReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->focus = idrmap(rep->focus);
}

static void
xlate_xQueryKeymapReply(char *p)
{
	xQueryKeymapReply *rep = (xQueryKeymapReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xQueryFontReply(char *p)
{
	xQueryFontReply *rep = (xQueryFontReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	/* FIXME: Property Mapping */
}

static void
xlate_xQueryTextExtentsReply(char *p)
{
	xQueryTextExtentsReply *rep = (xQueryTextExtentsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xListFontsReply(char *p)
{
	xListFontsReply *rep = (xListFontsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xListFontsWithInfoReply(char *p)
{
	xListFontsWithInfoReply *rep = (xListFontsWithInfoReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	/* FIXME: Property Mapping */
}

static void
xlate_xGetFontPathReply(char *p)
{
	xGetFontPathReply *rep = (xGetFontPathReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetImageReply(char *p)
{
	xGetImageReply *rep = (xGetImageReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	rep->visual = idrmap(rep->visual);
}

static void
xlate_xListInstalledColormapsReply(char *p)
{
	int i;
	xListInstalledColormapsReply *rep = (xListInstalledColormapsReply *)p;
	unsigned long *x = (unsigned long*)(p+sizeof(*rep));
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	for (i = 0; i < rep->nColormaps; i++) {
		*x = idrmap(*x);
		x++;
	}
}

static void
xlate_xAllocColorReply(char *p)
{
	xAllocColorReply *rep = (xAllocColorReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xAllocNamedColorReply(char *p)
{
	xAllocNamedColorReply *rep = (xAllocNamedColorReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xAllocColorCellsReply(char *p)
{
	xAllocColorCellsReply *rep = (xAllocColorCellsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xAllocColorPlanesReply(char *p)
{
	xAllocColorPlanesReply *rep = (xAllocColorPlanesReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xQueryColorsReply(char *p)
{
	xQueryColorsReply *rep = (xQueryColorsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xLookupColorReply(char *p)
{
	xLookupColorReply *rep = (xLookupColorReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xQueryBestSizeReply(char *p)
{
	xQueryBestSizeReply *rep = (xQueryBestSizeReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xQueryExtensionReply(char *p)
{
	xQueryExtensionReply *rep = (xQueryExtensionReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xListExtensionsReply(char *p)
{
	xListExtensionsReply *rep = (xListExtensionsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xSetMappingReply(char *p)
{
	xSetMappingReply *rep = (xSetMappingReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetPointerMappingReply(char *p)
{
	xGetPointerMappingReply *rep = (xGetPointerMappingReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetKeyboardMappingReply(char *p)
{
	xGetKeyboardMappingReply *rep = (xGetKeyboardMappingReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetModifierMappingReply(char *p)
{
	xGetModifierMappingReply *rep = (xGetModifierMappingReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetKeyboardControlReply(char *p)
{
	xGetKeyboardControlReply *rep = (xGetKeyboardControlReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetPointerControlReply(char *p)
{
	xGetPointerControlReply *rep = (xGetPointerControlReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xGetScreenSaverReply(char *p)
{
	xGetScreenSaverReply *rep = (xGetScreenSaverReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xListHostsReply(char *p)
{
	xListHostsReply *rep = (xListHostsReply *)p;
	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
}

static void
xlate_xEvictWindowReply(char *p)
{
	int i;
	union resu *u;
	xEvictResourceReply *rep = (xEvictResourceReply *)p;

	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	u = (union resu *) (p+sizeof(*rep));
	rs_log("evict window reply %d windows\n", rep->num);
	for (i = 0; i < rep->num; i++) {
		u->winx.id = idrmap(u->winx.id);
		if (u->winx.backgroundState == 3L) /* BackgroundPixmap */
			u->winx.bgpm = idrmap(u->winx.bgpm);
		u++;
	}
}

static void
xlate_xEvictCursorReply(char *p)
{
	int i;
	union resu *u;
	xEvictResourceReply *rep = (xEvictResourceReply *)p;

	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	u = (union resu *) (p+sizeof(*rep));
	for (i = 0; i < rep->num; i++) {
		u->cursor.id = idrmap(u->cursor.id);
		u++;
	}
}

static void
xlate_xEvictGCReply(char *p)
{
	int i;
	union resu *u;
	xEvictResourceReply *rep = (xEvictResourceReply *)p;

	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	u = (union resu *) (p+sizeof(*rep));
	for (i = 0; i < rep->num; i++) {
		u->gc.id = idrmap(u->gc.id);
		u->gc.values.stipple = idrmap(u->gc.values.stipple);
		if (!u->gc.values.tile_is_pixel)
			u->gc.values.tilepm = idrmap(u->gc.values.tilepm);
		u->gc.values.font = idrmap(u->gc.values.font);
		u++;
	}
}

static void
xlate_xEvictGenReply(char *p)
{
	int i;
	union resu *u;
	xEvictResourceReply *rep = (xEvictResourceReply *)p;

	rep->sequenceNumber = seqrmap(rep->sequenceNumber);
	u = (union resu *) (p+sizeof(*rep));
	for (i = 0; i < rep->num; i++) {
		u->xid = idrmap(u->xid);
		u++;
	}
}

static void
xlate_xError(char *p)
{
	xError *e = (xError *)p;
	e->sequenceNumber = seqrmap(e->sequenceNumber);
	e->resourceID = idrmap(e->resourceID);
}

struct dispatch {
	char *reqname;
	void (*xlate_req)(char *);
	void (*xlate_rep)(char *);
};

struct dispatch reqdispatch[] = {
[X_CreateWindow]    { "X_CreateWindow", xlate_xCreateWindowReq, NULL },
[X_ChangeWindowAttributes]    { "X_ChangeWindowAttributes", xlate_xChangeWindowAttributesReq, NULL },
[X_GetWindowAttributes]    { "X_GetWindowAttributes", xlate_xResourceReq, xlate_xGetWindowAttributesReply },
[X_DestroyWindow]    { "X_DestroyWindow", xlate_xResourceReq, NULL },
[X_DestroySubwindows]    { "X_DestroySubwindows", xlate_xResourceReq, NULL },
[X_ChangeSaveSet]    { "X_ChangeSaveSet", xlate_xChangeSaveSetReq, NULL },
[X_ReparentWindow]    { "X_ReparentWindow", xlate_xReparentWindowReq, NULL },
[X_MapWindow]    { "X_MapWindow", xlate_xResourceReq, NULL },
[X_MapSubwindows]    { "X_MapSubwindows", xlate_xResourceReq, NULL },
[X_UnmapWindow]    { "X_UnmapWindow", xlate_xResourceReq, NULL },
[X_UnmapSubwindows]    { "X_UnmapSubwindows", xlate_xResourceReq, NULL },
[X_ConfigureWindow]    { "X_ConfigureWindow", xlate_xConfigureWindowReq, NULL },
[X_CirculateWindow]    { "X_CirculateWindow", xlate_xCirculateWindowReq, NULL },
[X_GetGeometry]    { "X_GetGeometry", xlate_xResourceReq, xlate_xGetGeometryReply },
[X_QueryTree]    { "X_QueryTree", xlate_xResourceReq, xlate_xQueryTreeReply },
[X_InternAtom]    { "X_InternAtom", xlate_xInternAtomReq, xlate_xInternAtomReply },
[X_GetAtomName]    { "X_GetAtomName", xlate_xResourceReq, xlate_xGetAtomNameReply },
[X_ChangeProperty]    { "X_ChangeProperty", xlate_xChangePropertyReq, NULL },
[X_DeleteProperty]    { "X_DeleteProperty", xlate_xDeletePropertyReq, NULL },
[X_GetProperty]    { "X_GetProperty", xlate_xGetPropertyReq, xlate_xGetPropertyReply },
[X_ListProperties]    { "X_ListProperties", xlate_xResourceReq, xlate_xListPropertiesReply },
[X_SetSelectionOwner]    { "X_SetSelectionOwner", xlate_xSetSelectionOwnerReq, NULL },
[X_GetSelectionOwner]    { "X_GetSelectionOwner", xlate_xResourceReq, xlate_xGetSelectionOwnerReply },
[X_ConvertSelection]    { "X_ConvertSelection", xlate_xConvertSelectionReq, NULL },
[X_SendEvent]    { "X_SendEvent", xlate_xSendEventReq, NULL },
[X_GrabPointer]    { "X_GrabPointer", xlate_xGrabPointerReq, xlate_xGrabPointerReply },
[X_UngrabPointer]    { "X_UngrabPointer", xlate_xPlainReq, NULL },
[X_GrabButton]    { "X_GrabButton", xlate_xGrabButtonReq, NULL },
[X_UngrabButton]    { "X_UngrabButton", xlate_xUngrabButtonReq, NULL },
[X_ChangeActivePointerGrab]    { "X_ChangeActivePointerGrab", xlate_xChangeActivePointerGrabReq, NULL },
[X_GrabKeyboard]    { "X_GrabKeyboard", xlate_xGrabKeyboardReq,  xlate_xGrabPointerReply },
[X_UngrabKeyboard]    { "X_UngrabKeyboard", xlate_xPlainReq, NULL },
[X_GrabKey]    { "X_GrabKey", xlate_xGrabKeyReq, NULL },
[X_UngrabKey]    { "X_UngrabKey", xlate_xUngrabKeyReq, NULL },
[X_AllowEvents]    { "X_AllowEvents", xlate_xAllowEventsReq, NULL },
[X_GrabServer]    { "X_GrabServer", xlate_xPlainReq, NULL },
[X_UngrabServer]    { "X_UngrabServer", xlate_xPlainReq, NULL },
[X_QueryPointer]    { "X_QueryPointer", xlate_xResourceReq, xlate_xQueryPointerReply },
[X_GetMotionEvents]    { "X_GetMotionEvents", xlate_xGetMotionEventsReq, xlate_xGetMotionEventsReply },
[X_TranslateCoords]    { "X_TranslateCoords", xlate_xTranslateCoordsReq, xlate_xTranslateCoordsReply },
[X_WarpPointer]    { "X_WarpPointer", xlate_xWarpPointerReq, NULL },
[X_SetInputFocus]    { "X_SetInputFocus", xlate_xSetInputFocusReq, NULL },
[X_GetInputFocus]    { "X_GetInputFocus", xlate_xPlainReq, xlate_xGetInputFocusReply },
[X_QueryKeymap]    { "X_QueryKeymap", xlate_xPlainReq, xlate_xQueryKeymapReply },
[X_OpenFont]    { "X_OpenFont", xlate_xOpenFontReq, NULL },
[X_CloseFont]    { "X_CloseFont", xlate_xResourceReq, NULL },
[X_QueryFont]    { "X_QueryFont", xlate_xResourceReq, xlate_xQueryFontReply },
[X_QueryTextExtents]    { "X_QueryTextExtents", xlate_xQueryTextExtentsReq, xlate_xQueryTextExtentsReply },
[X_ListFonts]    { "X_ListFonts", xlate_xListFontsReq, xlate_xListFontsReply },
[X_ListFontsWithInfo]    { "X_ListFontsWithInfo", xlate_xListFontsReq, xlate_xListFontsWithInfoReply },
[X_SetFontPath]    { "X_SetFontPath", xlate_xSetFontPathReq, NULL },
[X_GetFontPath]    { "X_GetFontPath", xlate_xPlainReq, xlate_xGetFontPathReply },
[X_CreatePixmap]    { "X_CreatePixmap", xlate_xCreatePixmapReq, NULL },
[X_FreePixmap]    { "X_FreePixmap", xlate_xResourceReq, NULL },
[X_CreateGC]    { "X_CreateGC", xlate_xCreateGCReq, NULL },
[X_ChangeGC]    { "X_ChangeGC", xlate_xChangeGCReq, NULL },
[X_CopyGC]    { "X_CopyGC", xlate_xCopyGCReq, NULL },
[X_SetDashes]    { "X_SetDashes", xlate_xSetDashesReq, NULL },
[X_SetClipRectangles]    { "X_SetClipRectangles", xlate_xSetClipRectanglesReq, NULL },
[X_FreeGC]    { "X_FreeGC", xlate_xResourceReq, NULL },
[X_ClearArea]    { "X_ClearArea", xlate_xClearAreaReq, NULL },
[X_CopyArea]    { "X_CopyArea", xlate_xCopyAreaReq, NULL },
[X_CopyPlane]    { "X_CopyPlane", xlate_xCopyPlaneReq, NULL },
[X_PolyPoint]    { "X_PolyPoint", xlate_xPolyPointReq, NULL },
[X_PolyLine]    { "X_PolyLine", xlate_xPolyPointReq, NULL },
[X_PolySegment]    { "X_PolySegment", xlate_xPolySegmentReq, NULL },
[X_PolyRectangle]    { "X_PolyRectangle", xlate_xPolySegmentReq, NULL },
[X_PolyArc]    { "X_PolyArc", xlate_xPolySegmentReq, NULL },
[X_FillPoly]    { "X_FillPoly", xlate_xFillPolyReq, NULL },
[X_PolyFillRectangle]    { "X_PolyFillRectangle", xlate_xPolySegmentReq, NULL },
[X_PolyFillArc]    { "X_PolyFillArc", xlate_xPolySegmentReq, NULL },
[X_PutImage]    { "X_PutImage", xlate_xPutImageReq, NULL },
[X_GetImage]    { "X_GetImage", xlate_xGetImageReq, xlate_xGetImageReply },
[X_PolyText8]     { "X_PolyText8", xlate_xPolyTextReq, NULL },
[X_PolyText16]     { "X_PolyText16", xlate_xPolyTextReq, NULL },
[X_ImageText8]     { "X_ImageText8", xlate_xImageTextReq, NULL },
[X_ImageText16]     { "X_ImageText16", xlate_xImageTextReq, NULL },
[X_CreateColormap]    { "X_CreateColormap", xlate_xCreateColormapReq, NULL },
[X_FreeColormap]    { "X_FreeColormap", xlate_xResourceReq, NULL },
[X_CopyColormapAndFree]    { "X_CopyColormapAndFree", xlate_xCopyColormapAndFreeReq, NULL },
[X_InstallColormap]    { "X_InstallColormap", xlate_xResourceReq, NULL },
[X_UninstallColormap]    { "X_UninstallColormap", xlate_xResourceReq, NULL },
[X_ListInstalledColormaps]    { "X_ListInstalledColormaps", xlate_xResourceReq, xlate_xListInstalledColormapsReply },
[X_AllocColor]    { "X_AllocColor", xlate_xAllocColorReq, xlate_xAllocColorReply },
[X_AllocNamedColor]    { "X_AllocNamedColor", xlate_xAllocNamedColorReq, xlate_xAllocNamedColorReply },
[X_AllocColorCells]    { "X_AllocColorCells", xlate_xAllocColorCellsReq, xlate_xAllocColorCellsReply },
[X_AllocColorPlanes]    { "X_AllocColorPlanes", xlate_xAllocColorPlanesReq, xlate_xAllocColorPlanesReply },
[X_FreeColors]    { "X_FreeColors", xlate_xFreeColorsReq, NULL },
[X_StoreColors]    { "X_StoreColors", xlate_xStoreColorsReq, NULL },
[X_StoreNamedColor]    { "X_StoreNamedColor", xlate_xStoreNamedColorReq, NULL },
[X_QueryColors]    { "X_QueryColors", xlate_xQueryColorsReq, xlate_xQueryColorsReply },
[X_LookupColor]    { "X_LookupColor", xlate_xLookupColorReq, xlate_xLookupColorReply },
[X_CreateCursor]    { "X_CreateCursor", xlate_xCreateCursorReq, NULL },
[X_CreateGlyphCursor]    { "X_CreateGlyphCursor", xlate_xCreateGlyphCursorReq, NULL },
[X_FreeCursor]    { "X_FreeCursor", xlate_xResourceReq, NULL },
[X_RecolorCursor]    { "X_RecolorCursor", xlate_xRecolorCursorReq, NULL },
[X_QueryBestSize]    { "X_QueryBestSize", xlate_xQueryBestSizeReq, xlate_xQueryBestSizeReply },
[X_QueryExtension]    { "X_QueryExtension", xlate_xQueryExtensionReq, xlate_xQueryExtensionReply },
[X_ListExtensions]    { "X_ListExtensions", xlate_xPlainReq, xlate_xListExtensionsReply },
[X_ChangeKeyboardMapping]    { "X_ChangeKeyboardMapping", xlate_xChangeKeyboardMappingReq, NULL },
[X_GetKeyboardMapping]    { "X_GetKeyboardMapping", xlate_xGetKeyboardMappingReq, xlate_xGetKeyboardMappingReply },
[X_ChangeKeyboardControl]    { "X_ChangeKeyboardControl", xlate_xChangeKeyboardControlReq, NULL },
[X_GetKeyboardControl]    { "X_GetKeyboardControl", xlate_xPlainReq, xlate_xGetKeyboardControlReply },
[X_Bell]    { "X_Bell", xlate_xBellReq, NULL },
[X_ChangePointerControl]    { "X_ChangePointerControl", xlate_xChangePointerControlReq, NULL },
[X_GetPointerControl]    { "X_GetPointerControl", xlate_xPlainReq, xlate_xGetPointerControlReply },
[X_SetScreenSaver]    { "X_SetScreenSaver", xlate_xSetScreenSaverReq, NULL },
[X_GetScreenSaver]    { "X_GetScreenSaver", xlate_xPlainReq, xlate_xGetScreenSaverReply },
[X_ChangeHosts]    { "X_ChangeHosts", xlate_xChangeHostsReq, NULL },
[X_ListHosts]    { "X_ListHosts", xlate_xListHostsReq, xlate_xListHostsReply },
[X_SetAccessControl]    { "X_SetAccessControl", xlate_xChangeModeReq, NULL },
[X_SetCloseDownMode]    { "X_SetCloseDownMode", xlate_xChangeModeReq, NULL },
[X_KillClient]    { "X_KillClient", xlate_xResourceReq, NULL },
[X_RotateProperties]    { "X_RotateProperties", xlate_xRotatePropertiesReq, NULL },
[X_ForceScreenSaver]    { "X_ForceScreenSaver", xlate_xChangeModeReq, NULL },
[X_SetPointerMapping]    { "X_SetPointerMapping", xlate_xSetPointerMappingReq, xlate_xSetMappingReply },
[X_GetPointerMapping]    { "X_GetPointerMapping", xlate_xPlainReq, xlate_xGetPointerMappingReply },
[X_SetModifierMapping]    { "X_SetModifierMapping", xlate_xSetModifierMappingReq, xlate_xSetMappingReply },
[X_GetModifierMapping]    { "X_GetModifierMapping", xlate_xPlainReq, xlate_xGetModifierMappingReply },
[X_NoOperation]    { "X_NoOperation", xlate_xPlainReq, NULL },
};

static int maxreq = X_NoOperation;

struct memo {
	int reqtype;
	unsigned seq;
	void (*xlatereply)(char *);
	struct memo *next;
};

static struct memo *memos = NULL;

static void
xlate_unknownReply(char *p)
{
	rs_log("waving through reply for unknown request\n");
}

static void
memoreq(int type, unsigned seq, void (*xlatereply)(char *p))
{
	struct memo *m;
	m = xmalloc(sizeof(*m));
	m->reqtype = type;
	m->seq = seq;
	m->xlatereply = xlatereply;
	if (geverbose)
		rs_log("remembering request for sequence number %ld\n", seq);
	LIST_INSERT(memos, m);
}

static void
unknownreq(int type, unsigned seq)
{
	struct memo *m;

	rs_log("unknown request %d\n", type);

	m = xmalloc(sizeof(*m));
	m->reqtype = type;
	m->seq = seq;
	m->xlatereply = xlate_unknownReply;
	LIST_INSERT(memos, m);
}

struct msgstream {
	int fd;
	char *buf;       /* buffer */
	char *hd;        /* first byte of valid data */
	unsigned sz;     /* num valid data bytes */
	unsigned bufsz;  /* bytes in buf */
};

static struct msgstream *
ms_init(int fd)
{
	unsigned long sz = 10240000;
	struct msgstream *ms;
	ms = xmalloc(sizeof(*ms));
	ms->fd = fd;
	ms->buf = xmalloc(sz);
	ms->bufsz = sz;
	ms->hd = ms->buf;
	ms->sz = 0;
	return ms;
}

static void
ms_flush(struct msgstream *ms)
{
	int rv;
	if (ms->sz == 0)
		return;
	rv = xwrite(ms->fd, ms->buf, ms->sz);
	if (0 >= rv)
		assert(0);
	ms->sz = 0;
}

static void
ms_write(struct msgstream *ms, void *buf, int n)
{
	assert(n <= ms->bufsz);
	if (ms->sz+n > ms->bufsz)
		ms_flush(ms);
	memcpy(ms->buf+ms->sz, buf, n);
	ms->sz += n;
}

static int
ms_read(struct msgstream *ms)
{
	int rv;
	if (ms->sz > 0 && ms->hd > ms->buf)
		memmove(ms->buf, ms->hd, ms->sz);
	ms->hd = ms->buf;
	rv = read(ms->fd, ms->buf+ms->sz, ms->bufsz-ms->sz);
	if (0 > rv)
		assert(0);
	ms->sz += rv;
	return rv;
}

struct bigreq {
	xReq req;
	unsigned long len;
};

static xReq *
ms_next(struct msgstream *ms, unsigned long *length)
{
	int rv;
	xReq *req;
	struct bigreq *big;
	unsigned long len;

	/* request header */
	while (ms->sz < sizeof(*req)) {
		rv = ms_read(ms);
		if (!rv) {
			if (ms->sz > 0)
				assert(0); /* incomplete message */
			*length = 0;
			return 0;
		}
	}

	/* request length */
	req = (xReq *)ms->hd;
	if (!req->length) {
		/* big request */
		rs_log("BIG REQUEST\n");
		while (ms->sz < sizeof(*big)) {
			rv = ms_read(ms);
			if (0 >= rv)
				assert(0);
		}
		req = (xReq *)ms->hd;  /* in case of ms_read memmove */
		big = (struct bigreq*)ms->hd;
		len = big->len*4;

		/* delete bigreq length from buffer.  the request
		   translation procedures expect non-bigreqs, but they
		   can handle these trimmed bigreqs because they do
		   not look at the length.  note that we 
		   re-insert the length when we forward the message. */
		memmove(ms->hd+sizeof(*req), ms->hd+sizeof(*big),
			ms->sz-sizeof(*big));
		len -= sizeof(big->len);
	} else {
		big = NULL;
		len = req->length*4;
	}

	/* request body */
	assert(len <= ms->bufsz);
	while (ms->sz < len) {
		rv = ms_read(ms);
		if (0 >= rv)
			assert(0);
		req = (xReq *)ms->hd;  /* in case of ms_read memmove */
	}

	ms->sz -= len;
	ms->hd += len;
	*length = len;
	return req;
}

static void
ms_free(struct msgstream *ms)
{
	if (ms->sz > 0)
		rs_log("WARNING: freeing nonempty msgstream\n");
	free(ms->buf);
	free(ms);
}

/* read, translate, and forward a message from the
   application (A) to the server (S) */
static int
xapp2server(struct msgstream *a, struct msgstream *s)
{
	xReq *req;
	char *p;
	unsigned long len;

	req = ms_next(a, &len);
	if (!req) {
		if (geverbose)
			rs_log("app closed connection\n");
		return 0;
	}

	/* xlate */
	if (req->reqType == cextmajoropcode) {
		xEvictResourceReq *ereq;
		void (*xrep)(char *p);

		ereq = (xEvictResourceReq *)req;

		/* we do not handle the rest yet */
		assert(ereq->evictType == X_EvictResource);
		assert(ereq->res <= ExtRT_COLORMAP);
		ereq->reqType = sextmajoropcode;

		switch (ereq->res) {
		case ExtRT_WINDOW:
			rs_log("Evict window translation\n");
			xrep = xlate_xEvictWindowReply;
			break;
		case ExtRT_GC:
			rs_log("Evict GC translation\n");
			xrep = xlate_xEvictGCReply;
			break;
		case ExtRT_CURSOR:
			rs_log("Evict cursor translation\n");
			xrep = xlate_xEvictCursorReply;
			break;
		default:
			rs_log("Evict gen translation\n");
			xrep = xlate_xEvictGenReply;
			break;
		}
		memoreq(ereq->reqType, xlateseq, xrep);
	} else if (req->reqType > maxreq
		   || !reqdispatch[req->reqType].xlate_req)
		unknownreq(req->reqType, xlateseq);
	else {
		if (geverbose)
			rs_log("sending request (%s) seq=%d\n",
			       reqdispatch[req->reqType].reqname,
			       xlateseq);
		reqdispatch[req->reqType].xlate_req((char*)req);
		if (reqdispatch[req->reqType].xlate_rep)
			memoreq(req->reqType, xlateseq,
				reqdispatch[req->reqType].xlate_rep);
	}

	/* forward and clean up */
	if (!req->length) {
		/* big request */
		unsigned long reallen;
		rs_log("BIG REQUEST\n");
		ms_write(s, req, sizeof(*req));
		reallen = len+sizeof(len);
		reallen >>= 2;
		ms_write(s, &reallen, sizeof(reallen));
		p = (char*)req;
		ms_write(s, p+sizeof(*req), len-sizeof(*req));
	} else
		ms_write(s, req, len);
	xlateseq++;

	return 1; /* not closed */
}

static int
app2server(struct msgstream *a, struct msgstream *s)
{
	int rv;
	int num = 0;
	int ret = 1;  /* not closed */

	/* the application descriptor is ready for reading.  consume
	   and process a multi-message chunk. */
	do {
		num++;
		rv = xapp2server(a, s);
		if (!rv) {
			ret = 0; /* closed */
			break;
		}
	} while (a->sz > 0);
	ms_flush(s);
	return ret;
}

static int
cmpseq(struct memo *m, unsigned seq)
{
	return m->seq != seq;
}

static int
replyretire(struct memo *m, xGenericReply *rep)
{
	xListFontsWithInfoReply *frep;
	if (m->reqtype != X_ListFontsWithInfo)
		return 1;
	frep = (xListFontsWithInfoReply *)rep;
	if (!frep->nameLength)
		return 1;
	return 0;
}

static void
doreply(int s, int a, char *p)
{
	struct memo *m;
	xGenericReply *rep = (xGenericReply *)p;
	char *rest;
	int rv, len;
	char *reqname;

	rest = NULL;

	/* get the rest of the reply */
	len = rep->length*4;
	if (len > 0) {
		rest = xmalloc(len+sizeof(*rep));
		memcpy(rest, rep, sizeof(*rep));
		rep = (xGenericReply *)rest;
		rv = xread(s, rest+sizeof(*rep), len);
		if (0 >= rv)
			assert(0);
	}
	LIST_FIND(memos, cmpseq, rep->sequenceNumber, &m);
	if (!m) {
		rs_log("reply without a sequence number (%d)\n",
			rep->sequenceNumber);
	} else {
		unsigned long oldseq;
		if (m->reqtype <= X_NoOperation && reqdispatch[m->reqtype].reqname)
			reqname = reqdispatch[m->reqtype].reqname;
		else
			reqname = "Unknown request";
		oldseq = rep->sequenceNumber;
		m->xlatereply((char*)rep);
		if (replyretire(m, rep))
			LIST_REMOVE(memos, m);
		if (geverbose)
			rs_log("sending %d byte reply (%s) for seq %ld -> %ld\n",
			       len+sizeof(*rep), reqname, oldseq, rep->sequenceNumber);
	}
	rv = xwrite(a, rep, len+sizeof(*rep));
	if (0 >= rv)
		assert(0);
	if (rest)
		free(rest);
}

static char *errs[] = {
[Success] "Success",
[BadRequest] "BadRequest",
[BadValue] "BadValue",
[BadWindow] "BadWindow",
[BadPixmap] "BadPixmap",
[BadAtom] "BadAtom",
[BadCursor] "BadCursor",
[BadFont] "BadFont",
[BadMatch] "BadMatch",
[BadDrawable] "BadDrawable",
[BadAccess] "BadAccess",
[BadAlloc] "BadAlloc",
[BadColor] "BadColor",
[BadGC] "BadGC",
[BadIDChoice] "BadIDChoice",
[BadName] "BadName",
[BadLength] "BadLength",
[BadImplementation] "BadImplementation",
};

static char *
xerr2str(int err)
{
	if (err < 0 || err > BadImplementation)
		return "unknown error";
	return errs[err];
}

/* read, translate, and forward a message from the
   server (S) to the application (A) */
static void
server2app(int s, int a)
{
	int rv;
	int drop;
	xError *err;

	char buf[32];
	rv = xread(s, buf, sizeof(buf));
	if (0 == rv) {
		rs_log("server closed connection\n");
		exit(1);
	}
	if (0 > rv)
		assert(0);
	switch (buf[0]) {
	case X_Error:
		err = (xError*)buf;
		rs_log("got an error (%s id = %lx seq = %lx)!\n",
			xerr2str(err->errorCode),
			(unsigned long)(err->resourceID),
			(unsigned long)(err->sequenceNumber));
		xlate_xError(buf);
		rv = xwrite(a, buf, sizeof(buf));
		if (0 >= rv)
			assert(0);
		return;
		break;
	case X_Reply:
		doreply(s, a, buf);
		break;
	default:
		if (geverbose)
			rs_log("sending event\n");
		xlate_xEvent(buf, REVERSE, &drop);
#if 0
		if (!drop) {
			rv = xwrite(a, buf, sizeof(buf));
			if (0 >= rv)
				assert(0);
		}
#else
		/* FIXME: What's going on here? */
		rv = xwrite(a, buf, sizeof(buf));
		if (0 >= rv)
			assert(0);
#endif
		break;
	}
}

static void
consumeserver(int s)
{
	int rv;
	char buf[32];
	xError *err;
	int notused;

	rv = xread(s, buf, sizeof(buf));
	if (0 == rv) {
		rs_log("server closed connection\n");
		exit(1);
	}
	if (0 > rv)
		assert(0);
	switch (buf[0]) {
	case X_Error:
		err = (xError*)buf;
		rs_log("got an error (%s id = %lx seq = %lx)!\n",
			xerr2str(err->errorCode),
			(unsigned long)(err->resourceID),
			(unsigned long)(err->sequenceNumber));
		xlate_xError(buf);
		break;
	case X_Reply:
		if (geverbose)
			rs_log("got a reply!\n");
		break;
	default:
		if (geverbose)
			rs_log("got an event!\n");
		xlate_xEvent(buf, REVERSE, &notused);
		break;
	}
}

#define XMAX(x,y)  ((x)>(y)?(x):(y))

void
doxlate2(int a, int s, struct sctl *sctl)
{
	fd_set fds;
	int rv, max;
	struct msgstream *ms_a, *ms_s;

	ms_a = ms_init(a);
	ms_s = ms_init(s);
	max = XMAX(a,s);
	while (1) {
		FD_ZERO(&fds);
		FD_SET(s, &fds);
		FD_SET(a, &fds);
		/* call select directly.  some apps (e.g., netscape)
		   redefine select with unpleasant side effects.
		   on linux, the select we want is __newselect. */
		rv = syscall(SYS__newselect, 1+max, &fds, NULL, NULL, NULL);
		if (0 > rv && errno == EINTR)
			continue;
		if (0 > rv) {
			perror("select");
			assert(0);
		}
		if (FD_ISSET(s, &fds))
			server2app(s, a);
		if (FD_ISSET(a, &fds)) {
			rv = app2server(ms_a, ms_s);
			if (!rv) {
				ms_free(ms_a);
				ms_free(ms_s);
				exit(0);
			}
		}
	}
}

static fd_set fds; /* off the stack */

void
doxlate1(int a, int s, struct sctl *sctl)
{
	int rv, max;
	struct msgstream *ms_a, *ms_s;

	initmap(sctl);
	ms_a = ms_init(a);
	ms_s = ms_init(s);
	max = XMAX(a,s);
	while (1) {
		FD_ZERO(&fds);
		FD_SET(s, &fds);
		FD_SET(a, &fds);
		/* call select directly.  some apps (e.g., netscape)
		   redefine select with unpleasant side effects.
		   on linux, the select we want is __newselect. */
		rv = syscall(SYS__newselect, 1+max, &fds, NULL, NULL, NULL);
		if (0 > rv && errno == EINTR)
			continue;
		if (0 > rv) {
			perror("select");
			assert(0);
		}
		if (FD_ISSET(s, &fds))
			consumeserver(s);
		if (FD_ISSET(a, &fds)) {
			rv = app2server(ms_a, ms_s);
			if (!rv) {
				ms_free(ms_a);
				ms_free(ms_s);
				return;
			}
		}
	}
}
