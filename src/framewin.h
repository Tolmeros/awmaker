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

#ifndef WMFRAMEWINDOW_H_
#define WMFRAMEWINDOW_H_

#include "wcore.h"
#include "pixmap.h"
#include "texture.h"
#include "menu.h"
#include "window.h"

#define BORDER_TOP	1
#define BORDER_BOTTOM	2
#define BORDER_LEFT	4
#define BORDER_RIGHT	8
#define BORDER_ALL	(1|2|4|8)


#define WFF_TITLEBAR	(1<<0)
#define WFF_LEFT_BUTTON	(1<<1)
#define WFF_RIGHT_BUTTON (1<<2)
#define WFF_RESIZEBAR	(1<<3)
#define WFF_BORDER	(1<<4)
#define WFF_SINGLE_STATE (1<<5)
#ifdef XKB_BUTTON_HINT
#define WFF_LANGUAGE_BUTTON	(1<<6)
#endif
#define WFF_SELECTED	(1<<7)


#define WFF_IS_SHADED	(1<<16)

typedef struct WFrameWindow {
    virtual_screen *vscr;	       /* pointer to the virtual screen structure */

    WWindow *parent_wwin;               /* Parent WWindow */
    WMenu *parent_wmenu;                /* Parent WMenu */

    WCoreWindow *core;

    WCoreWindow *titlebar;	       /* the titlebar */
    WCoreWindow *left_button;	       /* miniaturize button */
#ifdef XKB_BUTTON_HINT
    WCoreWindow *language_button;
#endif
    WCoreWindow *right_button;	       /* close button */

    short workspace;		       /* workspace that the window occupies */

    short top_width;
    int *title_clearance;
    int *title_min_height;
    int *title_max_height;
    short bottom_width;

    short resizebar_corner_width;

    WCoreWindow *resizebar;	       /* bottom resizebar */

    Pixmap title_back[3];	       /* focused, unfocused, pfocused */
    Pixmap resizebar_back[3];	       /* any, None, None */
    Pixmap lbutton_back[3];
    Pixmap rbutton_back[3];
#ifdef XKB_BUTTON_HINT
    Pixmap languagebutton_back[3];
#endif

    WPixmap *lbutton_image;
    WPixmap *rbutton_image;
#ifdef XKB_BUTTON_HINT
    WPixmap *languagebutton_image;
#endif

    union WTexture **title_texture;
    union WTexture **resizebar_texture;
    WMColor **title_color;
    WMFont **font;

#ifdef KEEP_XKB_LOCK_STATUS
    int languagemode;
    int last_languagemode;
#endif /* KEEP_XKB_LOCK_STATUS */

    /* thing that uses this frame. passed as data to callbacks */
    void *child;

    /* callbacks */
    void (*on_click_left)(WCoreWindow *sender, void *data, XEvent *event);
#ifdef XKB_BUTTON_HINT
    void (*on_click_language)(WCoreWindow *sender, void *data, XEvent *event);
#endif

    void (*on_click_right)(WCoreWindow *sender, void *data, XEvent *event);
    void (*on_dblclick_right)(WCoreWindow *sender, void *data, XEvent *event);

    void (*on_mousedown_titlebar)(WCoreWindow *sender, void *data, XEvent *event);
    void (*on_dblclick_titlebar)(WCoreWindow *sender, void *data, XEvent *event);

    void (*on_mousedown_resizebar)(WCoreWindow *sender, void *data, XEvent *event);

    struct {
        unsigned int state:2;	       /* 3 possible states */
        unsigned int justification:2;
        unsigned int titlebar:1;
        unsigned int resizebar:1;
        unsigned int map_titlebar:1;
        unsigned int map_resizebar:1;
        unsigned int left_button:1;
        unsigned int right_button:1;
#ifdef XKB_BUTTON_HINT
        unsigned int language_button:1;
#endif
	unsigned int map_left_button:1;
	unsigned int map_right_button:1;
#ifdef XKB_BUTTON_HINT
	unsigned int map_language_button:1;
#endif

        unsigned int need_texture_remake:1;

        unsigned int single_texture:1;
	unsigned int shaded:1;
	unsigned int border:1;

        unsigned int hide_left_button:1;
        unsigned int hide_right_button:1;
#ifdef XKB_BUTTON_HINT
        unsigned int hide_language_button:1;
#endif

        unsigned int need_texture_change:1;

        unsigned int lbutton_dont_fit:1;
        unsigned int rbutton_dont_fit:1;
#ifdef XKB_BUTTON_HINT
        unsigned int languagebutton_dont_fit:1;
#endif

        unsigned int repaint_only_titlebar:1;
        unsigned int repaint_only_resizebar:1;

        unsigned int is_client_window_frame:1;

        unsigned int incomplete_title:1;
    } flags;
    int depth;
    int bordersize;
    int width;		/* Framewin width */
    int height;		/* Framewin height */
    Visual *visual;
    Colormap colormap;
    unsigned long *border_pixel;
    unsigned long *focused_border_pixel;
    unsigned long *selected_border_pixel;

	/* Titlebar */
	int titlebar_width;
	int titlebar_height;

	/* Resizebar */
	int resizebar_height;

	/* Buttons */
	int left_button_pos_width;
	int left_button_pos_height;
} WFrameWindow;

void wframewin_set_borders(WFrameWindow *fwin, int flags);
void wFrameWindowDestroy(WFrameWindow *fwin);
void wFrameWindowChangeState(WFrameWindow *fwin, int state);
void wFrameWindowPaint(WFrameWindow *fwin);
void wFrameWindowConfigure(WFrameWindow *fwin, int x, int y, int width, int height);
void wFrameWindowResize(WFrameWindow *fwin, int width, int height);
int wFrameWindowChangeTitle(WFrameWindow *fwin, const char *new_title);

void wframewindow_show_rightbutton(WFrameWindow *fwin);
void wframewindow_hide_rightbutton(WFrameWindow *fwin);
void wframewindow_show_languagebutton(WFrameWindow *fwin);
void wframewindow_hide_languagebutton(WFrameWindow *fwin);
void wframewindow_refresh_titlebar(WFrameWindow *fwin);

#ifdef XKB_BUTTON_HINT
void wFrameWindowUpdateLanguageButton(WFrameWindow *fwin);
#endif

WFrameWindow *wframewindow_create(WWindow *parent_wwin, WMenu *parent_wmenu,
				  int width, int height, int flags);
void wframewindow_map(WFrameWindow *fwin, virtual_screen *vscr, int wlevel,
                      int x, int y, int *clearance,
                      int *title_min, int *title_max,
                      WTexture **title_texture, WTexture **resize_texture,
                      WMColor **color, WMFont **font, int depth,
                      Visual *visual, Colormap colormap);
void framewindow_unmap(WFrameWindow *fwin);
#endif
