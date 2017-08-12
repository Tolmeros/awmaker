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


#ifndef WMDIALOG_H_
#define WMDIALOG_H_

typedef struct IconPanel IconPanel;
typedef struct LegalPanel LegalPanel;
typedef struct InfoPanel InfoPanel;

#include "appicon.h"
#include "dockedapp.h"
#include "winspector.h"

enum {
	WMAbort = 0,
	WMRestart,
	WMStartAlternate
};

struct IconPanel {
	virtual_screen *vscr;
	WWindow *wwin;
	WMWindow *win;

	WMLabel *dirLabel;
	WMLabel *iconLabel;

	WMList *dirList;
	WMList *iconList;
	WMFont *normalfont;

	WMButton *previewButton;

	WMLabel *iconView;

	WMLabel *fileLabel;
	WMTextField *fileField;

	WMButton *okButton;
	WMButton *cancelButton;

	short done;
	short result;
	short preview;
};

struct LegalPanel {
	virtual_screen *vscr;
	WWindow *wwin;
	WMWindow *win;
	WMLabel *licenseL;
};

struct InfoPanel {
	virtual_screen *vscr;
	WWindow *wwin;
	WMWindow *win;
	WMLabel *logoL;
	WMLabel *name1L;
	WMFrame *lineF;
	WMLabel *name2L;
	WMLabel *versionL;
	WMLabel *infoL;
	WMLabel *copyrL;
};

int wMessageDialog(virtual_screen *vscr, const char *title, const char *message,
		   const char *defBtn, const char *altBtn, const char *othBtn);
int wAdvancedInputDialog(virtual_screen *vscr, const char *title, const char *message, const char *name, char **text);
int wInputDialog(virtual_screen *vscr, const char *title, const char *message, char **text);

int wExitDialog(virtual_screen *vscr, const char *title, const char *message, const char *defBtn,
		const char *altBtn, const char *othBtn);

Bool wIconChooserDialog(AppSettingsPanel *app_panel, InspectorPanel *ins_panel, WAppIcon *icon, char **file);

void wShowInfoPanel(virtual_screen *vscr);
void wShowLegalPanel(virtual_screen *vscr);
int wShowCrashingDialogPanel(int whatSig);


#endif
