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

#include <stdlib.h>
#include <string.h>
#include "filters.h"
#include "iir.h"
#include "hip.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define FIL4_URI "http://gareus.org/oss/lv2/fil4#mono"

#define NSECT (4)

typedef enum {
	FIL_INPUT = 0,
	FIL_OUTPUT,

	FIL_ENABLE,
	FIL_GAIN,
	FIL_HIPASS,

	IIR_LS_EN, IIR_LS_FREQ, IIR_LS_Q, IIR_LS_GAIN,

	FIL_SEC1, FIL_FREQ1, FIL_Q1, FIL_GAIN1,
	FIL_SEC2, FIL_FREQ2, FIL_Q2, FIL_GAIN2,
	FIL_SEC3, FIL_FREQ3, FIL_Q3, FIL_GAIN3,
	FIL_SEC4, FIL_FREQ4, FIL_Q4, FIL_GAIN4,

	IIR_HS_EN, IIR_HS_FREQ, IIR_HS_Q, IIR_HS_GAIN,

	FIL_LAST
} PortIndex;

typedef struct {
	float        *_port [FIL_LAST];
	float         _gain;
	int           _fade;
	Fil4Paramsect _sect [NSECT];
	float         _fsam;

	HighPass      hip;

	IIRProc       iir_lowshelf;
	IIRProc       iir_highshelf;
} Fil4;

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	Fil4* self = (Fil4*)calloc(1, sizeof(Fil4));

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

	hip_setup (&self->hip, rate, 20);

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Fil4* self = (Fil4*)instance;
	if (port < FIL_LAST) {
		self->_port[port] = (float*) data;
	}
}

static float exp2ap (float x) {
	int i;

	i = (int)(floorf (x));
	x -= i;
	return ldexpf (1 + x * (0.6930f + x * (0.2416f + x * (0.0517f + x * 0.0137f))), i);
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
	const float ls_q    = .347f + self->_port[IIR_LS_Q][0] / 22.27; // map to 2..4 octaves
	const float hs_q    = .347f + self->_port[IIR_HS_Q][0] / 22.27;
	const bool  hipass  = *self->_port[FIL_HIPASS] > 0 ? true : false;

	float *aip = self->_port [FIL_INPUT];
	float *aop = self->_port [FIL_OUTPUT];

	float sfreq [NSECT];
	float sband [NSECT];
	float sgain [NSECT];

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

	while (n_samples) {
		uint32_t i;
		float sig [48];
		const uint32_t k = (n_samples > 48) ? 32 : n_samples;

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

		hip_interpolate (&self->hip, hipass);

		/* run filters */

		hip_compute (&self->hip, k, sig);

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
		n_samples -= k;
	}
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
