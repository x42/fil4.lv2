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
	float y1, y2;
	float z1, z2;
	float a;
	float omega;
} HighPass;

static void hip_setup (HighPass *f, float rate, float freq) {
	memset (f, 0, sizeof(HighPass));
	f->omega = exp (-2.0 * M_PI * freq / rate);
	f->a = 1.0;
}

static void hip_interpolate (HighPass *f, bool en) {
	const float target = en ? f->omega : 1.0;
	if (fabsf(target - f->a) < 1e-6) {
		f->a = target;
	} else {
		f->a += .01 * (target - f->a);
	}
	if (!en) {
		f->z1 += .01 * (f->y1 - f->z1);
		f->z2 += .01 * (f->y2 - f->z2);
	}
#ifndef NO_NAN_PROTECTION
	if (isnan(f->z1)) f->z1 = 0;
	if (isnan(f->z2)) f->z2 = 0;
	if (isnan(f->y1)) f->y1 = 0;
	if (isnan(f->y2)) f->y2 = 0;
#endif
}

static void hip_compute (HighPass *f, uint32_t n_samples, float *buf) {
	float y1 = f->y1;
	float z1 = f->z1;
	float y2 = f->y2;
	float z2 = f->z2;
	const float a = f->a;
	for (uint32_t i = 0; i < n_samples; ++i) {
		const float _z1 = z1;
		const float _z2 = z2;
		z1 = buf[i];
		y1 = a * (y1 + buf[i] - _z1);
		z2 = y1;
		y2 = buf[i] = y1 + a * y2 - _z2;
	}
	f->y2 = y2;
	f->y1 = y1;
	f->z1 = z1 + 1e-12;
	f->z2 = z2 + 1e-12;
}
