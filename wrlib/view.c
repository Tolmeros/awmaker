#include <X11/Xlib.h>
#include "wraster.h"
#include <stdlib.h>
#include <stdio.h>
#include "tile.xpm"
Display *dpy;
Window win;
RContext *ctx;
RImage *img;
Pixmap pix;

int main(int argc, char **argv)
{
    RContextAttributes attr;

    dpy = XOpenDisplay("");
    if (!dpy) {
	puts("cant open display");
	exit(1);
    }
    attr.flags = RC_RenderMode | RC_ColorsPerChannel;
    attr.render_mode = RM_DITHER;
    attr.colors_per_channel = 4;
    ctx = RCreateContext(dpy, DefaultScreen(dpy), &attr);
    if (argc<2) 
	img = RGetImageFromXPMData(ctx, image_name);
    else
	img = RLoadImage(ctx, argv[1], 0);

    if (!img) {
	puts(RMessageForError(RErrorCode));
	exit(1);
    }
    if (argc > 2) {
	RImage *tmp = img;
	
	img = RSmoothScaleImage(tmp, tmp->width*atol(argv[2]), 
				tmp->height*atol(argv[2]));
	RDestroyImage(tmp);
    }
    if (!RConvertImage(ctx, img, &pix)) {
	puts(RMessageForError(RErrorCode));
	exit(1);
    }
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 10, 10, img->width,
			      img->height, 0, 0, 0);
    RDestroyImage(img);
    XSetWindowBackgroundPixmap(dpy, win, pix);
    XClearWindow(dpy, win);
    XMapRaised(dpy, win);
    XFlush(dpy);

    getchar();
}
