/* action.c- misc. window commands (miniaturize, hide etc.)
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 1998-2003 Dan Pascu
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wconfig.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "client.h"
#include "icon.h"
#include "miniwindow.h"
#include "colormap.h"
#include "application.h"
#include "actions.h"
#include "stacking.h"
#include "appicon.h"
#include "dock-core.h"
#include "dock.h"
#include "appmenu.h"
#include "winspector.h"
#include "workspace.h"
#include "xinerama.h"
#include "usermenu.h"
#include "placement.h"
#include "misc.h"
#include "event.h"
#include "animations.h"

static void find_Maximus_geometry(WWindow *wwin, WArea usableArea, int *new_x, int *new_y,
				  unsigned int *new_width, unsigned int *new_height);
static void save_old_geometry(WWindow *wwin, int directions);
/******* Local Variables *******/

static int compareTimes(Time t1, Time t2)
{
	Time diff;
	if (t1 == t2)
		return 0;
	diff = t1 - t2;
	return (diff < 60000) ? 1 : -1;
}

/*
 *----------------------------------------------------------------------
 * wSetFocusTo--
 *	Changes the window focus to the one passed as argument.
 * If the window to focus is not already focused, it will be brought
 * to the head of the list of windows. Previously focused window is
 * unfocused.
 *
 * Side effects:
 *	Window list may be reordered and the window focus is changed.
 *
 *----------------------------------------------------------------------
 */
void wSetFocusTo(virtual_screen *vscr, WWindow *wwin)
{
	static virtual_screen *old_scr = NULL;

	WWindow *old_focused;
	WWindow *focused = vscr->window.focused;
	Time timestamp = w_global.timestamp.last_event;
	WApplication *oapp = NULL, *napp = NULL;
	int wasfocused;

	if (vscr->screen_ptr->flags.ignore_focus_events || compareTimes(w_global.timestamp.focus_change, timestamp) > 0)
		return;

	if (!old_scr)
		old_scr = vscr;

	old_focused = old_scr->window.focused;

	w_global.timestamp.focus_change = timestamp;

	if (old_focused)
		oapp = wApplicationOf(old_focused->main_window);

	if (oapp) {
		destroy_app_menu(oapp);
#ifdef USER_MENU
		destroy_user_menu(oapp);
#endif
	}

	if (wwin == NULL) {
		XSetInputFocus(dpy, vscr->screen_ptr->no_focus_win, RevertToParent, CurrentTime);
		if (old_focused)
			wWindowUnfocus(old_focused);

		if ((oapp) && wPreferences.highlight_active_app)
			wApplicationDeactivate(oapp);

		WMPostNotificationName(WMNChangedFocus, NULL, (void *)True);
		return;
	}

	if (old_scr != vscr && old_focused)
		wWindowUnfocus(old_focused);

	wasfocused = wwin->flags.focused;
	napp = wApplicationOf(wwin->main_window);

	/* remember last workspace where the app has been */
	if (napp)
		napp->last_workspace = wwin->frame->workspace;

	if (wwin->flags.mapped && !WFLAGP(wwin, no_focusable)) {
		/* install colormap if colormap mode is lock mode */
		if (wPreferences.colormap_mode == WCM_CLICK)
			wColormapInstallForWindow(vscr, wwin);

		/* set input focus */
		switch (wwin->focus_mode) {
		case WFM_NO_INPUT:
			XSetInputFocus(dpy, vscr->screen_ptr->no_focus_win, RevertToParent, CurrentTime);
			break;
		case WFM_PASSIVE:
		case WFM_LOCALLY_ACTIVE:
			XSetInputFocus(dpy, wwin->client_win, RevertToParent, CurrentTime);
			break;
		case WFM_GLOBALLY_ACTIVE:
			break;
		}

		XFlush(dpy);
		if (wwin->protocols.TAKE_FOCUS)
			wClientSendProtocol(wwin, w_global.atom.wm.take_focus, timestamp);

		XSync(dpy, False);
	} else {
		XSetInputFocus(dpy, vscr->screen_ptr->no_focus_win, RevertToParent, CurrentTime);
	}

	if (WFLAGP(wwin, no_focusable))
		return;

	/* if this is not the focused window focus it */
	if (focused != wwin) {
		/* change the focus window list order */
		if (wwin->prev)
			wwin->prev->next = wwin->next;

		if (wwin->next)
			wwin->next->prev = wwin->prev;

		wwin->prev = focused;
		focused->next = wwin;
		wwin->next = NULL;
		vscr->window.focused = wwin;

		if (oapp && oapp != napp) {
			destroy_app_menu(napp);
#ifdef USER_MENU
			destroy_user_menu(oapp);
#endif
			if (wPreferences.highlight_active_app)
				wApplicationDeactivate(oapp);
		}

		/* reset fullscreen if temporarily removed due to lost focus*/
		if (wwin->flags.fullscreen)
			ChangeStackingLevel(wwin->frame->vscr, wwin->frame->core, WMFullscreenLevel);
	}

	wWindowFocus(wwin, focused);

	if (napp && !wasfocused) {
		create_app_menu(vscr, napp);
#ifdef USER_MENU
		if (!napp->app_menu)
			create_user_menu(vscr, napp);
#endif
	}

	if (napp && wPreferences.highlight_active_app)
		wApplicationActivate(napp);

	XFlush(dpy);
	old_scr = vscr;
}

void wShadeWindow(WWindow *wwin)
{

	if (wwin->flags.shaded)
		return;

	XLowerWindow(dpy, wwin->client_win);
	animation_shade(wwin, SHADE);

	wwin->flags.skip_next_animation = 0;
	wwin->flags.shaded = 1;
	wwin->flags.mapped = 0;
	/* prevent window withdrawal when getting UnmapNotify */
	XSelectInput(dpy, wwin->client_win, wwin->event_mask & ~StructureNotifyMask);
	XUnmapWindow(dpy, wwin->client_win);
	XSelectInput(dpy, wwin->client_win, wwin->event_mask);

	/* for the client it's just like iconification */
	wFrameWindowResize(wwin->frame, wwin->frame->width, wwin->frame->top_width - 1);

	wwin->client.y = wwin->frame_y - wwin->height + wwin->frame->top_width;
	wWindowSynthConfigureNotify(wwin);

	/* wClientSetState(wwin, IconicState, None); */
	WMPostNotificationName(WMNChangedState, wwin, "shade");
	animation_catchevents();
}

void wUnshadeWindow(WWindow *wwin)
{

	if (!wwin->flags.shaded)
		return;

	wwin->flags.shaded = 0;
	wwin->flags.mapped = 1;
	XMapWindow(dpy, wwin->client_win);

	animation_shade(wwin, UNSHADE);

	wwin->flags.skip_next_animation = 0;
	wFrameWindowResize(wwin->frame, wwin->frame->width,
			   wwin->frame->top_width + wwin->height + wwin->frame->bottom_width);

	wwin->client.y = wwin->frame_y + wwin->frame->top_width;
	wWindowSynthConfigureNotify(wwin);

	/* if the window is focused, set the focus again as it was disabled during
	 * shading */
	if (wwin->flags.focused)
		wSetFocusTo(wwin->vscr, wwin);

	WMPostNotificationName(WMNChangedState, wwin, "shade");
}

/* Set the old coordinates using the current values */
static void save_old_geometry(WWindow *wwin, int directions)
{
	/* never been saved? */
	if (!wwin->old_geometry.width)
		directions |= SAVE_GEOMETRY_X | SAVE_GEOMETRY_WIDTH;
	if (!wwin->old_geometry.height)
		directions |= SAVE_GEOMETRY_Y | SAVE_GEOMETRY_HEIGHT;

	if (directions & SAVE_GEOMETRY_X)
		wwin->old_geometry.x = wwin->frame_x;
	if (directions & SAVE_GEOMETRY_Y)
		wwin->old_geometry.y = wwin->frame_y;
	if (directions & SAVE_GEOMETRY_WIDTH)
		wwin->old_geometry.width = wwin->width;
	if (directions & SAVE_GEOMETRY_HEIGHT)
		wwin->old_geometry.height = wwin->height;
}

static void remember_geometry(WWindow *wwin, int *x, int *y, int *w, int *h)
{
	WMRect old_geom_rect;
	int old_head;
	Bool same_head;

	old_geom_rect = wmkrect(wwin->old_geometry.x, wwin->old_geometry.y, wwin->old_geometry.width, wwin->old_geometry.height);
	old_head = wGetHeadForRect(wwin->vscr, old_geom_rect);
	same_head = (wGetHeadForWindow(wwin) == old_head);
	*x = ((wwin->old_geometry.x || wwin->old_geometry.width) && same_head) ? wwin->old_geometry.x : wwin->frame_x;
	*y = ((wwin->old_geometry.y || wwin->old_geometry.height) && same_head) ? wwin->old_geometry.y : wwin->frame_y;
	*w = wwin->old_geometry.width ? wwin->old_geometry.width : wwin->width;
	*h = wwin->old_geometry.height ? wwin->old_geometry.height : wwin->height;
}

/* Remember geometry for unmaximizing */
void update_saved_geometry(WWindow *wwin)
{
	/* NOT if we aren't already maximized
	 * we'll save geometry when maximizing */
	if (!wwin->flags.maximized)
		return;

	/* NOT if we are fully maximized */
	if ((wwin->flags.maximized & MAX_MAXIMUS) ||
	    ((wwin->flags.maximized & MAX_HORIZONTAL) &&
	    (wwin->flags.maximized & MAX_VERTICAL)))
		return;

	/* save the co-ordinate in the axis in which we AREN'T maximized */
	if (wwin->flags.maximized & MAX_HORIZONTAL)
		save_old_geometry(wwin, SAVE_GEOMETRY_Y);
	if (wwin->flags.maximized & MAX_VERTICAL)
		save_old_geometry(wwin, SAVE_GEOMETRY_X);
}

void wMaximizeWindow(WWindow *wwin, int directions, int head)
{
	unsigned int new_width, new_height, half_scr_width, half_scr_height;
	int new_x = 0;
	int new_y = 0;
	int maximus_x = 0;
	int maximus_y = 0;
	unsigned int maximus_width = 0;
	unsigned int maximus_height = 0;
	WArea usableArea, totalArea;
	Bool has_border = 1;
	int adj_size;
	WScreen *scr = wwin->vscr->screen_ptr;

	if (!IS_RESIZABLE(wwin))
		return;

	if (!HAS_BORDER(wwin))
		has_border = 0;

	if (wPreferences.drag_maximized_window == DRAGMAX_NOMOVE)
		wwin->client_flags.no_movable = 1;

	/* the size to adjust the geometry */
	adj_size = wwin->vscr->frame.border_width * 2 * has_border;

	/* save old coordinates before we change the current values */
	if (!wwin->flags.maximized)
		save_old_geometry(wwin, SAVE_GEOMETRY_ALL);

	totalArea.x2 = scr->scr_width;
	totalArea.y2 = scr->scr_height;
	totalArea.x1 = 0;
	totalArea.y1 = 0;


	/* In case of mouse initiated maximize, use the head in which pointer is
	 * located, rather than window position, which is passed to the function */
	if (!(directions & MAX_IGNORE_XINERAMA) && !(directions & MAX_KEYBOARD))
		head = wGetHeadForPointerLocation(wwin->vscr);

	usableArea = wGetUsableAreaForHead(wwin->vscr, head, &totalArea, True);


	/* Only save directions, not kbd or xinerama hints */
	directions &= (MAX_HORIZONTAL | MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS);

	if (WFLAGP(wwin, full_maximize))
		usableArea = totalArea;
	half_scr_width = (usableArea.x2 - usableArea.x1)/2;
	half_scr_height = (usableArea.y2 - usableArea.y1)/2;

	if (wwin->flags.shaded) {
		wwin->flags.skip_next_animation = 1;
		wUnshadeWindow(wwin);
	}

	if (directions & MAX_MAXIMUS) {
		find_Maximus_geometry(wwin, usableArea, &maximus_x, &maximus_y, &maximus_width, &maximus_height);
		new_width = maximus_width - adj_size;
		new_height = maximus_height - adj_size;
		new_x = maximus_x;
		new_y = maximus_y;
		if (WFLAGP(wwin, full_maximize) && (new_y == 0)) {
			new_height += wwin->frame->bottom_width - 1;
			new_y -= wwin->frame->top_width;
		}

		wwin->maximus_x = new_x;
		wwin->maximus_y = new_y;
		wwin->flags.old_maximized |= MAX_MAXIMUS;
	} else {
		/* set default values if no option set then */
		if (!(directions & (MAX_HORIZONTAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_MAXIMUS))) {
			new_width = (wwin->old_geometry.width) ? wwin->old_geometry.width : wwin->frame->width;
			new_x = (wwin->old_geometry.x) ? wwin->old_geometry.x : wwin->frame_x;
		}
		if (!(directions & (MAX_VERTICAL | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS))) {
			new_height = (wwin->old_geometry.height) ? wwin->old_geometry.height : wwin->frame->height;
			new_y = (wwin->old_geometry.y) ? wwin->old_geometry.y : wwin->frame_y;
		}

		/* left|right position */
		if (directions & MAX_LEFTHALF) {
			new_width = half_scr_width - adj_size;
			new_x = usableArea.x1;
		} else if (directions & MAX_RIGHTHALF) {
			new_width = half_scr_width - adj_size;
			new_x = usableArea.x1 + half_scr_width;
		}
		/* top|bottom position */
		if (directions & MAX_TOPHALF) {
			new_height = half_scr_height - adj_size;
			new_y = usableArea.y1;
		} else if (directions & MAX_BOTTOMHALF) {
			new_height = half_scr_height - adj_size;
			new_y = usableArea.y1 + half_scr_height;
		}

		/* vertical|horizontal position */
		if (directions & MAX_HORIZONTAL) {
			new_width = usableArea.x2 - usableArea.x1 - adj_size;
			new_x = usableArea.x1;
		}
		if (directions & MAX_VERTICAL) {
			new_height = usableArea.y2 - usableArea.y1 - adj_size;
			new_y = usableArea.y1;
			if (WFLAGP(wwin, full_maximize) && (new_y == 0))
				new_y -= wwin->frame->top_width;
		}
	}

	if (!WFLAGP(wwin, full_maximize) && !(directions == MAX_MAXIMUS || directions == MAX_HORIZONTAL))
		new_height -= wwin->frame->top_width + wwin->frame->bottom_width;

	/* set maximization state */
	wwin->flags.maximized = directions;
	if ((wwin->flags.old_maximized & MAX_MAXIMUS) && !wwin->flags.maximized)
		wwin->flags.maximized = MAX_MAXIMUS;

	wWindowConstrainSize(wwin, &new_width, &new_height);

	wWindowCropSize(wwin, usableArea.x2 - usableArea.x1,
			usableArea.y2 - usableArea.y1, &new_width, &new_height);

	wWindowConfigure(wwin, new_x, new_y, new_width, new_height);
	wWindowSynthConfigureNotify(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "maximize");
}

/* generic (un)maximizer */
void handleMaximize(WWindow *wwin, int directions)
{
	int current = wwin->flags.maximized;
	int requested = directions & (MAX_HORIZONTAL | MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS);
	int effective = requested ^ current;
	int flags = directions & ~requested;
	int head = wGetHeadForWindow(wwin);
	int dest_head = -1;

	if (!effective) {
		/* allow wMaximizeWindow to restore the Maximusized size */
		if ((wwin->flags.old_maximized & MAX_MAXIMUS) &&
				!(requested & MAX_MAXIMUS))
			wMaximizeWindow(wwin, MAX_MAXIMUS | flags, head);
		else if (wPreferences.alt_half_maximize &&
			 current & MAX_HORIZONTAL && current & MAX_VERTICAL &&
			 requested & MAX_HORIZONTAL && requested & MAX_VERTICAL)
			wUnmaximizeWindow(wwin);

		/* Apply for window state, which is only horizontally or vertically
		 * maximized. Quarters cannot be handled here, since there is not clear
		 * on which direction user intend to move such window. */
		else if (wPreferences.move_half_max_between_heads &&
			 current & (MAX_VERTICAL | MAX_HORIZONTAL)) {
			if (requested & MAX_LEFTHALF && current & MAX_LEFTHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->vscr,
						head, DIRECTION_LEFT);
				if (dest_head != -1) {
					effective |= MAX_RIGHTHALF;
					effective |= MAX_VERTICAL;
					effective &= ~(MAX_HORIZONTAL | MAX_LEFTHALF);
				}
			} else if (requested & MAX_RIGHTHALF &&
					current & MAX_RIGHTHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->vscr,
						head, DIRECTION_RIGHT);
				if (dest_head != -1) {
					effective |= MAX_LEFTHALF;
					effective |= MAX_VERTICAL;
					effective &= ~(MAX_HORIZONTAL | MAX_RIGHTHALF);
				}
			} else if (requested & MAX_TOPHALF && current & MAX_TOPHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->vscr,
						head, DIRECTION_UP);
				if (dest_head != -1) {
					effective |= MAX_BOTTOMHALF;
					effective |= MAX_HORIZONTAL;
					effective &= ~(MAX_VERTICAL | MAX_TOPHALF);
				}
			} else if (requested & MAX_BOTTOMHALF &&
					current & MAX_BOTTOMHALF) {
				dest_head = wGetHeadRelativeToCurrentHead(wwin->vscr,
						head, DIRECTION_DOWN);
				if (dest_head != -1) {
					effective |= MAX_TOPHALF;
					effective |= MAX_HORIZONTAL;
					effective &= ~(MAX_VERTICAL | MAX_BOTTOMHALF);
				}
			}
		}

		if (dest_head != -1)
			/* tell wMaximizeWindow that we were using keyboard, not mouse,
			 * so that it will use calculated head as destination for
			 * move_half_max_between_heads feature, not from mouse pointer */
			wMaximizeWindow(wwin, (effective | flags | MAX_KEYBOARD),
					dest_head);
		else if (!wPreferences.alt_half_maximize)
			wUnmaximizeWindow(wwin);

	} else if (!wPreferences.alt_half_maximize)
		wUnmaximizeWindow(wwin);

	/* these alone mean vertical|horizontal toggle */
	if ((effective == MAX_LEFTHALF) ||
	    (effective == MAX_RIGHTHALF) ||
	    (effective == MAX_TOPHALF) ||
	    (effective == MAX_BOTTOMHALF))
		wUnmaximizeWindow(wwin);

	/* Following conditions might look complicated, but they are really simple:
	 * allow fullscreen transition only for half maximized state (and not
	 * corners) and only when requested state is also half maximized, but on
	 * opposite side of the screen. As for corners, it is similar, but
	 * expected is that only quarter maximized windows on corner can change
	 * its state to half maximized window, depending on direction. Note, that
	 * MAX_KEYBOARD is passed to the wMaximizeWindow function, to preserve the
	 * head, even if mouse was used for triggering the action. */

	/* Quarters alternative transition. */
	else if (wPreferences.alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_TOPHALF &&
			  current & MAX_RIGHTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_TOPHALF &&
			  current & MAX_LEFTHALF && current & MAX_TOPHALF)))
		wMaximizeWindow(wwin, (MAX_TOPHALF | MAX_HORIZONTAL | MAX_KEYBOARD),
				head);
	else if (wPreferences.alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_RIGHTHALF && current & MAX_BOTTOMHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_LEFTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_BOTTOMHALF | MAX_HORIZONTAL | MAX_KEYBOARD),
				head);
	else if (wPreferences.alt_half_maximize &&
			((requested & MAX_LEFTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_LEFTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_LEFTHALF && requested & MAX_TOPHALF &&
			 current & MAX_LEFTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_LEFTHALF | MAX_VERTICAL| MAX_KEYBOARD),
				head);
	else if (wPreferences.alt_half_maximize &&
			((requested & MAX_RIGHTHALF && requested & MAX_BOTTOMHALF &&
			 current & MAX_RIGHTHALF && current & MAX_TOPHALF) ||
			(requested & MAX_RIGHTHALF && requested & MAX_TOPHALF &&
			 current & MAX_RIGHTHALF && current & MAX_BOTTOMHALF)))
		wMaximizeWindow(wwin, (MAX_RIGHTHALF | MAX_VERTICAL| MAX_KEYBOARD),
				head);

	/* Half-maximized alternative transition */
	else if (wPreferences.alt_half_maximize && (
				(requested & MAX_LEFTHALF && requested & MAX_VERTICAL &&
				 current & MAX_RIGHTHALF && current & MAX_VERTICAL) ||
				(requested & MAX_RIGHTHALF && requested & MAX_VERTICAL &&
				 current & MAX_LEFTHALF && current & MAX_VERTICAL) ||
				(requested & MAX_TOPHALF && requested & MAX_HORIZONTAL &&
				 current & MAX_BOTTOMHALF && current & MAX_HORIZONTAL) ||
				(requested & MAX_BOTTOMHALF && requested & MAX_HORIZONTAL &&
				 current & MAX_TOPHALF && current & MAX_HORIZONTAL)))
		wMaximizeWindow(wwin, (MAX_HORIZONTAL|MAX_VERTICAL|MAX_KEYBOARD),
				head);
	else {
		if ((requested == (MAX_HORIZONTAL | MAX_VERTICAL)) ||
				(requested == MAX_MAXIMUS))
			effective = requested;
		else {
			if (requested & MAX_LEFTHALF) {
				if (!(requested & (MAX_TOPHALF | MAX_BOTTOMHALF)))
					effective |= MAX_VERTICAL;
				else
					effective |= requested & (MAX_TOPHALF | MAX_BOTTOMHALF);
				effective |= MAX_LEFTHALF;
				effective &= ~(MAX_HORIZONTAL | MAX_RIGHTHALF);
			} else if (requested & MAX_RIGHTHALF) {
				if (!(requested & (MAX_TOPHALF | MAX_BOTTOMHALF)))
					effective |= MAX_VERTICAL;
				else
					effective |= requested & (MAX_TOPHALF | MAX_BOTTOMHALF);
				effective |= MAX_RIGHTHALF;
				effective &= ~(MAX_HORIZONTAL | MAX_LEFTHALF);
			}
			if (requested & MAX_TOPHALF) {
				if (!(requested & (MAX_LEFTHALF | MAX_RIGHTHALF)))
					effective |= MAX_HORIZONTAL;
				else
					effective |= requested & (MAX_LEFTHALF | MAX_RIGHTHALF);
				effective |= MAX_TOPHALF;
				effective &= ~(MAX_VERTICAL | MAX_BOTTOMHALF);
			} else if (requested & MAX_BOTTOMHALF) {
				if (!(requested & (MAX_LEFTHALF | MAX_RIGHTHALF)))
					effective |= MAX_HORIZONTAL;
				else
					effective |= requested & (MAX_LEFTHALF | MAX_RIGHTHALF);
				effective |= MAX_BOTTOMHALF;
				effective &= ~(MAX_VERTICAL | MAX_TOPHALF);
			}
			if (requested & MAX_HORIZONTAL)
				effective &= ~(MAX_LEFTHALF | MAX_RIGHTHALF);
			if (requested & MAX_VERTICAL)
				effective &= ~(MAX_TOPHALF | MAX_BOTTOMHALF);
			effective &= ~MAX_MAXIMUS;
		}
		wMaximizeWindow(wwin, effective | flags, head);
	}
}

/* Moving window between heads
 *
 * Window is moved between all avaiable heads for current screen in
 * requested direction.
 *
 * If heads have different sizes, window position will be adjusted to be inside
 * screen. Window will be unmaximized on move, so that it can be immediately be
 * maximixed by another shortcut.
 *
 *
 * direction = 0 means move clock-wise from 12h position to 6h
 *               (first try moving right, then bottom, then left, then up)
 * direction = 1 means move clock-wise from 6h position to 12h
 *               (first try moving left, then up, then right, then bottom)
 *
 *
 * As side effect this mean if you have 9 monitors - you will not be able to
 * move window into central cell without mouse.
 *
 * For common cases with 1 or 2 extra monitors this should work just fine.
 *
 * */

void moveBetweenHeads(WWindow *wwin, int direction)
{
	int head = wGetHeadForWindow(wwin);
	int try_movement_idx, destHead = -1;
	unsigned int new_width, new_height;
	int offsetX, newX = 0;
	int offsetY, newY = 0;
	WArea totalArea, oldHeadArea, destHeadArea;
	WScreen *scr = wwin->vscr->screen_ptr;
	int try_movements[2][4] = {
		{DIRECTION_RIGHT, DIRECTION_DOWN, DIRECTION_LEFT, DIRECTION_UP},
		{DIRECTION_LEFT, DIRECTION_UP, DIRECTION_RIGHT, DIRECTION_DOWN}
	};

	/* loop through directions array and try movements until one works */
	for (try_movement_idx = 0; destHead == -1 && try_movement_idx < 4; try_movement_idx++)
		destHead = wGetHeadRelativeToCurrentHead(wwin->vscr, head,
			   try_movements[direction][try_movement_idx]);

	if (destHead == -1)
		return;

	totalArea.x1 = 0;
	totalArea.y1 = 0;
	totalArea.x2 = scr->scr_width;
	totalArea.y2 = scr->scr_height;

	oldHeadArea = wGetUsableAreaForHead(wwin->vscr, head, &totalArea, True);
	destHeadArea = wGetUsableAreaForHead(wwin->vscr, destHead, &totalArea, True);

	offsetX = wwin->frame_x - oldHeadArea.x1;
	offsetY = wwin->frame_y - oldHeadArea.y1;

	newX = destHeadArea.x1 + offsetX;
	newY = destHeadArea.y1 + offsetY;

	new_width = wwin->width;
	new_height = wwin->height;

	/* try to brind window inside head coordinates */
	if (newX > destHeadArea.x2)
		newX = destHeadArea.x2 - wwin->width;

	if (newX < destHeadArea.x1)
		newX = destHeadArea.x1;

	if (newY > destHeadArea.y2)
		newY = destHeadArea.y2 - wwin->height;

	if (newY < destHeadArea.y1)
		newY = destHeadArea.y1;

	/* unset maximization state */
	wwin->flags.maximized = 0;
	wWindowConfigure(wwin, newX, newY, new_width, new_height);
}

/* the window boundary coordinates */
typedef struct {
	int left;
	int right;
	int bottom;
	int top;
	int width;
	int height;
} win_coords;

static void set_window_coords(WWindow *wwin, win_coords *obs)
{
	obs->left = wwin->frame_x;
	obs->top = wwin->frame_y;
	obs->width = wwin->frame->width;
	obs->height = wwin->frame->height;
	obs->bottom = obs->top + obs->height;
	obs->right = obs->left + obs->width;
}

/*
 * Maximus: tiled maximization (maximize without overlapping other windows)
 *
 * The original window 'orig' will be maximized to new coordinates 'new'.
 * The windows obstructing the maximization of 'orig' are denoted 'obs'.
 */
static void find_Maximus_geometry(WWindow *wwin, WArea usableArea, int *new_x, int *new_y,
				  unsigned int *new_width, unsigned int *new_height)
{
	WWindow *tmp;
	short int tbar_height_0 = 0, rbar_height_0 = 0, bd_width_0 = 0;
	short int adjust_height;
	/* the obstructing, original and new windows */
	win_coords obs, orig, new;

	/* set the original coordinate positions of the window to be Maximumized */
	if (wwin->flags.maximized) {
		/* window is already maximized; consider original geometry */
		remember_geometry(wwin, &orig.left, &orig.top, &orig.width, &orig.height);
		orig.bottom = orig.top + orig.height;
		orig.right = orig.left + orig.width;
	} else {
		set_window_coords(wwin, &orig);
	}

	/* Try to fully maximize first, then readjust later */
	new.left    = usableArea.x1;
	new.right   = usableArea.x2;
	new.top     = usableArea.y1;
	new.bottom  = usableArea.y2;

	if (HAS_TITLEBAR(wwin))
		tbar_height_0 = TITLEBAR_HEIGHT;

	if (HAS_RESIZEBAR(wwin))
		rbar_height_0 = RESIZEBAR_HEIGHT;

	if (HAS_BORDER(wwin))
		bd_width_0 = wwin->vscr->frame.border_width;

	/* the length to be subtracted if the window has titlebar, etc */
	adjust_height = tbar_height_0 + 2 * bd_width_0 + rbar_height_0;

	tmp = wwin;
	/* The focused window is always the last in the list */
	while (tmp->prev) {
		/* ignore windows in other workspaces etc */
		if (tmp->prev->frame->workspace != wwin->vscr->workspace.current ||
		    tmp->prev->flags.miniaturized || tmp->prev->flags.hidden) {
			tmp = tmp->prev;
			continue;
		}

		tmp = tmp->prev;

		/* Set the coordinates of obstructing window */
		set_window_coords(tmp, &obs);

		/* Try to maximize in the y direction first */
		if (calcIntersectionLength(orig.left, orig.width, obs.left, obs.width) != 0) {
			/* TODO: Consider the case when coords are equal */
			if (obs.bottom < orig.top && obs.bottom > new.top)
				/* w_0 is below the bottom of w_j */
				new.top = obs.bottom + 1;

			if (orig.bottom < obs.top && obs.top < new.bottom)
				/* The bottom of w_0 is above the top of w_j */
				new.bottom = obs.top - 1;
		}
	}

	tmp = wwin;
	while (tmp->prev) {
		/* ignore windows in other workspaces etc */
		if (tmp->prev->frame->workspace != wwin->vscr->workspace.current ||
		    tmp->prev->flags.miniaturized || tmp->prev->flags.hidden) {
			tmp = tmp->prev;
			continue;
		}

		tmp = tmp->prev;

		/* Set the coordinates of obstructing window */
		set_window_coords(tmp, &obs);

		/*
		 * Use the new.top and new.height instead of original values
		 * as they may have different intersections with the obstructing windows
		 */
		new.height = new.bottom - new.top - adjust_height;
		if (calcIntersectionLength(new.top, new.height, obs.top, obs.height) != 0) {
			if (obs.right < orig.left && obs.right > new.left)
				/* w_0 is completely to the right of w_j */
				new.left = obs.right + 1;

			if (orig.right < obs.left && obs.left < new.right)
				/* w_0 is completely to the left of w_j */
				new.right = obs.left - 1;
		}
	}

	*new_x = new.left;
	*new_y = new.top;
	/* xcalc needs -7 here, but other apps don't */
	*new_height = new.bottom - new.top - adjust_height - 1;
	*new_width = new.right - new.left;
}

void wUnmaximizeWindow(WWindow *wwin)
{
	int x, y, w, h;

	if (!wwin->flags.maximized)
		return;

	if (wwin->flags.shaded) {
		wwin->flags.skip_next_animation = 1;
		wUnshadeWindow(wwin);
	}

	if (wPreferences.drag_maximized_window == DRAGMAX_NOMOVE)
		wwin->client_flags.no_movable = 0;

	/* Use old coordinates if they are set, current values otherwise */
	remember_geometry(wwin, &x, &y, &w, &h);

	/* unMaximusize relative to original position */
	if (wwin->flags.maximized & MAX_MAXIMUS) {
		x += wwin->frame_x - wwin->maximus_x;
		y += wwin->frame_y - wwin->maximus_y;
	}

	wwin->flags.maximized = 0;
	wwin->flags.old_maximized = 0;
	wWindowConfigure(wwin, x, y, w, h);
	wWindowSynthConfigureNotify(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "maximize");
}

void wFullscreenWindow(WWindow *wwin)
{
	int head;
	WMRect rect;

	if (wwin->flags.fullscreen)
		return;

	wwin->flags.fullscreen = True;

	wWindowConfigureBorders(wwin);

	ChangeStackingLevel(wwin->frame->vscr, wwin->frame->core, WMFullscreenLevel);

	wwin->bfs_geometry.x = wwin->frame_x;
	wwin->bfs_geometry.y = wwin->frame_y;
	wwin->bfs_geometry.width = wwin->frame->width;
	wwin->bfs_geometry.height = wwin->frame->height;

	head = wGetHeadForWindow(wwin);
	rect = wGetRectForHead(wwin->vscr->screen_ptr, head);
	wWindowConfigure(wwin, rect.pos.x, rect.pos.y, rect.size.width, rect.size.height);

	wwin->vscr->window.bfs_focused = wwin->vscr->window.focused;
	wSetFocusTo(wwin->vscr, wwin);

	WMPostNotificationName(WMNChangedState, wwin, "fullscreen");
}

void wUnfullscreenWindow(WWindow *wwin)
{
	if (!wwin->flags.fullscreen)
		return;

	wwin->flags.fullscreen = False;

	if (wwin->frame->core->stacking->window_level == WMFullscreenLevel) {
		if (WFLAGP(wwin, sunken)) {
			ChangeStackingLevel(wwin->frame->vscr, wwin->frame->core, WMSunkenLevel);
		} else if (WFLAGP(wwin, floating)) {
			ChangeStackingLevel(wwin->frame->vscr, wwin->frame->core, WMFloatingLevel);
		} else {
			ChangeStackingLevel(wwin->frame->vscr, wwin->frame->core, WMNormalLevel);
		}
	}

	wWindowConfigure(wwin, wwin->bfs_geometry.x, wwin->bfs_geometry.y,
			 wwin->bfs_geometry.width, wwin->bfs_geometry.height);

	wWindowConfigureBorders(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "fullscreen");

	if (wwin->vscr->window.bfs_focused) {
		wSetFocusTo(wwin->vscr, wwin->vscr->window.bfs_focused);
		wwin->vscr->window.bfs_focused = NULL;
	}
}

static void flushExpose(void)
{
	XEvent tmpev;

	while (XCheckTypedEvent(dpy, Expose, &tmpev))
		WMHandleEvent(&tmpev);
	XSync(dpy, 0);
}

static void unmapTransientsFor(WWindow *wwin)
{
	WWindow *tmp;

	tmp = wwin->vscr->window.focused;
	while (tmp) {
		/* unmap the transients for this transient */
		if (tmp != wwin && tmp->transient_for == wwin->client_win
		    && (tmp->flags.mapped || w_global.startup.phase1 || tmp->flags.shaded)) {
			unmapTransientsFor(tmp);
			tmp->flags.miniaturized = 1;
			if (!tmp->flags.shaded)
				wWindowUnmap(tmp);
			else
				XUnmapWindow(dpy, tmp->frame->core->window);

			wClientSetState(tmp, IconicState, None);

			WMPostNotificationName(WMNChangedState, tmp, "iconify-transient");
		}
		tmp = tmp->prev;
	}
}

static void mapTransientsFor(WWindow *wwin)
{
	WWindow *tmp;

	tmp = wwin->vscr->window.focused;
	while (tmp) {
		/* recursively map the transients for this transient */
		if (tmp != wwin && tmp->transient_for == wwin->client_win && /*!tmp->flags.mapped */ tmp->flags.miniaturized
		    && tmp->miniwindow->icon == NULL) {
			mapTransientsFor(tmp);
			tmp->flags.miniaturized = 0;
			if (!tmp->flags.shaded)
				wWindowMap(tmp);
			else
				XMapWindow(dpy, tmp->frame->core->window);

			tmp->flags.semi_focused = 0;
			wClientSetState(tmp, NormalState, None);
			WMPostNotificationName(WMNChangedState, tmp, "iconify-transient");
		}
		tmp = tmp->prev;
	}
}

static WWindow *recursiveTransientFor(WWindow *wwin)
{
	int i;

	if (!wwin)
		return None;

	/* hackish way to detect transient_for cycle */
	i = wwin->vscr->window_count + 1;

	while (wwin && wwin->transient_for != None && i > 0) {
		wwin = wWindowFor(wwin->transient_for);
		i--;
	}

	if (i == 0 && wwin) {
		wwarning(_("window \"%s\" has a severely broken WM_TRANSIENT_FOR hint"),
			 wwin->title);
		return NULL;
	}

	return wwin;
}

void wIconifyWindow(WWindow *wwin)
{
	XWindowAttributes attribs;
	int present;

	if (!XGetWindowAttributes(dpy, wwin->client_win, &attribs))
		return; /* the window doesn't exist anymore */

	if (wwin->flags.miniaturized)
		return; /* already miniaturized */

	if (wwin->transient_for != None && wwin->transient_for != wwin->vscr->screen_ptr->root_win) {
		WWindow *owner = wWindowFor(wwin->transient_for);

		if (owner && owner->flags.miniaturized)
			return;
	}

	present = wwin->frame->workspace == wwin->vscr->workspace.current;

	/* if the window is in another workspace, simplify process */
	if (present)
		/* icon creation may take a while */
		XGrabPointer(dpy, wwin->vscr->screen_ptr->root_win, False,
			     ButtonMotionMask | ButtonReleaseMask, GrabModeAsync,
			     GrabModeAsync, None, None, CurrentTime);

	if (!wPreferences.disable_miniwindows && !wwin->flags.net_handle_icon)
		miniwindow_icon_show(wwin);

	wwin->flags.miniaturized = 1;
	wwin->flags.mapped = 0;

	/* unmap transients */
	unmapTransientsFor(wwin);

	if (present) {
		XUngrabPointer(dpy, CurrentTime);
		wWindowUnmap(wwin);
		/* let all Expose events arrive so that we can repaint
		 * something before the animation starts (and the server is grabbed) */
		XSync(dpy, 0);

		if (wPreferences.disable_miniwindows || wwin->flags.net_handle_icon)
			wClientSetState(wwin, IconicState, None);
		else
			wClientSetState(wwin, IconicState, wwin->miniwindow->icon->icon_win);

		flushExpose();
		animation_minimize(wwin);
	}

	wwin->flags.skip_next_animation = 0;

	if (!wPreferences.disable_miniwindows && !wwin->flags.net_handle_icon) {
		if (wwin->vscr->workspace.current == wwin->frame->workspace ||
		    IS_OMNIPRESENT(wwin) || wPreferences.sticky_icons)
			miniwindow_map(wwin);

		AddToStackList(wwin->miniwindow->icon->vscr, wwin->miniwindow->icon->core);
		wLowerFrame(wwin->miniwindow->icon->vscr, wwin->miniwindow->icon->core);
	}

	if (present) {
		WWindow *owner = recursiveTransientFor(wwin->vscr->window.focused);

		if ((wwin->flags.focused || (owner && wwin->client_win == owner->client_win))
		    && wPreferences.focus_mode == WKF_CLICK) {
			WWindow *tmp;

			tmp = wwin->prev;
			while (tmp) {
				if (!WFLAGP(tmp, no_focusable)
				    && !(tmp->flags.hidden || tmp->flags.miniaturized)
				    && (wwin->frame->workspace == tmp->frame->workspace))
					break;
				tmp = tmp->prev;
			}

			wSetFocusTo(wwin->vscr, tmp);
		} else if (wPreferences.focus_mode != WKF_CLICK) {
			wSetFocusTo(wwin->vscr, NULL);
		}

		if (animation_iconify_window(wwin))
			return;
	}

	/* maybe we want to do this regardless of net_handle_icon
	 * it seems to me we might break behaviour this way.
	 */
	if (wwin->flags.selected && !wPreferences.disable_miniwindows
	    && !wwin->flags.net_handle_icon)
		wIconSelect(wwin->miniwindow->icon);

	WMPostNotificationName(WMNChangedState, wwin, "iconify");

	if (wPreferences.auto_arrange_icons)
		wArrangeIcons(wwin->vscr, True);
}

void wDeiconifyWindow(WWindow *wwin)
{
	/* Let's avoid changing workspace while deiconifying */
	wwin->vscr->workspace.ignore_change = True;

	/* we're hiding for show_desktop */
	int netwm_hidden = wwin->flags.net_show_desktop &&
	    wwin->frame->workspace != wwin->vscr->workspace.current;

	if (!netwm_hidden)
		wWindowChangeWorkspace(wwin, wwin->vscr->workspace.current);

	if (!wwin->flags.miniaturized) {
		wwin->vscr->workspace.ignore_change = False;
		return;
	}

	if (wwin->transient_for != None && wwin->transient_for != wwin->vscr->screen_ptr->root_win) {
		WWindow *owner = recursiveTransientFor(wwin);

		if (owner && owner->flags.miniaturized) {
			wDeiconifyWindow(owner);
			wSetFocusTo(wwin->vscr, wwin);
			wRaiseFrame(wwin->frame->vscr, wwin->frame->core);
			wwin->vscr->workspace.ignore_change = False;
			return;
		}
	}

	wwin->flags.miniaturized = 0;

	if (!netwm_hidden && !wwin->flags.shaded)
		wwin->flags.mapped = 1;

	if (!netwm_hidden || wPreferences.sticky_icons) {
		/* maybe we want to do this regardless of net_handle_icon
		 * it seems to me we might break behaviour this way.
		 */
		if (!wPreferences.disable_miniwindows && !wwin->flags.net_handle_icon
		    && wwin->miniwindow->icon != NULL) {
			if (wwin->miniwindow->icon->selected)
				wIconSelect(wwin->miniwindow->icon);

			miniwindow_unmap(wwin);
		}
	}

	/* if the window is in another workspace, do it silently */
	if (!netwm_hidden) {
		animation_maximize(wwin);
		wwin->flags.skip_next_animation = 0;
		XGrabServer(dpy);
		if (!wwin->flags.shaded)
			XMapWindow(dpy, wwin->client_win);

		XMapWindow(dpy, wwin->frame->core->window);
		wRaiseFrame(wwin->frame->vscr, wwin->frame->core);
		if (!wwin->flags.shaded)
			wClientSetState(wwin, NormalState, None);

		mapTransientsFor(wwin);
	}

	if (!wPreferences.disable_miniwindows && wwin->miniwindow->icon != NULL
	    && !wwin->flags.net_handle_icon) {
		wSetFocusTo(wwin->vscr, wwin);
		miniwindow_destroy_icon(wwin);
	}

	if (!netwm_hidden) {
		XUngrabServer(dpy);
		wSetFocusTo(wwin->vscr, wwin);
		if (animation_deiconify_window(wwin))
			return;
	}

	if (wPreferences.auto_arrange_icons)
		wArrangeIcons(wwin->vscr, True);

	WMPostNotificationName(WMNChangedState, wwin, "iconify");

	/* In case we were shaded and iconified, also unshade */
	if (!netwm_hidden)
		wUnshadeWindow(wwin);

	wwin->vscr->workspace.ignore_change = False;
}

static void hideWindow(WIcon *icon, int icon_x, int icon_y, WWindow *wwin, int animate)
{
	if (wwin->flags.miniaturized) {
		miniwindow_unmap(wwin);
		wwin->flags.hidden = 1;
		WMPostNotificationName(WMNChangedState, wwin, "hide");
		return;
	}

	if (wwin->flags.inspector_open)
		wHideInspectorForWindow(wwin);

	wwin->flags.hidden = 1;
	wWindowUnmap(wwin);

	wClientSetState(wwin, IconicState, icon->icon_win);
	flushExpose();

	if (animate)
		animation_hide(wwin, icon_x, icon_y);

	wwin->flags.skip_next_animation = 0;
	WMPostNotificationName(WMNChangedState, wwin, "hide");
}

void wHideAll(virtual_screen *vscr)
{
	WWindow **windows, *wwin;
	unsigned int wcount = 0;
	int i;

	if (!vscr->screen_ptr)
		return;

	windows = wmalloc(sizeof(WWindow *));

	wwin = vscr->window.focused;
	while (wwin) {
		windows[wcount] = wwin;
		wcount++;
		windows = wrealloc(windows, sizeof(WWindow *) * (wcount + 1));
		wwin = wwin->prev;
	}

	for (i = 0; i < wcount; i++) {
		wwin = windows[i];
		if (wwin->frame->workspace == wwin->vscr->workspace.current
		    && !(wwin->flags.miniaturized || wwin->flags.hidden)
		    && !wwin->flags.internal_window
		    && !WFLAGP(wwin, no_miniaturizable)) {
			wwin->flags.skip_next_animation = 1;
			wIconifyWindow(wwin);
		}
	}

	wfree(windows);
}

void wHideOtherApplications(WWindow *awin)
{
	WWindow *wwin;
	WApplication *tapp;

	if (!awin)
		return;

	wwin = awin->vscr->window.focused;
	while (wwin) {
		if (wwin != awin
		    && wwin->frame->workspace == awin->vscr->workspace.current
		    && !(wwin->flags.miniaturized || wwin->flags.hidden)
		    && !wwin->flags.internal_window
		    && wGetWindowOfInspectorForWindow(wwin) != awin && !WFLAGP(wwin, no_hide_others)) {

			if (wwin->main_window == None || WFLAGP(wwin, no_appicon)) {
				if (!WFLAGP(wwin, no_miniaturizable)) {
					wwin->flags.skip_next_animation = 1;
					wIconifyWindow(wwin);
				}
			} else if (wwin->main_window != None && awin->main_window != wwin->main_window) {
				tapp = wApplicationOf(wwin->main_window);
				if (tapp) {
					tapp->flags.skip_next_animation = 1;
					wHideApplication(tapp);
				} else {
					if (!WFLAGP(wwin, no_miniaturizable)) {
						wwin->flags.skip_next_animation = 1;
						wIconifyWindow(wwin);
					}
				}
			}
		}
		wwin = wwin->prev;
	}
}

void wHideApplication(WApplication *wapp)
{
	virtual_screen *vscr;
	WWindow *wlist;
	int hadfocus, animate;

	if (!wapp) {
		wwarning("trying to hide a non grouped window");
		return;
	}

	if (!wapp->main_window_desc) {
		wwarning("group leader not found for window group");
		return;
	}

	vscr = wapp->main_window_desc->vscr;
	hadfocus = 0;
	wlist = vscr->window.focused;
	if (!wlist)
		return;

	if (wlist->main_window == wapp->main_window)
		wapp->last_focused = wlist;
	else
		wapp->last_focused = NULL;

	animate = !wapp->flags.skip_next_animation;

	while (wlist) {
		if (wlist->main_window == wapp->main_window) {
			if (wlist->flags.focused)
				hadfocus = 1;
			if (wapp->app_icon) {
				hideWindow(wapp->app_icon->icon, wapp->app_icon->x_pos,
					   wapp->app_icon->y_pos, wlist, animate);
				animate = False;
			}
		}
		wlist = wlist->prev;
	}

	wapp->flags.skip_next_animation = 0;

	if (hadfocus) {
		if (wPreferences.focus_mode == WKF_CLICK) {
			wlist = vscr->window.focused;
			while (wlist) {
				if (!WFLAGP(wlist, no_focusable) && !wlist->flags.hidden
				    && (wlist->flags.mapped || wlist->flags.shaded))
					break;

				wlist = wlist->prev;
			}

			wSetFocusTo(vscr, wlist);
		} else {
			wSetFocusTo(vscr, NULL);
		}
	}

	wapp->flags.hidden = 1;

	if (wPreferences.auto_arrange_icons)
		wArrangeIcons(vscr, True);

#ifdef HIDDENDOT
	if (wapp->app_icon) {
		wIconPaint(wapp->app_icon->icon);
		wAppIconPaint(wapp->app_icon);
	}
#endif
}

static void unhideWindow(int icon_x, int icon_y, WWindow *wwin, int animate, int bringToCurrentWS)
{
	if (bringToCurrentWS)
		wWindowChangeWorkspace(wwin, wwin->vscr->workspace.current);

	wwin->flags.hidden = 0;

	if (animate)
		animation_unhide(wwin, icon_x, icon_y);

	wwin->flags.skip_next_animation = 0;
	if (wwin->vscr->workspace.current == wwin->frame->workspace) {
		XMapWindow(dpy, wwin->client_win);
		XMapWindow(dpy, wwin->frame->core->window);
		wClientSetState(wwin, NormalState, None);
		wwin->flags.mapped = 1;
		wRaiseFrame(wwin->frame->vscr, wwin->frame->core);
	}

	if (wwin->flags.inspector_open)
		wUnhideInspectorForWindow(wwin);

	WMPostNotificationName(WMNChangedState, wwin, "hide");
}

void wUnhideApplication(WApplication *wapp, Bool miniwindows, Bool bringToCurrentWS)
{
	virtual_screen *vscr;
	WWindow *wlist, *next;
	WWindow *focused = NULL;
	int animate;

	if (!wapp)
		return;

	vscr = wapp->main_window_desc->vscr;
	wlist = vscr->window.focused;
	if (!wlist)
		return;

	/* goto beginning of list */
	while (wlist->prev)
		wlist = wlist->prev;

	animate = !wapp->flags.skip_next_animation;

	while (wlist) {
		next = wlist->next;

		if (wlist->main_window == wapp->main_window) {
			if (wlist->flags.focused)
				focused = wlist;
			else if (!focused || !focused->flags.focused)
				focused = wlist;

			if (wlist->flags.miniaturized) {
				if ((bringToCurrentWS || wPreferences.sticky_icons ||
				     wlist->frame->workspace == vscr->workspace.current) && wlist->miniwindow->icon) {
					if (!wlist->miniwindow->icon->mapped) {
						WCoord *coord;

						coord = PlaceIcon(vscr, wGetHeadForWindow(wlist));
						if (wlist->miniwindow->icon_x != coord->x ||
							wlist->miniwindow->icon_y != coord->y)
							XMoveWindow(dpy, wlist->miniwindow->icon->core->window, coord->x, coord->y);

						wlist->miniwindow->icon_x = coord->x;
						wlist->miniwindow->icon_y = coord->y;
						miniwindow_map(wlist);
						wfree(coord);
					}

					wRaiseFrame(wlist->miniwindow->icon->vscr, wlist->miniwindow->icon->core);
				}

				if (bringToCurrentWS)
					wWindowChangeWorkspace(wlist, vscr->workspace.current);

				wlist->flags.hidden = 0;
				if (miniwindows && wlist->frame->workspace == vscr->workspace.current)
					wDeiconifyWindow(wlist);

				WMPostNotificationName(WMNChangedState, wlist, "hide");
			} else if (wlist->flags.shaded) {
				if (bringToCurrentWS)
					wWindowChangeWorkspace(wlist, vscr->workspace.current);

				wlist->flags.hidden = 0;
				wRaiseFrame(wlist->frame->vscr, wlist->frame->core);
				if (wlist->frame->workspace == vscr->workspace.current) {
					XMapWindow(dpy, wlist->frame->core->window);
					if (miniwindows)
						wUnshadeWindow(wlist);
				}

				WMPostNotificationName(WMNChangedState, wlist, "hide");
			} else if (wlist->flags.hidden) {
				unhideWindow(wapp->app_icon->x_pos,
					     wapp->app_icon->y_pos, wlist, animate, bringToCurrentWS);
				animate = False;
			} else {
				if (bringToCurrentWS && wlist->frame->workspace != vscr->workspace.current)
					wWindowChangeWorkspace(wlist, vscr->workspace.current);

				wRaiseFrame(wlist->frame->vscr, wlist->frame->core);
			}
		}
		wlist = next;
	}

	wapp->flags.skip_next_animation = 0;
	wapp->flags.hidden = 0;

	if (wapp->last_focused && wapp->last_focused->flags.mapped) {
		wRaiseFrame(wapp->last_focused->frame->vscr, wapp->last_focused->frame->core);
		wSetFocusTo(vscr, wapp->last_focused);
	} else if (focused) {
		wSetFocusTo(vscr, focused);
	}

	wapp->last_focused = NULL;
	if (wPreferences.auto_arrange_icons)
		wArrangeIcons(vscr, True);

#ifdef HIDDENDOT
	if (wapp->app_icon) {
		wIconPaint(wapp->app_icon->icon);
		wAppIconPaint(wapp->app_icon);
	}
#endif
}

void wShowAllWindows(virtual_screen *vscr)
{
	WApplication *wapp;
	WWindow *wwin, *old_foc;

	old_foc = wwin = vscr->window.focused;
	while (wwin) {
		if (!wwin->flags.internal_window &&
		    (vscr->workspace.current == wwin->frame->workspace || IS_OMNIPRESENT(wwin))) {
			if (wwin->flags.miniaturized) {
				wwin->flags.skip_next_animation = 1;
				wDeiconifyWindow(wwin);
			} else if (wwin->flags.hidden) {
				wapp = wApplicationOf(wwin->main_window);
				if (wapp) {
					wUnhideApplication(wapp, False, False);
				} else {
					wwin->flags.skip_next_animation = 1;
					wDeiconifyWindow(wwin);
				}
			}
		}
		wwin = wwin->prev;
	}

	wSetFocusTo(vscr, old_foc);
}

void wRefreshDesktop(virtual_screen *vscr)
{
	Window win;
	XSetWindowAttributes attr;

	attr.backing_store = NotUseful;
	attr.save_under = False;
	win = XCreateWindow(dpy, vscr->screen_ptr->root_win, 0, 0, vscr->screen_ptr->scr_width,
			    vscr->screen_ptr->scr_height, 0, CopyFromParent, CopyFromParent,
			    (Visual *) CopyFromParent, CWBackingStore | CWSaveUnder, &attr);
	XMapRaised(dpy, win);
	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

void wArrangeIcons(virtual_screen *vscr, Bool arrangeAll)
{
	WWindow *wwin;
	WAppIcon *aicon;

	int head;
	const int heads = wXineramaHeads(vscr->screen_ptr);

	struct HeadVars {
		int pf;		/* primary axis */
		int sf;		/* secondary axis */
		int fullW;
		int fullH;
		int pi, si;
		int sx1, sx2, sy1, sy2;	/* screen boundary */
		int sw, sh;
		int xo, yo;
		int xs, ys;
	} *vars;

	int isize = wPreferences.icon_size;

	vars = (struct HeadVars *)wmalloc(sizeof(struct HeadVars) * heads);

	for (head = 0; head < heads; ++head) {
		WArea area = wGetUsableAreaForHead(vscr, head, NULL, False);
		WMRect rect;

		if (vscr->dock.dock) {
			int offset = wPreferences.icon_size + DOCK_EXTRA_SPACE;

			if (vscr->dock.dock->on_right_side)
				area.x2 -= offset;
			else
				area.x1 += offset;
		}

		rect = wmkrect(area.x1, area.y1, area.x2 - area.x1, area.y2 - area.y1);

		vars[head].pi = vars[head].si = 0;
		vars[head].sx1 = rect.pos.x;
		vars[head].sy1 = rect.pos.y;
		vars[head].sw = rect.size.width;
		vars[head].sh = rect.size.height;
		vars[head].sx2 = vars[head].sx1 + vars[head].sw;
		vars[head].sy2 = vars[head].sy1 + vars[head].sh;
		vars[head].sw = isize * (vars[head].sw / isize);
		vars[head].sh = isize * (vars[head].sh / isize);
		vars[head].fullW = (vars[head].sx2 - vars[head].sx1) / isize;
		vars[head].fullH = (vars[head].sy2 - vars[head].sy1) / isize;

		/* icon yard boundaries */
		if (wPreferences.icon_yard & IY_VERT) {
			vars[head].pf = vars[head].fullH;
			vars[head].sf = vars[head].fullW;
		} else {
			vars[head].pf = vars[head].fullW;
			vars[head].sf = vars[head].fullH;
		}

		if (wPreferences.icon_yard & IY_RIGHT) {
			vars[head].xo = vars[head].sx2 - isize;
			vars[head].xs = -1;
		} else {
			vars[head].xo = vars[head].sx1;
			vars[head].xs = 1;
		}

		if (wPreferences.icon_yard & IY_TOP) {
			vars[head].yo = vars[head].sy1;
			vars[head].ys = 1;
		} else {
			vars[head].yo = vars[head].sy2 - isize;
			vars[head].ys = -1;
		}
	}

#define X ((wPreferences.icon_yard & IY_VERT) \
	? vars[head].xo + vars[head].xs*(vars[head].si*isize) \
	: vars[head].xo + vars[head].xs*(vars[head].pi*isize))

#define Y ((wPreferences.icon_yard & IY_VERT) \
	? vars[head].yo + vars[head].ys*(vars[head].pi*isize) \
	: vars[head].yo + vars[head].ys*(vars[head].si*isize))

	/* arrange application icons */
	aicon = w_global.app_icon_list;
	/* reverse them to avoid unnecessarily sliding of icons */
	while (aicon && aicon->next)
		aicon = aicon->next;

	while (aicon) {
		if (!aicon->docked) {
			/* CHECK: can icon be NULL here ? */
			/* The intention here is to place the AppIcon on the head that
			 * contains most of the applications _main_ window. */
			head = wGetHeadForWindow(aicon->icon->owner);

			if (aicon->x_pos != X || aicon->y_pos != Y)
				animation_slide_window(aicon->icon->core->window, aicon->x_pos, aicon->y_pos, X, Y);

			wAppIconMove(aicon, X, Y);
			vars[head].pi++;
			if (vars[head].pi >= vars[head].pf) {
				vars[head].pi = 0;
				vars[head].si++;
			}
		}
		aicon = aicon->prev;
	}

	/* arrange miniwindows */
	wwin = vscr->window.focused;
	/* reverse them to avoid unnecessarily shuffling */
	while (wwin && wwin->prev)
		wwin = wwin->prev;

	while (wwin) {
		if (wwin->miniwindow->icon && wwin->flags.miniaturized && !wwin->flags.hidden &&
		    (wwin->frame->workspace == vscr->workspace.current ||
		     IS_OMNIPRESENT(wwin) || wPreferences.sticky_icons)) {

			head = wGetHeadForWindow(wwin);

			if (arrangeAll || !wwin->flags.icon_moved) {
				if (wwin->miniwindow->icon_x != X || wwin->miniwindow->icon_y != Y)
					move_window(wwin->miniwindow->icon->core->window, wwin->miniwindow->icon_x, wwin->miniwindow->icon_y, X, Y);

				wwin->miniwindow->icon_x = X;
				wwin->miniwindow->icon_y = Y;

				vars[head].pi++;
				if (vars[head].pi >= vars[head].pf) {
					vars[head].pi = 0;
					vars[head].si++;
				}
			}
		}
		if (arrangeAll)
			wwin->flags.icon_moved = 0;
		/* we reversed the order, so we use next */
		wwin = wwin->next;
	}

	wfree(vars);
}

void wSelectWindow(WWindow *wwin, Bool flag)
{
	WScreen *scr = wwin->vscr->screen_ptr;

	if (flag) {
		wwin->flags.selected = 1;
		if (wwin->frame->selected_border_pixel)
			XSetWindowBorder(dpy, wwin->frame->core->window, *wwin->frame->selected_border_pixel);
		else
			XSetWindowBorder(dpy, wwin->frame->core->window, scr->white_pixel);

		if (!HAS_BORDER(wwin))
			XSetWindowBorderWidth(dpy, wwin->frame->core->window, wwin->vscr->frame.border_width);

		if (!scr->selected_windows)
			scr->selected_windows = WMCreateArray(4);

		WMAddToArray(scr->selected_windows, wwin);
	} else {
		wwin->flags.selected = 0;
		if (wwin->flags.focused) {
			if (wwin->frame->focused_border_pixel)
				XSetWindowBorder(dpy, wwin->frame->core->window, *wwin->frame->focused_border_pixel);
			else
				XSetWindowBorder(dpy, wwin->frame->core->window, scr->frame_focused_border_pixel);
		} else {
			if (wwin->frame->border_pixel)
				XSetWindowBorder(dpy, wwin->frame->core->window, *wwin->frame->border_pixel);
			else
				XSetWindowBorder(dpy, wwin->frame->core->window, scr->frame_border_pixel);
		}

		if (!HAS_BORDER(wwin))
			XSetWindowBorderWidth(dpy, wwin->frame->core->window, 0);

		if (scr->selected_windows)
			WMRemoveFromArray(scr->selected_windows, wwin);
	}
}

void wMakeWindowVisible(WWindow *wwin)
{
	if (wwin->frame->workspace != wwin->vscr->workspace.current)
		wWorkspaceChange(wwin->vscr, wwin->frame->workspace);

	if (wwin->flags.shaded)
		wUnshadeWindow(wwin);

	if (wwin->flags.hidden) {
		WApplication *app;

		app = wApplicationOf(wwin->main_window);
		if (app) {
			/* trick to get focus to this window */
			app->last_focused = wwin;
			wUnhideApplication(app, False, False);
		}
	}

	if (wwin->flags.miniaturized) {
		wDeiconifyWindow(wwin);
	} else {
		if (!WFLAGP(wwin, no_focusable))
			wSetFocusTo(wwin->vscr, wwin);

		wRaiseFrame(wwin->frame->vscr, wwin->frame->core);
	}
}

void movePionterToWindowCenter(WWindow *wwin)
{
	if (!wPreferences.pointer_with_half_max_windows)
		return;

	XSelectInput(dpy, wwin->client_win, wwin->event_mask);
	XWarpPointer(dpy, None, wwin->vscr->screen_ptr->root_win, 0, 0, 0, 0,
			wwin->frame_x + wwin->width / 2,
			wwin->frame_y + wwin->height / 2);
	XFlush(dpy);
}
