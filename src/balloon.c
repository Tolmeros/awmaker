/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1998-2003 Alfredo K. Kojima
 *  Copyright (c) 2014 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wconfig.h"

#ifdef BALLOON_TEXT

#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef SHAPED_BALLOON
#include <X11/extensions/shape.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <wraster.h>

#include "WindowMaker.h"
#include "screen.h"
#include "texture.h"
#include "framewin.h"
#include "icon.h"
#include "appicon.h"
#include "workspace.h"
#include "balloon.h"
#include "misc.h"


typedef struct _WBalloon {
	Window window;

#ifdef SHAPED_BALLOON
	GC monoGC;
#endif
	int prevType;

	Window objectWindow;
	char *text;
	int h;

	WMHandlerID timer;

	Pixmap contents;
	Pixmap apercu;

	char mapped;
	char ignoreTimer;
} WBalloon;

#define TOP	0
#define BOTTOM	1
#define LEFT	0
#define RIGHT	2

#define TLEFT (TOP|LEFT)
#define TRIGHT (TOP|RIGHT)
#define BLEFT (BOTTOM|LEFT)
#define BRIGHT (BOTTOM|RIGHT)

static int countLines(const char *text)
{
	const char *p = text;
	int h = 1;

	while (*p) {
		if (*p == '\n' && p[1] != 0)
			h++;
		p++;
	}
	return h;
}

static int getMaxStringWidth(WMFont *font, const char *text)
{
	const char *p = text;
	const char *pb = p;
	int pos = 0;
	int w = 0, wt;

	while (*p) {
		if (*p == '\n') {
			wt = WMWidthOfString(font, pb, pos);
			if (wt > w)
				w = wt;
			pos = 0;
			pb = p + 1;
		} else {
			pos++;
		}
		p++;
	}
	if (pos > 0) {
		wt = WMWidthOfString(font, pb, pos);
		if (wt > w)
			w = wt;
	}
	return w;
}

static void drawMultiLineString(WMScreen *scr, Pixmap pixmap, WMColor *color,
		    WMFont *font, int x, int y, const char *text, int len)
{
	const char *p = text;
	const char *pb = p;
	int l = 0, pos = 0;
	int height = WMFontHeight(font);

	while (*p && p - text < len) {
		if (*p == '\n') {
			WMDrawString(scr, pixmap, color, font, x, y + l * height, pb, pos);
			l++;
			pos = 0;
			pb = p + 1;
		} else {
			pos++;
		}
		p++;
	}
	if (pos > 0)
		WMDrawString(scr, pixmap, color, font, x, y + l * height, pb, pos);
}

#ifdef SHAPED_BALLOON

#define SPACE 12

static void drawBalloon(virtual_screen *vscr, Pixmap bitmap, Pixmap pix, int x, int y, int w, int h, int side)
{
	WScreen *scr = vscr->screen_ptr;
	GC bgc = scr->balloon->monoGC;
	GC gc = scr->draw_gc;
	int rad = h * 3 / 10;
	XPoint pt[3], ipt[3];
	int w1;

	/* outline */
	XSetForeground(dpy, bgc, 1);

	XFillArc(dpy, bitmap, bgc, x, y, rad, rad, 90 * 64, 90 * 64);
	XFillArc(dpy, bitmap, bgc, x, y + h - 1 - rad, rad, rad, 180 * 64, 90 * 64);

	XFillArc(dpy, bitmap, bgc, x + w - 1 - rad, y, rad, rad, 0 * 64, 90 * 64);
	XFillArc(dpy, bitmap, bgc, x + w - 1 - rad, y + h - 1 - rad, rad, rad, 270 * 64, 90 * 64);

	XFillRectangle(dpy, bitmap, bgc, x, y + rad / 2, w, h - rad);
	XFillRectangle(dpy, bitmap, bgc, x + rad / 2, y, w - rad, h);

	/* interior */
	XSetForeground(dpy, gc, scr->white_pixel);

	XFillArc(dpy, pix, gc, x + 1, y + 1, rad, rad, 90 * 64, 90 * 64);
	XFillArc(dpy, pix, gc, x + 1, y + h - 2 - rad, rad, rad, 180 * 64, 90 * 64);

	XFillArc(dpy, pix, gc, x + w - 2 - rad, y + 1, rad, rad, 0 * 64, 90 * 64);
	XFillArc(dpy, pix, gc, x + w - 2 - rad, y + h - 2 - rad, rad, rad, 270 * 64, 90 * 64);

	XFillRectangle(dpy, pix, gc, x + 1, y + 1 + rad / 2, w - 2, h - 2 - rad);
	XFillRectangle(dpy, pix, gc, x + 1 + rad / 2, y + 1, w - 2 - rad, h - 2);

	if (side & BOTTOM) {
		pt[0].y = y + h - 1;
		pt[1].y = y + h - 1 + SPACE;
		pt[2].y = y + h - 1;
		ipt[0].y = pt[0].y - 1;
		ipt[1].y = pt[1].y - 1;
		ipt[2].y = pt[2].y - 1;
	} else {
		pt[0].y = y;
		pt[1].y = y - SPACE;
		pt[2].y = y;
		ipt[0].y = pt[0].y + 1;
		ipt[1].y = pt[1].y + 1;
		ipt[2].y = pt[2].y + 1;
	}

	/*w1 = WMAX(h, 24); */
	w1 = WMAX(h, 21);

	if (side & RIGHT) {
		pt[0].x = x + w - w1 + 2 * w1 / 16;
		pt[1].x = x + w - w1 + 11 * w1 / 16;
		pt[2].x = x + w - w1 + 7 * w1 / 16;
		ipt[0].x = x + 1 + w - w1 + 2 * (w1 - 1) / 16;
		ipt[1].x = x + 1 + w - w1 + 11 * (w1 - 1) / 16;
		ipt[2].x = x + 1 + w - w1 + 7 * (w1 - 1) / 16;
	} else {
		pt[0].x = x + w1 - 2 * w1 / 16;
		pt[1].x = x + w1 - 11 * w1 / 16;
		pt[2].x = x + w1 - 7 * w1 / 16;
		ipt[0].x = x - 1 + w1 - 2 * (w1 - 1) / 16;
		ipt[1].x = x - 1 + w1 - 11 * (w1 - 1) / 16;
		ipt[2].x = x - 1 + w1 - 7 * (w1 - 1) / 16;
	}

	XFillPolygon(dpy, bitmap, bgc, pt, 3, Convex, CoordModeOrigin);
	XFillPolygon(dpy, pix, gc, ipt, 3, Convex, CoordModeOrigin);

	/* fix outline */
	XSetForeground(dpy, gc, scr->black_pixel);

	XDrawLines(dpy, pix, gc, pt, 3, CoordModeOrigin);
	if (side & RIGHT) {
		pt[0].x++;
		pt[2].x--;
	} else {
		pt[0].x--;
		pt[2].x++;
	}

	XDrawLines(dpy, pix, gc, pt, 3, CoordModeOrigin);
}

static Pixmap makePixmap(virtual_screen *vscr, int width, int height, int side, Pixmap *mask)
{
	WScreen *scr = vscr->screen_ptr;
	WBalloon *bal = scr->balloon;
	Pixmap bitmap;
	Pixmap pixmap;
	int x, y;

	bitmap = XCreatePixmap(dpy, scr->root_win, width + SPACE, height + SPACE, 1);

	if (!bal->monoGC)
		bal->monoGC = XCreateGC(dpy, bitmap, 0, NULL);

	XSetForeground(dpy, bal->monoGC, 0);
	XFillRectangle(dpy, bitmap, bal->monoGC, 0, 0, width + SPACE, height + SPACE);

	pixmap = XCreatePixmap(dpy, scr->root_win, width + SPACE, height + SPACE, scr->w_depth);
	XSetForeground(dpy, scr->draw_gc, scr->black_pixel);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, width + SPACE, height + SPACE);

	if (side & BOTTOM)
		y = 0;
	else
		y = SPACE;
	x = 0;

	drawBalloon(vscr, bitmap, pixmap, x, y, width, height, side);

	*mask = bitmap;

	return pixmap;
}

static void showText(virtual_screen *vscr, int x, int y, int h, int w, const char *text)
{
	WScreen *scr = vscr->screen_ptr;
	int width;
	int height;
	Pixmap pixmap;
	Pixmap mask;
	WMFont *font = scr->info_text_font;
	int side = 0;
	int ty;
	int bx, by;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width = getMaxStringWidth(font, text) + 16;
	height = countLines(text) * WMFontHeight(font) + 4;

	if (height < 16)
		height = 16;
	if (width < height)
		width = height;

	if (x + width > scr->scr_width) {
		side = RIGHT;
		bx = x - width + w / 2;
		if (bx < 0)
			bx = 0;
	} else {
		side = LEFT;
		bx = x + w / 2;
	}

	if (bx + width > scr->scr_width)
		bx = scr->scr_width - width;

	if (y - (height + SPACE) < 0) {
		side |= TOP;
		by = y + h - 1;
		ty = SPACE;
	} else {
		side |= BOTTOM;
		by = y - (height + SPACE);
		ty = 0;
	}

	pixmap = makePixmap(vscr, width, height, side, &mask);

	drawMultiLineString(scr->wmscreen, pixmap, scr->black, font, 8, ty + 2, text, strlen(text));

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);
	scr->balloon->contents = pixmap;

	XResizeWindow(dpy, scr->balloon->window, width, height + SPACE);
	XShapeCombineMask(dpy, scr->balloon->window, ShapeBounding, 0, 0, mask, ShapeSet);
	XFreePixmap(dpy, mask);
	XMoveWindow(dpy, scr->balloon->window, bx, by);
	XMapRaised(dpy, scr->balloon->window);

	scr->balloon->mapped = 1;
}
#else				/* !SHAPED_BALLOON */

static void showText(virtual_screen *vscr, int x, int y, int h, int w, const char *text)
{
	WScreen *scr = vscr->scr;
	int width;
	int height;
	Pixmap pixmap;
	WMFont *font = scr->info_text_font;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width = getMaxStringWidth(font, text) + 8;
	/*width = WMWidthOfString(font, text, strlen(text))+8; */
	height = countLines(text) * WMFontHeight(font) + 4;

	if (x < 0)
		x = 0;
	else if (x + width > scr->scr_width - 1)
		x = scr->scr_width - width;

	if (y - height - 2 < 0) {
		y += h;
		if (y < 0)
			y = 0;
	} else {
		y -= height + 2;
	}

	if (scr->window_title_texture[0])
		XSetForeground(dpy, scr->draw_gc, scr->window_title_texture[0]->any.color.pixel);
	else
		XSetForeground(dpy, scr->draw_gc, scr->light_pixel);

	pixmap = XCreatePixmap(dpy, scr->root_win, width, height, scr->w_depth);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, width, height);

	drawMultiLineString(scr->wmscreen, pixmap, scr->window_title_color[0], font, 4, 2, text, strlen(text));

	XResizeWindow(dpy, scr->balloon->window, width, height);
	XMoveWindow(dpy, scr->balloon->window, x, y);

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);
	XClearWindow(dpy, scr->balloon->window);
	XMapRaised(dpy, scr->balloon->window);

	scr->balloon->contents = pixmap;

	scr->balloon->mapped = 1;
}
#endif				/* !SHAPED_BALLOON */

static void showApercu(virtual_screen *vscr, int x, int y, const char *title, Pixmap apercu)
{
	WScreen *scr = vscr->screen_ptr;
	Pixmap pixmap;
	WMFont *font = scr->info_text_font;
	int width, height, titleHeight = 0;
	char *shortenTitle = NULL;

	if (scr->balloon->contents)
		XFreePixmap(dpy, scr->balloon->contents);

	width  = wPreferences.apercu_size;
	height = wPreferences.apercu_size;

	if (wPreferences.miniwin_title_balloon) {
		shortenTitle = ShrinkString(font, title, width - APERCU_BORDER * 2);
		titleHeight = countLines(shortenTitle) * WMFontHeight(font) + 4;
		height += titleHeight;
	}

	if (x < 0)
		x = 0;
	else if (x + width > scr->scr_width - 1)
		x = scr->scr_width - width - 1;

	if (y - height - 2 < 0) {
		y += wPreferences.icon_size;
		if (y < 0)
			y = 0;
	} else {
		y -= height + 2;
	}

	if (scr->window_title_texture[0])
		XSetForeground(dpy, scr->draw_gc, scr->window_title_texture[0]->any.color.pixel);
	else
		XSetForeground(dpy, scr->draw_gc, scr->light_pixel);

	pixmap = XCreatePixmap(dpy, scr->root_win, width, height, scr->w_depth);
	XFillRectangle(dpy, pixmap, scr->draw_gc, 0, 0, width, height);

	if (shortenTitle) {
		drawMultiLineString(scr->wmscreen, pixmap, scr->window_title_color[0], font,
						APERCU_BORDER, APERCU_BORDER, shortenTitle, strlen(shortenTitle));
		wfree(shortenTitle);
	}

	XCopyArea(dpy, apercu, pixmap, scr->draw_gc,
		  0, 0, (wPreferences.apercu_size - 1 - APERCU_BORDER * 2),
		  (wPreferences.apercu_size - 1 - APERCU_BORDER * 2),
		  APERCU_BORDER, APERCU_BORDER + titleHeight);

#ifdef SHAPED_BALLOON
	XShapeCombineMask(dpy, scr->balloon->window, ShapeBounding, 0, 0, None, ShapeSet);
#endif
	XResizeWindow(dpy, scr->balloon->window, width, height);
	XMoveWindow(dpy, scr->balloon->window, x, y);

	XSetWindowBackgroundPixmap(dpy, scr->balloon->window, pixmap);

	XClearWindow(dpy, scr->balloon->window);
	XMapRaised(dpy, scr->balloon->window);

	scr->balloon->contents = pixmap;
	scr->balloon->mapped = 1;
}

static void showBalloon(virtual_screen *vscr)
{
	int x, y;
	Window foow;
	unsigned foo, w;

	vscr->screen_ptr->balloon->timer = NULL;
	vscr->screen_ptr->balloon->ignoreTimer = 1;

	if (!XGetGeometry(dpy, vscr->screen_ptr->balloon->objectWindow, &foow, &x, &y, &w, &foo, &foo, &foo)) {
		vscr->screen_ptr->balloon->prevType = 0;
		return;
	}

	if (wPreferences.miniwin_apercu_balloon && vscr->screen_ptr->balloon->apercu != None)
		/* used to display either the apercu alone or the apercu and the title */
		showApercu(vscr, x, y, vscr->screen_ptr->balloon->text, vscr->screen_ptr->balloon->apercu);
	else
		showText(vscr, x, y, vscr->screen_ptr->balloon->h, w, vscr->screen_ptr->balloon->text);
}

static void frameBalloon(WObjDescriptor *object)
{
	WFrameWindow *fwin = (WFrameWindow *) object->parent;
	virtual_screen *vscr = fwin->core->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (fwin->titlebar != object->self || !fwin->flags.is_client_window_frame) {
		wBalloonHide(vscr);
		return;
	}

	if (fwin->title && fwin->flags.incomplete_title) {
		scr->balloon->h = (fwin->titlebar ? fwin->titlebar->height : 0);
		scr->balloon->text = wstrdup(fwin->title);
		scr->balloon->objectWindow = fwin->core->window;
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

static void miniwindowBalloon(WObjDescriptor *object)
{
	WIcon *icon = (WIcon *) object->parent;
	virtual_screen *vscr = icon->core->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (!icon->icon_name) {
		wBalloonHide(vscr);
		return;
	}

	scr->balloon->h = icon->core->height;
	scr->balloon->text = wstrdup(icon->icon_name);
	scr->balloon->apercu = icon->apercu;
	scr->balloon->objectWindow = icon->core->window;

	if ((scr->balloon->prevType == object->parent_type || scr->balloon->prevType == WCLASS_APPICON)
	    && scr->balloon->ignoreTimer) {
		XUnmapWindow(dpy, scr->balloon->window);
		showBalloon(vscr);
	} else {
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

static void appiconBalloon(WObjDescriptor *object)
{
	WAppIcon *aicon = (WAppIcon *) object->parent;
	virtual_screen *vscr = aicon->icon->core->vscr;
	WScreen *scr = vscr->screen_ptr;
	char *tmp;

	/* Show balloon if it is the Clip and the workspace name is > 5 chars */
	if (object->parent == w_global.clip.icon) {
		if (strlen(vscr->workspace.array[vscr->workspace.current]->name) > 5) {
			scr->balloon->text = wstrdup(vscr->workspace.array[vscr->workspace.current]->name);
		} else {
			wBalloonHide(vscr);
			return;
		}
	} else if (aicon->command && aicon->wm_class) {
		int len;
		WApplication *app;
		unsigned int app_win_cnt = 0;

		if (object->parent_type == WCLASS_DOCK_ICON) {
			if (aicon->main_window) {
				app = wApplicationOf(aicon->main_window);
				if (app && app->main_window_desc && app->main_window_desc->fake_group)
					app_win_cnt = app->main_window_desc->fake_group->retainCount - 1;
			}
		}

		/* Check to see if it is a GNUstep app */
		if (strcmp(aicon->wm_class, "GNUstep") == 0)
			len = strlen(aicon->command) + strlen(aicon->wm_instance) + 8;
		else
			len = strlen(aicon->command) + strlen(aicon->wm_class) + 8;

		if (app_win_cnt > 0)
			len += 1 + snprintf(NULL, 0, "%u", app_win_cnt);

		tmp = wmalloc(len);
		/* Check to see if it is a GNUstep App */
		if (strcmp(aicon->wm_class, "GNUstep") == 0)
			if (app_win_cnt > 0)
				snprintf(tmp, len, "%u %s\n(%s)", app_win_cnt, aicon->wm_instance, aicon->command);
			else
				snprintf(tmp, len, "%s\n(%s)", aicon->wm_instance, aicon->command);
		else
			if (app_win_cnt > 0)
				snprintf(tmp, len, "%u %s\n(%s)", app_win_cnt, aicon->wm_class, aicon->command);
			else
				snprintf(tmp, len, "%s\n(%s)", aicon->wm_class, aicon->command);
		scr->balloon->text = tmp;
	} else if (aicon->command) {
		scr->balloon->text = wstrdup(aicon->command);
	} else if (aicon->wm_class) {
		/* Check to see if it is a GNUstep App */
		if (strcmp(aicon->wm_class, "GNUstep") == 0)
			scr->balloon->text = wstrdup(aicon->wm_instance);
		else
			scr->balloon->text = wstrdup(aicon->wm_class);
	} else {
		wBalloonHide(vscr);
		return;
	}

	scr->balloon->h = aicon->icon->core->height - 2;
	scr->balloon->objectWindow = aicon->icon->core->window;
	if ((scr->balloon->prevType == object->parent_type || scr->balloon->prevType == WCLASS_MINIWINDOW)
	    && scr->balloon->ignoreTimer) {
		XUnmapWindow(dpy, scr->balloon->window);
		showBalloon(vscr);
	} else {
		scr->balloon->timer = WMAddTimerHandler(BALLOON_DELAY, (WMCallback *) showBalloon, scr);
	}
}

void wBalloonInitialize(virtual_screen *vscr)
{
	WBalloon *bal;
	XSetWindowAttributes attribs;
	unsigned long vmask;
	WScreen *scr = vscr->screen_ptr;

	bal = wmalloc(sizeof(WBalloon));

	scr->balloon = bal;

	vmask = CWSaveUnder | CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel;
	attribs.save_under = True;
	attribs.override_redirect = True;
	attribs.colormap = scr->w_colormap;
	attribs.background_pixel = scr->icon_back_texture->normal.pixel;
	attribs.border_pixel = 0;	/* do not care */

	bal->window = XCreateWindow(dpy, scr->root_win, 1, 1, 10, 10, 1,
				    scr->w_depth, CopyFromParent, scr->w_visual, vmask, &attribs);
}

void wBalloonEnteredObject(virtual_screen *vscr, WObjDescriptor *object)
{
	WScreen *scr = vscr->screen_ptr;
	WBalloon *balloon = scr->balloon;

	if (balloon->timer) {
		WMDeleteTimerHandler(balloon->timer);
		balloon->timer = NULL;
		balloon->ignoreTimer = 0;
	}

	if (scr->balloon->text)
		wfree(scr->balloon->text);

	scr->balloon->text = NULL;
	scr->balloon->apercu = None;

	if (!object) {
		wBalloonHide(vscr);
		balloon->ignoreTimer = 0;
		return;
	}

	switch (object->parent_type) {
	case WCLASS_FRAME:
		if (wPreferences.window_balloon)
			frameBalloon(object);
		break;
	case WCLASS_DOCK_ICON:
		if (wPreferences.appicon_balloon)
			appiconBalloon(object);
		break;
	case WCLASS_MINIWINDOW:
		if (wPreferences.miniwin_title_balloon || wPreferences.miniwin_apercu_balloon)
			miniwindowBalloon(object);
		break;
	case WCLASS_APPICON:
		if (wPreferences.appicon_balloon)
			appiconBalloon(object);
		break;
	default:
		wBalloonHide(vscr);
		break;
	}

	scr->balloon->prevType = object->parent_type;
}

void wBalloonHide(virtual_screen *vscr)
{
	if (!vscr->screen_ptr)
		return;

	if (vscr->screen_ptr->balloon->mapped) {
		XUnmapWindow(dpy, vscr->screen_ptr->balloon->window);
		vscr->screen_ptr->balloon->mapped = 0;
	} else if (vscr->screen_ptr->balloon->timer) {
		WMDeleteTimerHandler(vscr->screen_ptr->balloon->timer);
		vscr->screen_ptr->balloon->timer = NULL;
	}

	vscr->screen_ptr->balloon->prevType = 0;
}

#endif
