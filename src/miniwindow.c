/* miniwindow.c - Window Icon code
 *
 *  AWindow Maker window manager
 *
 *  Copyright (c) 2019 Rodolfo García Peñas (kix)
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include "WindowMaker.h"
#include "miniwindow.h"
#include "actions.h"
#include "event.h"
#include "misc.h"
#include "stacking.h"
#include "winmenu.h"
#include "placement.h"
#include "xinerama.h"

static void miniwindow_create_minipreview_showerror(WWindow *wwin);
static void miniwindow_DblClick(WObjDescriptor *desc, XEvent *event);
static WIcon *miniwindow_create_icon(WWindow *wwin);
static void miniwindow_create_minipreview(WWindow *wwin);
static void miniwindow_icon_map(WIcon *icon);

WMiniWindow *miniwindow_create(void)
{
	WMiniWindow *miniwindow = NULL;

	miniwindow = wmalloc(sizeof(WMiniWindow));
	return miniwindow;
}

void miniwindow_destroy(WWindow *wwin)
{
	wfree(wwin->miniwindow);
	wwin->miniwindow = NULL;
}

static WIcon *miniwindow_create_icon(WWindow *wwin)
{
	WIcon *icon = NULL;

	icon = icon_create_core(wwin->vscr);
	icon->owner = wwin;
	icon->tile_type = TILE_NORMAL;
	set_icon_image_from_database(icon, wwin->wm_instance, wwin->wm_class, NULL);

#ifdef NO_MINIWINDOW_TITLES
	icon->show_title = 0;
#else
	icon->show_title = 1;
#endif

	return icon;
}

static void miniwindow_create_minipreview(WWindow *wwin)
{
	Pixmap pixmap;
	int ret;

	ret = create_minipixmap_for_wwindow(wwin->vscr, wwin, &pixmap);
	if (ret) {
		miniwindow_create_minipreview_showerror(wwin);
		return;
	}

	if (wwin->miniwindow->icon->mini_preview != None)
		XFreePixmap(dpy, wwin->miniwindow->icon->mini_preview);

	wwin->miniwindow->icon->mini_preview = pixmap;
}

static void miniwindow_create_minipreview_showerror(WWindow *wwin)
{
	const char *title;
	char title_buf[32];

	if (wwin->title) {
		title = wwin->title;
	} else {
		snprintf(title_buf, sizeof(title_buf), "(id=0x%lx)", wwin->client_win);
		title = title_buf;
	}

	wwarning(_("creation of mini-preview failed for window \"%s\""), title);
}

static void miniwindow_icon_map(WIcon *icon)
{
	WWindow *wwin = icon->owner;
	virtual_screen *vscr = wwin->vscr;
	WScreen *scr = vscr->screen_ptr;

	wcore_map_toplevel(icon->core, vscr, wwin->miniwindow->icon_x, wwin->miniwindow->icon_y,
			   wPreferences.icon_size, wPreferences.icon_size, 0, scr->w_depth,
			   scr->w_visual, scr->w_colormap, scr->white_pixel);

	if (wwin->wm_hints && (wwin->wm_hints->flags & IconWindowHint)) {
		if (wwin->client_win == wwin->main_window) {
			WApplication *wapp;
			/* do not let miniwindow steal app-icon's icon window */
			wapp = wApplicationOf(wwin->client_win);
			if (!wapp || wapp->app_icon == NULL)
				icon->icon_win = wwin->wm_hints->icon_window;
		} else {
			icon->icon_win = wwin->wm_hints->icon_window;
		}
	}

	wIconChangeTitle(icon, wwin);

	map_icon_image(icon);

	WMAddNotificationObserver(icon_appearanceObserver, icon, WNIconAppearanceSettingsChanged, icon);
	WMAddNotificationObserver(icon_tileObserver, icon, WNIconTileSettingsChanged, icon);
}

void miniwindow_destroy_icon(WWindow *wwin)
{
	if (!wwin->miniwindow->icon)
		return;

	RemoveFromStackList(wwin->miniwindow->icon->vscr, wwin->miniwindow->icon->core);
	wIconDestroy(wwin->miniwindow->icon);
	wwin->miniwindow->icon = NULL;
}

void miniwindow_removeIcon(WWindow *wwin)
{
	if (wwin->miniwindow->icon == NULL)
		return;

	if (wwin->flags.miniaturized && wwin->miniwindow->icon->mapped) {
		miniwindow_unmap(wwin);
		miniwindow_destroy_icon(wwin);
	}
}

void miniwindow_updatetitle(WWindow *wwin)
{
	if (!wwin->miniwindow->icon)
		return;

	wIconChangeTitle(wwin->miniwindow->icon, wwin);
	wIconPaint(wwin->miniwindow->icon);
}

void miniwindow_map(WWindow *wwin)
{
	if (!wwin->miniwindow->icon)
		return;

	XMapWindow(dpy, wwin->miniwindow->icon->core->window);
	wwin->miniwindow->icon->mapped = 1;
}

void miniwindow_unmap(WWindow *wwin)
{
	if (!wwin->miniwindow->icon)
		return;

	XUnmapWindow(dpy, wwin->miniwindow->icon->core->window);
	wwin->miniwindow->icon->mapped = 0;
}

void miniwindow_iconupdate(WWindow *wwin)
{
	if (wwin->miniwindow->icon) {
		wIconUpdate(wwin->miniwindow->icon);
		wIconPaint(wwin->miniwindow->icon);
	}
}

/* Callbacks */

void miniwindow_Expose(WObjDescriptor *desc, XEvent *event)
{
	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wIconPaint(desc->parent);
}

static void miniwindow_DblClick(WObjDescriptor *desc, XEvent *event)
{
	WIcon *icon = desc->parent;

	/* Parameter not used, but tell the compiler that it is ok */
	(void) event;

	wDeiconifyWindow(icon->owner);
}

void miniwindow_MouseDown(WObjDescriptor *desc, XEvent *event)
{
	WIcon *icon = desc->parent;
	WWindow *wwin = icon->owner;
	XEvent ev;
	int x = wwin->miniwindow->icon_x, y = wwin->miniwindow->icon_y;
	int dx = event->xbutton.x, dy = event->xbutton.y;
	int grabbed = 0;
	int clickButton = event->xbutton.button;
	Bool hasMoved = False;

	if (WCHECK_STATE(WSTATE_MODAL))
		return;

	if (IsDoubleClick(icon->vscr, event)) {
		miniwindow_DblClick(desc, event);
		return;
	}

	if (event->xbutton.button == Button1) {
		if (event->xbutton.state & wPreferences.modifier_mask)
			wLowerFrame(icon->vscr, icon->core);
		else
			wRaiseFrame(icon->vscr, icon->core);
		if (event->xbutton.state & ShiftMask) {
			wIconSelect(icon);
			wSelectWindow(icon->owner, !wwin->flags.selected);
		}
	} else if (event->xbutton.button == Button3) {
		WObjDescriptor *desc;

		OpenWindowMenu(wwin, event->xbutton.x_root, event->xbutton.y_root, False);

		/* allow drag select of menu */
		desc = &wwin->vscr->menu.window_menu->core->descriptor;
		event->xbutton.send_event = True;
		(*desc->handle_mousedown) (desc, event);

		return;
	}

	if (XGrabPointer(dpy, icon->core->window, False, ButtonMotionMask
			 | ButtonReleaseMask | ButtonPressMask, GrabModeAsync,
			 GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
	}

	while (1) {
		WMMaskEvent(dpy, PointerMotionMask | ButtonReleaseMask | ButtonPressMask
			    | ButtonMotionMask | ExposureMask, &ev);
		switch (ev.type) {
		case Expose:
			WMHandleEvent(&ev);
			break;

		case MotionNotify:
			hasMoved = True;
			if (!grabbed) {
				if (abs(dx - ev.xmotion.x) >= MOVE_THRESHOLD ||
				    abs(dy - ev.xmotion.y) >= MOVE_THRESHOLD) {
					XChangeActivePointerGrab(dpy, ButtonMotionMask
								 | ButtonReleaseMask | ButtonPressMask,
								 wPreferences.cursor[WCUR_MOVE], CurrentTime);
					grabbed = 1;
				} else {
					break;
				}
			}
			x = ev.xmotion.x_root - dx;
			y = ev.xmotion.y_root - dy;
			XMoveWindow(dpy, icon->core->window, x, y);
			break;

		case ButtonPress:
			break;

		case ButtonRelease:
			if (ev.xbutton.button != clickButton)
				break;

			if (wwin->miniwindow->icon_x != x || wwin->miniwindow->icon_y != y)
				wwin->flags.icon_moved = 1;

			XMoveWindow(dpy, icon->core->window, x, y);
			wwin->miniwindow->icon_x = x;
			wwin->miniwindow->icon_y = y;
			XUngrabPointer(dpy, CurrentTime);

			if (wPreferences.auto_arrange_icons)
				wArrangeIcons(wwin->vscr, True);

			if (wPreferences.single_click && !hasMoved)
				miniwindow_DblClick(desc, event);

			return;

		}
	}
}

int miniwindow_get_xpos(WWindow *wwin)
{
	if (!wwin)
		return 0;

	return wwin->miniwindow->icon_x;
}

int miniwindow_get_ypos(WWindow *wwin)
{
	if (!wwin)
		return 0;

	return wwin->miniwindow->icon_y;
}

void miniwindow_icon_show(WWindow *wwin)
{
	WCoord *coord;

	if (!wwin->flags.icon_moved) {
		coord = PlaceIcon(wwin->vscr, wGetHeadForWindow(wwin));
		wwin->miniwindow->icon_x = coord->x;
		wwin->miniwindow->icon_y = coord->y;
		wfree(coord);
	}

	wwin->miniwindow->icon = miniwindow_create_icon(wwin);
	miniwindow_icon_map(wwin->miniwindow->icon);
	wwin->miniwindow->icon->mapped = 1;

	/* extract the window screenshot every time, as the option can be enable anytime */
	if (wwin->client_win && wwin->flags.mapped)
		miniwindow_create_minipreview(wwin);
}
