/* fil4.lv2
 *
 * Copyright (C) 2016 Robin Gareus <robin@gareus.org>
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

#ifdef DISPLAY_INTERFACE

#ifndef HYPOTF
#define HYPOTF(X,Y) (sqrtf (SQUARE(X) + SQUARE(Y)))
#endif

#ifndef MIN
#define MIN(A,B) ((A) < (B)) ? (A) : (B)
#endif

struct omega {
	float c1, s1, c2, s2;
};

/* drawing helpers, calculate respone for given frequency */
static float get_filter_response (Fil4Paramsect const * const flt, struct omega const * const w) {
	float x = w->c2 + flt->s1() * w->c1 + flt->s2();
	float y = w->s2 + flt->s1() * w->s1;
	const float t1 = HYPOTF (x, y);
	x += flt->g0 () * (w->c2 - 1.f);
	y += flt->g0 () * w->s2;
	const float t2 = HYPOTF (x, y);
	return 20.f * log10f (t2 / t1);
}

static float get_shelf_response (IIRProc const * const flt, struct omega const * const w) {
	const float _A  = flt->b0 + flt->b2;
	const float _B  = flt->b0 - flt->b2;
	const float _C  = 1.0     + flt->a2;
	const float _D  = 1.0     - flt->a2;

	const float A = _A * w->c1 + flt->b1;
	const float B = _B * w->s1;
	const float C = _C * w->c1 + flt->a1;
	const float D = _D * w->s1;
	return 20.f * log10f (sqrtf ((SQUARE(A) + SQUARE(B)) * (SQUARE(C) + SQUARE(D))) / (SQUARE(C) + SQUARE(D)));
}

static float get_highpass_response (HighPass const * const hip, const float freq) {
	if (!hip->en) {
		return 0;
	} else {
		// this is only approximate, not including resonance
		const float wr = hip->freq / freq;
		float q = hip->q;
		return -10.f * log10f (SQUARE(1 + SQUARE(wr)) - SQUARE(q * wr));
	}
}

static float get_lowpass_response (LowPass const * const lop, const float freq, const float rate , struct omega const * const _w) {
	if (!lop->en) {
		return 0;
	} else {
		// this is only an approx.
		const float w  = sin (M_PI * freq / rate);
		const float wc = sin (M_PI * lop->freq / rate);
		const float q =  sqrtf(4.f * lop->r / (1 + lop->r));
		float xhs = 0;
#ifdef LP_EXTRA_SHELF
		xhs = get_shelf_response (&lop->iir_hs, _w);
#endif
		return -10.f * log10f (SQUARE(1 + SQUARE(w/wc)) - SQUARE(q * w / wc)) + xhs;
	}
}

static float freq_at_x (const int x, const float w) {
	return 20.f * powf (1000.f, x / w);
}

static float x_at_freq (const float f, const float w) {
	return rintf (w * logf (f / 20.0) / logf (1000.0));
}

static LV2_Inline_Display_Image_Surface *
fil4_render(LV2_Handle instance, uint32_t w, uint32_t max_h)
{
#ifdef WITH_SIGNATURE
	if (!is_licensed (instance)) { return NULL; }
#endif
	uint32_t h = MIN (ceilf (w * 9.f/16.f), max_h);

	Fil4* self = (Fil4*)instance;
	if (!self->display || self->w != w || self->h != h) {
		if (self->display) cairo_surface_destroy(self->display);
		self->display = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
		self->w = w;
		self->h = h;
	}
	cairo_t* cr = cairo_create (self->display);
	cairo_rectangle (cr, 0, 0, w, h);
	if (self->enabled) {
		cairo_set_source_rgba (cr, .2, .2, .2, 1.0);
	} else {
		cairo_set_source_rgba (cr, .1, .1, .1, 1.0);
	}
	cairo_fill (cr);

	const float yr = (h - 2.f) / (2.f * 20); // +/- 20dB
	const float ym = rintf ((h - 2.f) * .5f) - .5;
	const float xw = w - 1;

	const float a = self->enabled ? 1.0 : .2;

	/* zero line */
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba (cr, .6, .6, .6, a);
	cairo_move_to (cr, 1,     ym);
	cairo_line_to (cr, w - 1, ym);
	cairo_stroke(cr);

#define X_GRID(FREQ) { \
	const float xx = .5 + x_at_freq (FREQ, xw); \
	cairo_move_to (cr, xx, 0); \
	cairo_line_to (cr, xx, h); \
	cairo_stroke (cr); \
}

#define Y_GRID(dB) { \
	const float yy = rintf (yr * dB); \
	cairo_move_to (cr, 0, ym - yy); \
	cairo_line_to (cr, w, ym - yy); \
	cairo_stroke (cr); \
	cairo_move_to (cr, 0, ym + yy); \
	cairo_line_to (cr, w, ym + yy); \
	cairo_stroke (cr); \
}

	const double dash2[] = {1, 3};
	cairo_save (cr);
	cairo_set_dash(cr, dash2, 2, 2);
	cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.5 * a);
	X_GRID (100);
	X_GRID (1000);
	X_GRID (10000);
	Y_GRID (6);
	//Y_GRID (12);
	Y_GRID (18);
	cairo_restore (cr);

	FilterChannel const * const fc = &self->fc[0];

	for (uint32_t i = 0; i < xw; ++i) {
		const float freq = freq_at_x (i, xw);
		const float w = 2.f * M_PI * freq / self->rate;
		struct omega _w;
		_w.c1 = cosf (w);
		_w.s1 = sinf (w);
		_w.c2 = cosf (2.f * w);
		_w.s2 = sinf (2.f * w);

		float y = 0;
		for (int j = 0; j < NSECT; ++j) {
			y += yr * get_filter_response (&fc->_sect[j], &_w);
		}
		y += yr * get_shelf_response (&fc->iir_lowshelf, &_w);
		y += yr * get_shelf_response (&fc->iir_highshelf, &_w);

		y += yr * get_highpass_response (&fc->hip, freq);
		y += yr * get_lowpass_response (&fc->lop, freq, self->rate, &_w);

		if (i == 0) {
			cairo_move_to (cr, 0.5 + i, ym - y);
		} else {
			cairo_line_to (cr, 0.5 + i, ym - y);
		}
	}

	cairo_set_source_rgba (cr, .8, .8, .8, a);
	cairo_stroke_preserve(cr);
	cairo_line_to (cr, w, ym);
	cairo_line_to (cr, 0, ym);
	cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.5 * a);
	cairo_fill (cr);

	cairo_destroy (cr);
	cairo_surface_flush (self->display);
	self->surf.width = cairo_image_surface_get_width (self->display);
	self->surf.height = cairo_image_surface_get_height (self->display);
	self->surf.stride = cairo_image_surface_get_stride (self->display);
	self->surf.data = cairo_image_surface_get_data  (self->display);

	return &self->surf;
}
#endif
