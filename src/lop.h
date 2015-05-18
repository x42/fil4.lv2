/* fil4.lv2 - highpass
 *
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

#include <math.h>

typedef struct {
	float z1, z2;
	float a;
	float alpha;
	float freq;
	float rate;
} LowPass;

static float calc_lop_alpha (float rate, float freq) {
	float fr = freq / rate;
	if (fr < 0.0002) fr = 0.0002;
	if (fr > 0.4998) fr = 0.4998;
	return 1.0 - exp (-2.0 * M_PI * fr);
}

static void lop_setup (LowPass *f, float rate, float freq, float res) {
	memset (f, 0, sizeof(LowPass));
	f->rate = rate;
	f->freq = freq;
	f->alpha = calc_lop_alpha (rate, freq);
	f->a = 1.0;
}

static void lop_interpolate (LowPass *f, bool en, float freq, float res) {
	if (freq != f->freq) {
		f->alpha = calc_lop_alpha (f->rate, freq);
		f->freq = freq;
	}
	const float target = en ? f->alpha : 1.0;
	if (fabsf(target - f->a) < 1e-5) {
		f->a = target;
	} else {
		f->a += .01 * (target - f->a);
	}
#ifndef NO_NAN_PROTECTION
	if (isnan(f->z1)) f->z1 = 0;
	if (isnan(f->z2)) f->z2 = 0;
#endif
}

static void lop_compute (LowPass *f, uint32_t n_samples, float *buf) {
	float z1 = f->z1;
	float z2 = f->z2;
	const float a = f->a;
	for (uint32_t i = 0; i < n_samples; ++i) {
		const float in = (2.0 - a) * buf[i] - z2 * (1.0 - a);
		z1 += a * (in - z1);
		z2 += a * (z1 - z2);
		buf[i] = z2;
	}
	f->z1 = z1 + 1e-12;
	f->z2 = z2 + 1e-12;
}
