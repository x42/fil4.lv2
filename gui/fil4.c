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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>

#include "../src/uris.h"
#include "fft.c"
#include "analyser.cc"

#define MTR_URI "http://gareus.org/oss/lv2/fil4#"
#define MTR_GUI "ui"

#define DOTRADIUS (9) // radius of draggable nodes on the plot

#define NSECT (6) // number of filter-bands + 2 (lo,hi-shelf)
#define FFT_MAX 512

#ifndef MAX
#define MAX(A,B) ((A) > (B)) ? (A) : (B)
#endif

#ifndef MIN
#define MIN(A,B) ((A) < (B)) ? (A) : (B)
#endif


#ifndef SQUARE
#define SQUARE(X) ( (X) * (X) )
#endif

#ifndef HYPOTF
#define HYPOTF(X,Y) (sqrtf (SQUARE(X) + SQUARE(Y)))
#endif

/* plugin port mapping - see src/lv2.c and lv2ttl/fil4.ttl.in */
enum {
	FIL_ENABLE = 2,
	FIL_GAIN = 3,
	FIL_HIPASS, FIL_HIFREQ,
	FIL_LOPASS, FIL_LOFREQ,
	FIL_SEC1, FIL_FREQ1, FIL_Q1, FIL_GAIN1,
	FIL_ATOM_CONTROL = 32, FIL_ATOM_NOTIFY = 33
};

/* cached filter state */
typedef struct {
	float rate;
	float gain_db;
	float s1, s2;
	float A, B, C, D, A1, B1; // IIR
	float x0, y0; // mouse position
} FilterSection;

/* filter parameters */
typedef struct {
	float min;
	float max;
	float dflt;
} FilterFreq;

typedef struct {
	LV2UI_Write_Function write;
	LV2UI_Controller controller;
	LV2_Atom_Forge   forge;
	LV2_URID_Map*    map;
	Fil4LV2URIs      uris;

	PangoFontDescription *font[2];

	RobWidget *rw; // top-level container
	RobWidget *ctbl; // control element table

	/* main drawing area */
	RobWidget *m0;
	int m0_width;
	int m0_height;

	float m0_xw;
	float m0_ym;
	float m0_yr;
	float m0_y0;
	float m0_y1;

	RobTkSep  *sep_v0;
	RobTkSep  *sep_h0;

	// global section
	RobTkCBtn *btn_g_enable;
	RobTkDial *spn_g_gain;
	RobTkDial *spn_fftgain;

	RobTkIBtn *btn_g_hipass;
	RobTkIBtn *btn_g_lopass;
	RobTkDial *spn_g_hifreq;
	RobTkDial *spn_g_lofreq;

	// filter section
	RobTkCBtn *btn_enable[NSECT];
	RobTkDial *spn_freq[NSECT];
	RobTkDial *spn_gain[NSECT];
	RobTkDial *spn_bw[NSECT];

	// shelf section
	RobTkCBtn *btn_s_enable[2];
	RobTkDial *spn_s_freq[2];
	RobTkDial *spn_s_gain[2];
	RobTkDial *spn_s_bw[2];

	// fft
	float samplerate;
	RobTkSelect* sel_fft;

	// scrolling history
	struct FFTAnalysis *fa;
	float *ffy;
	int fft_hist_line;
	cairo_surface_t* fft_history;
	cairo_surface_t* fft_scale;

	Analyser *japa; // fons, how many bottles of wine is this going to cost me?
	int _ipsize;
	int _ipstep;
	int _stepcnt;
	int _bufpos;
	int _fpscnt;
	float _fscale[FFT_MAX + 1];
	float _bwcorr[FFT_MAX + 1]; // unused

	// misc other stuff
	cairo_surface_t* m0_grid;
	cairo_surface_t* m0_filters;
	cairo_surface_t* hpf_btn[2];
	cairo_surface_t* lpf_btn[2];
	cairo_surface_t* dial_bg[4];
	cairo_surface_t* dial_fq[NSECT];
	cairo_surface_t* dial_hplp[2];

	FilterSection flt[NSECT];
	float hilo[2];
	int dragging;
	bool filter_redisplay;
	bool disable_signals;

	bool scale_cached;
	float xscale[FFT_MAX + 1];

	const char *nfo;
} Fil4UI;

///////////////////////////////////////////////////////////////////////////////

/* frequency mapping */
static FilterFreq freqs[NSECT] = {
	/*min    max   dflt*/
	{  25,   400,    80}, // LS
	{  20,  2000,   160},
	{  40,  4000,   397},
	{ 100, 10000,  1250},
	{ 200, 20000,  2500},
	{1000, 16000,  8000}, // HS
};

static FilterFreq lphp[2] = {
	{   5,   200,    13}, // HP
	{5000, 20000, 18000}, // LP
};

/* vidual filter colors */
static const float c_fil[NSECT][4] = {
	{0.5, 0.6, 0.7, 0.8},
	{1.0, 0.2, 0.2, 0.8},
	{0.2, 1.0, 0.2, 0.8},
	{0.2, 0.2, 1.0, 0.8},
	{0.8, 0.7, 0.4, 0.8},
	{0.7, 0.4, 0.7, 0.8},
};

static const float c_ann[4] = {0.5, 0.5, 0.5, 1.0}; // text annotation color
static const float c_dlf[4] = {0.8, 0.8, 0.8, 1.0}; // dial faceplate fg

///////////////////////////////////////////////////////////////////////////////

/* graph log-scale mapping */
static float freq_at_x (const int x, const int m0_width) {
	return 20.f * powf (1000.f, x / (float) m0_width);
}

static float x_at_freq (const float f, const int m0_width) {
	return rintf(m0_width * logf (f / 20.0) / logf (1000.0));
}

/**** dial value mappings ****/
/* bandwidth [1/8 .. 8] <> dial [0..1] */
#define WARPBW (18)
static float bw_to_dial (float v) {
	if (v < .125) return 0.f;
	if (v >  8.0) return 1.f;
	return log (1. + WARPBW * (v - .125) / 7.875) / log (1. + WARPBW);
}

static float dial_to_bw (const float v) {
	return .125 + 7.875 * (pow((1. + WARPBW), v) - 1.) / WARPBW;
}

/* freq [min .. max] <> dial 0..1 */
#define WARPFQ (100.0)
static float freq_to_dial (FilterFreq *m, float f) {
	if (f < m->min) return 0.f;
	if (f > m->max) return 1.f;
	return log (1. + WARPFQ * (f - m->min) / (m->max - m->min)) / log (1. + WARPFQ);
}

static float dial_to_freq (FilterFreq *m, float f) {
	return m->min + (m->max - m->min) * (pow((1. + WARPFQ), f) - 1.) / WARPFQ;
}

/*** faceplates and annotation ***/

static void dial_annotation_db (RobTkDial * d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	char txt[16];
	snprintf(txt, 16, "%+5.1fdB", d->cur);

	int tw, th;
	cairo_save(cr);
	PangoLayout * pl = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(pl, ui->font[0]);
	pango_layout_set_text(pl, txt, -1);
	pango_layout_get_pixel_size(pl, &tw, &th);
	cairo_translate (cr, d->w_width / 2, d->w_height - 3);
	cairo_translate (cr, -tw / 2.0 , -th);
	cairo_set_source_rgba (cr, .0, .0, .0, .5);
	rounded_rectangle(cr, -1, -1, tw+3, th+1, 3);
	cairo_fill(cr);
	CairoSetSouerceRGBA(c_wht);
	pango_cairo_layout_path(cr, pl);
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
	cairo_restore(cr);
	cairo_new_path(cr);
}

static void dial_annotation_hz (RobTkCBtn *l, const int which, const float hz) {
	char txt[24];
	const char *pfx = (which == 0) ? "\u2abc" : ((which == NSECT -1) ? "\u2abb" : "");
	if (hz > 5000) {
		snprintf(txt, 16, "%s%.1fKHz", pfx, hz / 1000.f);
	} else {
		snprintf(txt, 16, "%s%.0fHz", pfx, hz);
	}
	robtk_cbtn_set_text(l, txt);
}

static void print_hz (char *t, float hz) {
	//printf("%f ", hz);
	hz = 5 * rintf(hz / 5.f);
	if (hz >= 990) {
		int dec = ((int)rintf (hz / 100.f)) % 10;
		if (dec != 0) {
			snprintf(t, 8, "%.0fK%d", floor(hz / 1000.f), dec);
		} else {
			snprintf(t, 8, "%.0fK", hz / 1000.f);
		}
	} else {
		snprintf(t, 8, "%.0f", hz);
	}
	//printf("-> %f -> %s\n", hz, t);
}

/*** knob faceplates ***/
static void prepare_faceplates(Fil4UI* ui) {
	cairo_t *cr;
	float xlp, ylp;

	ui->hpf_btn[0] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 26, 20);
	cr = cairo_create (ui->hpf_btn[0]);
	cairo_move_to (cr,  4, 16);
	cairo_line_to (cr,  9,  4);
	cairo_line_to (cr, 22,  4);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	CairoSetSouerceRGBA (c_blk);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_g80);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	ui->hpf_btn[1] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 26, 20);
	cr = cairo_create (ui->hpf_btn[1]);
	cairo_move_to (cr,  4, 16);
	cairo_line_to (cr,  9, 4);
	cairo_line_to (cr, 22, 4);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	CairoSetSouerceRGBA (c_blk);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_grn);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	ui->lpf_btn[0] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 26, 20);
	cr = cairo_create (ui->lpf_btn[0]);
	cairo_move_to (cr,  4,  4);
	cairo_line_to (cr, 17,  4);
	cairo_line_to (cr, 22, 16);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	CairoSetSouerceRGBA (c_blk);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_g80);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	ui->lpf_btn[1] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 26, 20);
	cr = cairo_create (ui->lpf_btn[1]);
	cairo_move_to (cr,  4, 4);
	cairo_line_to (cr, 17, 4);
	cairo_line_to (cr, 22, 16);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	CairoSetSouerceRGBA (c_blk);
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_grn);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);




#define INIT_DIAL_SF(VAR, W, H) \
	VAR = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H); \
	cr = cairo_create (VAR); \
	CairoSetSouerceRGBA(c_trs); \
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE); \
	cairo_rectangle (cr, 0, 0, GED_WIDTH, GED_HEIGHT); \
	cairo_fill (cr); \
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER); \

#define DIALDOTS(V, XADD, YADD) \
	float ang = (-.75 * M_PI) + (1.5 * M_PI) * (V); \
	xlp = GED_CX + XADD + sinf (ang) * (GED_RADIUS + 3.0); \
	ylp = GED_CY + YADD - cosf (ang) * (GED_RADIUS + 3.0); \
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); \
	CairoSetSouerceRGBA(c_dlf); \
	cairo_set_line_width(cr, 2.5); \
	cairo_move_to(cr, rint(xlp)-.5, rint(ylp)-.5); \
	cairo_close_path(cr); \
	cairo_stroke(cr);

#define RESPLABLEL(V) \
	{ \
	DIALDOTS(V, 6.5, 15.5) \
	xlp = GED_CX + 6.5 + sinf (ang) * (GED_RADIUS + 9.5); \
	ylp = GED_CY + 15.5 - cosf (ang) * (GED_RADIUS + 9.5); \
	}

	/* gain knob */
	INIT_DIAL_SF(ui->dial_bg[0], GED_WIDTH + 12, GED_HEIGHT + 20);
	RESPLABLEL(0.00);
	write_text_full(cr, "-18", ui->font[0], xlp, ylp,  0, 1, c_dlf);
	RESPLABLEL(.16);
	write_text_full(cr, "-12", ui->font[0], xlp, ylp,  0, 1, c_dlf);
	RESPLABLEL(.33);
	write_text_full(cr,  "-6", ui->font[0], xlp, ylp,  0, 1, c_dlf);
	RESPLABLEL(0.5);
	write_text_full(cr,   "0", ui->font[0], xlp, ylp,  0, 2, c_dlf);
	RESPLABLEL(.66);
	write_text_full(cr,  "+6", ui->font[0], xlp-2, ylp,  0, 3, c_dlf);
	RESPLABLEL(.83);
	write_text_full(cr, "+12", ui->font[0], xlp-2, ylp,  0, 3, c_dlf);
	RESPLABLEL(1.0);
	write_text_full(cr, "+18", ui->font[0], xlp-2, ylp,  0, 3, c_dlf);
	cairo_destroy (cr);

	/* bandwidth */
#define GZLINE (GED_HEIGHT - 0.5)
	INIT_DIAL_SF(ui->dial_bg[1], GED_WIDTH, GED_HEIGHT + 4);
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to (cr,  1, GZLINE);
	cairo_line_to (cr,  8, GZLINE);
	cairo_line_to (cr, 10, GZLINE - 4);
	cairo_line_to (cr, 12, GZLINE);
	cairo_line_to (cr, 19, GZLINE);
	cairo_move_to (cr, 12, GZLINE);
	cairo_line_to (cr, 10, GZLINE + 4);
	cairo_line_to (cr,  8, GZLINE);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH -  1, GZLINE);
	cairo_line_to (cr, GED_WIDTH -  4, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 10, GZLINE - 4);
	cairo_line_to (cr, GED_WIDTH - 16, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 19, GZLINE);
	cairo_move_to (cr, GED_WIDTH - 16, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 10, GZLINE + 4);
	cairo_line_to (cr, GED_WIDTH -  4, GZLINE);
	cairo_stroke (cr);

	{ DIALDOTS(0.00, .5, 3.5) }
	{ DIALDOTS(1/8., .5, 3.5) }
	{ DIALDOTS(2/8., .5, 3.5) }
	{ DIALDOTS(3/8., .5, 3.5) }
	{ DIALDOTS(4/8., .5, 3.5) }
	{ DIALDOTS(5/8., .5, 3.5) }
	{ DIALDOTS(6/8., .5, 3.5) }
	{ DIALDOTS(7/8., .5, 3.5) }
	{ DIALDOTS(1.00, .5, 3.5) }
	cairo_destroy (cr);

	/* low shelf */
	INIT_DIAL_SF(ui->dial_bg[2], GED_WIDTH, GED_HEIGHT + 4);
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to (cr,  1, GZLINE - 3);
	cairo_line_to (cr,  4, GZLINE - 3);
	cairo_line_to (cr, 14, GZLINE);
	cairo_line_to (cr, 18, GZLINE);
	cairo_move_to (cr, 14, GZLINE);
	cairo_line_to (cr,  4, GZLINE + 3);
	cairo_line_to (cr,  1, GZLINE + 3);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH -  1, GZLINE);
	cairo_line_to (cr, GED_WIDTH -  7, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 10, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH - 18, GZLINE - 3);
	cairo_move_to (cr, GED_WIDTH -  7, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 10, GZLINE + 3);
	cairo_line_to (cr, GED_WIDTH - 18, GZLINE + 3);
	cairo_stroke (cr);

	cairo_set_line_width (cr, .5);
	cairo_arc (cr, GED_CX + 1, GED_CY + 3, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);

	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(1/6.f, .5, 3.5) }
	{ DIALDOTS(2/6.f, .5, 3.5) }
	{ DIALDOTS(3/6.f, .5, 3.5) }
	{ DIALDOTS(4/6.f, .5, 3.5) }
	{ DIALDOTS(5/6.f, .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);

	/* high shelf */
	INIT_DIAL_SF(ui->dial_bg[3], GED_WIDTH, GED_HEIGHT + 4);
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.0);
	cairo_move_to (cr, 18, GZLINE - 3);
	cairo_line_to (cr, 15, GZLINE - 3);
	cairo_line_to (cr,  5, GZLINE);
	cairo_line_to (cr,  1, GZLINE);
	cairo_move_to (cr,  5, GZLINE);
	cairo_line_to (cr, 15, GZLINE + 3);
	cairo_line_to (cr, 18, GZLINE + 3);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - 18, GZLINE);
	cairo_line_to (cr, GED_WIDTH - 12, GZLINE);
	cairo_line_to (cr, GED_WIDTH -  8, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH -  1, GZLINE - 3);
	cairo_move_to (cr, GED_WIDTH - 12, GZLINE);
	cairo_line_to (cr, GED_WIDTH -  9, GZLINE + 3);
	cairo_line_to (cr, GED_WIDTH -  1, GZLINE + 3);
	cairo_stroke (cr);

	cairo_set_line_width (cr, .5);
	cairo_arc (cr, GED_CX + 1, GED_CY + 3, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);

	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(1/6.f, .5, 3.5) }
	{ DIALDOTS(2/6.f, .5, 3.5) }
	{ DIALDOTS(3/6.f, .5, 3.5) }
	{ DIALDOTS(4/6.f, .5, 3.5) }
	{ DIALDOTS(5/6.f, .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);


	/* frequency knob faceplate */
	for (int i = 0; i < NSECT; ++i) {
		INIT_DIAL_SF(ui->dial_fq[i], GED_WIDTH + 12, GED_HEIGHT + 20);
		char tfq[8];

		print_hz(tfq, dial_to_freq(&freqs[i], 0));
		RESPLABLEL(0.00); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 1, c_dlf);

		print_hz(tfq, dial_to_freq(&freqs[i], .25));
		RESPLABLEL(0.25); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 1, c_dlf);

		print_hz(tfq, dial_to_freq(&freqs[i], .50));
		RESPLABLEL(0.50); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 2, c_dlf);

		print_hz(tfq, dial_to_freq(&freqs[i], .75));
		RESPLABLEL(0.75); write_text_full(cr, tfq, ui->font[0], xlp-2, ylp, 0, 3, c_dlf);

		print_hz(tfq, dial_to_freq(&freqs[i], 1.0));
		RESPLABLEL(1.00); write_text_full(cr, tfq, ui->font[0], xlp-2, ylp, 0, 3, c_dlf);

		cairo_destroy (cr);
	}

	/* hi/lo pass faceplates */
	for (int i = 0; i < 2; ++i) {
		INIT_DIAL_SF(ui->dial_hplp[i], GED_WIDTH + 12, GED_HEIGHT + 20);
		char tfq[8];

		print_hz(tfq, dial_to_freq(&lphp[i], 0));
		RESPLABLEL(0.00); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 1, c_dlf);

		print_hz(tfq, dial_to_freq(&lphp[i], .25));
		RESPLABLEL(0.25); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 1, c_dlf);

		print_hz(tfq, dial_to_freq(&lphp[i], .50));
		RESPLABLEL(0.50); write_text_full(cr, tfq, ui->font[0], xlp, ylp, 0, 2, c_dlf);

		print_hz(tfq, dial_to_freq(&lphp[i], .75));
		RESPLABLEL(0.75); write_text_full(cr, tfq, ui->font[0], xlp-2, ylp, 0, 3, c_dlf);

		print_hz(tfq, dial_to_freq(&lphp[i], 1.0));
		RESPLABLEL(1.00); write_text_full(cr, tfq, ui->font[0], xlp-2, ylp, 0, 3, c_dlf);

		cairo_destroy (cr);
	}
}

static void update_filter_display (Fil4UI* ui) {
	ui->filter_redisplay = true;
	queue_draw(ui->m0);
}

///////////////////////////////////////////////////////////////////////////////

static double warp_freq (double w, double f)
{
	f *= 2 * M_PI;
	return fabs (atan2 ((1 - w * w) * sin (f), (1 + w * w) * cos (f) - 2 * w) / (2 * M_PI));
}

static void reinitialize_fft(Fil4UI* ui) {
	// History
	fftx_free(ui->fa);
	ui->fa = (struct FFTAnalysis*) malloc(sizeof(struct FFTAnalysis));
	fftx_init (ui->fa, 8192, ui->samplerate, 25);

	ui->scale_cached = false;

	// JAPA
	ui->_ipstep = (ui->samplerate > 64e3f) ? 0x2000 : 0x1000;
	ui->_ipsize = 2 * ui->_ipstep;
	ui->japa = new Analyser (ui->_ipsize, FFT_MAX, ui->samplerate);
	ui->japa->set_fftlen (512);
	ui->japa->set_wfact (0.95); // high:).95 med:.9
	ui->japa->set_speed (0.1); // was .2 [med]

	for (int i = 0; i <= FFT_MAX; ++i) {
		const double f = 0.5 * i / FFT_MAX;
		ui->_fscale [i] = warp_freq (-.95, f);
	}

	for (int i = 1; i < FFT_MAX; ++i) {
		ui->_bwcorr [i] = 1.f / (30.0f * (ui->_fscale [i + 1] - ui->_fscale [i - 1]) / ui->_fscale [i]);
	}
	ui->_bwcorr [0]       = ui->_bwcorr [1];
	ui->_bwcorr [FFT_MAX] = ui->_bwcorr [FFT_MAX - 1];
}

static void hsl2rgb(float c[3], const float hue, const float sat, const float lum) {
	const float cq = lum < 0.5 ? lum * (1 + sat) : lum + sat - lum * sat;
	const float cp = 2.f * lum - cq;
	c[0] = rtk_hue2rgb(cp, cq, hue + 1.f/3.f);
	c[1] = rtk_hue2rgb(cp, cq, hue);
	c[2] = rtk_hue2rgb(cp, cq, hue - 1.f/3.f);
}


static void update_fft_scale (Fil4UI* ui) {
	assert(ui->fft_scale);
	const float mode = robtk_select_get_value(ui->sel_fft);
	const int align = - robtk_dial_get_value (ui->spn_fftgain);

	cairo_t *cr = cairo_create (ui->fft_scale);
	cairo_rectangle (cr, 0, ui->m0_y0, 12,  ui->m0_y1 -  ui->m0_y0);
	CairoSetSouerceRGBA(c_blk);
	cairo_fill (cr);

#define FFT_DB(db, len) { \
	const float yy = rintf(ui->m0_ym + .5 - ui->m0_yr * db) - .5; \
	cairo_move_to (cr, 0,   yy); \
	cairo_line_to (cr, len, yy); \
	cairo_stroke(cr); \
}

	const float *txt_c;
	int txt_x;

	if (mode < 5) {
		cairo_set_line_width(cr, 1.0);
		CairoSetSouerceRGBA(c_g30);
		FFT_DB(-30, 5.5);
		FFT_DB(-20, 4.5);
		FFT_DB(-10, 4.5);
		FFT_DB(0, 3.5);
		FFT_DB(10, 4.5);
		FFT_DB(20, 4.5);
		FFT_DB(30, 5.5);
		txt_c = &c_ann[0];
		txt_x = 3;
	} else {
		cairo_set_line_width(cr, 1.0);
		const int yh = ui->m0_y1 -  ui->m0_y0;

		for (int i=0; i < yh; ++i) {
			float clr[3];
			const float pk = 1.f - (i / (float)yh);
			hsl2rgb(clr, .70 - .72 * pk, .9, .3 + pk * .4);
			cairo_set_source_rgba(cr, clr[0], clr[1], clr[2], 1.0);
			cairo_move_to(cr, 2, ui->m0_y0 + i + .5);
			cairo_line_to(cr, 11, ui->m0_y0 + i + .5);
			cairo_stroke(cr);
		}
		txt_c = &c_blk[0];
		txt_x = 2;
	}

	char tmp[16];
	sprintf(tmp, "%+3d", align);
	write_text_full(cr, tmp, ui->font[0], txt_x, ui->m0_y0 + 2, 1.5 * M_PI, 7, txt_c);

	sprintf(tmp, "%+3d dBFS", align - 30);
	write_text_full(cr, tmp, ui->font[0], txt_x, ui->m0_y0 + 30 * ui->m0_yr, 1.5 * M_PI, 8, txt_c);

	sprintf(tmp, "%+3d", align - 60);
	write_text_full(cr, tmp, ui->font[0], txt_x, ui->m0_y1 + 1, 1.5 * M_PI, 9, txt_c);
	cairo_destroy (cr);
}

static void update_spectrum_history (Fil4UI* ui, const size_t n_elem, float const * data) {
	if (!ui->fft_history) {
		return;
	}
	const float mode = robtk_select_get_value(ui->sel_fft);
	if (mode < 5) {
		if (ui->fft_hist_line >= 0) {
			ui->fft_hist_line = -1;
			cairo_t *cr = cairo_create (ui->fft_history);
			cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
			cairo_paint (cr);
			cairo_destroy (cr);
		}
		return;
	}
	if (!fftx_run(ui->fa, n_elem, data)) {
		cairo_t *cr = cairo_create (ui->fft_history);
		cairo_set_line_width (cr, 1.0);

		// increase line
		const int m0_h = ui->m0_y1 - ui->m0_y0;
		ui->fft_hist_line = (ui->fft_hist_line + 1) % m0_h;

		const uint32_t b = fftx_bins(ui->fa);
		const float yy = ui->fft_hist_line;

		// clear current line
		cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
		cairo_rectangle (cr, 0, yy, ui->m0_xw, 1);
		cairo_fill (cr);

		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		float gain = 6.f + robtk_dial_get_value (ui->spn_fftgain);
		for (uint32_t i = 1; i < b-1; ++i) {
			const float freq = fftx_freq_at_bin (ui->fa, i);
			const float f0 = x_at_freq (MAX (5, freq - 2 * ui->fa->freq_per_bin), ui->m0_xw);
			const float f1 = x_at_freq (        freq + 2 * ui->fa->freq_per_bin,  ui->m0_xw);

			const float level = gain + fftx_power_to_dB (ui->fa->power[i]);
			if (level < -60) continue;
			const float pk = level > 0.0 ? 1.0 : (60 + level) / 60.0; // TODO Range
			float clr[3];
			hsl2rgb(clr, .70 - .72 * pk, .9, .3 + pk * .4);
			cairo_set_source_rgba(cr, clr[0], clr[1], clr[2], .3 + pk * .2);

			cairo_move_to (cr, f0, yy);
			cairo_line_to (cr, f1, yy);
			cairo_stroke (cr);

		}
		cairo_destroy (cr);
		queue_draw(ui->m0);
	}
}

static void update_spectrum_japa (Fil4UI* ui, const size_t n_elem, float const * data) {
	float *d = ui->japa->ipdata();
	const int step =  ui->_ipstep;
	int remain = n_elem;

	const float mode = robtk_select_get_value(ui->sel_fft);
	if (mode < 1 || mode > 4) {
		// TODO clear 1st time
		return;
	}

	while (remain > 0) {
		int sc = MIN(step, MIN (ui->_ipsize - ui->_bufpos, remain));
		memcpy(d + ui->_bufpos, data, sc * sizeof(float));

		ui->_stepcnt += sc;
		ui->_bufpos  = (ui->_bufpos + sc) % ui->_ipsize;
		remain -= sc;

		if (ui->_stepcnt >= step) {
			ui->japa->process (step, false);
			ui->_stepcnt -= step;
			ui->_fpscnt += step;
		}
	}

	if (ui->_fpscnt > ui->samplerate / 25) {
		ui->_fpscnt -= (ui->samplerate / 25);
		queue_draw(ui->m0);
	}
}

static inline float y_power_flat (Fil4UI* ui, float v, const float gain) {
	return gain + 10.f * log10f ((v + 1e-30));
}

static inline float y_power_prop (Fil4UI* ui, float v, const float gain, const float corr) {
	return gain + 10.f * log10f (corr * (v + 1e-30));
}

///////////////////////////////////////////////////////////////////////////////

/*** calculate filter parameter constants for plotting ***/
static void update_filter (FilterSection *flt, const float freq, const float bw, const float gain) {
	// see src/lv2.c  run()
	float freq_ratio = freq / flt->rate;
	if (freq_ratio < 0.0002) freq_ratio = 0.0002;
	if (freq_ratio > 0.4998) freq_ratio = 0.4998;
	const float g = powf (10.f, 0.05 * gain); // XXX exp2ap()

	// see src/filters.h  proc()
	const float b = 7.f * bw * freq_ratio / sqrtf (g);
	flt->s2 = (1.f - b) / (1.f + b);
	flt->s1 = -cos (2 * M_PI * freq_ratio);
	flt->s1 *= (1.f + flt->s2);

	flt->gain_db = .5f * (g - 1.f) * (1.f - flt->s2);
}

static void update_iir (FilterSection *flt, const int hs, const float freq, const float bw, const float gain) {
	float freq_ratio = freq / flt->rate;
	float q = .337 + bw / 7.425; // map [.125 .. 8] to [2^(-3/2) .. 2^(1/2)]
	if (freq_ratio < 0.0002) freq_ratio = 0.0002;
	if (freq_ratio > 0.4998) freq_ratio = 0.4998;
	if (q < .25f) { q = .25f; }
	if (q > 1.f)  { q = 1.f; }

	// TODO check if double precision is needed here & simplify maths
	// compare to src/iir.h
	const float w0 = 2. * M_PI * (freq_ratio);
	const float _cosW = cosf (w0);

	const float A  = powf (10., .025 * gain); // sqrt(gain_as_coeff)
	const float As = sqrtf (A);
	const float a  = sinf (w0) / 2 * (1 / q);

	if (hs) { // high shelf
		const float b0 =  A *      ((A + 1) + (A - 1) * _cosW + 2 * As * a);
		const float b1 = -2 * A  * ((A - 1) + (A + 1) * _cosW);
		const float b2 =  A *      ((A + 1) + (A - 1) * _cosW - 2 * As * a);
		const float a0 = (A + 1) -  (A - 1) * _cosW + 2 * As * a;
		const float a1 =  2 *      ((A - 1) - (A + 1) * _cosW);
		const float a2 = (A + 1) -  (A - 1) * _cosW - 2 * As * a;

		const float _b0 = b0 / a0;
		const float _b2 = b2 / a0;
		const float _a2 = a2 / a0;

		flt->A  = _b0 + _b2;
		flt->B  = _b0 - _b2;
		flt->C  = 1.0 + _a2;
		flt->D  = 1.0 - _a2;
		flt->A1 = a1 / a0;
		flt->B1 = b1 / a0;
	} else { // low shelf
		const float b0 =  A *      ((A + 1) - (A - 1) * _cosW + 2 * As * a);
		const float b1 =  2 * A  * ((A - 1) - (A + 1) * _cosW);
		const float b2 =  A *      ((A + 1) - (A - 1) * _cosW - 2 * As * a);
		const float a0 = (A + 1) +  (A - 1) * _cosW + 2 * As * a;
		const float a1 = -2 *      ((A - 1) + (A + 1) * _cosW);
		const float a2 = (A + 1) +  (A - 1) * _cosW - 2 * As * a;

		const float _b0 = b0 / a0;
		const float _b2 = b2 / a0;
		const float _a2 = a2 / a0;

		flt->A  = _b0 + _b2;
		flt->B  = _b0 - _b2;
		flt->C  = 1.0 + _a2;
		flt->D  = 1.0 - _a2;
		flt->A1 = a1 / a0;
		flt->B1 = b1 / a0;
	}
}

static void update_filters (Fil4UI *ui) {
	for (uint32_t i = 1; i < NSECT -1; ++i) {
		update_filter (&ui->flt[i],
				dial_to_freq(&freqs[i], robtk_dial_get_value (ui->spn_freq[i])),
				dial_to_bw (robtk_dial_get_value (ui->spn_bw[i])),
				robtk_dial_get_value (ui->spn_gain[i])
				);
	}
	update_iir (&ui->flt[0], 0,
			dial_to_freq(&freqs[0], robtk_dial_get_value (ui->spn_freq[0])),
			dial_to_bw (robtk_dial_get_value (ui->spn_bw[0])),
			robtk_dial_get_value (ui->spn_gain[0])
			);
	update_iir (&ui->flt[NSECT-1], 1,
			dial_to_freq(&freqs[NSECT-1], robtk_dial_get_value (ui->spn_freq[NSECT-1])),
			dial_to_bw (robtk_dial_get_value (ui->spn_bw[NSECT-1])),
			robtk_dial_get_value (ui->spn_gain[NSECT-1])
			);
	update_filter_display (ui);
}

/* drawing helpers, calculate respone for given frequency */
static float get_filter_response (FilterSection *flt, const float freq) {
	const float w = 2.f * M_PI * freq / flt->rate;
	const float c1 = cosf (w);
	const float s1 = sinf (w);
	const float c2 = cosf (2.f * w);
	const float s2 = sinf (2.f * w);

	float x = c2 + flt->s1 * c1 + flt->s2;
	float y = s2 + flt->s1 * s1;

	const float t1 = HYPOTF (x, y);

	x += flt->gain_db * (c2 - 1.f);
	y += flt->gain_db * s2;

	const float t2 = HYPOTF (x, y);

	return 20.f * log10f (t2 / t1);
}

/* ditto for IIR */
static float get_shelf_response (FilterSection *flt, const float freq) {
	const float w = 2.f * M_PI * freq / flt->rate;
	const float c1 = cosf(w);
	const float s1 = sinf(w);
	const float A = flt->A * c1 + flt->B1;
	const float B = flt->B * s1;
	const float C = flt->C * c1 + flt->A1;
	const float D = flt->D * s1;
	return 20.f * log10f (sqrtf ((SQUARE(A) + SQUARE(B)) * (SQUARE(C) + SQUARE(D))) / (SQUARE(C) + SQUARE(D)));
}

static float get_highpass_response (Fil4UI *ui, const float freq) {
	const float w = freq / ui->hilo[0];
	const float v = (w / sqrtf (1 + w * w));
	return 40.f * log10f (v); // 20 * log(v^2);
}

static float get_lowpass_response (Fil4UI *ui, const float freq) {
	const float w0 = freq / ui->hilo[1];
	const float w1 = 2 * freq / ui->samplerate;
	return -20.f * log10f ((1 + SQUARE(w0)) / (1 + SQUARE(w1)));
}

///////////////////////////////////////////////////////////////////////////////


/*** knob & button callbacks ****/

// TODO separate handle and data, update single value only
// (no big deal, LV2 hosts ignore values if not changed, DSP backend does not care either)

static bool cb_btn_en (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	if (ui->disable_signals) return TRUE;
	for (uint32_t i = 0; i < NSECT; ++i) {
		float val = robtk_cbtn_get_active(ui->btn_enable[i]) ? 1.f : 0.f;
		ui->write(ui->controller, FIL_SEC1 + i * 4, sizeof(float), 0, (const void*) &val);
	}
	update_filter_display (ui);
	return TRUE;
}

static bool cb_spn_freq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	for (uint32_t i = 0; i < NSECT; ++i) {
		const float val = dial_to_freq(&freqs[i], robtk_dial_get_value (ui->spn_freq[i]));
		dial_annotation_hz (ui->btn_enable[i], i, val);
		if (ui->disable_signals) continue;
		ui->write(ui->controller, FIL_FREQ1 + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static bool cb_spn_bw (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	if (ui->disable_signals) return TRUE;
	for (uint32_t i = 0; i < NSECT; ++i) {
		const float val = dial_to_bw (robtk_dial_get_value (ui->spn_bw[i]));
		ui->write(ui->controller, FIL_Q1 + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static bool cb_spn_gain (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	for (uint32_t i = 0; i < NSECT; ++i) {
		const float val = robtk_dial_get_value (ui->spn_gain[i]);
		if (ui->disable_signals) continue;
		ui->write(ui->controller, FIL_GAIN1 + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static bool cb_spn_g_hifreq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float val = dial_to_freq(&lphp[0], robtk_dial_get_value (ui->spn_g_hifreq));
	ui->hilo[0] = val;
	// printf("HPF %f\n", val); // XXX
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_HIFREQ, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_spn_g_lofreq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	//update_filters(ui);
	const float val = dial_to_freq(&lphp[1], robtk_dial_get_value (ui->spn_g_lofreq));
	ui->hilo[1] = val;
	//printf("LPF %f\n", val); // XXX
	// TODO display freq
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_LOFREQ, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_btn_g_en (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	if (ui->disable_signals) return TRUE;
	const float val = robtk_cbtn_get_active(ui->btn_g_enable) ? 1.f : 0.f;
	ui->write(ui->controller, FIL_ENABLE, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_btn_g_hi (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	if (ui->disable_signals) return TRUE;
	const float val = robtk_ibtn_get_active(ui->btn_g_hipass) ? 1.f : 0.f;
	ui->write(ui->controller, FIL_HIPASS, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_btn_g_lo (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	if (ui->disable_signals) return TRUE;
	const float val = robtk_ibtn_get_active(ui->btn_g_lopass) ? 1.f : 0.f;
	ui->write(ui->controller, FIL_LOPASS, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_spn_g_gain (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float val = robtk_dial_get_value (ui->spn_g_gain);
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_GAIN, sizeof(float), 0, (const void*) &val);
	update_filter_display (ui);
	return TRUE;
}

static bool cb_set_fft (RobWidget* w, void *handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	if (ui->disable_signals) return TRUE;
	const float val = robtk_select_get_value(ui->sel_fft);

	robtk_dial_set_sensitive (ui->spn_fftgain, val > 0);

	uint8_t obj_buf[64];
	lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(&ui->forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &frame, 1, ui->uris.fftmode);
	lv2_atom_forge_property_head(&ui->forge, ui->uris.fftmode, 0);
	lv2_atom_forge_int(&ui->forge, val);
	lv2_atom_forge_pop(&ui->forge, &frame);
	ui->write(ui->controller, FIL_ATOM_CONTROL, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
	update_filter_display (ui);
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

/* cache grid as image surface */
static void draw_grid (Fil4UI* ui) {
	assert(!ui->m0_grid);
	ui->m0_grid = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, ui->m0_width, ui->m0_height);
	cairo_t* cr = cairo_create (ui->m0_grid);

	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	const float x0 = 30;
	const float x1 = x0 + ui->m0_xw;

#define GRID_FREQ(fq, hz) { \
	const float xx = x0 + x_at_freq(fq, ui->m0_xw) - .5f; \
	cairo_move_to (cr, xx, ui->m0_y0); \
	cairo_line_to (cr, xx, ui->m0_y1 + 5); \
	cairo_stroke(cr); \
	write_text_full(cr, hz, ui->font[0], xx, ui->m0_y1 + 5, 0, 8, c_ann); \
}

#define GRID_LINE(fq) { \
	const float xx = x_at_freq(fq, ui->m0_xw) - .5f; \
	cairo_move_to (cr, x0 + xx, ui->m0_y1); \
	cairo_line_to (cr, x0 + xx, ui->m0_y1 + 4); \
	cairo_stroke(cr); \
}

#define GRID_DB_DOT(db) { \
	double dash = 4; \
	const float yy = rintf(ui->m0_ym + .5 - ui->m0_yr * db) - .5; \
	cairo_save (cr); \
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT); \
	cairo_set_dash (cr, &dash, 1, 0); \
	cairo_move_to (cr, x0, yy); \
	cairo_line_to (cr, x1, yy); \
	cairo_stroke(cr); \
	cairo_restore (cr); \
}

#define GRID_DB(db, tx) { \
	const float yy = rintf(ui->m0_ym + .5 - ui->m0_yr * db) - .5; \
	cairo_move_to (cr, x0, yy); \
	cairo_line_to (cr, x1, yy); \
	cairo_stroke(cr); \
	write_text_full(cr, tx, ui->font[0], x0-5, yy, 0, 1, c_ann); \
}

	write_text_full(cr, "dB", ui->font[0], x0 - 22, ui->m0_ym, M_PI * -.5, 8, c_ann);

	if (ui->m0_height > 300 ) {
		cairo_set_line_width(cr, .75);
		CairoSetSouerceRGBA(c_g20);
		GRID_DB_DOT(24);
		GRID_DB_DOT(21);
		GRID_DB_DOT(15);
		GRID_DB_DOT(12);
		GRID_DB_DOT(6);
		GRID_DB_DOT(3);
		GRID_DB_DOT(-24);
		GRID_DB_DOT(-21);
		GRID_DB_DOT(-15);
		GRID_DB_DOT(-12);
		GRID_DB_DOT(-6);
		GRID_DB_DOT(-3);
	}

	cairo_set_line_width(cr, 1.0);
	CairoSetSouerceRGBA(c_g30);

	GRID_DB_DOT(27);
	GRID_DB_DOT(-27);

	GRID_DB(30, "+30");
	GRID_DB(18, "+18");
	GRID_DB(9, "+9");
	GRID_DB(0, "0");
	GRID_DB(-9, "-9");
	GRID_DB(-18, "-18");
	GRID_DB(-30, "-30");

	CairoSetSouerceRGBA(c_g30);

	GRID_FREQ(20, "20");
	GRID_LINE(25);
	GRID_LINE(31.5);
	GRID_FREQ(40, "40");
	GRID_LINE(50);
	GRID_LINE(63);
	GRID_FREQ(80, "80");
	GRID_LINE(100);
	GRID_LINE(125);
	GRID_FREQ(160, "160");
	GRID_LINE(200);
	GRID_LINE(250);
	GRID_FREQ(315, "315");
	GRID_LINE(400);
	GRID_LINE(500);
	GRID_FREQ(630, "630");
	GRID_LINE(800);
	GRID_LINE(1000);
	GRID_FREQ(1250, "1K25");
	GRID_LINE(1600);
	GRID_LINE(2000);
	GRID_FREQ(2500, "2K5");
	GRID_LINE(3150);
	GRID_LINE(4000);
	GRID_FREQ(5000, "5K");
	GRID_LINE(6300);
	GRID_LINE(8000);
	GRID_FREQ(10000, "10K");
	GRID_LINE(12500);
	GRID_LINE(16000);
	GRID_FREQ(20000, "20K");

	write_text_full (cr,
			ui->nfo ? ui->nfo : "x42 fil4.LV2",
			ui->font[0], x1 + 2, ui->m0_y0, 1.5 * M_PI, 7, c_g30);

	cairo_destroy (cr);
}

/* callbacks and iteraction related to graph */

static void
m0_size_request (RobWidget* handle, int *w, int *h) {
	*w = 600;
	*h = 200;
}

static void
m0_size_allocate (RobWidget* handle, int w, int h) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	ui->m0_width = w;
	ui->m0_height = h;
	robwidget_set_size(ui->m0, w, h);

	if (ui->m0_grid) {
		cairo_surface_destroy (ui->m0_grid);
		ui->m0_grid = NULL;
	}

	if (ui->m0_filters) {
		cairo_surface_destroy (ui->m0_filters);
		ui->m0_filters = NULL;
	}

	ui->scale_cached = false;

	// old size
	const int m0_w = ui->m0_xw;
	const int m0_h = ui->m0_y1 - ui->m0_y0; // old height

	const int m0h = h & ~1;
	ui->m0_xw = ui->m0_width - 48;
	ui->m0_ym = rintf((m0h - 12) * .5f) - .5;
	ui->m0_yr = (m0h - 20) / 64.f;
	ui->m0_y0 = floor (ui->m0_ym - 30.f * ui->m0_yr);
	ui->m0_y1 = ceil  (ui->m0_ym + 30.f * ui->m0_yr);

	const int m0_H = ui->m0_y1 - ui->m0_y0; // new height

	if (m0_w != ui->m0_xw) {
		free (ui->ffy);
		ui->ffy = (float*) calloc(ui->m0_xw, sizeof(float));
	}

	if (m0_w != ui->m0_xw || m0_h != m0_H) {
		ui->fft_hist_line = -1;
		if (ui->fft_history) {
			cairo_surface_destroy (ui->fft_history);
		}
		if (ui->fft_scale) {
			cairo_surface_destroy (ui->fft_scale);
		}
		ui->fft_history = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, ui->m0_xw, m0_H);

		cairo_t *cr = cairo_create (ui->fft_history);
		cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint (cr);
		cairo_destroy (cr);

		ui->fft_scale = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 12, ui->m0_height);
	}
}

static RobWidget* m0_mouse_up (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	ui->dragging = -1;
	update_filter_display (ui);
	return NULL;
}

static RobWidget* m0_mouse_scroll (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	int sect = -1;

	for (int i = 0; i < NSECT; ++i) {
		if (abs(ev->x - ui->flt[i].x0) <= DOTRADIUS && abs(ev->y - ui->flt[i].y0) <= DOTRADIUS) {
			sect = i;
			break;
		}
	}

	if (sect < 0) {
		return NULL;
	}

	float v = robtk_dial_get_value (ui->spn_bw[sect]);
	const float delta = (ev->state & ROBTK_MOD_CTRL) ? ui->spn_bw[sect]->acc : ui->spn_bw[sect]->scroll_mult * ui->spn_bw[sect]->acc;

	switch (ev->direction) {
		case ROBTK_SCROLL_RIGHT:
		case ROBTK_SCROLL_UP:
			v += delta;
			robtk_dial_set_value (ui->spn_bw[sect], v);
			break;
		case ROBTK_SCROLL_LEFT:
		case ROBTK_SCROLL_DOWN:
			v -= delta;
			robtk_dial_set_value (ui->spn_bw[sect], v);
			break;
		default:
			break;
	}
	return NULL;
}

static RobWidget* m0_mouse_down (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	if (ev->button != 1) {
		return NULL;
	}

	assert (ui->dragging == -1);

	for (int i = 0; i < NSECT; ++i) {
		if (fabsf(ev->x - ui->flt[i].x0) <= DOTRADIUS && fabsf(ev->y - ui->flt[i].y0) <= DOTRADIUS) {
			ui->dragging = i;
			update_filter_display (ui);
			break;
		}
	}
	if (ev->state & ROBTK_MOD_SHIFT && ui->dragging >= 0) {
		// XXX dial needs an API for this
		robtk_dial_set_value (ui->spn_freq[ui->dragging], ui->spn_freq[ui->dragging]->dfl);
		robtk_dial_set_value (ui->spn_gain[ui->dragging], ui->spn_gain[ui->dragging]->dfl);
		robtk_dial_set_value (ui->spn_bw[ui->dragging], ui->spn_bw[ui->dragging]->dfl);
		ui->dragging = -1;
		update_filter_display (ui);
		return NULL;
	}

	if (ui->dragging < 0) {
		return NULL;
	} else {
		return handle;
	}
}

static RobWidget* m0_mouse_move (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	if (ui->dragging < 0) return NULL;

	const float x0 = 30;
	const float x1 = x0 + ui->m0_xw;
	const float y0 = ui->m0_y0;
	const float y1 = ui->m0_y1;

	float g_gain = robtk_dial_get_value (ui->spn_g_gain);
	const int sect = ui->dragging;

	if (ev->x >= x0 && ev->x <= x1) {
		const float hz = freq_at_x (ev->x - x0, ui->m0_xw);
		robtk_dial_set_value (ui->spn_freq[sect], freq_to_dial (&freqs[sect], hz));
	}
	if (ev->y >= y0 && ev->y <= y1) {
		const float db = (ui->m0_ym - ev->y) / ui->m0_yr;
		robtk_dial_set_value (ui->spn_gain[sect], db - g_gain);
	}
	return handle;
}

inline float x_power_to_dB (float a) {
	/* 10 instead of 20 because of squared signal -- no sqrt(powerp[]) */
	return a > 1e-10 ? 10.0 * fast_log10(a) : -60;
}

static void draw_filters (Fil4UI* ui) {
	if (!ui->m0_filters) {
		ui->m0_filters = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, ui->m0_xw, ui->m0_height);
	}

	cairo_t* cr = cairo_create (ui->m0_filters);
	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);

	float shade = 1.0;
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

	float g_gain = robtk_dial_get_value (ui->spn_g_gain);
	if (!robtk_cbtn_get_active(ui->btn_g_enable)) {
		shade = 0.5;
	}
	const float xw = ui->m0_xw;
	const float ym = ui->m0_ym;
	const float yr = ui->m0_yr;
	const float x0 = 30;

	/* draw dots */
	cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	cairo_set_line_width(cr, 1.0);
	for (int j = 0 ; j < NSECT; ++j) {
		float fshade = shade;
		if (!robtk_cbtn_get_active(ui->btn_enable[j])) {
			fshade = .5;
		}
		const float fq = dial_to_freq(&freqs[j], robtk_dial_get_value (ui->spn_freq[j]));
		const float db = robtk_dial_get_value (ui->spn_gain[j]);

		const float xx = x_at_freq(fq, xw) - .5f;
		const float yy = rintf(ym + .5 - yr * (db + g_gain)) - .5;
		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], .6 * fshade);
		cairo_arc (cr, xx, yy, DOTRADIUS, 0, 2 * M_PI);
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], .3 * fshade);
		cairo_stroke (cr);
		/* cache position (for drag) */
		ui->flt[j].x0 = x0 + xx;
		ui->flt[j].y0 = yy;
	}

	/* draw filters */
	cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	cairo_set_line_width(cr, 1.0);
	for (int j = 0 ; j < NSECT; ++j) {
		float fshade = shade;
		if (!robtk_cbtn_get_active(ui->btn_enable[j])) {
			fshade = .5;
		}

		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], fshade);

		for (int i = 0 ; i < xw; ++i) {
			const float xf = freq_at_x(i, xw);
			float y = yr;
			if (j == 0) {
				y *= get_shelf_response (&ui->flt[j], xf);
			} else if (j == NSECT -1) {
				y *= get_shelf_response (&ui->flt[j], xf);
			} else {
				y *= get_filter_response (&ui->flt[j], xf);
			}
			y += yr * g_gain;
			if (i == 0) {
				cairo_move_to (cr, i, ym - y);
			} else {
				cairo_line_to (cr, i, ym - y);
			}
		}
		if (ui->dragging == j) {
			cairo_stroke_preserve(cr);
			cairo_line_to (cr, xw, ym - yr * g_gain);
			cairo_line_to (cr, 0, ym - yr * g_gain);
			cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], 0.4 * fshade);
			cairo_fill (cr);
		} else {
			cairo_stroke(cr);
		}
	}

	/* zero line - mask added colors */
	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_line_width(cr, 1.0);
	CairoSetSouerceRGBA(c_g60);
	cairo_move_to (cr, 0, ym - yr * g_gain);
	cairo_line_to (cr, xw -1 , ym - yr * g_gain);
	cairo_stroke(cr);

	/* draw total */
	cairo_set_line_width(cr, 2.0 * shade);
	cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, shade);
	for (int i = 0 ; i < xw; ++i) {
		const float xf = freq_at_x(i, xw);
		float y = yr * g_gain;
		for (int j = 0 ; j < NSECT; ++j) {
			if (!robtk_cbtn_get_active(ui->btn_enable[j])) continue;
			if (j == 0) {
				y += yr * get_shelf_response (&ui->flt[j], xf);
			} else if (j == NSECT -1) {
				y += yr * get_shelf_response (&ui->flt[j], xf);
			} else {
				y += yr * get_filter_response (&ui->flt[j], xf);
			}
		}
		if (robtk_ibtn_get_active(ui->btn_g_hipass)) {
			y += yr * get_highpass_response (ui, xf);
		}
		if (robtk_ibtn_get_active(ui->btn_g_lopass)) {
			y += yr * get_lowpass_response (ui, xf);
		}
		if (i == 0) {
			cairo_move_to (cr, i, ym - y);
		} else {
			cairo_line_to (cr, i, ym - y);
		}
	}
	cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	cairo_stroke_preserve(cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	cairo_line_to (cr, xw, ym - yr * g_gain);
	cairo_line_to (cr, 0, ym - yr * g_gain);
	cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.33 * shade);
	cairo_fill (cr);

	cairo_destroy (cr);
}

/*** main drawing function ***/
static bool m0_expose_event (RobWidget* handle, cairo_t* cr, cairo_rectangle_t *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip_preserve (cr);
	CairoSetSouerceRGBA(c_trs);
	cairo_fill (cr);

	rounded_rectangle (cr, 4, 4, ui->m0_width - 8 , ui->m0_height - 8, 8);
	CairoSetSouerceRGBA(c_blk);
	cairo_fill (cr);

	const float xw = ui->m0_xw;
	const float ym = ui->m0_ym;
	const float yr = ui->m0_yr;
	const float x0 = 30;

	if (!ui->m0_grid) {
		draw_grid (ui);
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_surface(cr, ui->m0_grid, 0, 0);
	cairo_paint (cr);

	const int fft_mode = robtk_select_get_value(ui->sel_fft);

	if (fft_mode > 0) {
		update_fft_scale (ui);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_surface(cr, ui->fft_scale, x0 + xw, 0);
		cairo_paint (cr);
	}

	cairo_rectangle (cr, x0, ui->m0_y0, xw, ui->m0_y1 - ui->m0_y0);
	cairo_clip (cr);


	if (fft_mode >= 5 &&  ui->fft_hist_line >= 0) {
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		const int y0 = ui->m0_y0;
		const int yh = ui->m0_y1 - ui->m0_y0;
		if (ui->fft_hist_line == yh - 1) {
			cairo_set_source_surface(cr, ui->fft_history, x0, ui->m0_y0);
			cairo_rectangle (cr, x0, y0, xw, ui->m0_y1 - ui->m0_y0);
			cairo_fill (cr);
		} else {
			int yp = yh - ui->fft_hist_line - 1;

			cairo_set_source_surface(cr, ui->fft_history, x0, y0 + yp);
			cairo_rectangle (cr, x0, y0 + yp, xw, ui->fft_hist_line);
			cairo_fill (cr);

			cairo_set_source_surface(cr, ui->fft_history, x0, y0 - ui->fft_hist_line - 1);
			cairo_rectangle (cr, x0, y0, xw, yp);
			cairo_fill (cr);
		}
	}
	else if (fft_mode > 0 && fft_mode < 5) {
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgba (cr, .5, .6, .7, .75);
		float *d = ui->japa->power ()->_data;
		if (!ui->scale_cached) {
			ui->scale_cached = true;
			for (int i = 0; i <= FFT_MAX; ++i) {
				ui->xscale[i] = x0 + x_at_freq(ui->_fscale[i] * ui->samplerate, xw) - .5;
			}
		}
		const float align = 30 + robtk_dial_get_value (ui->spn_fftgain);
		if (fft_mode > 2) {
			cairo_move_to (cr, ui->xscale[0], ym - yr * y_power_prop(ui, d[0], align, ui->_bwcorr[0]));
			for (int i = 1; i <= FFT_MAX; ++i) {
				cairo_line_to (cr, ui->xscale[i], ym - yr * y_power_prop(ui, d[i], align, ui->_bwcorr[i]));
			}
		} else {
			cairo_move_to (cr, ui->xscale[0], ym - yr * y_power_flat(ui, d[0], align));
			for (int i = 1; i <= FFT_MAX; ++i) {
				cairo_line_to (cr, ui->xscale[i], ym - yr * y_power_flat(ui, d[i], align));
			}
		}
		cairo_stroke (cr);
	}

	if (ui->filter_redisplay || ! ui->m0_filters) {
		draw_filters(ui);
		ui->filter_redisplay = false;
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	cairo_set_source_surface(cr, ui->m0_filters, x0, 0);
	cairo_rectangle (cr, x0, 0, xw, ui->m0_height);
	cairo_fill (cr);
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////

static RobWidget * toplevel(Fil4UI* ui, void * const top) {
	/* main widget: layout */
	ui->rw = rob_vbox_new (FALSE, 2);
	robwidget_make_toplevel (ui->rw, top);

	ui->font[0] = pango_font_description_from_string("Mono 9px");
	ui->font[1] = pango_font_description_from_string("Mono 10px");

	prepare_faceplates (ui);

	ui->ctbl = rob_table_new (/*rows*/4, /*cols*/ 2 * NSECT + 4, FALSE);

#define GBT_W(PTR) robtk_cbtn_widget(PTR)
#define GBI_W(PTR) robtk_ibtn_widget(PTR)
#define GSP_W(PTR) robtk_dial_widget(PTR)
#define GLB_W(PTR) robtk_lbl_widget(PTR)

	int col = 0;

	/* Global section */
	ui->btn_g_enable = robtk_cbtn_new ("Enable", GBT_LED_LEFT, false);
	ui->spn_g_gain   = robtk_dial_new_with_size (-18, 18, .2,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	ui->spn_fftgain  = robtk_dial_new(-30, 30, 1.0);
	robtk_dial_set_value (ui->spn_fftgain, 0);
	robtk_dial_set_sensitive (ui->spn_fftgain, false);

	robtk_dial_annotation_callback(ui->spn_g_gain, dial_annotation_db, ui);
	robtk_dial_annotation_callback(ui->spn_fftgain, dial_annotation_db, ui);
	robtk_cbtn_set_callback (ui->btn_g_enable, cb_btn_g_en, ui);
	robtk_dial_set_callback (ui->spn_g_gain,   cb_spn_g_gain, ui);
	robtk_dial_set_surface (ui->spn_g_gain, ui->dial_bg[0]);

	robtk_cbtn_set_color_on(ui->btn_g_enable,  1.0, 1.0, 1.0);
	robtk_cbtn_set_color_off(ui->btn_g_enable, .2, .2, .2);
	robtk_dial_set_default(ui->spn_g_gain, 0.0);
	robtk_dial_set_detent_default (ui->spn_g_gain, true);
	robtk_dial_set_default(ui->spn_fftgain, 0.0);
	robtk_dial_set_detent_default (ui->spn_fftgain, true);

	ui->sel_fft = robtk_select_new();
	robtk_select_add_item (ui->sel_fft, 0, "FFT\nOff");
	robtk_select_add_item (ui->sel_fft, 1, "Flat\nPre");
	robtk_select_add_item (ui->sel_fft, 2, "Flat\nPost");
	robtk_select_add_item (ui->sel_fft, 3, "Prop\nPre");
	robtk_select_add_item (ui->sel_fft, 4, "Prop\nPost");
	robtk_select_add_item (ui->sel_fft, 5, "Hist\nPre");
	robtk_select_add_item (ui->sel_fft, 6, "Hist\nPost");
	robtk_select_set_default_item (ui->sel_fft, 0);
	robtk_select_set_value (ui->sel_fft, 0);
	robtk_select_set_callback(ui->sel_fft, cb_set_fft, ui);

	ui->sep_h0 = robtk_sep_new(TRUE);

	rob_table_attach (ui->ctbl, GBT_W(ui->btn_g_enable), col, col+2, 0, 1, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_gain),   col, col+2, 1, 2, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_h0), col, col+2, 2, 3);
	rob_table_attach (ui->ctbl, robtk_select_widget(ui->sel_fft), col,   col+1, 3, 4, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_fftgain),           col+1, col+2, 3, 4, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* separator */
	col += 2;
	ui->sep_v0 = robtk_sep_new(FALSE);
	rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_v0), col, col+1, 0, 5);

	/* HPF & LPF */
	++col;
	ui->btn_g_hipass = robtk_ibtn_new (ui->hpf_btn[0], ui->hpf_btn[1]);
	ui->btn_g_lopass = robtk_ibtn_new (ui->lpf_btn[0], ui->lpf_btn[1]); // XXX

	robtk_ibtn_set_alignment(ui->btn_g_hipass, .5, 0);
	robtk_ibtn_set_alignment(ui->btn_g_lopass, .5, 0);

	ui->spn_g_hifreq = robtk_dial_new_with_size (0, 1, .00625,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	ui->spn_g_lofreq = robtk_dial_new_with_size (0, 1, .00625,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);

	robtk_ibtn_set_callback (ui->btn_g_hipass, cb_btn_g_hi, ui);
	robtk_ibtn_set_callback (ui->btn_g_lopass, cb_btn_g_lo, ui);
	robtk_dial_set_callback (ui->spn_g_hifreq, cb_spn_g_hifreq, ui);
	robtk_dial_set_callback (ui->spn_g_lofreq, cb_spn_g_lofreq, ui);

	robtk_dial_set_constained (ui->spn_g_hifreq, false);
	robtk_dial_set_constained (ui->spn_g_lofreq, false);
	robtk_dial_set_default(ui->spn_g_hifreq, freq_to_dial (&lphp[0], lphp[0].dflt));
	robtk_dial_set_default(ui->spn_g_lofreq, freq_to_dial (&lphp[1], lphp[1].dflt));
	robtk_dial_set_scroll_mult (ui->spn_g_hifreq, 4.f);
	robtk_dial_set_scroll_mult (ui->spn_g_lofreq, 4.f);

	robtk_dial_set_surface (ui->spn_g_hifreq, ui->dial_hplp[0]);
	robtk_dial_set_surface (ui->spn_g_lofreq, ui->dial_hplp[1]);

	rob_table_attach (ui->ctbl, GBI_W(ui->btn_g_hipass), col, col+1, 0, 2, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_hifreq), col, col+1, 3, 4, 5, 0, RTK_EXANDF, RTK_SHRINK);

	// LPF AT END
	rob_table_attach (ui->ctbl, GBI_W(ui->btn_g_lopass), col + NSECT + 2, col + NSECT + 3, 0, 2, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_lofreq), col + NSECT + 2, col + NSECT + 3, 3, 4, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* Filter bands */
	++col;
	for (int i = 0; i < NSECT; ++i, ++col) {
		ui->btn_enable[i] = robtk_cbtn_new("\u2abc88.8KHz", GBT_LED_LEFT, false);

		ui->spn_freq[i] = robtk_dial_new_with_size (0, 1, .00625,
				GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->spn_gain[i] = robtk_dial_new_with_size (-18, 18, .2, // .2 dB steps
				GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->spn_bw[i]   = robtk_dial_new_with_size (0, 1.0, 1./120, // 8 [octaves] * 3 [clicks/oct] * 5 [fine grained]
				GED_WIDTH, GED_HEIGHT + 4, GED_CX, GED_CY + 3, GED_RADIUS);

		rob_table_attach (ui->ctbl, GBT_W(ui->btn_enable[i]), col, col+1, 0, 1, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_gain[i]),   col, col+1, 1, 2, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_bw[i]),     col, col+1, 2, 3, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_freq[i]),   col, col+1, 3, 4, 0, 0, RTK_EXANDF, RTK_SHRINK);

		robtk_dial_annotation_callback(ui->spn_gain[i], dial_annotation_db, ui);
		robtk_dial_set_constained (ui->spn_freq[i], false);
		robtk_dial_set_default(ui->spn_freq[i], freq_to_dial (&freqs[i], freqs[i].dflt));
		robtk_dial_set_default(ui->spn_gain[i], 0.0);
		robtk_dial_set_default(ui->spn_bw[i], bw_to_dial(0.6));

		robtk_cbtn_set_callback (ui->btn_enable[i], cb_btn_en, ui);
		robtk_dial_set_callback (ui->spn_freq[i],   cb_spn_freq, ui);
		robtk_dial_set_callback (ui->spn_bw[i],     cb_spn_bw, ui);
		robtk_dial_set_callback (ui->spn_gain[i],   cb_spn_gain, ui);

		robtk_dial_set_alignment (ui->spn_freq[i], 0.0, .5);
		robtk_dial_set_alignment (ui->spn_bw[i], 1.0, .5);
		robtk_dial_set_alignment (ui->spn_gain[i], 0.0, .5);

		robtk_cbtn_set_color_on (ui->btn_enable[i],  c_fil[i][0], c_fil[i][1], c_fil[i][2]);
		robtk_cbtn_set_color_off (ui->btn_enable[i], c_fil[i][0] * .3, c_fil[i][1] * .3, c_fil[i][2] * .3);

		robtk_dial_set_surface (ui->spn_gain[i], ui->dial_bg[0]);
		robtk_dial_set_surface (ui->spn_freq[i], ui->dial_fq[i]);
		if (i == 0) {
			robtk_dial_set_surface (ui->spn_bw[i],   ui->dial_bg[2]);
		} else if (i == NSECT -1) {
			robtk_dial_set_surface (ui->spn_bw[i],   ui->dial_bg[3]);
		} else {
			robtk_dial_set_surface (ui->spn_bw[i],   ui->dial_bg[1]);
		}

		robtk_dial_set_detent_default (ui->spn_gain[i], true);
		robtk_dial_set_scroll_mult (ui->spn_freq[i], 4.f); // 24 clicks per octave
		robtk_dial_set_scroll_mult (ui->spn_gain[i], 5.f); // 1dB per click
		robtk_dial_set_scroll_mult (ui->spn_bw[i],   5.f); // 1/3 octave per click
	}

	/* shelf filter range */
	robtk_dial_update_range (ui->spn_bw[0], 0, 1, 1 / 90.f); // 3 clicks for 1:2
	robtk_dial_update_range (ui->spn_bw[NSECT - 1], 0, 1, 1 / 90.f);
	robtk_dial_set_default(ui->spn_bw[0], bw_to_dial(2.80));
	robtk_dial_set_default(ui->spn_bw[NSECT -1], bw_to_dial(2.80));

	/* graph display */
	ui->m0 = robwidget_new (ui);
	robwidget_set_alignment (ui->m0, .5, .5);
	robwidget_set_expose_event (ui->m0, m0_expose_event);
	robwidget_set_size_request (ui->m0, m0_size_request);
	robwidget_set_size_allocate (ui->m0, m0_size_allocate);
	robwidget_set_mousemove (ui->m0, m0_mouse_move);
	robwidget_set_mouseup (ui->m0, m0_mouse_up);
	robwidget_set_mousedown (ui->m0, m0_mouse_down);
	robwidget_set_mousescroll (ui->m0, m0_mouse_scroll);

	/* top-level packing */
	rob_vbox_child_pack(ui->rw, ui->m0, TRUE, TRUE);
	rob_vbox_child_pack(ui->rw, ui->ctbl, FALSE, TRUE);
	return ui->rw;
}

static void gui_cleanup(Fil4UI* ui) {
	for (int i = 0; i < NSECT; ++i) {
		robtk_cbtn_destroy (ui->btn_enable[i]);
		robtk_dial_destroy (ui->spn_bw[i]);
		robtk_dial_destroy (ui->spn_gain[i]);
		robtk_dial_destroy (ui->spn_freq[i]);
		cairo_surface_destroy (ui->dial_fq[i]);
	}

	robtk_cbtn_destroy (ui->btn_g_enable);
	robtk_dial_destroy (ui->spn_g_gain);
	robtk_dial_destroy (ui->spn_fftgain);
	robtk_ibtn_destroy (ui->btn_g_hipass);
	robtk_dial_destroy (ui->spn_g_hifreq);
	robtk_ibtn_destroy (ui->btn_g_lopass);
	robtk_dial_destroy (ui->spn_g_lofreq);
	robtk_select_destroy(ui->sel_fft);

	robtk_sep_destroy (ui->sep_v0);

	pango_font_description_free(ui->font[0]);
	pango_font_description_free(ui->font[1]);

	cairo_surface_destroy (ui->dial_bg[0]);
	cairo_surface_destroy (ui->dial_bg[1]);
	cairo_surface_destroy (ui->dial_bg[2]);
	cairo_surface_destroy (ui->dial_bg[3]);
	cairo_surface_destroy (ui->hpf_btn[0]);
	cairo_surface_destroy (ui->hpf_btn[1]);
	cairo_surface_destroy (ui->lpf_btn[0]);
	cairo_surface_destroy (ui->lpf_btn[1]);

	if (ui->fft_history) {
		cairo_surface_destroy (ui->fft_history);
	}
	if (ui->fft_scale) {
		cairo_surface_destroy (ui->fft_scale);
	}
	fftx_free(ui->fa);
	free(ui->ffy);

	delete ui->japa;

	if (ui->m0_grid) {
		cairo_surface_destroy (ui->m0_grid);
	}
	if (ui->m0_filters) {
		cairo_surface_destroy (ui->m0_filters);
	}

	robwidget_destroy (ui->m0);
	rob_table_destroy (ui->ctbl);
	rob_box_destroy(ui->rw);

}

/******************************************************************************
 * RobTk + LV2
 */

#define LVGL_RESIZEABLE

static void ui_enable(LV2UI_Handle handle) { }

static void ui_disable(LV2UI_Handle handle) {
	Fil4UI* ui = (Fil4UI*)handle;

	uint8_t obj_buf[64];
	lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(&ui->forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &frame, 1, ui->uris.ui_off);
	lv2_atom_forge_pop(&ui->forge, &frame);
	ui->write(ui->controller, FIL_ATOM_CONTROL, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

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

	Fil4UI* ui = (Fil4UI*) calloc(1, sizeof(Fil4UI));

	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID_URI "#map")) {
			ui->map = (LV2_URID_Map*)features[i]->data;
		}
	}

	if (!ui->map) {
		fprintf (stderr, "Fil4.lv2 UI: Host does not support urid:map\n");
		free(ui);
		return NULL;
	}

	ui->nfo = robtk_info(ui_toplevel);
	ui->write      = write_function;
	ui->controller = controller;
	ui->dragging   = -1;
	ui->samplerate = 48000;
	ui->filter_redisplay = true;

	for (int i = 0; i < NSECT; ++i) {
		/* used for analysis only, but should eventually
		 * match actual rate (rails at bounrary) */
		ui->flt[i].rate = 48000;
	}

	ui->hilo[0] = lphp[0].dflt;
	ui->hilo[1] = lphp[1].dflt;

	map_fil4_uris (ui->map, &ui->uris);
	lv2_atom_forge_init(&ui->forge, ui->map);
	reinitialize_fft (ui);

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
	Fil4UI* ui = (Fil4UI*)handle;
	gui_cleanup(ui);
	free(ui);
}

/* receive information from DSP */
static void
port_event(LV2UI_Handle handle,
		uint32_t     port_index,
		uint32_t     buffer_size,
		uint32_t     format,
		const void*  buffer)
{
	Fil4UI* ui = (Fil4UI*)handle;

	if (format == ui->uris.atom_eventTransfer && port_index == FIL_ATOM_NOTIFY) {
		LV2_Atom* atom = (LV2_Atom*)buffer;
		if (atom->type == ui->uris.atom_Blank || atom->type == ui->uris.atom_Object) {
			/* cast the buffer to Atom Object */
			LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;
			LV2_Atom *a0 = NULL;
			LV2_Atom *a1 = NULL;
			if (
					/* handle raw-audio data objects */
					obj->body.otype == ui->uris.rawaudio
					/* retrieve properties from object and
					 * check that there the [here] two required properties are set.. */
					&& 2 == lv2_atom_object_get(obj, ui->uris.samplerate, &a0, ui->uris.audiodata, &a1, NULL)
					/* ..and non-null.. */
					&& a0
					&& a1
					/* ..and match the expected type */
					&& a0->type == ui->uris.atom_Float
					&& a1->type == ui->uris.atom_Vector
				 )
			{
				const float sr = ((LV2_Atom_Float*)a0)->body;
				LV2_Atom_Vector* vof = (LV2_Atom_Vector*)LV2_ATOM_BODY(a1);
				assert (vof->atom.type == ui->uris.atom_Float);

				const size_t n_elem = (a1->size - sizeof(LV2_Atom_Vector_Body)) / vof->atom.size;
				const float *data = (float*) LV2_ATOM_BODY(&vof->atom);
				if (ui->samplerate != sr) {
					ui->samplerate = sr;
					reinitialize_fft (ui);
				}
				update_spectrum_history (ui, n_elem, data);
				update_spectrum_japa (ui, n_elem, data);
			}
		}
	}

	if (format != 0 || port_index < FIL_ENABLE) return;

	const float v = *(float *)buffer;
	ui->disable_signals = true;
	if (port_index == FIL_ENABLE) {
		robtk_cbtn_set_active (ui->btn_g_enable, v > 0 ? true : false);
	}
	else if (port_index == FIL_GAIN) {
		robtk_dial_set_value (ui->spn_g_gain, v);
	}
	else if (port_index == FIL_HIPASS) {
		robtk_ibtn_set_active (ui->btn_g_hipass, v > 0 ? true : false);
	}
	else if (port_index == FIL_HIFREQ) {
		robtk_dial_set_value (ui->spn_g_hifreq, freq_to_dial (&lphp[0], v));
	}
	else if (port_index == FIL_LOPASS) {
		robtk_ibtn_set_active (ui->btn_g_lopass, v > 0 ? true : false);
	}
	else if (port_index == FIL_LOFREQ) {
		robtk_dial_set_value (ui->spn_g_lofreq, freq_to_dial (&lphp[1], v));
	}
	else if (port_index >= FIL_SEC1 && port_index < FIL_ATOM_CONTROL) {
		const int param = (port_index - FIL_SEC1) % 4;
		const int sect = (port_index - FIL_SEC1) / 4;
		assert (sect >= 0 && sect < NSECT);
		switch (param) {
			case 0:
				robtk_cbtn_set_active (ui->btn_enable[sect], v > 0 ? true : false);
				break;
			case 1:
				robtk_dial_set_value (ui->spn_freq[sect], freq_to_dial (&freqs[sect], v));
				break;
			case 2:
				robtk_dial_set_value (ui->spn_bw[sect], bw_to_dial(v));
				break;
			case 3:
				robtk_dial_set_value (ui->spn_gain[sect], v);
				break;
			default:
				assert (0);
				break;
		}
	}
	ui->disable_signals = false;
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}
