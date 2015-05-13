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
#include "iir.h"
#include "hip.h"
#include "lop.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "uris.h"

#define NSECT (4)

static bool printed_capacity_warning = false;

typedef enum {
	FIL_INPUT = 0,
	FIL_OUTPUT,

	FIL_ENABLE,
	FIL_GAIN,
	FIL_HIPASS, FIL_HIFREQ,
	FIL_LOPASS, FIL_LOFREQ,

	IIR_LS_EN, IIR_LS_FREQ, IIR_LS_Q, IIR_LS_GAIN,

	FIL_SEC1, FIL_FREQ1, FIL_Q1, FIL_GAIN1,
	FIL_SEC2, FIL_FREQ2, FIL_Q2, FIL_GAIN2,
	FIL_SEC3, FIL_FREQ3, FIL_Q3, FIL_GAIN3,
	FIL_SEC4, FIL_FREQ4, FIL_Q4, FIL_GAIN4,

	IIR_HS_EN, IIR_HS_FREQ, IIR_HS_Q, IIR_HS_GAIN,

	FIL_FFT_MODE, FIL_ATOM_NOTIFY
} PortIndex;

typedef struct {
	float        *_port [FIL_FFT_MODE];
	float         _gain;
	int           _fade;
	Fil4Paramsect _sect [NSECT];
	float         _fsam;

	HighPass      hip;
	LowPass       lop;

	IIRProc       iir_lowshelf;
	IIRProc       iir_highshelf;

	/* atom-forge & fft related */
	float               *fft_ctrl;
	LV2_Atom_Sequence   *notify;
	LV2_URID_Map        *map;
	Fil4LV2URIs          uris;
	LV2_Atom_Forge       forge;
	LV2_Atom_Forge_Frame frame;

} Fil4;

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	Fil4* self = (Fil4*)calloc(1, sizeof(Fil4));

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

	self->_fsam = rate;
	self->_fade = 0;
	self->_gain = 1.f;
	for (int j = 0; j < NSECT; ++j) {
		self->_sect [j].init ();
	}

	iir_init (&self->iir_lowshelf, rate);
	iir_init (&self->iir_highshelf, rate);

	self->iir_lowshelf.freq = 50;
	self->iir_highshelf.freq = 8000;

	iir_calc_lowshelf (&self->iir_lowshelf);
	iir_calc_highshelf (&self->iir_highshelf);

	hip_setup (&self->hip, rate, 100);
	lop_setup (&self->lop, rate, 10000);

	lv2_atom_forge_init (&self->forge, self->map);
	map_fil4_uris (self->map, &self->uris);

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Fil4* self = (Fil4*)instance;
	if (port < FIL_FFT_MODE) {
		self->_port[port] = (float*) data;
	} else if (port == FIL_FFT_MODE) {
		self->fft_ctrl = (float*) data;
	} else if (port == FIL_ATOM_NOTIFY) {
		self->notify = (LV2_Atom_Sequence*)data;
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
                        const float sr, const size_t n_samples, void *data)
{
	LV2_Atom_Forge_Frame frame;
	/* forge container object of type 'rawaudio' */
	lv2_atom_forge_frame_time(forge, 0);
	x_forge_object(forge, &frame, 1, uris->rawaudio);

	/* add float attribute 'samplerate' */
	lv2_atom_forge_property_head(forge, uris->samplerate, 0);
	lv2_atom_forge_float(forge, sr);

	/* add vector of floats raw 'audiodata' */
	lv2_atom_forge_property_head(forge, uris->audiodata, 0);
	lv2_atom_forge_vector(forge, sizeof(float), uris->atom_Float, n_samples, data);

	/* close off atom-object */
	lv2_atom_forge_pop(forge, &frame);
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	Fil4* self = (Fil4*)instance;

	/* localize variables */
	const float ls_gain = *self->_port[IIR_LS_EN] > 0 ? powf (10.f, .05f * self->_port[IIR_LS_GAIN][0]) : 1.f;
	const float hs_gain = *self->_port[IIR_HS_EN] > 0 ? powf (10.f, .05f * self->_port[IIR_HS_GAIN][0]) : 1.f;
	const float ls_freq = *self->_port[IIR_LS_FREQ];
	const float hs_freq = *self->_port[IIR_HS_FREQ];
	const float ls_q    = .337f + self->_port[IIR_LS_Q][0] / 7.425; // map [.125 .. 8] to [2^(-3/2) .. 2^(1/2)]
	const float hs_q    = .337f + self->_port[IIR_HS_Q][0] / 7.425;
	const bool  hipass  = *self->_port[FIL_HIPASS] > 0 ? true : false;
	const bool  lopass  = *self->_port[FIL_LOPASS] > 0 ? true : false;
	const float hifreq  = *self->_port[FIL_HIFREQ];
	const float lofreq  = *self->_port[FIL_LOFREQ];

	float *aip = self->_port [FIL_INPUT];
	float *aop = self->_port [FIL_OUTPUT];

	float sfreq [NSECT];
	float sband [NSECT];
	float sgain [NSECT];

	int fft_mode = rint(*self->fft_ctrl);

	/* check atom buffer size */
	const size_t size = (sizeof(float) * n_samples + 64);
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

	// send raw input
	if ((fft_mode == 1 || fft_mode == 3) && capacity_ok) {
		tx_rawaudio (&self->forge, &self->uris, self->_fsam, n_samples, aip);
	}

	/* calculate target values, parameter smoothing */
	const float fgain = exp2ap (0.1661 * self->_port [FIL_GAIN][0]);

	for (int j = 0; j < NSECT; ++j) {
		float t = self->_port [FIL_SEC1 + 4 * j + Fil4Paramsect::FREQ][0] / self->_fsam;
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

	uint32_t p_samples = n_samples;

	while (p_samples) {
		uint32_t i;
		float sig [48];
		const uint32_t k = (p_samples > 48) ? 32 : p_samples;

		float t = fgain;
		float g = self->_gain;
		if      (t > 1.25 * g) t = 1.25 * g;
		else if (t < 0.80 * g) t = 0.80 * g;
		self->_gain = t;
		float d = (t - g) / k;

		/* apply gain */
		for (i = 0; i < k; i++) {
			g += d;
			sig [i] = g * aip [i];
		}

		/* update IIR */
		if (iir_interpolate (&self->iir_lowshelf,  ls_gain, ls_freq, ls_q)) {
			iir_calc_lowshelf (&self->iir_lowshelf);
		}
		if (iir_interpolate (&self->iir_highshelf, hs_gain, hs_freq, hs_q)) {
			iir_calc_highshelf (&self->iir_highshelf);
		}

		hip_interpolate (&self->hip, hipass, hifreq);
		lop_interpolate (&self->lop, lopass, lofreq);

		/* run filters */

		hip_compute (&self->hip, k, sig);
		lop_compute (&self->lop, k, sig);

		for (int j = 0; j < NSECT; ++j) {
			self->_sect [j].proc (k, sig, sfreq [j], sband [j], sgain [j]);
		}

		iir_compute (&self->iir_lowshelf, k, sig);
		iir_compute (&self->iir_highshelf, k, sig);

		/* fade 16 * 32 samples when enable changes */
		int j = self->_fade;
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
		self->_fade = j;

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

	// send processed output
	if ((fft_mode == 2 || fft_mode == 4) && capacity_ok) {
		tx_rawaudio (&self->forge, &self->uris, self->_fsam, n_samples, self->_port [FIL_OUTPUT]);
	}
	
	/* close off atom-sequence */
	lv2_atom_forge_pop(&self->forge, &self->frame);
}

static void
cleanup(LV2_Handle instance)
{
	free(instance);
}

const void*
extension_data(const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	FIL4_URI,
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
		return &descriptor;
	default:
		return NULL;
	}
}
