#include "extapi.h"

#include <X11/Xproto.h>

/* Something needs to be #include'd before this to
   define CARD*, etc. */
#define X_EvictNop		0
#define X_EvictPixmaps		1
#define X_EvictResource		2
#define X_EvictConn		3
#define X_EvictCursor		4

typedef struct EvictNop {
	CARD8   reqType;
	CARD8   evictType;
	CARD16  length;
} xEvictNopReq;
#define sz_xEvictNopReq 4

typedef struct {
	BYTE     type;  /* X_Reply */
	CARD8	 unused;
	CARD16	 sequenceNumber;
	CARD32	 length;
	CARD32   reply;
	CARD32	 pad0;
	CARD32	 pad1;
	CARD32	 pad2;
	CARD32	 pad3;
	CARD32	 pad4;
} xEvictNopReply;

typedef struct EvictPixmaps {
	CARD8   reqType;
	CARD8   evictType;
	CARD16  length;
} xEvictPixmapsReq;
#define sz_xEvictPixmapsReq 4

typedef struct {
	BYTE     type;  /* X_Reply */
	CARD8	 unused;
	CARD16	 sequenceNumber;
	CARD32	 length;
	CARD32   reply;
	CARD32	 pad0;
	CARD32	 pad1;
	CARD32	 pad2;
	CARD32	 pad3;
	CARD32	 pad4;
} xEvictPixmapsReply;


typedef struct EvictResource {
	CARD8   reqType;
	CARD8   evictType;
	CARD16  length;
	CARD32  res;
} xEvictResourceReq;
#define sz_xEvictResourceReq 8

typedef struct {
	BYTE     type;  /* X_Reply */
	CARD8	 unused;
	CARD16	 sequenceNumber;
	CARD32	 length;
	CARD32   num;
	CARD32	 pad0;
	CARD32	 pad1;
	CARD32	 pad2;
	CARD32	 pad3;
	CARD32	 pad4;
} xEvictResourceReply;

/* Resource values for res field of EvictResourceReq */
enum {
	ExtRT_WINDOW = 0,
	ExtRT_PIXMAP,
	ExtRT_GC,
	ExtRT_FONT,
	ExtRT_CURSOR,
	ExtRT_COLORMAP,
	ExtRT_CMAPENTRY,
	ExtRT_OTHERCLIENT,
	ExtRT_PASSIVEGRAB,
	ExtRT_NUM,
};

typedef struct EvictConn {
	CARD8   reqType;
	CARD8   evictType;
	CARD16  length;
	CARD32  id;
} xEvictConnReq;
#define sz_xEvictConnReq 8

typedef struct {
	BYTE     type;  /* X_Reply */
	CARD8	 unused;
	CARD16	 sequenceNumber;
	CARD32	 length;
	CARD32	inode;
	CARD32   pad0;
	CARD32	 pad1;
	CARD32	 pad2;
	CARD32	 pad3;
	CARD32	 pad4;
} xEvictConnReply;

#define EXTNAME "GUIEVICT"
#define ExtNumberEvents   0
#define ExtNumberErrors   0
#define ExtMAGIC 0x01234567


enum {
	CURSORMAX = 128  /* BYTES, not bits */
};

struct cursor {
	unsigned long id;
	char src[CURSORMAX];
	char msk[CURSORMAX];
	int emptymsk;
	int width, height, xhot, yhot;
	unsigned short fore_red, fore_green, fore_blue;
	unsigned short back_red, back_green, back_blue;
	unsigned long nbytes;
};

struct windowx {
	unsigned long id;
	int backgroundState;   /* None, Relative, Pixel, Pixmap */
	unsigned long bgpixel;
	unsigned long bgpm;
	int mapped;
};

union resu {
	unsigned long xid;
	struct extGC gc;
	struct cursor cursor;
	struct windowx winx;
};
