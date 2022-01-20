#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xproto.h>
#include <X11/Xauth.h>
#include <assert.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "rt.h"

/* read a generic X reply; barf on events and errors */
void
readxreply(int fd, char *rep)
{
	int rv;

	rv = xread(fd, rep, sizeof(xReply));
	if (0 >= rv)
		assert(0);
	if (rep[0] == X_Reply)
		return;
	assert(0);
}

/* read a generic X reply; barf on errors; ignore events */
void
readxreplyignore(int fd, char *rep)
{
	int rv;

	while (1) {
		rv = xread(fd, rep, sizeof(xReply));
		if (0 >= rv)
			assert(0);
		if (rep[0] == X_Reply)
			return;
		if (rep[0] == X_Error)
			assert(0);
		rs_log("ignoring event %d\n", rep[0]);
	}
}

int
disp_to_saddr(char *idisp, struct sockaddr_in *addr)
{
	char *num, *p;
	char *disp;
	int rv = -1;

	disp = xstrdup(idisp);
	num = strchr(disp, ':');
	if (!num)
		goto out;
	*num++ = '\0';
	p = strchr(num, '.');
	if (p)
		*p = '\0'; /* chuck screen */
	addr->sin_port = htons(6000+atoi(num));
	if (*disp)
		rv = parse_addr(disp, &addr->sin_addr);
	else {
		addr->sin_addr.s_addr = inet_addr("127.0.0.1");
		rv = 0;
	}
out:
	free(disp);
	return rv;
}

int
xauth(char *disp, char **type, char **data, int *len)
{
	Xauth *xau;
	struct sockaddr_in addr;
	char buf[10];

	*type = *data = NULL;
	*len = 0;
	if (0 > disp_to_saddr(disp, &addr))
		return -1;
	snprintf(buf, sizeof(buf), "%d", ntohs(addr.sin_port)-6000);
	xau = XauGetAuthByAddr(FamilyWild, sizeof(addr.sin_addr.s_addr),
			       (char*)&addr.sin_addr.s_addr,
			       strlen(buf), buf, 0, NULL);
	if (!xau)
		return -1;
	*type = xmalloc(xau->name_length+1);
	memcpy(*type, xau->name, xau->name_length);
	*data = xmalloc(xau->data_length);
	memcpy(*data, xau->data, xau->data_length);
	*len = xau->data_length;
	XauDisposeAuth(xau);
	return 0;
}

static int
nodelay(int sd, int optval)
{
	if (0 > setsockopt(sd, SOL_TCP, TCP_NODELAY,
			   &optval, sizeof(optval)))
		return -1;
	return 0;
}

typedef struct {
	CARD8 reqType;
	CARD8 opcode;
	CARD16 length;
} xBigReqEnableReq;

typedef struct {
	BYTE type;
	CARD8 pad;
	CARD16 sequenceNumber;
	CARD32 length;
	CARD32 maxRequestLength;
	CARD32 pad1;
	CARD32 pad2;
	CARD32 pad3;
	CARD32 pad4;
	CARD32 pad5;
} xBigReqEnableReply;

static void
initbigreq(int s, struct xcon *xcon)
{
	xQueryExtensionReq qreq;
	xQueryExtensionReply qrep;
	xBigReqEnableReq ereq;
	xBigReqEnableReply erep;
	char pad[3];

	static char *name = "BIG-REQUESTS";
	int rv, nlen = strlen(name);
	
	qreq.reqType = X_QueryExtension;
	qreq.length = (sizeof(qreq)+nlen+xpad(nlen))>>2;
	qreq.nbytes = nlen;
	rv = xwrite(s, &qreq, sizeof(qreq));
	if (0 >= rv)
		assert(0);
	rv = xwrite(s, name, nlen);
	if (0 >= rv)
		assert(0);
	if (xpad(nlen) > 0) {
		rv = xwrite(s, pad, xpad(nlen));
		if (0 >= rv)
			assert(0);
	}
	readxreply(s, (char*)&qrep);
	if (!qrep.present) {
		rs_log("I need BIG-REQUESTS but it's not present\n");
		assert(0);
	}

	ereq.reqType = qrep.major_opcode; /* big request major opcode */
	ereq.opcode = 0;
	ereq.length = sizeof(ereq)>>2;
	rv = xwrite(s, &ereq, sizeof(ereq));
	if (0 >= rv)
		assert(0);
	readxreply(s, (char*)&erep);
}

static void
initvicext(int s, struct xcon *xcon)
{
	xQueryExtensionReq qreq;
	xQueryExtensionReply qrep;
	char pad[3];

	static char *name = "GUIEVICT";
	int rv, nlen = strlen(name);
	
	qreq.reqType = X_QueryExtension;
	qreq.length = (sizeof(qreq)+nlen+xpad(nlen))>>2;
	qreq.nbytes = nlen;
	rv = xwrite(s, &qreq, sizeof(qreq));
	if (0 >= rv)
		assert(0);
	rv = xwrite(s, name, nlen);
	if (0 >= rv)
		assert(0);
	if (xpad(nlen) > 0) {
		rv = xwrite(s, pad, xpad(nlen));
		if (0 >= rv)
			assert(0);
	}
	readxreply(s, (char*)&qrep);
	if (!qrep.present) {
		rs_log("I need the GUIEVICT extension but it's not present\n");
		assert(0);
	}
	xcon->vicmajor = qrep.major_opcode;
}

/* P points to the first byte after the xConnSetup from the server */
static void
parse_visuals(char *p, struct xcon *xcon)
{
	char *oldp;
	xWindowRoot *root;
	int i, j, k, maxvis;
	
	if (xcon->setup.numRoots > 1)
		rs_log("warning: multiple screens, using the first one\n");

	maxvis = 0;
	root = (xWindowRoot *)p;
	xcon->rootid = root->windowId;
	xcon->defaultcmap = root->defaultColormap;
	xcon->rootvis = root->rootVisualID;
	xcon->rootdepth = root->rootDepth;

	p += sizeof(*root);
	oldp = p;
	for (i = 0; i < root->nDepths; i++) {
		xDepth *xd;
		xd = (xDepth*)p;
		p += sizeof(*xd);
		for (j = 0; j < xd->nVisuals; j++) {
			xVisualType *xv;
			xv = (xVisualType *)p;
			p += sizeof(*xv);
			maxvis++;
		}
	}
	
	xcon->vis = xmalloc(maxvis*sizeof(*xcon->vis));
	xcon->num_vis = 0;
	p = oldp;
	for (i = 0; i < root->nDepths; i++) {
		xDepth *xd;
		xd = (xDepth*)p;
		p += sizeof(*xd);
		for (j = 0; j < xd->nVisuals; j++) {
			xVisualType *xv;
			struct xvisual *v;
			xv = (xVisualType *)p;
			p += sizeof(*xv);

			/* skip duplicates */
			for (k = 0; k < xcon->num_vis; k++)
				if (xcon->vis[k].id == xv->visualID)
					break;
			if (k < xcon->num_vis)
				continue;

			v = &xcon->vis[xcon->num_vis++];
			v->id = xv->visualID;
			v->depth = xd->depth;
			v->class = xv->class;
			v->bits_per_rgb = xv->bitsPerRGB;
			v->red_mask = xv->redMask;
			v->green_mask = xv->greenMask;
			v->blue_mask = xv->blueMask;
			v->entries = xv->colormapEntries;
		}
	}
}

static char *hex_table[] = {		/* for printing hex digits */
    "00", "01", "02", "03", "04", "05", "06", "07", 
    "08", "09", "0a", "0b", "0c", "0d", "0e", "0f", 
    "10", "11", "12", "13", "14", "15", "16", "17", 
    "18", "19", "1a", "1b", "1c", "1d", "1e", "1f", 
    "20", "21", "22", "23", "24", "25", "26", "27", 
    "28", "29", "2a", "2b", "2c", "2d", "2e", "2f", 
    "30", "31", "32", "33", "34", "35", "36", "37", 
    "38", "39", "3a", "3b", "3c", "3d", "3e", "3f", 
    "40", "41", "42", "43", "44", "45", "46", "47", 
    "48", "49", "4a", "4b", "4c", "4d", "4e", "4f", 
    "50", "51", "52", "53", "54", "55", "56", "57", 
    "58", "59", "5a", "5b", "5c", "5d", "5e", "5f", 
    "60", "61", "62", "63", "64", "65", "66", "67", 
    "68", "69", "6a", "6b", "6c", "6d", "6e", "6f", 
    "70", "71", "72", "73", "74", "75", "76", "77", 
    "78", "79", "7a", "7b", "7c", "7d", "7e", "7f", 
    "80", "81", "82", "83", "84", "85", "86", "87", 
    "88", "89", "8a", "8b", "8c", "8d", "8e", "8f", 
    "90", "91", "92", "93", "94", "95", "96", "97", 
    "98", "99", "9a", "9b", "9c", "9d", "9e", "9f", 
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", 
    "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af", 
    "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", 
    "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf", 
    "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", 
    "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf", 
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", 
    "d8", "d9", "da", "db", "dc", "dd", "de", "df", 
    "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", 
    "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef", 
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", 
    "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff", 
};

static void
print_bytes(char *data, int n)
{
	int i;
	char hexdata[2*n+1], *s;
	for (i = 0; i < n; i++) {
		s = hex_table[(unsigned char)data[i]];
		hexdata[2*i] = s[0];
		hexdata[2*i+1] = s[1];
	}	
	hexdata[2*n] = '\0';
	rs_log("auth data is %s\n", hexdata);
}

int
dial_xserver(char *disp, struct xcon *xcon)
{
	int s, rv, len;
	struct sockaddr_in saddr;
	xConnClientPrefix cprefix;
	char *xau_name, *xau_data, *buf;
	int xau_len;
	char *p;

	/* Force unix sockets to TCP sockets
	   because we're too lazy to support both */
	if (0 > disp_to_saddr(disp, &saddr)) {
		rs_log("cannot parse display %s\n", disp);
		return -1;
	}

	/* auth */
	if (0 > xauth(disp, &xau_name, &xau_data, &xau_len)) {
		rs_log("warning: no cookie for %s\n", disp);
		cprefix.nbytesAuthProto = 0;
		cprefix.nbytesAuthString = 0;
	} else {
		rs_log("using %s authorization\n", xau_name);
		print_bytes(xau_data, xau_len);
		cprefix.nbytesAuthProto = strlen(xau_name);
		cprefix.nbytesAuthString = xau_len;
	}

	/* connect */
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (0 > s) {
		rs_logerror("socket");
		return -1;
	}
	saddr.sin_family = AF_INET;
	if (0 > connect(s, (struct sockaddr*)&saddr, sizeof(saddr))) {
		rs_logerror("connect");
		return -1;
	}
	nodelay(s, 1);

	/* init */
	cprefix.byteOrder = 0x6c;   /* LSB first (MSB first is 0x42) */
	cprefix.majorVersion = 11;
	cprefix.minorVersion = 0;
	rv = xwrite(s, &cprefix, sizeof(cprefix));
	if (0 >= rv)
		assert(0);
	if (cprefix.nbytesAuthProto > 0) {
		rv = xwritepad(s, xau_name, strlen(xau_name));
		if (0 >= rv)
			assert(0);
		free(xau_name);
	}
	if (xau_len > 0) {
		rv = xwritepad(s, xau_data, xau_len);
		if (0 >= rv)
			assert(0);
		free(xau_data);
	}
	rv = xread(s, &xcon->prefix, sizeof(xcon->prefix));
	if (0 >= rv) {
		rs_log("failed to get reply to X11 initialization\n");
		assert(0);
	}
	switch (xcon->prefix.success) {
	case 0: /* failed */
		rs_log("failed to connect: ");
		assert(sizeof(buf) > xcon->prefix.lengthReason);
		rv = xread(s, buf, xcon->prefix.lengthReason);
		if (0 >= rv)
			assert(0);
		buf[rv] = '\0';
		rs_log("%s\n", buf);
		break;
	case 1: /* success */
		break;
	case 2: /* authenticate */
		rs_log("authentication required (fix me)\n");
		break;
	default:
		rs_log("server sent unexpected reply\n");
		break;
	}
	rv = xread(s, &xcon->setup, sizeof(xcon->setup));
	if (0 >= rv)
		assert(0);

	/* consume and process the rest */
	len = xcon->prefix.length*4-sizeof(xcon->setup);
	buf = xmalloc(len);
	rv = xread(s, buf, len);
	if (0 >= rv)
		assert(0);
	p = buf;
	p += xcon->setup.nbytesVendor+xpad(xcon->setup.nbytesVendor);
	p += 8*xcon->setup.numFormats;
	parse_visuals(p, xcon);
	free(buf);

	initbigreq(s, xcon);
	initvicext(s, xcon);
	return s;
}
