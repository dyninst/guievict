/*
Section "Files"
    ...
    # If we're adding our own module path,
    # we need to add the system path, too.
    ModulePath "/usr/X11R6/lib/modules"
    ModulePath "/home/vic/src/ge"
EndSection

Section "Module"
    ...
    Load  "evict"
EndSection
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "os.h"
#include "osdep.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "xf86Module.h"
#include "resource.h"
#include "pixmap.h"
#include "pixmapstr.h"
#include "window.h"
#include "windowstr.h"
#include "gcstruct.h"
#include "cursorstr.h"
#include "servermd.h"
#include "fonts/fontstruct.h"

#include "ext.h"
#include "refun.h"

static XF86ModuleVersionInfo vers = {
	"evict",
	"zandy",
	MODINFOSTRING1,
	MODINFOSTRING2,
	XF86_VERSION_CURRENT,
	0,
	0,
	0,
	ABI_CLASS_NONE,
	0,
	MOD_CLASS_NONE,
	{ 0 },
};

static ExtensionEntry *extentry;

static int
EvictNop(ClientPtr client)
{
	xEvictNopReply rep;
	int n;

	REQUEST_SIZE_MATCH(xEvictNopReq);
	rep.type = X_Reply;
	rep.length = 0;
	rep.sequenceNumber = client->sequence;
	rep.reply = ExtMAGIC;
	if (client->swapped) {
		swaps(&rep.sequenceNumber, n);
		swapl(&rep.length, n);
	}
	WriteToClient(client, sizeof(xEvictNopReply), (char *)&rep);
	return(client->noClientException);
}

static int
SEvictNop(ClientPtr client)
{
	int n;
	REQUEST(xEvictNopReq);
	swaps(&stuff->length, n);
	return EvictNop(client);
}

typedef void (*fcrbt)(ClientPtr, RESTYPE, FindResType, pointer);
static fcrbt findclientresbytype = NULL;

enum {
	MAXRES = 1024,
};

struct resvec {
	ClientPtr client;
	union resu u[MAXRES];
	unsigned long max;
	unsigned long cnt;
	int res;
};

static void
extract_pixmap(pointer value, XID id, pointer data)
{
	PixmapPtr pmap = (PixmapPtr)value;
	struct resvec *vec = (struct resvec *)data;
	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	vec->u[vec->cnt++].xid = pmap->drawable.id;
}

static void
extract_window(pointer value, XID id, pointer data)
{
	WindowPtr p = (WindowPtr)value;
	struct windowx *wx;
	struct resvec *vec = (struct resvec *)data;
	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	wx = &vec->u[vec->cnt++].winx;
	wx->id = p->drawable.id;
	wx->mapped = p->mapped;
	wx->backgroundState = (unsigned long)p->backgroundState;
	wx->bgpixel = (unsigned long)p->background.pixel;
	if (p->backgroundState == BackgroundPixmap)
		wx->bgpm = (unsigned long)(p->background.pixmap->drawable.id);
}

struct fontid {
	unsigned long id;
	FontPtr fp;
};

struct fontvec {
	struct fontid fontid[1024];
	int numfont;
};

static void
getfont(pointer value, XID id, pointer data)
{
	struct fontvec *fvec = (struct fontvec *)data;
	if (fvec->numfont >= 1024)
		FatalError("Out of GUIEVICT resources\n");
	fvec->fontid[fvec->numfont].id = id;
	fvec->fontid[fvec->numfont].fp = (FontPtr)value;
	fvec->numfont++;
}

static void
extract_gc(pointer value, XID id, pointer data)
{
	GC *p = (GC*)value;
	struct resvec *vec = (struct resvec *)data;
	struct _XGCValues *v;
	struct fontvec fvec;
	int i;

	/* get fontptr->fontid mapping: yes, hideous */
	fvec.numfont = 0;
	findclientresbytype(vec->client, RT_FONT, getfont, &fvec);

	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	
	vec->u[vec->cnt].gc.id = id;
	vec->u[vec->cnt].gc.depth = p->depth;
	v = &vec->u[vec->cnt++].gc.values;
	v->function = p->alu;
	v->plane_mask = p->planemask;
	v->foreground = p->fgPixel;
	v->background = p->bgPixel;
	v->line_width = p->lineWidth;
	v->line_style = p->lineStyle;
	v->cap_style = p->capStyle;
	v->join_style = p->joinStyle;
	v->fill_style = p->fillStyle;
	v->fill_rule = p->fillRule;
	v->arc_mode = p->arcMode;
	v->subwindow_mode = p->subWindowMode;
	v->graphics_exposures = p->graphicsExposures;
	v->stipple = p->stipple->drawable.id;
	v->tile_is_pixel = p->tileIsPixel;
	if (p->tileIsPixel)
		v->tilepx = p->tile.pixel;
	else
		v->tilepm = p->tile.pixmap->drawable.id;
	v->font = 0;
	for (i = 0; i < fvec.numfont; i++)
		if (fvec.fontid[i].fp == p->font) {
			v->font = fvec.fontid[i].id;
			break;
		}
#if 0
	v->dash_offset = p->dashOffset;
	v->dashes = x;
	v->clip_mask = x;
	v->clip_x_origin = x;
	v->clip_y_origin = x;
	v->ts_x_origin = x;
	v->ts_y_origin = x;
#endif
}

static void
extract_font(pointer value, XID id, pointer data)
{
	struct resvec *vec = (struct resvec *)data;
	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	vec->u[vec->cnt++].xid = id;
}

static void
extract_cursor(pointer value, XID id, pointer data)
{
	CursorPtr p = (CursorPtr)value;
	struct resvec *vec = (struct resvec *)data;
	struct cursor *c;

	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	c = &vec->u[vec->cnt++].cursor;

	c->id = id;
	c->width = p->bits->width;
	c->height = p->bits->height;
	c->xhot = p->bits->xhot;
	c->yhot = p->bits->yhot;
	c->fore_red = p->foreRed;
	c->fore_blue = p->foreBlue;
	c->fore_green = p->foreGreen;
	c->back_red = p->backRed;
	c->back_blue = p->backBlue;
	c->back_green = p->backGreen;
	c->emptymsk = p->bits->emptyMask;
	c->nbytes = BitmapBytePad(c->width)*c->height;
	if (c->nbytes > sizeof(c->src))
		FatalError("Large cursor\n");

	memcpy(c->src, p->bits->source, BitmapBytePad(c->width)*c->height);
	/* when there is no mask, regenerate with None for mask pixmap */
	if (!c->emptymsk)
		memcpy(c->msk, p->bits->mask, BitmapBytePad(c->width)*c->height);
}

static void
extract_colormap(pointer value, XID id, pointer data)
{
	struct resvec *vec = (struct resvec *)data;
	if (vec->cnt >= vec->max)
		FatalError("Out of GUIEVICT resources\n");
	vec->u[vec->cnt++].xid = id;
}

static void
extract_unimpl(pointer value, XID id, pointer data)
{
	FatalError("Unimplemented resource request\n");
}

struct rt {
	int xrt;
	void (*extract)(pointer value, XID id, pointer data);
};

/* FIXME:
   Probably each of these extract functions
   needs to be swapped-aware in their returned xids */
static struct rt rt[] = {
	{ RT_WINDOW,            extract_window },
	{ RT_PIXMAP,            extract_pixmap },
	{ RT_GC,                extract_gc     },
	{ RT_FONT,              extract_font   },
	{ RT_CURSOR,            extract_cursor },
	{ RT_COLORMAP,          extract_colormap },
	{ RT_CMAPENTRY,         extract_unimpl },
	{ RT_OTHERCLIENT,       extract_unimpl },
	{ RT_PASSIVEGRAB,       extract_unimpl },
};

static int
EvictResource(ClientPtr client)
{
	int n;
	struct resvec vec;
	xEvictResourceReply rep;

	REQUEST(xEvictResourceReq);
	if (stuff->res >= ExtRT_NUM)
		return BadMatch;
	vec.max = MAXRES;
	vec.cnt = 0;
	vec.res = stuff->res;
	vec.client = client;
	findclientresbytype(client, rt[stuff->res].xrt,
			    rt[stuff->res].extract, &vec);
	REQUEST_SIZE_MATCH(xEvictResourceReq);
	rep.type = X_Reply;
	rep.length = (vec.cnt*sizeof(vec.u[0]))>>2;
	rep.sequenceNumber = client->sequence;
	rep.num = vec.cnt;
	if (client->swapped) {
		swaps(&rep.sequenceNumber, n);
		swapl(&rep.length, n);
		swapl(&rep.num, n);
	}
	WriteToClient(client, sizeof(xEvictResourceReply), (char *)&rep);
	WriteToClient(client, vec.cnt*sizeof(vec.u[0]), (char*)vec.u);
	return(client->noClientException);
}

static int
SEvictResource(ClientPtr client)
{
	int n;
	REQUEST(xEvictResourceReq);
	swaps(&stuff->length, n);
	swapl(&stuff->res, n);
	return EvictResource(client);
}

static int
EvictConn(ClientPtr client)
{
	xEvictConnReply rep;
	int fd, n;
	ClientPtr c;
	struct stat statbuf;
	unsigned long cs[MAXCLIENTS];
	int i, num = 0;

	for (i = 0; i < currentMaxClients; i++) {
		c = clients[i];
		if (c == NullClient)
			continue;
		if (!c->osPrivate ||
		    !((OsCommPtr)c->osPrivate)->fd)
			continue;
		fd = ((OsCommPtr)c->osPrivate)->fd;
		if (0 > fstat(fd, &statbuf))
			continue;
		if (num >= MAXCLIENTS)
			break;
		cs[num++] = statbuf.st_ino;
	}

	rep.type = X_Reply;
	rep.length = num;
	rep.sequenceNumber = client->sequence;

	if (client->swapped) {
		swaps(&rep.sequenceNumber, n);
		swapl(&rep.length, n);
		swapl(&rep.inode, n);
	}
	WriteToClient(client, sizeof(xEvictConnReply), (char *)&rep);
	WriteToClient(client, 4*num, (char*)cs);
	return(client->noClientException);
}

static int
SEvictConn(ClientPtr client)
{
	int n;
	REQUEST(xEvictConnReq);
	swaps(&stuff->length, n);
	swapl(&stuff->id, n);
	return EvictConn(client);
}

static int
ExtDispatch(ClientPtr client)
{
	REQUEST(xReq);
	switch (stuff->data) {
	case X_EvictNop:
		return EvictNop(client);
	case X_EvictResource:
		return EvictResource(client);
	case X_EvictConn:
		return EvictConn(client);
	default:
		return BadRequest;
	}
}

static int
SExtDispatch(ClientPtr client)
{
	REQUEST(xReq);
	switch (stuff->data) {
	case X_EvictNop:
		return SEvictNop(client);
	case X_EvictResource:
		return SEvictResource(client);
	case X_EvictConn:
		return SEvictConn(client);
	default:
		return BadRequest;
	}
}

static void
ExtResetProc(ExtensionEntry *entry)
{
}

static void
extinit()
{
	struct modulelist *ml;
	ml = rf_parse(getpid());
	if (!ml)
		FatalError("Cannot parse myself\n");
	if (!findclientresbytype)
		findclientresbytype = (fcrbt) rf_find_function(ml, "FindClientResourcesByType");
	if (!findclientresbytype)
		FatalError("Cannot find FindClientResourcesByType\n");
	rf_free_modulelist(ml);
	extentry = AddExtension(EXTNAME,
				ExtNumberEvents,
				ExtNumberErrors,
				ExtDispatch,
				SExtDispatch,
				ExtResetProc,
				StandardMinorOpcode);
	if (!extentry)
		FatalError("Could not add GUIEVICT\n");
}

ExtensionModule extension = {
	extinit,
	EXTNAME,
	NULL,
	NULL,
	NULL
};

static pointer
setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	LoadExtension(&extension, FALSE);
	return (pointer)1;
}

XF86ModuleData evictModuleData = {
	&vers,
	setup,
	NULL
};
