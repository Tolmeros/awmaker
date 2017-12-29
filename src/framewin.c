/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef KEEP_XKB_LOCK_STATUS
#include <X11/XKBlib.h>
#endif				/* KEEP_XKB_LOCK_STATUS */

#include <stdlib.h>
#include <string.h>

#include <wraster.h>

#include "WindowMaker.h"
#include "GNUstep.h"
#include "resources.h"
#include "screen.h"
#include "wcore.h"
#include "window.h"
#include "framewin.h"
#include "stacking.h"
#include "misc.h"
#include "event.h"

static void handleExpose(WObjDescriptor * desc, XEvent * event);
static void handleButtonExpose(WObjDescriptor * desc, XEvent * event);

static void buttonMouseDown(WObjDescriptor * desc, XEvent * event);
static void titlebarMouseDown(WObjDescriptor * desc, XEvent * event);
static void resizebarMouseDown(WObjDescriptor * desc, XEvent * event);

static void checkTitleSize(WFrameWindow * fwin);

static void paintButton(WCoreWindow * button, WTexture * texture,
			unsigned long color, WPixmap * image, int pushed);

static void updateTitlebar(WFrameWindow *fwin);

static void allocFrameBorderPixel(Colormap colormap, const char *color_name, unsigned long **pixel);
static char *get_title(WFrameWindow *fwin);

static void allocFrameBorderPixel(Colormap colormap, const char *color_name, unsigned long **pixel) {
	XColor xcol;

	*pixel = NULL;

	if (! wGetColorForColormap(colormap, color_name, &xcol))
		return;

	*pixel = wmalloc(sizeof(unsigned long));
	if (*pixel)
		**pixel = xcol.pixel;
}

WFrameWindow *wframewindow_create(WWindow *parent_wwin, WMenu *parent_wmenu,
				  int width, int height, int flags)
{
	WFrameWindow *fwin;

	fwin = wmalloc(sizeof(WFrameWindow));
	fwin->width = width;
	fwin->height = height;
	fwin->core = wcore_create(fwin->width, fwin->height);
	fwin->parent_wwin = parent_wwin;
	fwin->parent_wmenu = parent_wmenu;

	fwin->flags.map_titlebar = (flags & WFF_TITLEBAR) ? 1 : 0;
	fwin->flags.map_resizebar = (flags & WFF_RESIZEBAR) ? 1 : 0;
	fwin->flags.map_left_button = (flags & WFF_LEFT_BUTTON) ? 1 : 0;
	fwin->flags.map_right_button = (flags & WFF_RIGHT_BUTTON) ? 1 : 0;
#ifdef XKB_BUTTON_HINT
	fwin->flags.map_language_button = (flags & WFF_LANGUAGE_BUTTON) ? 1 : 0;
#endif

	fwin->flags.single_texture = (flags & WFF_SINGLE_STATE) ? 1 : 0;
	fwin->flags.border = (flags & WFF_BORDER) ? 1 : 0;
	fwin->bordersize = 0;

	return fwin;
}

void wframewindow_destroy_wcorewindow(WCoreWindow *core)
{
	wcore_destroy(core);
}

void wframewindow_map(WFrameWindow *fwin, virtual_screen *vscr, int wlevel,
		      int x, int y, int *clearance,
		      int *title_min, int *title_max,
		      WTexture **title_texture, WTexture **resize_texture,
		      WMColor **color, WMFont **font, int depth,
		      Visual *visual, Colormap colormap)
{
	WCoreWindow *wcore = fwin->core;
	WScreen *scr = vscr->screen_ptr;
	int flags = 0;

	fwin->vscr = vscr;

	fwin->title_texture = title_texture;
	fwin->resizebar_texture = resize_texture;
	fwin->title_color = color;
	fwin->title_clearance = clearance;
	fwin->title_min_height = title_min;
	fwin->title_max_height = title_max;
	fwin->font = font;
#ifdef KEEP_XKB_LOCK_STATUS
	fwin->languagemode = XkbGroup1Index;
	fwin->last_languagemode = XkbGroup2Index;
#endif

	fwin->depth = depth;
	fwin->visual = visual;
	fwin->colormap = colormap;

	wcore_map_toplevel(wcore, vscr, x, y, fwin->width, fwin->height,
			   (fwin->flags.border) ? scr->frame_border_width : 0,
			   fwin->depth, fwin->visual,
			   fwin->colormap, scr->frame_border_pixel);

	/* setup stacking information */
	wcore->stacking = wmalloc(sizeof(WStacking));
	wcore->stacking->above = NULL;
	wcore->stacking->under = NULL;
	wcore->stacking->child_of = NULL;
	wcore->stacking->window_level = wlevel;

	AddToStackList(wcore);

	/* wFrameWindowUpdateBorders uses flags argument to
	 * udpate the flags and update the framewindow
	 */
	if (fwin->flags.border)
		flags |= WFF_BORDER;

	if (fwin->flags.map_titlebar)
		flags |= WFF_TITLEBAR;

	if (fwin->flags.map_resizebar)
		flags |= WFF_RESIZEBAR;

	if (fwin->flags.map_left_button)
		flags |= WFF_LEFT_BUTTON;

	if (fwin->flags.map_right_button)
		flags |= WFF_RIGHT_BUTTON;

#ifdef XKB_BUTTON_HINT
	if (fwin->flags.map_language_button)
		flags |= WFF_LANGUAGE_BUTTON;
#endif

	wframewin_set_borders(fwin, flags);
}


static void destroy_framewin_button(WFrameWindow *fwin, int state)
{
	destroy_pixmap(fwin->title_back[state]);
	if (wPreferences.new_style == TS_NEW) {
		destroy_pixmap(fwin->lbutton_back[state]);
		destroy_pixmap(fwin->rbutton_back[state]);
#ifdef XKB_BUTTON_HINT
		destroy_pixmap(fwin->languagebutton_back[state]);
#endif
	}
}

static void destroy_framewin_buttons(WFrameWindow *fwin)
{
	int state;

	for (state = 0; state < (fwin->flags.single_texture ? 1 : 3); state++)
		destroy_framewin_button(fwin, state);
}

static void set_framewin_descriptors(WCoreWindow *wcore, void *handle_expose,
				     void *parent, WClassType parent_type,
				     void *handle_mousedown)
{
	wcore->descriptor.handle_expose = handle_expose;
	wcore->descriptor.parent = parent;
	wcore->descriptor.parent_type = parent_type;
	wcore->descriptor.handle_mousedown = handle_mousedown;
}

static void left_button_create(WFrameWindow *fwin)
{
	fwin->left_button = wcore_create(fwin->bordersize, fwin->bordersize);

	if (fwin->left_button)
		set_framewin_descriptors(fwin->left_button, handleButtonExpose, fwin, WCLASS_FRAME, buttonMouseDown);
}

static void left_button_map(WFrameWindow *fwin, int theight)
{
	int left_button_pos_width, left_button_pos_height;
	int width = fwin->width;
	virtual_screen *vscr = fwin->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (wPreferences.new_style == TS_NEW) {
		left_button_pos_width = 0;
		left_button_pos_height = 0;
		wcore_map(fwin->left_button, fwin->core,
			  fwin->core->vscr,
			  left_button_pos_width, left_button_pos_height,
			  fwin->bordersize, fwin->bordersize,
			  0, fwin->core->vscr->screen_ptr->w_depth,
			  fwin->core->vscr->screen_ptr->w_visual,
			  fwin->core->vscr->screen_ptr->w_colormap);

		if (width < theight * 4)
			fwin->flags.lbutton_dont_fit = 1;
		else
			XMapRaised(dpy, fwin->left_button->window);
	} else {
		left_button_pos_width = 3;
		left_button_pos_height = (theight - fwin->bordersize) / 2;

		wcore_map(fwin->left_button, fwin->titlebar,
			  fwin->titlebar->vscr,
			  left_button_pos_width, left_button_pos_height,
			  fwin->bordersize, fwin->bordersize,
			  0, fwin->titlebar->vscr->screen_ptr->w_depth,
			  fwin->titlebar->vscr->screen_ptr->w_visual,
			  fwin->titlebar->vscr->screen_ptr->w_colormap);

		if (wPreferences.new_style == TS_OLD)
			XSetWindowBackground(dpy, fwin->left_button->window,
					     scr->widget_texture->normal.pixel);
		else
			XSetWindowBackground(dpy, fwin->left_button->window,
					     scr->widget_texture->dark.pixel);

		if (width < theight * 3)
			fwin->flags.lbutton_dont_fit = 1;
		else
			XMapRaised(dpy, fwin->left_button->window);
	}

	fwin->flags.left_button = 1;
}

static void left_button_unmap(WFrameWindow *fwin)
{
	wcore_unmap(fwin->left_button);
	fwin->flags.left_button = 0;
}

#ifdef XKB_BUTTON_HINT
static void language_button_create(WFrameWindow *fwin)
{
	fwin->language_button = wcore_create(fwin->bordersize, fwin->bordersize);

	if (fwin->language_button)
		set_framewin_descriptors(fwin->language_button, handleButtonExpose, fwin, WCLASS_FRAME, buttonMouseDown);
}

static void language_button_map(WFrameWindow *fwin, int theight)
{
	int language_button_pos_width, language_button_pos_height;
	int width = fwin->width;
	virtual_screen *vscr = fwin->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (wPreferences.new_style == TS_NEW) {
		language_button_pos_width = fwin->bordersize;
		language_button_pos_height = 0;
		wcore_map(fwin->language_button, fwin->core,
			  fwin->core->vscr,
			  language_button_pos_width, language_button_pos_height,
			  fwin->bordersize, fwin->bordersize,
			  0, fwin->core->vscr->screen_ptr->w_depth,
			  fwin->core->vscr->screen_ptr->w_visual,
			  fwin->core->vscr->screen_ptr->w_colormap);

		if (width < theight * 4)
			fwin->flags.languagebutton_dont_fit = 1;
		else
			XMapRaised(dpy, fwin->language_button->window);
	} else {
		language_button_pos_width = fwin->bordersize + 6;
		language_button_pos_height = (theight - fwin->bordersize) / 2;
		wcore_map(fwin->language_button, fwin->titlebar,
			  fwin->titlebar->vscr,
			  language_button_pos_width, language_button_pos_height,
			  fwin->bordersize, fwin->bordersize,
			  0, fwin->titlebar->vscr->screen_ptr->w_depth,
			  fwin->titlebar->vscr->screen_ptr->w_visual,
			  fwin->titlebar->vscr->screen_ptr->w_colormap);

		if (wPreferences.new_style == TS_OLD)
			XSetWindowBackground(dpy, fwin->language_button->window,
					     scr->widget_texture->normal.pixel);
		else
			XSetWindowBackground(dpy, fwin->language_button->window,
					     scr->widget_texture->dark.pixel);

		if (width < theight * 3)
			fwin->flags.languagebutton_dont_fit = 1;
		else
			XMapRaised(dpy, fwin->language_button->window);
	}

	fwin->flags.language_button = 1;
}

static void language_button_unmap(WFrameWindow *fwin)
{
	wcore_unmap(fwin->language_button);
	fwin->flags.language_button = 0;
}
#endif

static void right_button_create(WFrameWindow *fwin)
{
	fwin->right_button = wcore_create(fwin->bordersize, fwin->bordersize);

	if (fwin->right_button)
		set_framewin_descriptors(fwin->right_button, handleButtonExpose, fwin, WCLASS_FRAME, buttonMouseDown);
}

static void right_button_map(WFrameWindow *fwin, int theight)
{
	int right_button_pos_width, right_button_pos_height;
	int width = fwin->width;
	virtual_screen *vscr = fwin->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (wPreferences.new_style == TS_NEW) {
		right_button_pos_width = width - fwin->bordersize + 1;
		right_button_pos_height = 0;
		wcore_map(fwin->right_button, fwin->core,
			  fwin->core->vscr,
			  right_button_pos_width, right_button_pos_height,
			  fwin->bordersize, fwin->bordersize, 0,
			  fwin->core->vscr->screen_ptr->w_depth,
			  fwin->core->vscr->screen_ptr->w_visual,
			  fwin->core->vscr->screen_ptr->w_colormap);
	} else {
		right_button_pos_width = width - fwin->bordersize - 3;
		right_button_pos_height = (theight - fwin->bordersize) / 2;
		wcore_map(fwin->right_button, fwin->titlebar,
			  fwin->titlebar->vscr,
			  right_button_pos_width, right_button_pos_height,
			  fwin->bordersize, fwin->bordersize, 0,
			  fwin->titlebar->vscr->screen_ptr->w_depth,
			  fwin->titlebar->vscr->screen_ptr->w_visual,
			  fwin->titlebar->vscr->screen_ptr->w_colormap);

		if (wPreferences.new_style == TS_OLD)
			XSetWindowBackground(dpy, fwin->right_button->window,
					     scr->widget_texture->normal.pixel);
		else
			XSetWindowBackground(dpy, fwin->right_button->window,
					     scr->widget_texture->dark.pixel);
	}

	if (width < theight * 2)
		fwin->flags.rbutton_dont_fit = 1;
	else
		XMapRaised(dpy, fwin->right_button->window);

	fwin->flags.right_button = 1;
}

static void right_button_unmap(WFrameWindow *fwin)
{
	wcore_unmap(fwin->right_button);
	fwin->flags.right_button = 0;
}

static void titlebar_create(WFrameWindow *fwin, int theight, int flags)
{
	fwin->top_width = theight;
	fwin->titlebar = wcore_create(fwin->width + 1, theight);

	if (flags & WFF_LEFT_BUTTON) {
		fwin->flags.left_button = 1;
		fwin->flags.map_left_button = 1;
		left_button_create(fwin);
	}

#ifdef XKB_BUTTON_HINT
	if (flags & WFF_LANGUAGE_BUTTON) {
		fwin->flags.language_button = 1;
		fwin->flags.map_language_button = 1;
		language_button_create(fwin);
	}
#endif

	if (flags & WFF_RIGHT_BUTTON) {
		fwin->flags.right_button = 1;
		fwin->flags.map_right_button = 1;
		right_button_create(fwin);
	}

	set_framewin_descriptors(fwin->titlebar, handleExpose, fwin, WCLASS_FRAME, titlebarMouseDown);
}

static void titlebar_map(WFrameWindow *fwin, int theight)
{
	/* if we didn't have a titlebar and are being requested for
	 * one, create it */
	if (!fwin->flags.map_titlebar)
		return;

	fwin->flags.titlebar = 1;
	fwin->top_width = theight;

	wcore_map(fwin->titlebar, fwin->core, fwin->core->vscr,
		  0, 0, fwin->titlebar->width, fwin->titlebar->height, 0,
		  fwin->core->vscr->screen_ptr->w_depth,
		  fwin->core->vscr->screen_ptr->w_visual,
		  fwin->core->vscr->screen_ptr->w_colormap);

	if (fwin->flags.map_left_button)
		left_button_map(fwin, theight);

#ifdef XKB_BUTTON_HINT
	if (fwin->flags.map_language_button)
		language_button_map(fwin, theight);
#endif

	if (fwin->flags.map_right_button)
		right_button_map(fwin, theight);

	if (wPreferences.new_style == TS_NEW)
		updateTitlebar(fwin);

	XMapRaised(dpy, fwin->titlebar->window);

	fwin->flags.need_texture_remake = 1;
}

static void titlebar_update(WFrameWindow *fwin, int theight)
{
	int left_button_pos_width, left_button_pos_height;
	int right_button_pos_width, right_button_pos_height;
#ifdef XKB_BUTTON_HINT
	int language_button_pos_width, language_button_pos_height;
#endif
	int width = fwin->width;

	fwin->top_width = theight;
	fwin->flags.need_texture_remake = 1;

	if (wPreferences.new_style == TS_NEW) {
		left_button_pos_width = 0;
		left_button_pos_height = 0;
#ifdef XKB_BUTTON_HINT
		if (fwin->flags.hide_left_button || !fwin->left_button || fwin->flags.lbutton_dont_fit) {
			language_button_pos_width = 0;
			language_button_pos_height = 0;
		} else {
			language_button_pos_width = fwin->bordersize;
			language_button_pos_height = 0;
		}
#endif
		right_button_pos_width = width - fwin->bordersize + 1;
		right_button_pos_height = 0;
	} else {	/* !new_style */
		left_button_pos_width = 3;
		left_button_pos_height = (theight - fwin->bordersize) / 2;
#ifdef XKB_BUTTON_HINT
		language_button_pos_width = fwin->bordersize + 6;
		language_button_pos_height = (theight - fwin->bordersize) / 2;
#endif
		right_button_pos_width = width - fwin->bordersize - 3;
		right_button_pos_height = (theight - fwin->bordersize) / 2;
	}

	if (fwin->left_button && fwin->flags.map_left_button)
		wCoreConfigure(fwin->left_button, left_button_pos_width, left_button_pos_height, fwin->bordersize, fwin->bordersize);
#ifdef XKB_BUTTON_HINT
	if (fwin->language_button && fwin->flags.map_language_button)
		wCoreConfigure(fwin->language_button, language_button_pos_width, language_button_pos_height, fwin->bordersize, fwin->bordersize);
#endif
	if (fwin->right_button && fwin->flags.map_right_button)
		wCoreConfigure(fwin->right_button, right_button_pos_width, right_button_pos_height, fwin->bordersize, fwin->bordersize);

	updateTitlebar(fwin);
}

static void titlebar_unmap(WFrameWindow *fwin)
{
	if (fwin->flags.titlebar) {
		if (fwin->flags.map_left_button)
			left_button_unmap(fwin);
		if (fwin->flags.map_right_button)
			right_button_unmap(fwin);
#ifdef XKB_BUTTON_HINT
		if (fwin->flags.map_language_button)
			language_button_unmap(fwin);
#endif
		wcore_unmap(fwin->titlebar);

		fwin->top_width = 0;
		fwin->flags.titlebar = 0;
	}
}

static void titlebar_destroy(WFrameWindow *fwin)
{
	wframewindow_destroy_wcorewindow(fwin->left_button);
	fwin->left_button = NULL;
#ifdef XKB_BUTTON_HINT
	wframewindow_destroy_wcorewindow(fwin->language_button);
	fwin->language_button = NULL;
#endif
	wframewindow_destroy_wcorewindow(fwin->right_button);
	fwin->right_button = NULL;
	wframewindow_destroy_wcorewindow(fwin->titlebar);
	fwin->titlebar = NULL;
}

static void resizebar_create(WFrameWindow *fwin, int width)
{
	fwin->resizebar = wcore_create(width, RESIZEBAR_HEIGHT);
	set_framewin_descriptors(fwin->resizebar, handleExpose, fwin, WCLASS_FRAME, resizebarMouseDown);
}

static void resizebar_destroy(WFrameWindow *fwin)
{
	wframewindow_destroy_wcorewindow(fwin->resizebar);
	fwin->resizebar = NULL;
}

static void resizebar_map(WFrameWindow *fwin, int width, int height)
{
	fwin->bottom_width = RESIZEBAR_HEIGHT;
	wcore_map(fwin->resizebar, fwin->core, fwin->core->vscr,
		  0, height + fwin->top_width,
		  fwin->resizebar->width, fwin->resizebar->height, 0,
		  fwin->core->vscr->screen_ptr->w_depth,
		  fwin->core->vscr->screen_ptr->w_visual,
		  fwin->core->vscr->screen_ptr->w_colormap);

	fwin->resizebar_corner_width = RESIZEBAR_CORNER_WIDTH;
	if (width < RESIZEBAR_CORNER_WIDTH * 2 + RESIZEBAR_MIN_WIDTH) {
		fwin->resizebar_corner_width = (width - RESIZEBAR_MIN_WIDTH) / 2;
		if (fwin->resizebar_corner_width < 0)
			fwin->resizebar_corner_width = 0;
	}

	XMapWindow(dpy, fwin->resizebar->window);
	XLowerWindow(dpy, fwin->resizebar->window);

	fwin->flags.need_texture_remake = 1;
	fwin->flags.resizebar = 1;
}

static void resizebar_update(WFrameWindow *fwin, int width, int height)
{
	if (height + fwin->top_width + fwin->bottom_width != fwin->height)
		wCoreConfigure(fwin->resizebar, 0, height + fwin->top_width,
			       width, RESIZEBAR_HEIGHT);
}

static void resizebar_unmap(WFrameWindow *fwin)
{
	if (fwin->flags.resizebar) {
		fwin->bottom_width = 0;
		wcore_unmap(fwin->resizebar);
		fwin->flags.resizebar = 0;
	}
}

static int get_framewin_height(WFrameWindow *fwin, int flags)
{
	if (flags & WFF_IS_SHADED)
		return -1;
	else
		return fwin->height - fwin->top_width - fwin->bottom_width;
}

static int get_framewin_titleheight(WFrameWindow *fwin)
{
	int theight = 0;

	if (fwin->flags.map_titlebar) {
		theight = WMFontHeight(*fwin->font) + (*fwin->title_clearance + TITLEBAR_EXTEND_SPACE) * 2;

		if (theight > *fwin->title_max_height)
			theight = *fwin->title_max_height;

		if (theight < *fwin->title_min_height)
			theight = *fwin->title_min_height;
	}

	return theight;
}

static int get_framewin_bordersize(int titleheight)
{
	int bsize;

	if (wPreferences.new_style == TS_NEW) {
		bsize = titleheight;
	} else if (wPreferences.new_style == TS_OLD) {
		bsize = titleheight - 7;
	} else {
		bsize = titleheight - 8;
	}

	return bsize;
}

void wframewin_set_borders(WFrameWindow *fwin, int flags)
{
	int theight, width, height;
	virtual_screen *vscr = fwin->vscr;
	WScreen *scr = vscr->screen_ptr;

	if (flags & WFF_TITLEBAR)
		fwin->flags.map_titlebar = 1;
	else
		fwin->flags.map_titlebar = 0;

	if (flags & WFF_RESIZEBAR)
		fwin->flags.map_resizebar = 1;
	else
		fwin->flags.map_resizebar = 0;

	if (fwin->flags.shaded)
		flags |= WFF_IS_SHADED;

	if (flags & WFF_LEFT_BUTTON)
		fwin->flags.map_left_button = 1;
	else
		fwin->flags.map_left_button = 0;

	if (flags & WFF_RIGHT_BUTTON)
		fwin->flags.map_right_button = 1;
	else
		fwin->flags.map_left_button = 0;

	if (flags & WFF_BORDER)
		fwin->flags.border = 1;
	else
		fwin->flags.border = 0;

#ifdef XKB_BUTTON_HINT
	if (flags & WFF_LANGUAGE_BUTTON)
		fwin->flags.map_language_button = 1;
	else
		fwin->flags.map_language_button = 0;
#endif

	width = fwin->width;
	height = get_framewin_height(fwin, flags);
	theight = get_framewin_titleheight(fwin);
	fwin->bordersize = get_framewin_bordersize(theight);

	if (fwin->titlebar) {
		titlebar_unmap(fwin);
		if (fwin->flags.map_titlebar) {
			titlebar_map(fwin, theight);
			titlebar_update(fwin, theight);
		}
	} else {
		titlebar_create(fwin, theight, flags);
		titlebar_map(fwin, theight);
	}

	checkTitleSize(fwin);

	if (fwin->resizebar) {
		resizebar_unmap(fwin);
		if (fwin->flags.map_resizebar) {
			resizebar_map(fwin, width, height);
			resizebar_update(fwin, width, height);
		}
	} else {
		resizebar_create(fwin, width);
		resizebar_map(fwin, width, height);
	}

	if (height + fwin->top_width + fwin->bottom_width != fwin->height && !(flags & WFF_IS_SHADED))
		wFrameWindowResize(fwin, width, height + fwin->top_width + fwin->bottom_width);

	if (fwin->flags.border)
		XSetWindowBorderWidth(dpy, fwin->core->window, scr->frame_border_width);
	else
		XSetWindowBorderWidth(dpy, fwin->core->window, 0);

	checkTitleSize(fwin);

	allocFrameBorderPixel(fwin->colormap, WMGetColorRGBDescription(scr->frame_border_color), &fwin->border_pixel);
	allocFrameBorderPixel(fwin->colormap, WMGetColorRGBDescription(scr->frame_focused_border_color), &fwin->focused_border_pixel);
	allocFrameBorderPixel(fwin->colormap, WMGetColorRGBDescription(scr->frame_selected_border_color), &fwin->selected_border_pixel);

	if (flags & WFF_SELECTED) {
		if (fwin->selected_border_pixel)
			XSetWindowBorder(dpy, fwin->core->window, *fwin->selected_border_pixel);
	} else {
		if (fwin->flags.state == WS_FOCUSED) {
			if (fwin->focused_border_pixel)
				XSetWindowBorder(dpy, fwin->core->window, *fwin->focused_border_pixel);
		} else {
			if (fwin->border_pixel)
				XSetWindowBorder(dpy, fwin->core->window, *fwin->border_pixel);
		}
	}
}

void framewindow_unmap(WFrameWindow *fwin)
{
	titlebar_unmap(fwin);
	resizebar_unmap(fwin);

	RemoveFromStackList(fwin->core);
}

void wFrameWindowDestroy(WFrameWindow *fwin)
{
	titlebar_destroy(fwin);
	resizebar_destroy(fwin);

	if (fwin->core && fwin->core->stacking) {
		wfree(fwin->core->stacking);
		fwin->core->stacking = NULL;
	}

	wcore_unmap(fwin->core);
	wframewindow_destroy_wcorewindow(fwin->core);
	fwin->core = NULL;

	destroy_framewin_buttons(fwin);

	wfree(fwin);
}

void wFrameWindowChangeState(WFrameWindow * fwin, int state)
{
	if (fwin->flags.state == state)
		return;

	fwin->flags.state = state;
	fwin->flags.need_texture_change = 1;

	if (fwin->flags.state == WS_FOCUSED) {
		if (fwin->focused_border_pixel)
			XSetWindowBorder(dpy, fwin->core->window, *fwin->focused_border_pixel);
	} else {
		if (fwin->border_pixel)
			XSetWindowBorder(dpy, fwin->core->window, *fwin->border_pixel);
	}
	wFrameWindowPaint(fwin);
}

static void updateTitlebar(WFrameWindow *fwin)
{
	int w, x = 0;
#ifdef XKB_BUTTON_HINT
	int language_button_pos_width, language_button_pos_height;
#endif
	int theight = WMFontHeight(*fwin->font) + (*fwin->title_clearance + TITLEBAR_EXTEND_SPACE) * 2;

	if (theight > *fwin->title_max_height)
		theight = *fwin->title_max_height;

	if (theight < *fwin->title_min_height)
		theight = *fwin->title_min_height;

	w = fwin->width + 1;

#ifdef XKB_BUTTON_HINT
	if (fwin->language_button && fwin->flags.map_language_button) {
		if (!fwin->flags.hide_left_button && fwin->left_button && !fwin->flags.lbutton_dont_fit) {
			if (wPreferences.new_style == TS_NEW) {
				language_button_pos_width = fwin->bordersize;
				language_button_pos_height = 0;
			} else {
				language_button_pos_width = fwin->bordersize + 6;
				language_button_pos_height = 4;
			}
		} else {
			if (wPreferences.new_style == TS_NEW) {
				language_button_pos_width = 0;
				language_button_pos_height = 0;
			} else {
				language_button_pos_width = 3;
				language_button_pos_height = 4;
			}
		}

		wCoreConfigure(fwin->language_button, language_button_pos_width, language_button_pos_height,
			       fwin->bordersize, fwin->bordersize);
	}
#endif /* XKB_BUTTON_HINT */

	if (wPreferences.new_style == TS_NEW) {
		if (!fwin->flags.hide_left_button && fwin->left_button &&
		    fwin->flags.map_left_button && !fwin->flags.lbutton_dont_fit) {
			x = fwin->bordersize;
			w -= fwin->bordersize;
		}

#ifdef XKB_BUTTON_HINT
		if (!fwin->flags.hide_language_button && fwin->language_button &&
		    fwin->flags.map_language_button && !fwin->flags.languagebutton_dont_fit) {
			x += fwin->bordersize;
			w -= fwin->bordersize;
		}
#endif /* XKB_BUTTON_HINT */

		if (!fwin->flags.hide_right_button && fwin->right_button &&
		    fwin->flags.map_right_button && !fwin->flags.rbutton_dont_fit)
			w -= fwin->bordersize;

		fwin->flags.need_texture_remake = 1;
	} else {
		if (fwin->titlebar->width != w)
			fwin->flags.need_texture_remake = 1;
	}

	wCoreConfigure(fwin->titlebar, x, 0, w, theight);
}

void wframewindow_show_rightbutton(WFrameWindow *fwin)
{
	if (fwin->right_button && fwin->flags.hide_right_button) {
		if (!fwin->flags.rbutton_dont_fit)
			XMapWindow(dpy, fwin->right_button->window);

		fwin->flags.hide_right_button = 0;
		fwin->flags.map_right_button = 1;
	}
}

void wframewindow_hide_rightbutton(WFrameWindow *fwin)
{
	if (fwin->right_button && fwin->flags.map_right_button) {
		XUnmapWindow(dpy, fwin->right_button->window);
		fwin->flags.hide_right_button = 1;
		fwin->flags.map_right_button = 0;
	}
}

void wframewindow_show_languagebutton(WFrameWindow *fwin)
{
#ifdef XKB_BUTTON_HINT
	if (fwin->language_button && fwin->flags.hide_language_button) {
		if (!fwin->flags.languagebutton_dont_fit)
			XMapWindow(dpy, fwin->language_button->window);

		fwin->flags.hide_language_button = 0;
		fwin->flags.map_language_button = 1;
	}
#else
	(void) fwin;
#endif
}

void wframewindow_hide_languagebutton(WFrameWindow *fwin)
{
#ifdef XKB_BUTTON_HINT
	if (fwin->language_button && fwin->flags.map_language_button) {
		XUnmapWindow(dpy, fwin->language_button->window);
		fwin->flags.hide_language_button = 1;
		fwin->flags.map_language_button = 0;
	}
#else
	(void) fwin;
#endif
}

void wframewindow_refresh_titlebar(WFrameWindow *fwin)
{
	if (fwin->titlebar && fwin->flags.titlebar) {
		if (wPreferences.new_style == TS_NEW) {
			updateTitlebar(fwin);
		} else {
#ifdef XKB_BUTTON_HINT
			updateTitlebar(fwin);
#else
			XClearWindow(dpy, fwin->titlebar->window);
			wFrameWindowPaint(fwin);
#endif
		}
		checkTitleSize(fwin);
	}
}

static void renderTexture(WScreen *scr, WTexture *texture,
			 int width, int height,
			 int bwidth, int bheight,
			 Pixmap *title,
			 int left, Pixmap *lbutton,
#ifdef XKB_BUTTON_HINT
			 int language, Pixmap *languagebutton,
#endif
			 int right, Pixmap *rbutton)
{
	RImage *img, *limg = NULL, *rimg = NULL, *mimg;
#ifdef XKB_BUTTON_HINT
	RImage *timg = NULL;
#endif
	int x = 0, w;

	*title = None;
	*lbutton = None;
	*rbutton = None;
#ifdef XKB_BUTTON_HINT
	*languagebutton = None;
#endif

	img = wTextureRenderImage(texture, width, height, WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));
		return;
	}

	if (wPreferences.new_style != TS_NEW) {
		RBevelImage(img, RBEV_RAISED2);

		if (!RConvertImage(scr->rcontext, img, title))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

		RReleaseImage(img);
		return;
	}

	if (left)
		limg = RGetSubImage(img, 0, 0, bwidth, bheight);

	w = img->width;

#ifdef XKB_BUTTON_HINT
	if (language)
		timg = RGetSubImage(img, bwidth * left, 0, bwidth, bheight);
#endif

	if (limg) {
		RBevelImage(limg, RBEV_RAISED2);
		if (!RConvertImage(scr->rcontext, limg, lbutton))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

		x += limg->width;
		w -= limg->width;
		RReleaseImage(limg);
	}

#ifdef XKB_BUTTON_HINT
	if (timg) {
		RBevelImage(timg, RBEV_RAISED2);
		if (!RConvertImage(scr->rcontext, timg, languagebutton))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

		x += timg->width;
		w -= timg->width;
		RReleaseImage(timg);
	}
#endif

	if (right)
		rimg = RGetSubImage(img, width - bwidth, 0, bwidth, bheight);

	if (rimg) {
		RBevelImage(rimg, RBEV_RAISED2);
		if (!RConvertImage(scr->rcontext, rimg, rbutton))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

		w -= rimg->width;
		RReleaseImage(rimg);
	}

	if (w != width) {
		mimg = RGetSubImage(img, x, 0, w, img->height);
		RBevelImage(mimg, RBEV_RAISED2);

		if (!RConvertImage(scr->rcontext, mimg, title))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

		RReleaseImage(mimg);
	} else {
		RBevelImage(img, RBEV_RAISED2);

		if (!RConvertImage(scr->rcontext, img, title))
			wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));
	}

	RReleaseImage(img);
}

static void
renderResizebarTexture(WScreen * scr, WTexture * texture, int width, int height, int cwidth, Pixmap * pmap)
{
	RImage *img;
	RColor light;
	RColor dark;

	*pmap = None;

	img = wTextureRenderImage(texture, width, height, WREL_FLAT);
	if (!img) {
		wwarning(_("could not render texture: %s"), RMessageForError(RErrorCode));
		return;
	}

	light.alpha = 0;
	light.red = light.green = light.blue = 80;

	dark.alpha = 0;
	dark.red = dark.green = dark.blue = 40;

	ROperateLine(img, RSubtractOperation, 0, 0, width - 1, 0, &dark);
	ROperateLine(img, RAddOperation, 0, 1, width - 1, 1, &light);

	ROperateLine(img, RSubtractOperation, cwidth, 2, cwidth, height - 1, &dark);
	ROperateLine(img, RAddOperation, cwidth + 1, 2, cwidth + 1, height - 1, &light);

	if (width > 1)
		ROperateLine(img, RSubtractOperation, width - cwidth - 2, 2,
			     width - cwidth - 2, height - 1, &dark);
	ROperateLine(img, RAddOperation, width - cwidth - 1, 2, width - cwidth - 1, height - 1, &light);

#ifdef SHADOW_RESIZEBAR
	ROperateLine(img, RAddOperation, 0, 1, 0, height - 1, &light);
	ROperateLine(img, RSubtractOperation, width - 1, 1, width - 1, height - 1, &dark);
	ROperateLine(img, RSubtractOperation, 0, height - 1, width - 1, height - 1, &dark);
#endif				/* SHADOW_RESIZEBAR */

	if (!RConvertImage(scr->rcontext, img, pmap))
		wwarning(_("error rendering image: %s"), RMessageForError(RErrorCode));

	RReleaseImage(img);
}

static void updateTexture_titlebar(WFrameWindow *fwin)
{
	unsigned long pixel;
	int i = fwin->flags.state;

	if (fwin->titlebar && fwin->flags.titlebar) {
		if (fwin->title_texture[i]->any.type != WTEX_SOLID) {
			XSetWindowBackgroundPixmap(dpy, fwin->titlebar->window, fwin->title_back[i]);
			if (wPreferences.new_style == TS_NEW) {
				if (fwin->left_button && fwin->lbutton_back[i])
					XSetWindowBackgroundPixmap(dpy, fwin->left_button->window,
								   fwin->lbutton_back[i]);
#ifdef XKB_BUTTON_HINT
				if (fwin->language_button && fwin->languagebutton_back[i])
					XSetWindowBackgroundPixmap(dpy, fwin->language_button->window,
								   fwin->languagebutton_back[i]);
#endif
				if (fwin->right_button && fwin->rbutton_back[i])
					XSetWindowBackgroundPixmap(dpy, fwin->right_button->window,
								   fwin->rbutton_back[i]);
			}
		} else {
			pixel = fwin->title_texture[i]->solid.normal.pixel;
			XSetWindowBackground(dpy, fwin->titlebar->window, pixel);
			if (wPreferences.new_style == TS_NEW) {
				if (fwin->left_button)
					XSetWindowBackground(dpy, fwin->left_button->window, pixel);
#ifdef XKB_BUTTON_HINT
				if (fwin->language_button)
					XSetWindowBackground(dpy, fwin->language_button->window, pixel);
#endif
				if (fwin->right_button)
					XSetWindowBackground(dpy, fwin->right_button->window, pixel);
			}
		}
		XClearWindow(dpy, fwin->titlebar->window);

		if (fwin->left_button && fwin->flags.map_left_button) {
			XClearWindow(dpy, fwin->left_button->window);
			handleButtonExpose(&fwin->left_button->descriptor, NULL);
		}
#ifdef XKB_BUTTON_HINT
		if (fwin->language_button && fwin->flags.map_language_button) {
			XClearWindow(dpy, fwin->language_button->window);
			handleButtonExpose(&fwin->language_button->descriptor, NULL);
		}
#endif
		if (fwin->right_button && fwin->flags.map_right_button) {
			XClearWindow(dpy, fwin->right_button->window);
			handleButtonExpose(&fwin->right_button->descriptor, NULL);
		}
	}
}

static void updateTexture_resizebar(WFrameWindow *fwin)
{
	if (fwin->resizebar_texture && fwin->resizebar_texture[0] && fwin->resizebar) {
		if (fwin->resizebar_texture[0]->any.type != WTEX_SOLID)
			XSetWindowBackgroundPixmap(dpy, fwin->resizebar->window, fwin->resizebar_back[0]);
		else
			XSetWindowBackground(dpy, fwin->resizebar->window,
					     fwin->resizebar_texture[0]->solid.normal.pixel);

		XClearWindow(dpy, fwin->resizebar->window);
	}
}

static void remakeTexture_titlebar(WFrameWindow *fwin, int state)
{
	Pixmap pmap, lpmap, rpmap;
#ifdef XKB_BUTTON_HINT
	Pixmap tpmap;
#endif

	if (fwin->title_texture[state] && fwin->titlebar && fwin->flags.titlebar) {
		destroy_framewin_button(fwin, state);

		if (fwin->title_texture[state]->any.type != WTEX_SOLID) {
			int left, right;
			int width;
#ifdef XKB_BUTTON_HINT
			int language;
#endif

			/* eventually surrounded by if new_style */
			left = fwin->left_button && fwin->flags.map_left_button &&
			       !fwin->flags.hide_left_button && !fwin->flags.lbutton_dont_fit;
#ifdef XKB_BUTTON_HINT
			language = fwin->language_button && fwin->flags.map_language_button &&
				   !fwin->flags.hide_language_button && !fwin->flags.languagebutton_dont_fit;
#endif
			right = fwin->right_button && fwin->flags.map_right_button &&
				!fwin->flags.hide_right_button && !fwin->flags.rbutton_dont_fit;

			width = fwin->width + 1;

			renderTexture(fwin->vscr->screen_ptr, fwin->title_texture[state],
				      width, fwin->titlebar->height,
				      fwin->titlebar->height, fwin->titlebar->height,
				      &pmap,
				      left, &lpmap,
#ifdef XKB_BUTTON_HINT
				      language, &tpmap,
#endif
				      right, &rpmap);

			fwin->title_back[state] = pmap;
			if (wPreferences.new_style == TS_NEW) {
				fwin->lbutton_back[state] = lpmap;
				fwin->rbutton_back[state] = rpmap;
#ifdef XKB_BUTTON_HINT
				fwin->languagebutton_back[state] = tpmap;
#endif
			}
		}
	}
}

static void remakeTexture_resizebar(WFrameWindow *fwin, int state)
{
	Pixmap pmap;

	if (fwin->resizebar_texture && fwin->resizebar_texture[0]
	    && fwin->resizebar && fwin->flags.resizebar && state == 0) {
		destroy_pixmap(fwin->resizebar_back[0]);
		if (fwin->resizebar_texture[0]->any.type != WTEX_SOLID) {
			renderResizebarTexture(fwin->vscr->screen_ptr,
					       fwin->resizebar_texture[0],
					       fwin->resizebar->width,
					       fwin->resizebar->height, fwin->resizebar_corner_width, &pmap);

			fwin->resizebar_back[0] = pmap;
		}
	}
}

static char *get_title(WFrameWindow *fwin)
{
	if (fwin && fwin->parent_wwin && fwin->parent_wwin->title)
		return fwin->parent_wwin->title;

	if (fwin && fwin->parent_wmenu && fwin->parent_wmenu->title)
		return fwin->parent_wmenu->title;

	return NULL;
}

static void paint_title(WFrameWindow *fwin, int lofs, int rofs, int state)
{
	Drawable buf;
	virtual_screen *vscr = fwin->vscr;
	WScreen *scr = vscr->screen_ptr;
	char *title, *orig_title;
	int w, h, x, y;
	int titlelen;

	orig_title = get_title(fwin);

	if (!orig_title)
		return;

	title = ShrinkString(*fwin->font, orig_title, fwin->titlebar->width - lofs - rofs);
	titlelen = strlen(title);
	w = WMWidthOfString(*fwin->font, title, titlelen);

	switch (fwin->flags.justification) {
	case WTJ_LEFT:
		x = lofs;
		break;

	case WTJ_RIGHT:
		x = fwin->titlebar->width - w - rofs;
		break;

	default:
		x = lofs + (fwin->titlebar->width - w - lofs - rofs) / 2;
	}

	y = *fwin->title_clearance + TITLEBAR_EXTEND_SPACE;
	h = WMFontHeight(*fwin->font);

	if (y * 2 + h > *fwin->title_max_height)
		y = (*fwin->title_max_height - h) / 2;

	if (y * 2 + h < *fwin->title_min_height)
		y = (*fwin->title_min_height - h) / 2;

	/* We use a w+2 buffer to have an extra pixel on the left and
	 * another one on the right. This is because for some odd reason,
	 * sometimes when using AA fonts (when libfreetype2 is compiled
	 * with bytecode interpreter turned off), some fonts are drawn
	 * starting from x = -1 not from 0 as requested. Observed with
	 * capital A letter on the bold 'trebuchet ms' font. -Dan
	 */
	buf = XCreatePixmap(dpy, fwin->titlebar->window, w + 2, h, scr->w_depth);

	XSetClipMask(dpy, scr->copy_gc, None);

	if (fwin->title_texture[state]->any.type != WTEX_SOLID) {
		XCopyArea(dpy, fwin->title_back[state], buf, scr->copy_gc,
			  x - 1, y, w + 2, h, 0, 0);
	} else {
		XSetForeground(dpy, scr->copy_gc, fwin->title_texture[state]->solid.normal.pixel);
		XFillRectangle(dpy, buf, scr->copy_gc, 0, 0, w + 2, h);
	}

	WMDrawString(scr->wmscreen, buf, fwin->title_color[state],
		     *fwin->font, 1, 0, title, titlelen);

	XCopyArea(dpy, buf, fwin->titlebar->window, scr->copy_gc, 0, 0, w + 2, h, x - 1, y);
	XFreePixmap(dpy, buf);

	wfree(title);
}

void wFrameWindowPaint(WFrameWindow *fwin)
{
	int state, tmp_state, i;

	state = fwin->flags.state;

	if (fwin->flags.is_client_window_frame)
		fwin->flags.justification = wPreferences.title_justification;

	if (fwin->flags.need_texture_remake) {
		fwin->flags.need_texture_remake = 0;
		fwin->flags.need_texture_change = 0;

		if (fwin->flags.single_texture)
			tmp_state = 0;
		else
			tmp_state = state;

		/* first render the texture for the current state... */
		remakeTexture_titlebar(fwin, tmp_state);
		remakeTexture_resizebar(fwin, tmp_state);
		/* ... and paint it */
		updateTexture_titlebar(fwin);
		updateTexture_resizebar(fwin);

		if (!fwin->flags.single_texture) {
			for (i = 0; i < 3; i++) {
				if (i != state) {
					remakeTexture_titlebar(fwin, i);
					remakeTexture_resizebar(fwin, i);
					if (i == 0)
						updateTexture_resizebar(fwin);
				}
			}
		}
	}

	if (fwin->flags.need_texture_change) {
		fwin->flags.need_texture_change = 0;
		updateTexture_titlebar(fwin);
		updateTexture_resizebar(fwin);
	}

	if (fwin->titlebar && fwin->flags.titlebar && !fwin->flags.repaint_only_resizebar
	    && fwin->title_texture[state]->any.type == WTEX_SOLID)
		wDrawBevel(fwin->titlebar->window, fwin->titlebar->width,
			   fwin->titlebar->height, (WTexSolid *) fwin->title_texture[state], WREL_RAISED);

	if (fwin->resizebar && fwin->flags.resizebar &&
	    !fwin->flags.repaint_only_titlebar &&
	    fwin->resizebar_texture[0]->any.type == WTEX_SOLID)
		wDrawBevel_resizebar(fwin->resizebar->window, fwin->resizebar->width,
				     fwin->resizebar->height, (WTexSolid *) fwin->resizebar_texture[0],
				     fwin->resizebar_corner_width);

	if (fwin->titlebar && fwin->flags.titlebar && !fwin->flags.repaint_only_resizebar) {
		int lofs = 6, rofs = 6;

		if (!wPreferences.new_style == TS_NEW) {
			if (fwin->left_button && fwin->flags.map_left_button &&
			    !fwin->flags.hide_left_button && !fwin->flags.lbutton_dont_fit)
				lofs += fwin->bordersize + 3;

#ifdef XKB_BUTTON_HINT
			if (fwin->language_button && fwin->flags.map_language_button &&
			    !fwin->flags.hide_language_button && !fwin->flags.languagebutton_dont_fit)
				lofs += fwin->bordersize;
#endif

			if (fwin->right_button && fwin->flags.map_right_button &&
			    !fwin->flags.hide_right_button && !fwin->flags.rbutton_dont_fit)
				rofs += fwin->bordersize + 3;
		}
#ifdef XKB_BUTTON_HINT
		fwin->languagebutton_image = fwin->vscr->screen_ptr->b_pixmaps[WBUT_XKBGROUP1 + fwin->languagemode];
#endif

		paint_title(fwin, lofs, rofs, state);

		if (fwin->left_button && fwin->flags.map_left_button)
			handleButtonExpose(&fwin->left_button->descriptor, NULL);

		if (fwin->right_button && fwin->flags.map_right_button)
			handleButtonExpose(&fwin->right_button->descriptor, NULL);
#ifdef XKB_BUTTON_HINT
		if (fwin->language_button && fwin->flags.map_language_button)
			handleButtonExpose(&fwin->language_button->descriptor, NULL);
#endif
	}
}

static void reconfigure(WFrameWindow * fwin, int x, int y, int width, int height, Bool dontMove)
{
	int k = (wPreferences.new_style == TS_NEW ? 4 : 3);
	int resizedHorizontally = 0;

	if (dontMove)
		XResizeWindow(dpy, fwin->core->window, width, height);
	else
		XMoveResizeWindow(dpy, fwin->core->window, x, y, width, height);

	if (fwin->width != width) {
		fwin->flags.need_texture_remake = 1;
		resizedHorizontally = 1;
	}

	fwin->width = width;
	fwin->core->width = width;
	fwin->height = height;
	fwin->core->height = height;

	if (fwin->titlebar && fwin->flags.titlebar && resizedHorizontally) {
		/* Check if the titlebar is wide enough to hold the buttons.
		 * Temporarily remove them if can't
		 */
		if (fwin->left_button && fwin->flags.map_left_button) {
			if (width < fwin->top_width * k && !fwin->flags.lbutton_dont_fit) {
				if (!fwin->flags.hide_left_button)
					XUnmapWindow(dpy, fwin->left_button->window);

				fwin->flags.lbutton_dont_fit = 1;
			} else if (width >= fwin->top_width * k && fwin->flags.lbutton_dont_fit) {
				if (!fwin->flags.hide_left_button)
					XMapWindow(dpy, fwin->left_button->window);

				fwin->flags.lbutton_dont_fit = 0;
			}
		}
#ifdef XKB_BUTTON_HINT
		if (fwin->language_button && fwin->flags.map_language_button) {
			if (width < fwin->top_width * k && !fwin->flags.languagebutton_dont_fit) {
				if (!fwin->flags.hide_language_button)
					XUnmapWindow(dpy, fwin->language_button->window);

				fwin->flags.languagebutton_dont_fit = 1;
			} else if (width >= fwin->top_width * k && fwin->flags.languagebutton_dont_fit) {
				if (!fwin->flags.hide_language_button)
					XMapWindow(dpy, fwin->language_button->window);

				fwin->flags.languagebutton_dont_fit = 0;
			}
		}
#endif

		if (fwin->right_button && fwin->flags.map_right_button) {
			if (width < fwin->top_width * 2 && !fwin->flags.rbutton_dont_fit) {
				if (!fwin->flags.hide_right_button)
					XUnmapWindow(dpy, fwin->right_button->window);

				fwin->flags.rbutton_dont_fit = 1;
			} else if (width >= fwin->top_width * 2 && fwin->flags.rbutton_dont_fit) {
				if (!fwin->flags.hide_right_button)
					XMapWindow(dpy, fwin->right_button->window);

				fwin->flags.rbutton_dont_fit = 0;
			}
		}

		if (wPreferences.new_style == TS_NEW) {
			if (fwin->right_button && fwin->flags.map_right_button)
				XMoveWindow(dpy, fwin->right_button->window,
					    width - fwin->bordersize + 1, 0);
		} else {
			if (fwin->right_button && fwin->flags.map_right_button)
				XMoveWindow(dpy, fwin->right_button->window,
					    width - fwin->bordersize - 3,
					    (fwin->titlebar->height - fwin->bordersize) / 2);
		}
		updateTitlebar(fwin);
		checkTitleSize(fwin);
	}

	if (fwin->resizebar && fwin->flags.resizebar) {
		wCoreConfigure(fwin->resizebar, 0,
			       fwin->height - fwin->resizebar->height,
			       fwin->width, fwin->resizebar->height);

		fwin->resizebar_corner_width = RESIZEBAR_CORNER_WIDTH;
		if (fwin->width < RESIZEBAR_CORNER_WIDTH * 2 + RESIZEBAR_MIN_WIDTH)
			fwin->resizebar_corner_width = fwin->width / 2;
	}
}

void wFrameWindowConfigure(WFrameWindow * fwin, int x, int y, int width, int height)
{
	reconfigure(fwin, x, y, width, height, False);
}

void wFrameWindowResize(WFrameWindow * fwin, int width, int height)
{
	reconfigure(fwin, 0, 0, width, height, True);
}

int wFrameWindowChangeTitle(WFrameWindow *fwin, const char *new_title)
{
	if (new_title == NULL)
		return 0;

	if (fwin->titlebar && fwin->flags.titlebar) {
		XClearWindow(dpy, fwin->titlebar->window);
		wFrameWindowPaint(fwin);
	}

	checkTitleSize(fwin);

	return 1;
}

#ifdef XKB_BUTTON_HINT
void wFrameWindowUpdateLanguageButton(WFrameWindow * fwin)
{
	paintButton(fwin->language_button, fwin->title_texture[fwin->flags.state],
		    WMColorPixel(fwin->title_color[fwin->flags.state]), fwin->languagebutton_image, True);
}
#endif				/* XKB_BUTTON_HINT */

/*********************************************************************/

static void handleExpose(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;

	if (fwin->titlebar && fwin->flags.titlebar && fwin->titlebar->window == event->xexpose.window)
		fwin->flags.repaint_only_titlebar = 1;

	if (fwin->resizebar && fwin->resizebar->window == event->xexpose.window)
		fwin->flags.repaint_only_resizebar = 1;

	wFrameWindowPaint(fwin);
	fwin->flags.repaint_only_titlebar = 0;
	fwin->flags.repaint_only_resizebar = 0;
}

static void checkTitleSize(WFrameWindow *fwin)
{
	int width;
	char *title;

	title = get_title(fwin);

	if (!title) {
		fwin->flags.incomplete_title = 0;
		return;
	}

	if (!fwin->titlebar) {
		fwin->flags.incomplete_title = 1;
		return;
	}

	width = fwin->titlebar->width - 6 - 6;

	if (!wPreferences.new_style == TS_NEW) {
		if (fwin->left_button && fwin->flags.map_left_button &&
		    !fwin->flags.hide_left_button && !fwin->flags.lbutton_dont_fit)
			width -= fwin->bordersize + 3;

#ifdef XKB_BUTTON_HINT
		if (fwin->language_button && fwin->flags.map_language_button &&
		    !fwin->flags.hide_language_button && !fwin->flags.languagebutton_dont_fit)
			width -= fwin->bordersize + 3;
#endif

		if (fwin->right_button && fwin->flags.map_right_button &&
		    !fwin->flags.hide_right_button && !fwin->flags.rbutton_dont_fit)
			width -= fwin->bordersize + 3;
	}

	if (WMWidthOfString(*fwin->font, title, strlen(title)) > width)
		fwin->flags.incomplete_title = 1;
	else
		fwin->flags.incomplete_title = 0;
}

static void paintButton(WCoreWindow *button, WTexture *texture, unsigned long color, WPixmap *image, int pushed)
{
	WScreen *scr = button->vscr->screen_ptr;
	GC copy_gc = scr->copy_gc;
	int x = 0, y = 0, d = 0;
	int left = 0, width = 0;

	/* setup stuff according to the state */
	if (pushed) {
		if (image) {
			if (image->width >= image->height * 2) {
				/* the image contains 2 pictures: the second is for the
				 * pushed state */
				width = image->width / 2;
				left = image->width / 2;
			} else {
				width = image->width;
			}
		}
		XSetClipMask(dpy, copy_gc, None);
		if (wPreferences.new_style == TS_NEXT)
			XSetForeground(dpy, copy_gc, scr->black_pixel);
		else
			XSetForeground(dpy, copy_gc, scr->white_pixel);

		d = 1;
		if (wPreferences.new_style == TS_NEW) {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 1, button->height - 1);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 1, button->height - 1);
		} else if (wPreferences.new_style == TS_OLD) {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
		} else {
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 3, button->height - 3);
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			XDrawRectangle(dpy, button->window, copy_gc, 0, 0, button->width - 3, button->height - 3);
		}
	} else {
		XClearWindow(dpy, button->window);

		if (image) {
			if (image->width >= image->height * 2)
				width = image->width / 2;
			else
				width = image->width;
		}
		d = 0;

		if (wPreferences.new_style == TS_NEW) {
			if (texture->any.type == WTEX_SOLID || pushed)
				wDrawBevel(button->window, button->width, button->height,
					   (WTexSolid *) texture, WREL_RAISED);
		} else {
			wDrawBevel(button->window, button->width, button->height,
				   scr->widget_texture, WREL_RAISED);
		}
	}

	if (image) {
		/* display image */
		XSetClipMask(dpy, copy_gc, image->mask);
		x = (button->width - width) / 2 + d;
		y = (button->height - image->height) / 2 + d;
		XSetClipOrigin(dpy, copy_gc, x - left, y);
		if (!wPreferences.new_style == TS_NEW) {
			XSetForeground(dpy, copy_gc, scr->black_pixel);
			if (!pushed) {
				if (image->depth == 1)
					XCopyPlane(dpy, image->image, button->window, copy_gc,
						   left, 0, width, image->height, x, y, 1);
				else
					XCopyArea(dpy, image->image, button->window, copy_gc,
						  left, 0, width, image->height, x, y);
			} else {
				if (wPreferences.new_style == TS_OLD) {
					XSetForeground(dpy, copy_gc, scr->dark_pixel);
					XFillRectangle(dpy, button->window, copy_gc, 0, 0,
						       button->width, button->height);
				} else {
					XSetForeground(dpy, copy_gc, scr->black_pixel);
					XCopyArea(dpy, image->image, button->window, copy_gc,
						  left, 0, width, image->height, x, y);
				}
			}
		} else {
			if (pushed) {
				XSetForeground(dpy, copy_gc, scr->black_pixel);
			} else {
				XSetForeground(dpy, copy_gc, color);
				XSetBackground(dpy, copy_gc, texture->any.color.pixel);
			}
			XFillRectangle(dpy, button->window, copy_gc, 0, 0, button->width, button->height);
		}
	}
}

static void handleButtonExpose(WObjDescriptor *desc, XEvent *event)
{
	WFrameWindow *fwin = (WFrameWindow *) desc->parent;
	WCoreWindow *button = (WCoreWindow *) desc->self;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

#ifdef XKB_BUTTON_HINT
	if (button == fwin->language_button) {
		if (wPreferences.modelock)
			paintButton(button, fwin->title_texture[fwin->flags.state],
				    WMColorPixel(fwin->title_color[fwin->flags.state]),
				    fwin->languagebutton_image, False);
		return;
	}
#endif
	if (button == fwin->left_button) {
		paintButton(button, fwin->title_texture[fwin->flags.state],
			    WMColorPixel(fwin->title_color[fwin->flags.state]), fwin->lbutton_image, False);
		return;
	}

	if (button == fwin->right_button)
		paintButton(button, fwin->title_texture[fwin->flags.state],
			    WMColorPixel(fwin->title_color[fwin->flags.state]), fwin->rbutton_image, False);
}

static void titlebarMouseDown(WObjDescriptor *desc, XEvent *event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *titlebar = desc->self;

	if (IsDoubleClick(fwin->core->vscr, event)) {
		if (fwin->on_dblclick_titlebar)
			(*fwin->on_dblclick_titlebar) (titlebar, fwin->child, event);
	} else {
		if (fwin->on_mousedown_titlebar)
			(*fwin->on_mousedown_titlebar) (titlebar, fwin->child, event);
	}
}

static void resizebarMouseDown(WObjDescriptor * desc, XEvent * event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *resizebar = desc->self;

	if (fwin->on_mousedown_resizebar)
		(*fwin->on_mousedown_resizebar) (resizebar, fwin->child, event);
}

static void buttonMouseDown(WObjDescriptor *desc, XEvent *event)
{
	WFrameWindow *fwin = desc->parent;
	WCoreWindow *button = desc->self;
	WPixmap *image;
	XEvent ev;
	int done = 0, execute = 1;
	WTexture *texture;
	unsigned long pixel;
	int clickButton = event->xbutton.button;

	if (IsDoubleClick(fwin->core->vscr, event)) {
		if (button == fwin->right_button && fwin->on_dblclick_right)
			(*fwin->on_dblclick_right) (button, fwin->child, event);

		return;
	}

	if (button == fwin->left_button)
		image = fwin->lbutton_image;
	else
		image = fwin->rbutton_image;

#ifdef XKB_BUTTON_HINT
	if (button == fwin->language_button) {
		if (!wPreferences.modelock)
			return;
		image = fwin->languagebutton_image;
	}
#endif

	pixel = WMColorPixel(fwin->title_color[fwin->flags.state]);
	texture = fwin->title_texture[fwin->flags.state];
	paintButton(button, texture, pixel, image, True);

	while (!done) {
		WMMaskEvent(dpy, LeaveWindowMask | EnterWindowMask | ButtonReleaseMask
			    | ButtonPressMask | ExposureMask, &ev);
		switch (ev.type) {
		case LeaveNotify:
			execute = 0;
			paintButton(button, texture, pixel, image, False);
			break;

		case EnterNotify:
			execute = 1;
			paintButton(button, texture, pixel, image, True);
			break;

		case ButtonPress:
			break;

		case ButtonRelease:
			if (ev.xbutton.button == clickButton)
				done = 1;
			break;

		default:
			WMHandleEvent(&ev);
		}
	}
	paintButton(button, texture, pixel, image, False);

	if (!execute)
		return;

	if (button == fwin->left_button) {
		if (fwin->on_click_left)
			(*fwin->on_click_left) (button, fwin->child, &ev);
		return;
	}

	if (button == fwin->right_button) {
		if (fwin->on_click_right)
			(*fwin->on_click_right) (button, fwin->child, &ev);
		return;
	}

#ifdef XKB_BUTTON_HINT
	if (button == fwin->language_button) {
		if (fwin->on_click_language)
			(*fwin->on_click_language) (button, fwin->child, &ev);
		return;
	}
#endif
}
