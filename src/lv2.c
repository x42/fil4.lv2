/* fil4.lv2
 *
 * Copyright (C) 2004-2009 Fons Adriaensen <fons@kokkinizita.net>
 * Copyright (C) 2015 Robin Gareus <robin@gareus.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filters.h"
#include "uris.h"
#include "iir.h"
#include "hip.h"
#include "lop.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"

static bool printed_capacity_warning = false;

typedef struct {
	Fil4Paramsect _sect [NSECT];
	HighPass      hip;
	LowPass       lop;

	IIRProc       iir_lowshelf;
	IIRProc       iir_highshelf;

	int           _fade;
	float         _gain;
} FilterChannel;

typedef struct {
	float        *_port [FIL_LAST];
	float         rate;

	FilterChannel fc[2];
	uint32_t n_channels;

	/* atom-forge & fft related */
	const LV2_Atom_Sequence *control;
	LV2_Atom_Sequence       *notify;
	LV2_URID_Map            *map;
	Fil4LV2URIs              uris;
	LV2_Atom_Forge           forge;
	LV2_Atom_Forge_Frame     frame;

	/* GUI state */
	bool                     ui_active;
	bool                     send_state_to_ui;

	int                      fft_mode;
	int                      fft_chan;
	float                    fft_gain;
	float                    db_scale;

} Fil4;

static void init_filter_channel (FilterChannel *fc, double rate) {
	fc->_fade = 0;
	fc->_gain = 1.f;
	for (int j = 0; j < NSECT; ++j) {
		fc->_sect [j].init ();
	}

	iir_init (&fc->iir_lowshelf, rate);
	iir_init (&fc->iir_highshelf, rate);

	fc->iir_lowshelf.freq = 50;
	fc->iir_highshelf.freq = 8000;

	iir_calc_lowshelf (&fc->iir_lowshelf);
	iir_calc_highshelf (&fc->iir_highshelf);

	hip_setup (&fc->hip, rate, 20, .7);
	lop_setup (&fc->lop, rate, 10000, .7);
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	Fil4* self = (Fil4*)calloc(1, sizeof(Fil4));

	if (!strcmp (descriptor->URI, FIL4_URI "mono")) {
		self->n_channels = 1;
	} else if (!strcmp (descriptor->URI, FIL4_URI "stereo")) {
		self->n_channels = 2;
	} else {
		free (self);
		return NULL;
	}

	for (int i=0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID__map)) {
			self->map = (LV2_URID_Map*)features[i]->data;
		}
	}

	if (!self->map) {
		fprintf (stderr, "fil4.lv2 error: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	self->rate = rate;
	lv2_atom_forge_init (&self->forge, self->map);
	map_fil4_uris (self->map, &self->uris);

	for (uint32_t c = 0; c < self->n_channels; ++c) {
		init_filter_channel (&self->fc[c], rate);
	}

	self->ui_active = false;
	self->fft_mode = 0;
	self->fft_gain = 0;
	self->fft_chan = -1;
	self->db_scale = DEFAULT_YZOOM;

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Fil4* self = (Fil4*)instance;
	if (port == FIL_ATOM_CONTROL) {
		self->control = (const LV2_Atom_Sequence*) data;
	} else if (port == FIL_ATOM_NOTIFY) {
		self->notify = (LV2_Atom_Sequence*) data;
	} else if (port <= FIL_OUTPUT1) {
		self->_port[port] = (float*) data;
	}
}

static float exp2ap (float x) {
	int i;

	i = (int)(floorf (x));
	x -= i;
	return ldexpf (1 + x * (0.6930f + x * (0.2416f + x * (0.0517f + x * 0.0137f))), i);
}

/** forge atom-vector of raw data */
static void tx_rawaudio (LV2_Atom_Forge *forge, Fil4LV2URIs *uris,
                         const float sr, const uint32_t chn,
                         const size_t n_samples, void *data)
{
	LV2_Atom_Forge_Frame frame;
	/* forge container object of type 'rawaudio' */
	lv2_atom_forge_frame_time(forge, 0);
	x_forge_object(forge, &frame, 1, uris->rawaudio);

	/* add float attribute 'samplerate' */
	lv2_atom_forge_property_head(forge, uris->samplerate, 0);
	lv2_atom_forge_float(forge, sr);

	/* add integer attribute 'channelid' */
	lv2_atom_forge_property_head(forge, uris->channelid, 0);
	lv2_atom_forge_int(forge, chn);

	/* add vector of floats raw 'audiodata' */
	lv2_atom_forge_property_head(forge, uris->audiodata, 0);
	lv2_atom_forge_vector(forge, sizeof(float), uris->atom_Float, n_samples, data);

	/* close off atom-object */
	lv2_atom_forge_pop(forge, &frame);
}

static void tx_state (Fil4* self)
{
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(&self->forge, 0);
	x_forge_object(&self->forge, &frame, 1, self->uris.state);

	lv2_atom_forge_property_head(&self->forge, self->uris.samplerate, 0);
	lv2_atom_forge_float(&self->forge, self->rate);

	lv2_atom_forge_property_head(&self->forge, self->uris.s_dbscale, 0);
	lv2_atom_forge_float(&self->forge, self->db_scale);

	lv2_atom_forge_property_head(&self->forge, self->uris.s_fftgain, 0);
	lv2_atom_forge_float(&self->forge, self->fft_gain);

	lv2_atom_forge_property_head(&self->forge, self->uris.s_fftmode, 0);
	lv2_atom_forge_int(&self->forge, self->fft_mode);

	lv2_atom_forge_property_head(&self->forge, self->uris.s_fftchan, 0);
	lv2_atom_forge_int(&self->forge, self->fft_chan);

	lv2_atom_forge_pop(&self->forge, &frame);
}

static void process_channel(Fil4* self, FilterChannel *fc, uint32_t p_samples, uint32_t chn) {

	/* localize variables */
	const float ls_gain = *self->_port[IIR_LS_EN] > 0 ? powf (10.f, .05f * self->_port[IIR_LS_GAIN][0]) : 1.f;
	const float hs_gain = *self->_port[IIR_HS_EN] > 0 ? powf (10.f, .05f * self->_port[IIR_HS_GAIN][0]) : 1.f;
	const float ls_freq = *self->_port[IIR_LS_FREQ];
	const float hs_freq = *self->_port[IIR_HS_FREQ];
	// map [2^-4 .. 4] to [2^(-3/2) .. 2]
	const float ls_q    = .2129f + self->_port[IIR_LS_Q][0] / 2.25f;
	const float hs_q    = .2129f + self->_port[IIR_HS_Q][0] / 2.25f;
	const bool  hipass  = *self->_port[FIL_HIPASS] > 0 ? true : false;
	const bool  lopass  = *self->_port[FIL_LOPASS] > 0 ? true : false;
	const float hifreq  = *self->_port[FIL_HIFREQ];
	const float hi_q    = *self->_port[FIL_HIQ];
	const float lofreq  = *self->_port[FIL_LOFREQ];
	const float lo_q    = *self->_port[FIL_LOQ];

	float *aip = self->_port [FIL_INPUT0 + (chn<<1)];
	float *aop = self->_port [FIL_OUTPUT0 + (chn<<1)];

	float sfreq [NSECT];
	float sband [NSECT];
	float sgain [NSECT];



	/* calculate target values, parameter smoothing */
	const float fgain = exp2ap (0.1661 * self->_port [FIL_GAIN][0]);

	for (int j = 0; j < NSECT; ++j) {
		float t = self->_port [FIL_SEC1 + 4 * j + Fil4Paramsect::FREQ][0] / self->rate;
		if (t < 0.0002) t = 0.0002;
		if (t > 0.4998) t = 0.4998;

		sfreq [j] = t;
		sband [j] = self->_port [FIL_SEC1 + 4 * j + Fil4Paramsect::BAND][0];

		if (self->_port [FIL_SEC1 + 4 * j + Fil4Paramsect::SECT][0] > 0) {
			sgain [j] = exp2ap (0.1661 * self->_port [FIL_SEC1 + 4 * j + Fil4Paramsect::GAIN][0]);
		} else {
			sgain [j] = 1.0;
		}
	}

	while (p_samples) {
		uint32_t i;
		float sig [48];
		const uint32_t k = (p_samples > 48) ? 32 : p_samples;

		float t = fgain;
		float g = fc->_gain;
		if      (t > 1.25 * g) t = 1.25 * g;
		else if (t < 0.80 * g) t = 0.80 * g;
		fc->_gain = t;
		float d = (t - g) / k;

		/* apply gain */
		for (i = 0; i < k; i++) {
			g += d;
			sig [i] = g * aip [i];
		}

		/* update IIR */
		if (iir_interpolate (&fc->iir_lowshelf,  ls_gain, ls_freq, ls_q)) {
			iir_calc_lowshelf (&fc->iir_lowshelf);
		}
		if (iir_interpolate (&fc->iir_highshelf, hs_gain, hs_freq, hs_q)) {
			iir_calc_highshelf (&fc->iir_highshelf);
		}

		hip_interpolate (&fc->hip, hipass, hifreq, hi_q);
		lop_interpolate (&fc->lop, lopass, lofreq, lo_q);

		/* run filters */

		hip_compute (&fc->hip, k, sig);
		lop_compute (&fc->lop, k, sig);

		for (int j = 0; j < NSECT; ++j) {
			fc->_sect [j].proc (k, sig, sfreq [j], sband [j], sgain [j]);
		}

		iir_compute (&fc->iir_lowshelf, k, sig);
		iir_compute (&fc->iir_highshelf, k, sig);

		/* fade 16 * 32 samples when enable changes */
		int j = fc->_fade;
		g = j / 16.0;

		float *p = NULL;

		if (self->_port [FIL_ENABLE][0] > 0) {
			if (j == 16) p = sig;
			else ++j;
		}
		else
		{
			if (j == 0) p = aip;
			else --j;
		}
		fc->_fade = j;

		if (p) {
			/* active or bypassed */
			memcpy (aop, p, k * sizeof (float));
		} else {
			/* fade in/out */
			d = (j / 16.0 - g) / k;
			for (uint32_t i = 0; i < k; ++i) {
				g += d;
				aop [i] = g * sig [i] + (1 - g) * aip [i];
			}
		}

		aip += k;
		aop += k;
		p_samples -= k;
	}
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	Fil4* self = (Fil4*)instance;

	/* check atom buffer size */
	const size_t size = (sizeof(float) * self->n_channels * n_samples + 64);
	const uint32_t capacity = self->notify->atom.size;
	bool capacity_ok = true;
	if (capacity < size + 128) {
		capacity_ok = false;
		if (!printed_capacity_warning) {
#ifdef _WIN32
			fprintf (stderr, "meters.lv2 error: LV2 comm-buffersize is insufficient %d/%d bytes.\n",
					capacity, (int)size + 160);
#else
			fprintf (stderr, "meters.lv2 error: LV2 comm-buffersize is insufficient %d/%zu bytes.\n",
					capacity, size + 160);
#endif
			printed_capacity_warning = true;
		}
	}

	/* prepare forge buffer and initialize atom-sequence */
	lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->notify, capacity);
	lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);

	// process messages from GUI;
	if (self->control) {
		LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(self->control)->body);
		while(!lv2_atom_sequence_is_end(&(self->control)->body, (self->control)->atom.size, ev)) {
			if (ev->body.type == self->uris.atom_Blank || ev->body.type == self->uris.atom_Object) {
				const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
				if (obj->body.otype == self->uris.ui_off) {
					self->ui_active = false;
				}
				else if (obj->body.otype == self->uris.ui_on) {
					self->ui_active = true;
					self->send_state_to_ui = true;
				}
				else if (obj->body.otype == self->uris.state) {
					const LV2_Atom* v = NULL;
					lv2_atom_object_get(obj, self->uris.s_fftmode, &v, 0);
					if (v) { self->fft_mode = ((LV2_Atom_Int*)v)->body; }

					v = NULL;
					lv2_atom_object_get(obj, self->uris.s_fftgain, &v, 0);
					if (v) { self->fft_gain = ((LV2_Atom_Float*)v)->body; }

					v = NULL;
					lv2_atom_object_get(obj, self->uris.s_fftchan, &v, 0);
					if (v) { self->fft_chan = ((LV2_Atom_Int*)v)->body; }

					v = NULL;
					lv2_atom_object_get(obj, self->uris.s_dbscale, &v, 0);
					if (v) { self->db_scale = ((LV2_Atom_Float*)v)->body; }
				}
				ev = lv2_atom_sequence_next(ev);
			}
		}
	}

	if (self->ui_active && self->send_state_to_ui) {
		self->send_state_to_ui = false;
		tx_state (self);
	}

	const int fft_mode = self->ui_active ? self->fft_mode : 0;

	// send raw input
	if ((fft_mode & 1) == 1 && capacity_ok) {
		for (uint32_t c = 0; c < self->n_channels; ++c) {
			tx_rawaudio (&self->forge, &self->uris, self->rate, c, n_samples, self->_port [FIL_INPUT0 + (c<<1)]);
		}
	}

	// audio processing
	for (uint32_t c = 0; c < self->n_channels; ++c) {
		process_channel (self, &self->fc[c], n_samples, c);
	}

	// send processed output
	if (fft_mode > 0 && (fft_mode & 1) == 0 && capacity_ok) {
		for (uint32_t c = 0; c < self->n_channels; ++c) {
			tx_rawaudio (&self->forge, &self->uris, self->rate, c, n_samples, self->_port [FIL_OUTPUT0 + (c<<1)]);
		}
	}
	
	/* close off atom-sequence */
	lv2_atom_forge_pop(&self->forge, &self->frame);
}

#define STATESTORE(URI, TYPE, VALUE) \
	store(handle, self->uris.URI, \
			(void*) &(VALUE), sizeof(uint32_t), \
			self->uris.atom_ ## TYPE, \
			LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE); \

static LV2_State_Status
fil4_save(LV2_Handle                instance,
          LV2_State_Store_Function  store,
          LV2_State_Handle          handle,
          uint32_t                  flags,
          const LV2_Feature* const* features)
{
	Fil4* self = (Fil4*)instance;

	STATESTORE(s_dbscale, Float, self->db_scale)
	STATESTORE(s_fftgain, Float, self->fft_gain)
	STATESTORE(s_fftmode, Int, self->fft_mode)
	STATESTORE(s_fftchan, Int, self->fft_chan)

	return LV2_STATE_SUCCESS;
}

#define STATEREAD(URI, TYPE, CAST, PARAM) \
	value = retrieve(handle, self->uris.URI, &size, &type, &valflags); \
	if (value && size == sizeof(uint32_t) && type == self->uris.atom_ ## TYPE) { \
		PARAM = *((const CAST *)value); \
	}


static LV2_State_Status
fil4_restore(LV2_Handle                  instance,
             LV2_State_Retrieve_Function retrieve,
             LV2_State_Handle            handle,
             uint32_t                    flags,
             const LV2_Feature* const*   features)
{
	Fil4* self = (Fil4*)instance;
	const void* value;
	size_t   size;
	uint32_t type;
	uint32_t valflags;

	STATEREAD(s_dbscale, Float, float, self->db_scale)
	STATEREAD(s_fftgain, Float, float, self->fft_gain)
	STATEREAD(s_fftmode, Int,   int,   self->fft_mode)
	STATEREAD(s_fftchan, Int,   int,   self->fft_chan)

	self->send_state_to_ui = true;
	return LV2_STATE_SUCCESS;
}

static void
cleanup(LV2_Handle instance)
{
	free(instance);
}

const void*
extension_data(const char* uri)
{
	static const LV2_State_Interface  state  = { fil4_save, fil4_restore };
	if (!strcmp(uri, LV2_STATE__interface)) {
		return &state;
	}
	return NULL;
}

static const LV2_Descriptor descriptor_mono = {
	FIL4_URI "mono",
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

static const LV2_Descriptor descriptor_stereo = {
	FIL4_URI "stereo",
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor_mono;
	case 1:
		return &descriptor_stereo;
	default:
		return NULL;
	}
}
