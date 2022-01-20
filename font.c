#include <stdio.h>
#include <stdlib.h>
#include <X11/Xproto.h>

#include "rt.h"

enum {
	MAXNAMES = 65535
};
static unsigned numfonts;
static char *fontnames[MAXNAMES];
static XFontStruct fontinfo[MAXNAMES];

#if 0
static void
printfontinfo(XFontStruct *in)
{
	rs_log("[%d-%d] [%d-%d] (%d,%d) (%d,%d,%d,%d,%d) (%d,%d,%d,%d,%d)\n",
		in[0].min_byte1, in[0].max_byte1,
		in[0].min_char_or_byte2, in[0].max_char_or_byte2,
		in[0].ascent, in[0].descent, 
		in[0].min_bounds.lbearing,
		in[0].min_bounds.rbearing,
		in[0].min_bounds.width,
		in[0].min_bounds.ascent,
		in[0].min_bounds.descent,
		in[0].max_bounds.lbearing,
		in[0].max_bounds.rbearing,
		in[0].max_bounds.width,
		in[0].max_bounds.ascent,
		in[0].max_bounds.descent);
}
#endif

void
printfonts()
{
	int i;
	for (i = 0; i < numfonts; i++) {
		rs_log("%s\n", fontnames[i]);
	}
}

static size_t
xfread(void *p, size_t size, size_t n, FILE *fp)
{
	size_t rv;
	rv = fread(p, size, n, fp);
	assert(rv == n);
	return rv;
}

static size_t
xfwrite(void *p, size_t size, size_t n, FILE *fp)
{
	size_t rv;
	rv = fwrite(p, size, n, fp);
	assert(rv == n);
	return rv;
}

static void
savefontinfo(char *filename)
{
	int i;
	FILE *fp;
	XFontStruct *fs;
	int len;

	fp = fopen(filename, "w");
	assert(fp);

	xfwrite(&numfonts, sizeof(numfonts), 1, fp);
	for (i = 0; i < numfonts; i++) {
		fs = &fontinfo[i];
		xfwrite(fs, sizeof(*fs), 1, fp);
		xfwrite(fs->properties, sizeof(XFontProp), fs->n_properties, fp);
		len = strlen(fontnames[i])+1;
		xfwrite(&len, sizeof(len), 1, fp);
		xfwrite(fontnames[i], len, 1, fp);
	}
	fclose(fp);
}

static int
loadfontinfo(char *filename)
{
	int i;
	FILE *fp;
	XFontStruct *fs;
	int len;

	fp = fopen(filename, "r");
	if (!fp)
		return -1;
	
	xfread(&numfonts, sizeof(numfonts), 1, fp);
	for (i = 0; i < numfonts; i++) {
		fs = &fontinfo[i];
		xfread(fs, sizeof(*fs), 1, fp);
		fs->properties = xmalloc(fs->n_properties*sizeof(XFontProp));
		xfread(fs->properties, sizeof(XFontProp), fs->n_properties, fp);
		xfread(&len, sizeof(len), 1, fp);
		fontnames[i] = xmalloc(len);
		xfread(fontnames[i], len, 1, fp);
	}
	fclose(fp);
	return 0;
}

static void
xinitfont(struct sctl *sctl)
{
	xListFontsWithInfoReq req;
	xListFontsWithInfoReply rep;
	char *p, padbuf[3];
	int len, pad, rv;
	static char *pattern = "*";
	XFontStruct *fs;

	/* send ListFontsWithInfo request */
	bzero(&req, sizeof(req));
	req.reqType = X_ListFontsWithInfo;
	req.maxNames = MAXNAMES;
	req.nbytes = strlen(pattern);
	len = sizeof(req)+req.nbytes;
	pad = xpad(len);
	req.length = (len+pad)>>2;
	if (-1 == xwrite(sctl->xsock, &req, sizeof(req)))
		assert(0);
	if (-1 == xwrite(sctl->xsock, pattern, strlen(pattern)))
		assert(0);
	if (pad && -1 == xwrite(sctl->xsock, padbuf, pad))
		assert(0);

	/* process series of replies */
	numfonts = 0;
	while (1) {
#if 0
		if (!(numfonts%50))
			rs_log(".");
#endif

		fs = &fontinfo[numfonts];
		p = (char*)&rep;
		readxreplyignore(sctl->xsock, p);
		p += sizeof(xReply);
		rv = xread(sctl->xsock, p, sizeof(rep)-sizeof(xReply));
		if (0 >= rv)
			assert(0);

		if (rep.nameLength == 0)
			/* last (content-free) font record */
			break;

		fs->direction = rep.drawDirection;
		fs->min_char_or_byte2 = rep.minCharOrByte2;
		fs->max_char_or_byte2 = rep.maxCharOrByte2;
		fs->min_byte1 = rep.minByte1;
		fs->max_byte1 = rep.maxByte1;
		fs->all_chars_exist = rep.allCharsExist;
		fs->default_char = rep.defaultChar;
		fs->n_properties = rep.nFontProps;
		fs->ascent = rep.fontAscent;
		fs->descent = rep.fontDescent;
		memcpy(&fs->min_bounds, &rep.minBounds, sizeof(fs->min_bounds));
		memcpy(&fs->max_bounds, &rep.maxBounds, sizeof(fs->max_bounds));
		fs->properties = xmalloc(rep.nFontProps*sizeof(XFontProp));
		rv = xread(sctl->xsock, fs->properties, rep.nFontProps*sizeof(XFontProp));
		if (0 >= rv)
			assert(0);

		fontnames[numfonts] = xmalloc(1+rep.nameLength);
		rv = xread(sctl->xsock, fontnames[numfonts], rep.nameLength);
		if (0 >= rv)
			assert(0);
		if (xpad(rep.nameLength)) {
			rv = xread(sctl->xsock, padbuf, xpad(rep.nameLength));
			if (0 >= rv)
				assert(0);
		}
		numfonts++;
	}
#if 0
	rs_log("\n");
#endif
}

void
initfont(struct sctl *sctl)
{
	static char *filename = "/tmp/fonts";

#if 1
	if (0 == loadfontinfo(filename))
		return;
	xinitfont(sctl);
	savefontinfo(filename);
#else
	xinitfont(sctl);
#endif
}

void
getfont(struct sctl *sctl, unsigned long id, XFontStruct *fs, unsigned long *nchar)
{
	xResourceReq req;
	xQueryFontReply rep;
	char *p;
	int rv;

	bzero(&req, sizeof(req));
	req.reqType = X_QueryFont;
	req.length = sizeof(req)>>2;
	req.id = id;

	if (-1 == xwrite(sctl->xsock, &req, sizeof(req)))
		assert(0);
	p = (char*)&rep;
	readxreply(sctl->xsock, p);
	p += sizeof(xReply);
	rv = xread(sctl->xsock, p, sizeof(rep)-sizeof(xReply));
	if (0 >= rv)
		assert(0);

	fs->fid = id;
	fs->direction = rep.drawDirection;
	fs->min_char_or_byte2 = rep.minCharOrByte2;
	fs->max_char_or_byte2 = rep.maxCharOrByte2;
	fs->min_byte1 = rep.minByte1;
	fs->max_byte1 = rep.maxByte1;
	fs->all_chars_exist = rep.allCharsExist;
	fs->default_char = rep.defaultChar;
	fs->n_properties = rep.nFontProps;
	fs->ascent = rep.fontAscent;
	fs->descent = rep.fontDescent;
	memcpy(&fs->min_bounds, &rep.minBounds, sizeof(fs->min_bounds));
	memcpy(&fs->max_bounds, &rep.maxBounds, sizeof(fs->max_bounds));
	fs->properties = xmalloc(rep.nFontProps*sizeof(XFontProp));
	rv = xread(sctl->xsock, fs->properties, rep.nFontProps*sizeof(XFontProp));
	if (0 >= rv)
		assert(0);
	fs->per_char = xmalloc(rep.nCharInfos*sizeof(XCharStruct));
	*nchar = rep.nCharInfos;
	rv = xread(sctl->xsock, fs->per_char, rep.nCharInfos*sizeof(XCharStruct));
	if (0 > rv)
		assert(0);
}

static int
checkmatch(struct sctl *sctl, XFontStruct *fs, char *fontname)
{
	xOpenFontReq oreq;
	xResourceReq creq;
	char pad[3];
	int rv, len, i;
	unsigned long tid, nchar;
	XFontStruct ifs;
	int result = 0;

	tid = nextid(sctl);
	len = strlen(fontname);
	oreq.reqType = X_OpenFont;
	oreq.length = (sizeof(oreq)+len+xpad(len))>>2;
	oreq.nbytes = len;
	oreq.fid = tid;
	rv = xwrite(sctl->xsock, &oreq, sizeof(oreq));
	if (0 >= rv)
		assert(0);
	rv = xwrite(sctl->xsock, fontname, len);
	if (0 >= rv)
		assert(0);
	if (xpad(len)) {
		rv = xwrite(sctl->xsock, pad, xpad(len));
		if (0 >= rv)
			assert(0);
	}

	getfont(sctl, tid, &ifs, &nchar);
	for (i = 0; i < nchar; i++) {
		if (! (fs->per_char[i].lbearing == ifs.per_char[i].lbearing
		       && fs->per_char[i].rbearing == ifs.per_char[i].rbearing
		       && fs->per_char[i].width == ifs.per_char[i].width
		       && fs->per_char[i].ascent == ifs.per_char[i].ascent
		       && fs->per_char[i].descent == ifs.per_char[i].descent))
			goto out;
	}
	result = 1;
out:
	free(ifs.properties);
	free(ifs.per_char);
	creq.reqType = X_CloseFont;
	creq.id = tid;
	creq.length = sizeof(creq)>>2;
	rv = xwrite(sctl->xsock, &creq, sizeof(creq));
	if (0 >= rv)
		assert(0);
	return result;
}

char *
matchfont(struct sctl *sctl, XFontStruct *fs)
{
	int i;
	for (i = 0; i < numfonts; i++) {
		if (fontinfo[i].min_char_or_byte2 == fs->min_char_or_byte2
		    && fontinfo[i].max_char_or_byte2 == fs->max_char_or_byte2
		    && fontinfo[i].min_byte1 == fs->min_byte1
		    && fontinfo[i].max_byte1 == fs->max_byte1
		    && fontinfo[i].all_chars_exist == fs->all_chars_exist
		    && !memcmp(&fontinfo[i].min_bounds, &fs->min_bounds, sizeof(fontinfo[i].min_bounds))
		    && !memcmp(&fontinfo[i].max_bounds, &fs->max_bounds, sizeof(fontinfo[i].max_bounds))
		    && fontinfo[i].ascent == fs->ascent
		    && fontinfo[i].descent == fs->descent
		    && checkmatch(sctl, fs, fontnames[i]))
			return fontnames[i];
	}
	return NULL;
}


void
finifont()
{
	if (!fontnames)
		return;
	free(fontnames);
	free(fontinfo);
}
