#ifndef EXTENSION
#include <X11/Xlib.h>
#endif

/* The Xlib GCValues, renamed to avoid name conflicts in
   server code (cannot include Xlib definitions in server) */
struct _XGCValues {
	/* We're missing the data that is lifted from the drawable
	   passed to createGC, the screen and the depth. */
	int function;
	unsigned long plane_mask;
	unsigned long foreground;
	unsigned long background;
	int line_width;
	int line_style;
	int cap_style;
	int join_style;
	int fill_style;
	int fill_rule;
	int arc_mode;
	Pixmap stipple;	
	int tile_is_pixel;       
	Pixmap tilepm;
	unsigned long tilepx;   

	int ts_x_origin;
	int ts_y_origin;
	int subwindow_mode;
	Bool graphics_exposures;
	int clip_x_origin;
	int clip_y_origin;
        Font font;
#if 0
	Pixmap clip_mask;
	int dash_offset;
	char dashes;
#endif
};

struct extGC {
	unsigned long id, new;
	char depth;
	struct _XGCValues values;
};
