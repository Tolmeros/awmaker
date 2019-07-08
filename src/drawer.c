/* dock.c- Dock module for WindowMaker - Drawer
 *
 *  Window Maker window manager
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>

#include "WindowMaker.h"
#include "wcore.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "actions.h"
#include "stacking.h"
#include "dock.h"
#include "dockedapp.h"
#include "dialog.h"
#include "shell.h"
#include "properties.h"
#include "menu.h"
#include "client.h"
#include "wdefaults.h"
#include "workspace.h"
#include "framewin.h"
#include "superfluous.h"
#include "xinerama.h"
#include "placement.h"
#include "misc.h"
#include "event.h"
#ifdef USE_DOCK_XDND
#include "xdnd.h"
#endif

/***** Local variables ****/
static WMPropList *dCommand = NULL;
static WMPropList *dPasteCommand = NULL;
#ifdef USE_DOCK_XDND			/* XXX was OFFIX */
static WMPropList *dDropCommand = NULL;
#endif
static WMPropList *dAutoLaunch, *dLock;
static WMPropList *dName, *dForced, *dBuggyApplication, *dYes, *dNo;
static WMPropList *dHost, *dDock;
static WMPropList *dAutoAttractIcons;
static WMPropList *dPosition, *dApplications, *dLowered, *dCollapsed;
static WMPropList *dAutoCollapse, *dAutoRaiseLower, *dOmnipresent;
static WMPropList *dDrawers = NULL;

static char *findUniqueName(virtual_screen *vscr, const char *instance_basename);
static void drawerAppendToChain(WDock *drawer);
static void make_keys(void);


static void make_keys(void)
{
	if (dCommand != NULL)
		return;

	dCommand = WMRetainPropList(WMCreatePLString("Command"));
	dPasteCommand = WMRetainPropList(WMCreatePLString("PasteCommand"));
#ifdef USE_DOCK_XDND
	dDropCommand = WMRetainPropList(WMCreatePLString("DropCommand"));
#endif
	dLock = WMRetainPropList(WMCreatePLString("Lock"));
	dAutoLaunch = WMRetainPropList(WMCreatePLString("AutoLaunch"));
	dName = WMRetainPropList(WMCreatePLString("Name"));
	dForced = WMRetainPropList(WMCreatePLString("Forced"));
	dBuggyApplication = WMRetainPropList(WMCreatePLString("BuggyApplication"));
	dYes = WMRetainPropList(WMCreatePLString("Yes"));
	dNo = WMRetainPropList(WMCreatePLString("No"));
	dHost = WMRetainPropList(WMCreatePLString("Host"));

	dPosition = WMCreatePLString("Position");
	dApplications = WMCreatePLString("Applications");
	dLowered = WMCreatePLString("Lowered");
	dCollapsed = WMCreatePLString("Collapsed");
	dAutoCollapse = WMCreatePLString("AutoCollapse");
	dAutoRaiseLower = WMCreatePLString("AutoRaiseLower");
	dAutoAttractIcons = WMCreatePLString("AutoAttractIcons");

	dOmnipresent = WMCreatePLString("Omnipresent");

	dDock = WMCreatePLString("Dock");
	dDrawers = WMCreatePLString("Drawers");
}
WDock *drawer_create(virtual_screen *vscr, const char *name)
{
	WDock *dock;
	WAppIcon *btn;

	make_keys();

	dock = dock_create_core(vscr);

	/* Set basic variables */
	dock->type = WM_DRAWER;
	dock->auto_collapse = 1;

	if (!name)
		name = findUniqueName(vscr, "Drawer");

	btn = dock_icon_create(vscr, NULL, "WMDrawer", (char *) name);

	/* Create appicon's icon */
	btn->xindex = 0;
	btn->yindex = 0;
	btn->docked = 1;
	btn->dock = dock;
	dock->on_right_side = vscr->dock.dock->on_right_side;
	dock->icon_array[0] = btn;

	btn->icon->core->descriptor.parent_type = WCLASS_DOCK_ICON;
	btn->icon->core->descriptor.parent = btn;
	btn->icon->tile_type = TILE_DRAWER;
	dock->menu = NULL;
	drawerAppendToChain(dock);

	return dock;
}

/* Don't free the returned string. Duplicate it. */
static char *findUniqueName(virtual_screen *vscr, const char *instance_basename)
{
	static char buffer[128];
	WDrawerChain *dc;
	int i;
	Bool already_in_use = True;

#define UNIQUE_NAME_WATCHDOG 128
	for (i = 0; already_in_use && i < UNIQUE_NAME_WATCHDOG; i++) {
		snprintf(buffer, sizeof buffer, "%s%d", instance_basename, i);

		already_in_use = False;

		for (dc = vscr->drawer.drawers; dc != NULL; dc = dc->next) {
			if (!strncmp(dc->adrawer->icon_array[0]->wm_instance, buffer,
					sizeof buffer)) {
				already_in_use = True;
				break;
			}
		}
	}

	if (i == UNIQUE_NAME_WATCHDOG)
		wwarning("Couldn't find a unique name for drawer in %d attempts.", i);
#undef UNIQUE_NAME_WATCHDOG

	return buffer;
}

static void drawerAppendToChain(WDock *drawer)
{
	virtual_screen *vscr = drawer->vscr;
	WDrawerChain **where_to_add;

	where_to_add = &vscr->drawer.drawers;
	while ((*where_to_add) != NULL)
		where_to_add = &(*where_to_add)->next;

	*where_to_add = wmalloc(sizeof(WDrawerChain));
	(*where_to_add)->adrawer = drawer;
	(*where_to_add)->next = NULL;
	vscr->drawer.drawer_count++;
}
