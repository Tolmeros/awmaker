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


#ifndef WMICON_H_
#define WMICON_H_

#include "wcore.h"
#include "window.h"

#define TILE_NORMAL	0
#define TILE_CLIP	1
#define TILE_DRAWER	2

/* This is the border, in pixel, drawn around a Mini-Preview */
#define MINIPREVIEW_BORDER 1

typedef struct WIcon {
	WCoreWindow 	*core;
	WWindow 	*owner;		/* owner window */
	char 		*title;		/* the icon name hint */
	int		width;		/* window size x */
	int		height;		/* window size y */

	Window 		icon_win;	/* client suplied icon window */

	char		*file_name;	/* the file with the icon image */
	RImage 		*file_image;	/* the image from the file */

	unsigned int 	tile_type:4;
	unsigned int 	show_title:1;
	unsigned int 	selected:1;
	unsigned int	step:3;		/* selection cycle step */
	unsigned int	shadowed:1;	/* If the icon is to be blured */
	unsigned int 	mapped:1;
	unsigned int	highlighted:1;

	Pixmap		pixmap;
	Pixmap		mini_preview;

	WMHandlerID	handlerID;	/* timer handler ID for cycling select
					 * color */
} WIcon;

void icon_for_wwindow_map(WIcon *icon);
void icon_for_wwindow_miniwindow_map(WIcon *icon);
WIcon *icon_create_core(virtual_screen *vscr);

void set_icon_image_from_database(WIcon *icon, const char *wm_instance, const char *wm_class, const char *command);
void wIconDestroy(WIcon *icon);
void wIconPaint(WIcon *icon);
void wIconUpdate(WIcon *icon);
void wIconSelect(WIcon *icon);
void wIconChangeTitle(WIcon *icon, WWindow *wwin);
void update_icon_pixmap(WIcon *icon);

int wIconChangeImageFile(WIcon *icon, const char *file);

RImage *wIconValidateIconSize(RImage *icon, int max_size);
RImage *get_rimage_icon_from_wm_hints(WIcon *icon);

char *wIconStore(WIcon *icon);
char *get_name_for_instance_class(const char *wm_instance, const char *wm_class);

void wIconSetHighlited(WIcon *icon, Bool flag);
void set_icon_image_from_image(WIcon *icon, RImage *image);
void map_icon_image(WIcon *icon);
void unmap_icon_image(WIcon *icon);

void icon_appearanceObserver(void *self, WMNotification *notif);
void icon_tileObserver(void *self, WMNotification *notif);

void remove_cache_icon(char *filename);

int create_icon_minipreview(WWindow *wwin);
#endif /* WMICON_H_ */
