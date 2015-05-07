/* robtk fil4 gui
 *
 * Copyright 2015 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#define MTR_URI "http://gareus.org/oss/lv2/fil4#"
#define MTR_GUI "ui"

//#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

typedef struct {
	RobWidget *rw;

	RobTkLbl*  text_label;
} RobTkApp;


static RobWidget * toplevel(RobTkApp* ui, void * const top) {
	/* main widget: layout */
	ui->rw = rob_vbox_new(FALSE, 2);
	robwidget_make_toplevel(ui->rw, top);

	ui->text_label = robtk_lbl_new("Hello World.\nHow is life today?");

	rob_vbox_child_pack(ui->rw, robtk_lbl_widget(ui->text_label), TRUE, FALSE);

	return ui->rw;
}

static void gui_cleanup(RobTkApp* ui) {
	robtk_lbl_destroy(ui->text_label);
	rob_box_destroy(ui->rw);
}

/******************************************************************************
 * RobTk + LV2
 */

#define LVGL_RESIZEABLE

static void ui_enable(LV2UI_Handle handle) { }
static void ui_disable(LV2UI_Handle handle) { }

static LV2UI_Handle
instantiate(
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	if (strcmp(plugin_uri, MTR_URI "mono")) { return NULL; }

	RobTkApp* ui = (RobTkApp*) calloc(1,sizeof(RobTkApp));
	*widget = toplevel(ui, ui_toplevel);
	return ui;
}

static enum LVGLResize
plugin_scale_mode(LV2UI_Handle handle)
{
	return LVGL_LAYOUT_TO_FIT;
}

static void
cleanup(LV2UI_Handle handle)
{
	RobTkApp* ui = (RobTkApp*)handle;
	gui_cleanup(ui);
	free(ui);
}

static void
port_event(LV2UI_Handle handle,
		uint32_t     port_index,
		uint32_t     buffer_size,
		uint32_t     format,
		const void*  buffer)
{
	;
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}
