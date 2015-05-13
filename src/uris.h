/* simple spectrum analyzer
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SPR_URIS_H
#define SPR_URIS_H

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"

#define FIL4_URI "http://gareus.org/oss/lv2/fil4#"

#ifdef HAVE_LV2_1_8
#define x_forge_object lv2_atom_forge_object
#else
#define x_forge_object lv2_atom_forge_blank
#endif

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Vector;
	LV2_URID atom_Float;
	LV2_URID atom_eventTransfer;
	LV2_URID rawaudio;
	LV2_URID channelid;
	LV2_URID audiodata;
	LV2_URID samplerate;
	LV2_URID fftmode;
	LV2_URID ui_off;
} Fil4LV2URIs;

static inline void
map_fil4_uris(LV2_URID_Map* map, Fil4LV2URIs* uris) {
	uris->atom_Blank         = map->map(map->handle, LV2_ATOM__Blank);
	uris->atom_Object        = map->map(map->handle, LV2_ATOM__Object);
	uris->atom_Vector        = map->map(map->handle, LV2_ATOM__Vector);
	uris->atom_Float         = map->map(map->handle, LV2_ATOM__Float);
	uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	uris->rawaudio           = map->map(map->handle, FIL4_URI "rawaudio");
	uris->audiodata          = map->map(map->handle, FIL4_URI "audiodata");
	uris->samplerate         = map->map(map->handle, FIL4_URI "samplerate");
	uris->fftmode            = map->map(map->handle, FIL4_URI "fftmode");
	uris->ui_off             = map->map(map->handle, FIL4_URI "ui_off");
}

#endif
