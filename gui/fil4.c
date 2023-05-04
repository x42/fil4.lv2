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
#include "../src/lop.h"
#include "fft.c"
#define WITH_FFTW_LOCK
#include "analyser.cc"

#define RTK_USE_HOST_COLORS
#define OPTIMIZE_FOR_BROKEN_HOSTS // which send updates for non-changed values every cycle
#define USE_LOP_FFT // measure LowPass response rather than calculate its magnitude

#define RTK_URI FIL4_URI
#define RTK_GUI "ui"

#define DOTRADIUS (9) // radius of draggable nodes on the plot
#define BOXRADIUS (7)

#define PK_YOFFS (16)
#define PK_WHITE (24)
#define PK_BLACK (15)
#define PK_RADIUS (4.5)

#define NCTRL (NSECT + 2) // number of filter-bands + 2 (lo,hi-shelf)
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

#ifdef _WIN32
#define snprintf(s, l, ...) sprintf(s, __VA_ARGS__)
#endif

enum {
	Ctrl_HPF    = NCTRL,
	Ctrl_LPF    = NCTRL + 1,
	/* repeat for piano-dot drag */
	Ctrl_Piano,
	Ctrl_PHP = NCTRL + NCTRL + 2,
	Ctrl_PLP,

	Ctrl_Yaxis  = NCTRL + NCTRL + 4,
	Ctrl_Tuning,
};

/* cached filter state */
typedef struct {
	float rate;
	float gain_db;
	float s1, s2;
	float A, B, C, D, A1, B1; // IIR
	float x0, y0; // mouse position
} FilterSection;

typedef struct {
	float f;
	float q;
	float R; // cached resonance (derived from q)
	float x0; // mouse pos. vertical middle
} HoLoFilter;

/* filter parameters */
typedef struct {
	float min;
	float max;
	float dflt;
	float warp;
} FilterFreq;

typedef struct {
	LV2UI_Write_Function write;
	LV2UI_Controller controller;
	LV2_Atom_Forge   forge;
	LV2_URID_Map*    map;
	Fil4LV2URIs      uris;
	LV2UI_Touch*     touch;

	PangoFontDescription *font[2];

	RobWidget *rw; // top-level container
	RobWidget *ctbl; // control element table
	RobWidget *spbox; // spectrum analyzer settings

	/* main drawing area */
	RobWidget *m0;
	int m0_width;
	int m0_height;

	float m0_xw;
	float m0_ym;
	float m0_yr;
	float m0_y0;
	float m0_y1;

	RobTkSep  *sep_v[4];

	// global section
	RobTkCBtn *btn_g_enable;
	RobTkDial *spn_g_gain;
	RobTkLbl  *lbl_g_gain;

	RobTkIBtn *btn_g_hipass;
	RobTkIBtn *btn_g_lopass;
	RobTkDial *spn_g_hifreq;
	RobTkDial *spn_g_hiq;
	RobTkDial *spn_g_lofreq;
	RobTkDial *spn_g_loq;

	RobTkLbl  *lbl_hilo[2];

	// peak display
	RobTkLbl  *lbl_peak;
	RobTkPBtn *btn_peak;

	// filter section
	RobTkCBtn *btn_enable[NCTRL];
	RobTkDial *spn_freq[NCTRL];
	RobTkDial *spn_gain[NCTRL];
	RobTkDial *spn_bw[NCTRL];

	// shelf section
	RobTkCBtn *btn_s_enable[2];
	RobTkDial *spn_s_freq[2];
	RobTkDial *spn_s_gain[2];
	RobTkDial *spn_s_bw[2];

	// spectrum display
	float samplerate;
	RobTkDial *spn_fftgain;
	RobTkLbl  *lbl_fft;
	RobTkSelect* sel_fft; // off, flat, proportional, history
	RobTkSelect* sel_pos; // pre /post
	RobTkSelect* sel_chn; // all, L, R
	RobTkSelect* sel_res; // bark, med, high
	RobTkSelect* sel_spd; // slow, med, fast

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
	cairo_surface_t* dial_bg[5];
	cairo_surface_t* dial_fq[NCTRL];
	cairo_surface_t* dial_hplp[4];

	FilterSection flt[NCTRL];
	HoLoFilter hilo[2];
#if (defined LP_EXTRA_SHELF && ! defined USE_LOP_FFT)
	FilterSection lphs;
#endif

	bool solo_state[NCTRL + 2]; // NCTRL + LPF + HPF
	float solo_hplp[4];
	bool soloing;

	int dragging;
	int drag_y;
	int hover;
	bool fft_change;
	bool filter_redisplay;
	bool disable_signals;
	int peak_reset_val;
#ifdef OPTIMIZE_FOR_BROKEN_HOSTS
	float last_peak;
#endif

	bool scale_cached;
	float xscale[FFT_MAX + 1];
	float ydBrange;

	float tuning_fq; // for piano

	int n_channels;
	float mixdown[8192];
#ifdef USE_LOP_FFT
	LowPass lop;
	struct FFTAnalysis *lopfft;
#endif
	const char *nfo;
} Fil4UI;

///////////////////////////////////////////////////////////////////////////////

static const char* note_names[] = {
	"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
};

/* frequency mapping */
static const FilterFreq freqs[NCTRL] = {
	/*min    max   dflt*/
	{  25,   400,    80,  16}, // LS
	{  20,  2000,   160, 100},
	{  40,  4000,   397, 100},
	{ 100, 10000,  1250, 100},
	{ 200, 20000,  2500, 100},
	{1000, 16000,  8000,  16}, // HS
};

static const FilterFreq lphp[2] = {
	{   10,  1000,    20, 100}, // HP
	{  630, 20000, 20000,  32}, // LP
};

/* individual filter colors */
static const float c_fil[NCTRL+2][4] = {
	{0.5, 0.6, 0.7, 0.8}, //LS
	{1.0, 0.2, 0.2, 0.8},
	{0.2, 1.0, 0.2, 0.8},
	{0.2, 0.2, 1.0, 0.8},
	{0.8, 0.7, 0.4, 0.8},
	{0.7, 0.4, 0.7, 0.8}, // HS
	{0.5, 0.4, 0.3, 0.0}, // HP, alpha unused
	{0.3, 0.5, 0.4, 0.0}, // LP, alpha unused
};

static const float c_ann[4] = {0.5, 0.5, 0.5, 1.0}; // text annotation color
static float c_dlf[4] = {0.8, 0.8, 0.8, 1.0}; // dial faceplate fg

///////////////////////////////////////////////////////////////////////////////

/* graph log-scale mapping */
static float freq_at_x (const int x, const int m0_width) {
	return 20.f * powf (1000.f, x / (float) m0_width);
}

static float x_at_freq (const float f, const int m0_width) {
	return rintf(m0_width * logf (f / 20.0) / logf (1000.0));
}

/**** dial value mappings ****/
static float bw_to_dial (float v) {
	if (v < .0625) return 0.f;
	if (v >  4.0) return 1.f;
	return log2f (16.f * v) / 6.f;
}

static float dial_to_bw (const float v) {
	return powf (2, 6.f * v - 4.f);
}

static float shelf_q_ann (const float v) {
	return .222f + .444f * dial_to_bw (v);
}

static float hplp_to_dial (const float v) {
#if 1
	float rv = 0.525561 - 0.387896 * atan(4.5601 - 5.2275 * v);
	if (rv < 0) return 0;
	if (rv > 1.0) return 1.0;
	return rv;
#else
	return v / 1.4;
#endif
}

static float dial_to_hplp (const float v) {
#if 1
	float rv = 0.872328 + 0.191296 * tan (2.57801 * (v - 0.525561));
	if (rv < 0) return 0;
	if (rv > 1.4) return 1.4;
	return rv;
#else
	return v * 1.4;
#endif
}

/* freq [min .. max] <> dial 0..1 */
static float freq_to_dial (const FilterFreq *m, float f) {
	if (f < m->min) return 0.f;
	if (f > m->max) return 1.f;
	return log (1. + m->warp * (f - m->min) / (m->max - m->min)) / log (1. + m->warp);
}

static float dial_to_freq (const FilterFreq *m, float f) {
	return m->min + (m->max - m->min) * (pow((1. + m->warp), f) - 1.) / m->warp;
}

static char* freq_to_note (const float tuning, float freq) {
	const int note = rintf (12.f * log2f (freq / tuning) + 69.0);
	const float note_freq = tuning * powf (2.0, (note - 69.f) / 12.f);
	const float cent = 1200.0 * log2 (freq / note_freq);

	const int octave = note / 12 - 1;
	const size_t n = note % 12;
	static char buf[32];
	snprintf (buf, sizeof (buf), "%2s%d %+3.0fct", note_names[n], octave, cent);
	return buf;
}

/*** faceplates and annotation ***/

static void format_button_dbfs (RobTkPBtn *p, const float db) {
	char buf[32];
	if (db > 99.f) {
		snprintf(buf, 32, "++++");
	} else if (db < -120.f) {
		snprintf(buf, 32, "----");
	} else if (db < -119.f) {
		snprintf(buf, 32, " -\u221E dBFS");
	} else if (fabsf(db) > 9.94f) {
		snprintf(buf, 32, "%+.0f dBFS", db);
	} else {
		snprintf(buf, 32, "%+.1f dBFS", db);
	}

	if (db >= 0.f) {
		robtk_pbtn_set_bg (p, 1.0, 0.0, 0.0, 1.0);
	} else if (db > -1.f) {
		robtk_pbtn_set_bg (p, 0.9, 0.6, 0.05, 1.0);
	} else if (is_light_theme ()) {
		robtk_pbtn_set_bg (p, 0.8, 0.8, 0.8, 1.0);
	} else {
		robtk_pbtn_set_bg (p, 0.2, 0.2, 0.2, 1.0);
	}

	robtk_pbtn_set_text (p, buf);
}

static void tooltip_text (Fil4UI* ui, RobTkDial* d, cairo_t *cr, const char* txt) {
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
	pango_cairo_show_layout(cr, pl);
	g_object_unref(pl);
	cairo_restore(cr);
	cairo_new_path(cr);
}

static void dial_annotation_db (RobTkDial * d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	char txt[16];
	snprintf(txt, 16, "%+5.1fdB", d->cur);
	tooltip_text (ui, d, cr, txt);
}

static void dial_annotation_fq (RobTkDial * d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	int k = -1;
	for (uint32_t i = 0; i < NCTRL; ++i) {
		if (ui->spn_freq[i] == d) {
			k = i;
			break;
		}
	}
	if (k < 0) {
		return;
	}
	float freq = dial_to_freq (&freqs[k], d->cur);
	tooltip_text (ui, d, cr, freq_to_note (ui->tuning_fq, freq));
}

static void dial_annotation_hifreq (RobTkDial * d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	float freq = dial_to_freq (&lphp[0], d->cur);
	tooltip_text (ui, d, cr, freq_to_note (ui->tuning_fq, freq));
}

static void dial_annotation_lofreq (RobTkDial * d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	float freq = dial_to_freq (&lphp[1], d->cur);
	tooltip_text (ui, d, cr, freq_to_note (ui->tuning_fq, freq));
}

static void dial_annotation_hz (RobTkCBtn *l, const int which, const float hz) {
	char txt[24];
	if (hz > 5000) {
		snprintf(txt, 16, "%.1fKHz", hz / 1000.f);
	} else {
		snprintf(txt, 16, "%.0fHz", hz);
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

static void dial_annotation_bw (RobTkDial *d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	char txt[16];
	const int bw = rintf (1000.f * dial_to_bw (d->cur));
	switch (bw) {
		case   62: snprintf(txt, 16, "1/16 Oct"); break;
		case  125: snprintf(txt, 16, " 1/8 Oct"); break;
		case  250: snprintf(txt, 16, " 1/4 Oct"); break;
		case  500: snprintf(txt, 16, " 1/2 Oct"); break;
		case 1000: snprintf(txt, 16, "  1  Oct"); break;
		case 2000: snprintf(txt, 16, "  2  Oct"); break;
		case 4000: snprintf(txt, 16, "  4  Oct"); break;
		default:
			snprintf(txt, 16, "%4.2f Oct", dial_to_bw (d->cur));
			break;
	}
	tooltip_text (ui, d, cr, txt);
}

static void dial_annotation_q (RobTkDial *d, cairo_t *cr, void *data) {
	Fil4UI* ui = (Fil4UI*) (data);
	char txt[16];
	snprintf(txt, 16, "Q: %4.2f", shelf_q_ann (d->cur));
	tooltip_text (ui, d, cr, txt);
}

/*** knob faceplates ***/
static void prepare_faceplates(Fil4UI* ui) {
	cairo_t *cr;
	float xlp, ylp;

#define NEW_SF(VAR, W, H) \
	VAR = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H); \
	cr = cairo_create (VAR); \

#define FP_COLOR_BW              \
	if (is_light_theme ()) {       \
		CairoSetSouerceRGBA (c_wht); \
	} else {                       \
		CairoSetSouerceRGBA (c_blk); \
	}

#define FP_COLOR_GRY             \
	if (is_light_theme ()) {       \
		CairoSetSouerceRGBA (c_g20); \
	} else {                       \
		CairoSetSouerceRGBA (c_g80); \
	}

	NEW_SF(ui->hpf_btn[0], 26, 20);
	cairo_move_to (cr,  4, 16);
	cairo_line_to (cr,  9,  4);
	cairo_line_to (cr, 22,  4);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	FP_COLOR_BW
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	FP_COLOR_GRY
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	NEW_SF(ui->hpf_btn[1], 26, 20);
	cairo_move_to (cr,  4, 16);
	cairo_line_to (cr,  9, 4);
	cairo_line_to (cr, 22, 4);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	FP_COLOR_BW
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	CairoSetSouerceRGBA (c_grn);
	cairo_set_source_rgba (cr, c_fil[Ctrl_HPF][0], c_fil[Ctrl_HPF][1], c_fil[Ctrl_HPF][2], 1.0);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	NEW_SF(ui->lpf_btn[0], 26, 20);
	cairo_move_to (cr,  4,  4);
	cairo_line_to (cr, 17,  4);
	cairo_line_to (cr, 22, 16);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	FP_COLOR_BW
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	FP_COLOR_GRY
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

	NEW_SF(ui->lpf_btn[1], 26, 20);
	cairo_move_to (cr,  4, 4);
	cairo_line_to (cr, 17, 4);
	cairo_line_to (cr, 22, 16);
	cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
	FP_COLOR_BW
	cairo_set_line_width (cr, 3.0);
	cairo_stroke_preserve (cr);
	cairo_set_source_rgba (cr, c_fil[Ctrl_LPF][0], c_fil[Ctrl_LPF][1], c_fil[Ctrl_LPF][2], 1.0);
	cairo_set_line_width (cr, 1.5);
	cairo_stroke (cr);
	cairo_destroy (cr);

#define INIT_DIAL_SF(VAR, W, H) \
	VAR = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2 * (W), 2 * (H)); \
	cr = cairo_create (VAR); \
	cairo_scale (cr, 2.0, 2.0); \
	CairoSetSouerceRGBA(c_trs); \
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE); \
	cairo_rectangle (cr, 0, 0, W, H); \
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
	cairo_set_line_width(cr, 1.25);
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

	{ DIALDOTS(bw_to_dial (powf(2.f,  4 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f,  3 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f,  2 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f,  1 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f,  0 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f, -1 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -2 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f, -3 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -4 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f, -5 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -6 / 2.f)), .5, 3.5) }
	//{ DIALDOTS(bw_to_dial (powf(2.f, -7 / 2.f)), .5, 3.5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -8 / 2.f)), .5, 3.5) }
	cairo_destroy (cr);

	/* low shelf */
	INIT_DIAL_SF(ui->dial_bg[2], GED_WIDTH, GED_HEIGHT + 4);
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);
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

	CairoSetSouerceRGBA(c_ann);
	cairo_set_line_width (cr, 1.0);
	cairo_arc (cr, GED_CX + 1, GED_CY + 3, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);
	cairo_arc (cr, GED_CX - 1, GED_CY + 3, GED_RADIUS + 2.0, 0.75 * M_PI, 1.25 * M_PI);
	cairo_stroke (cr);
	CairoSetSouerceRGBA(c_dlf);

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
	cairo_set_line_width(cr, 1.25);
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

	CairoSetSouerceRGBA(c_ann);
	cairo_set_line_width (cr, 1.0);
	cairo_arc (cr, GED_CX + 1, GED_CY + 3, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);
	cairo_arc (cr, GED_CX - 1, GED_CY + 3, GED_RADIUS + 2.0, 0.75 * M_PI, 1.25 * M_PI);
	cairo_stroke (cr);
	CairoSetSouerceRGBA(c_dlf);

	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(1/6.f, .5, 3.5) }
	{ DIALDOTS(2/6.f, .5, 3.5) }
	{ DIALDOTS(3/6.f, .5, 3.5) }
	{ DIALDOTS(4/6.f, .5, 3.5) }
	{ DIALDOTS(5/6.f, .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);

	/* fft gain */
	INIT_DIAL_SF(ui->dial_bg[4], GED_WIDTH, GED_HEIGHT + 4);
	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(1/6.f, .5, 3.5) }
	{ DIALDOTS(2/6.f, .5, 3.5) }
	{ DIALDOTS(3/6.f, .5, 3.5) }
	{ DIALDOTS(4/6.f, .5, 3.5) }
	{ DIALDOTS(5/6.f, .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);

	/* frequency knob faceplate */
	for (int i = 0; i < NCTRL; ++i) {
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

#define HLX 3 // Hi/Low pass icon x-offset

	/* low Pass bandwidth */
	INIT_DIAL_SF(ui->dial_hplp[2], GED_WIDTH, GED_HEIGHT + 4); // 55 x 34, icon x=1..18  y= GZLINE +- 3
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);
	cairo_move_to (cr,  1 + HLX, GZLINE + 3);
	cairo_curve_to (cr, 1 + HLX, GZLINE - 0, 11 + HLX, GZLINE - 1, 13 + HLX, GZLINE - 1);
	cairo_line_to (cr, 18 + HLX, GZLINE - 1);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - HLX - 18, GZLINE + 3);
	cairo_line_to (cr, GED_WIDTH - HLX - 12, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH - HLX -  8, GZLINE - 1);
	cairo_line_to (cr, GED_WIDTH - HLX -  1, GZLINE - 1);
	cairo_stroke (cr);

	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(hplp_to_dial(.71), .5, 3.5) }
	{ DIALDOTS(  0.5, .5, 3.5) }
	{ DIALDOTS(hplp_to_dial(1.0), .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);

	INIT_DIAL_SF(ui->dial_hplp[3], GED_WIDTH, GED_HEIGHT + 4);
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);

	cairo_move_to (cr,  1 + HLX, GZLINE - 1);
	cairo_line_to (cr,  6 + HLX, GZLINE - 1);
	cairo_curve_to (cr, 8 + HLX, GZLINE - 1, 18 + HLX, GZLINE - 0, 18 + HLX, GZLINE + 3);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - HLX - 18, GZLINE - 1);
	cairo_line_to (cr, GED_WIDTH - HLX - 12, GZLINE - 1);
	cairo_line_to (cr, GED_WIDTH - HLX -  8, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH - HLX -  1, GZLINE + 3);
	cairo_stroke (cr);

	{ DIALDOTS(  0.0, .5, 3.5) }
	{ DIALDOTS(hplp_to_dial(.71), .5, 3.5) }
	{ DIALDOTS(  0.5, .5, 3.5) }
	{ DIALDOTS(hplp_to_dial(1.0), .5, 3.5) }
	{ DIALDOTS(  1.0, .5, 3.5) }
	cairo_destroy (cr);

}

static void update_filter_display (Fil4UI* ui) {
	ui->filter_redisplay = true;
	queue_draw(ui->m0);
}

static void update_grid (Fil4UI* ui) {
	if (ui->m0_grid) {
		cairo_surface_destroy (ui->m0_grid);
		ui->m0_grid = NULL;
	}
	queue_draw(ui->m0);
}

///////////////////////////////////////////////////////////////////////////////

static double warp_freq (double w, double f)
{
	f *= 2 * M_PI;
	return fabs (atan2 ((1 - w * w) * sin (f), (1 + w * w) * cos (f) - 2 * w) / (2 * M_PI));
}

static void recalc_scales (Fil4UI* ui) {

	const int spd = robtk_select_get_value(ui->sel_spd);
	const int wrp = robtk_select_get_value(ui->sel_res);
	float speed, wfact;

	ui->scale_cached = false;

	switch (spd) {
		case 4:
			speed = 20.0;
			break;
		case 3:
			speed = 2.0;
			break;
		case 2:
			speed = 0.2;
			break;
		case 1:
			speed = 0.08;
			break;
		default:
			speed = 0.03;
			break;
	}

	switch (wrp) {
		case 0:
			wfact = 0.8517f * sqrtf (atanf (65.83e-6f * ui->samplerate)) - 0.1916f;
			break;
		case 1:
			wfact = 0.90;
			break;
		default:
			wfact = 0.95;
			break;
	}

	ui->japa->set_speed (speed);
	ui->japa->set_wfact (wfact);

	for (int i = 0; i <= FFT_MAX; ++i) {
		const double f = 0.5 * i / FFT_MAX;
		ui->_fscale [i] = warp_freq (-wfact, f);
	}

	for (int i = 1; i < FFT_MAX; ++i) {
		ui->_bwcorr [i] = 1.f / (ui->ydBrange * (ui->_fscale [i + 1] - ui->_fscale [i - 1]) / ui->_fscale [i]);
	}
	ui->_bwcorr [0]       = ui->_bwcorr [1];
	ui->_bwcorr [FFT_MAX] = ui->_bwcorr [FFT_MAX - 1];
}

static void reinitialize_fft (Fil4UI* ui) {
	// History
	fftx_free(ui->fa);
	ui->fa = (struct FFTAnalysis*) malloc(sizeof(struct FFTAnalysis));
	fftx_init (ui->fa, 8192, ui->samplerate, 25);

	// JAPA
	ui->_ipstep = (ui->samplerate > 64e3f) ? 0x2000 : 0x1000;
	ui->_ipsize = 2 * ui->_ipstep;
	delete ui->japa;
	ui->japa = new Analyser (ui->_ipsize, FFT_MAX, ui->samplerate);
	ui->japa->set_fftlen (512);
	recalc_scales (ui);
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
	const int align = - DEFAULT_YZOOM - robtk_dial_get_value (ui->spn_fftgain);

	cairo_t *cr = cairo_create (ui->fft_scale);
	cairo_rectangle (cr, 0, ui->m0_y0, 12,  ui->m0_y1 -  ui->m0_y0);
	if (is_light_theme ()) {
		CairoSetSouerceRGBA(c_g80);
	} else {
		CairoSetSouerceRGBA(c_blk);
	}
	cairo_fill (cr);

#define FFT_DB(db, len) { \
	const float yy = rintf(ui->m0_ym + .5 + (db - ui->ydBrange) * ui->m0_yr) - .5; \
	cairo_move_to (cr, 0,   yy); \
	cairo_line_to (cr, len, yy); \
	cairo_stroke(cr); \
}

	const float *txt_c;
	const float *txt_b;
	int txt_x;

	if (mode < 3) {
		cairo_set_line_width(cr, 1.0);
		if (is_light_theme ()) {
			CairoSetSouerceRGBA(c_wht);
		} else {
			CairoSetSouerceRGBA(c_g30);
		}

		for (int i = 0; i < 2 * ui->ydBrange; ++i) {
			int dB = align + ui->ydBrange - i;
			if (dB == 0) {
				CairoSetSouerceRGBA(c_g60);
				FFT_DB(i, 5.5);
				if (is_light_theme ()) {
					CairoSetSouerceRGBA(c_wht);
				} else {
					CairoSetSouerceRGBA(c_g30);
				}
			}
			else if (dB % 10 == 0) {
				FFT_DB(i, 4.5);
			}
			else if (dB % 5 == 0) {
				FFT_DB(i, 2.5);
			}
		}

		txt_c = &c_ann[0];
		txt_b = &c_ann[0];
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
			cairo_line_to(cr, 12, ui->m0_y0 + i + .5);
			cairo_stroke(cr);
		}
		txt_c = &c_blk[0];
		txt_b = &c_wht[0];
		txt_x = 2;
	}

	char tmp[16];
	sprintf(tmp, "%+3.0f", align + ui->ydBrange);
	write_text_full(cr, tmp, ui->font[0], txt_x, ui->m0_y0 + 2, 1.5 * M_PI, 7, txt_c);

	write_text_full(cr, "dBFS", ui->font[0], txt_x, ui->m0_y0 + ui->ydBrange * ui->m0_yr, 1.5 * M_PI, 8, txt_c);

	sprintf(tmp, "%+3.0f", align - ui->ydBrange);
	write_text_full(cr, tmp, ui->font[0], txt_x, ui->m0_y1 + 1, 1.5 * M_PI, 9, txt_b);
	cairo_destroy (cr);
}

static void update_spectrum_history (Fil4UI* ui, const size_t n_elem, float const * data) {
	if (!ui->fft_history) {
		return;
	}
	const float mode = robtk_select_get_value(ui->sel_fft);
	if (mode < 3) {
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
		const float db = 2 * ui->ydBrange;

		// clear current line
		cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
		cairo_rectangle (cr, 0, yy, ui->m0_xw, 1);
		cairo_fill (cr);

		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		float gain = robtk_dial_get_value (ui->spn_fftgain) + DEFAULT_YZOOM - ui->ydBrange; // XXX
		for (uint32_t i = 1; i < b-1; ++i) {
			const float freq = fftx_freq_at_bin (ui->fa, i);
			const float f0 = x_at_freq (MAX (5, freq - 2 * ui->fa->freq_per_bin), ui->m0_xw);
			const float f1 = x_at_freq (        freq + 2 * ui->fa->freq_per_bin,  ui->m0_xw);

#if 0 // do we really have to be 'this' precise (take phase into account)
			float norm = freq / ui->fa->freq_per_bin;
			if (norm <= 1) { norm = 1; }
#else
			const float norm = i;
#endif
			const float level = gain + fftx_power_to_dB (ui->fa->power[i] * norm);
			if (level < -db) continue;
			const float pk = level > 0.0 ? 1.0 : (db + level) / db;
			float clr[3];
			hsl2rgb(clr, .70 - .72 * pk, .9, .3 + pk * .4);
			cairo_set_source_rgba(cr, clr[0], clr[1], clr[2], .3 + pk * .2);

			cairo_move_to (cr, f0, yy+.5);
			cairo_line_to (cr, f1, yy+.5);
			cairo_stroke (cr);
		}

		if (ui->fft_change) {
			ui->fft_change = false;
			double dash = 1;
			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
			if (is_light_theme ()) {
				cairo_set_source_rgba (cr, 0, 0, 0, .5);
			} else {
				cairo_set_source_rgba (cr, 1, 1, 1, .5);
			}
			cairo_set_dash (cr, &dash, 1, ui->fft_hist_line & 1);
			cairo_move_to (cr, 0, yy+.5);
			cairo_line_to (cr, ui->m0_xw, yy+.5);
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
	if (mode < 1 || mode > 2) {
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

static void handle_audio_data (Fil4UI* ui, const int chn, const size_t n_elem, const float *data) {
	if (ui->n_channels == 1) {
		update_spectrum_history (ui, n_elem, data);
		update_spectrum_japa (ui, n_elem, data);
		return;
	}

	const int chnsel = rint(robtk_select_get_value(ui->sel_chn));

	if (chnsel >= 0 && chn != chnsel) {
		return;
	}
	else if (chnsel >= 0) {
		update_spectrum_history (ui, n_elem, data);
		update_spectrum_japa (ui, n_elem, data);
		return;
	}

	/* mixdown */
	if (chn == 0) {
		memcpy(ui->mixdown, data, n_elem * sizeof(float));
	} else {
		for (size_t s = 0; s < n_elem; ++s) {
			ui->mixdown[s] += data[s];
		}
	}

	if (chn + 1 == ui->n_channels) {

		const float amp = 1.0 / (float) ui->n_channels;
		for (size_t s = 0; s < n_elem; ++s) {
			ui->mixdown[s] *= amp;
		}

		update_spectrum_history (ui, n_elem, ui->mixdown);
		update_spectrum_japa (ui, n_elem, ui->mixdown);
	}
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
	float q = .2129f + bw / 2.25f; // map [2^-4 .. 2^2] to [2^(-3/2) .. 2^(1/2)]
	if (freq_ratio < 0.0004) freq_ratio = 0.0004;
	if (freq_ratio > 0.4700) freq_ratio = 0.4700;
	if (q < .25f) { q = .25f; }
	if (q > 2.0f) { q = 2.0f; }

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

#ifdef USE_LOP_FFT
static void lop_run (void* handle, uint32_t n_samples, float *inout) {
	LowPass *lop = (LowPass*) handle;
	lop_compute(lop, n_samples, inout);
}
#endif

static void update_hilo (Fil4UI *ui) {
	float q, r;

	if (ui->hilo[0].f < 5) {
		ui->hilo[0].f = 5;
	}
	if (ui->hilo[0].f > ui->samplerate / 12.f) {
		ui->hilo[0].f = ui->samplerate / 12.f;
	}

	// high-pass
	//
	/* High Pass resonance
	 * f(0)    = 0   // -6dB
	 * f(0.57) = 2   // -3dB
	 * f(0.97) = 3   //  0dB
	 * ...
	 * f(1.60) = 3.85 // ~8dB
	 */
	r = RESHP(ui->hilo[0].q);
	if (r < 1.3) {
		q = 3.01 * sqrt(r / (r+2));
	} else {
		// clamp pole
		q = sqrt(4 - 0.09 / (r - 1.09));
	}
	ui->hilo[0].R = q;

	// low-pass
	if (ui->hilo[1].f < ui->samplerate * 0.0002) {
		ui->hilo[1].f = ui->samplerate * 0.0002;;
	}
	if (ui->hilo[1].f > ui->samplerate * 0.4998f) {
		ui->hilo[1].f = ui->samplerate * 0.4998;
	}
	r = RESLP(ui->hilo[1].q);
	ui->hilo[1].R = sqrtf(4.f * r / (1 + r));

#ifdef USE_LOP_FFT
	if (ui->lopfft) {
		lop_set (&ui->lop, ui->hilo[1].f, ui->hilo[1].q);
		fa_analyze_dsp (ui->lopfft, &lop_run, &ui->lop);
	}
#endif
}

static void update_filters (Fil4UI *ui) {
	for (uint32_t i = 1; i < NCTRL -1; ++i) {
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
	update_iir (&ui->flt[NCTRL-1], 1,
			dial_to_freq(&freqs[NCTRL-1], robtk_dial_get_value (ui->spn_freq[NCTRL-1])),
			dial_to_bw (robtk_dial_get_value (ui->spn_bw[NCTRL-1])),
			robtk_dial_get_value (ui->spn_gain[NCTRL-1])
			);
	update_filter_display (ui);
}

static void samplerate_changed (Fil4UI *ui) {
	for (int i = 0; i < NCTRL; ++i) {
		ui->flt[i].rate = ui->samplerate;
	}

#ifdef USE_LOP_FFT
	lop_setup (&ui->lop, ui->samplerate, ui->hilo[1].f, ui->hilo[1].q);
	fftx_free(ui->lopfft);
	ui->lopfft = (FFTAnalysis*) malloc(sizeof(struct FFTAnalysis));
	fftx_init (ui->lopfft, 8192, ui->samplerate, 25);
#elif defined LP_EXTRA_SHELF
	ui->lphs.rate = ui->samplerate;
	update_iir (&ui->lphs, 1, ui->samplerate / 3., .5 /*.444*/, -6);
#endif
	update_filters (ui);
	update_hilo (ui);
	reinitialize_fft (ui);

	// what else ?
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
#if 1
	/* for 0 < f <= 1/12 fsamp.
	 * the filter does not [yet] correct for the attenuation
	 * once  "0dB" reaches fsamp/2 (parameter is clamped
	 * both in DSP as well as in cb_spn_g_hifreq() here.)
	 */
	const float wr = ui->hilo[0].f / freq;
	float q = ui->hilo[0].R;
	// -20 log (sqrt( (1 + wc / w)^2 - (r * wc / w)^2))
	return -10.f * log10f (SQUARE(1 + SQUARE(wr)) - SQUARE(q * wr));
#else // fixed q=0
	const float w = freq / ui->hilo[0].f;
	const float v = (w / sqrtf (1 + w * w));
	return 40.f * log10f (v); // 20 * log(v^2);
#endif
}

static float get_lowpass_response (Fil4UI *ui, const float freq) {
#ifdef USE_LOP_FFT
	const float f = freq / ui->lopfft->freq_per_bin;
	uint32_t i = floorf (f);
	if (i + 1 >= fftx_bins (ui->lopfft)) {
		return fftx_power_to_dB (ui->lopfft->power[fftx_bins (ui->lopfft) - 2]);
	}
	return fftx_power_to_dB (ui->lopfft->power[i] * (1.f + i - f) + ui->lopfft->power[i+1] * (f - i));
#else
	// TODO limit in case SR < 40K, also lop.h w2
	const float w  = sin (M_PI * freq /ui->samplerate);
	const float wc = sin (M_PI * ui->hilo[1].f /ui->samplerate);
	const float q = ui->hilo[1].R;
	float xhs = 0;
#ifdef LP_EXTRA_SHELF
	xhs = get_shelf_response (&ui->lphs, freq);
#endif
	return -10.f * log10f (SQUARE(1 + SQUARE(w/wc)) - SQUARE(q * w/wc)) + xhs;
#endif
}

///////////////////////////////////////////////////////////////////////////////

static void tx_state (Fil4UI* ui) {
	uint8_t obj_buf[1024];
	lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 1024);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(&ui->forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &frame, 1, ui->uris.state);

	int fftmode = robtk_select_get_value(ui->sel_pos);
	fftmode |= ((int)robtk_select_get_value(ui->sel_fft)) << 1;
	fftmode |= ((int)robtk_select_get_value(ui->sel_spd)) << 8;
	fftmode |= ((int)robtk_select_get_value(ui->sel_res)) << 12;

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_fftmode, 0);
	lv2_atom_forge_int(&ui->forge, fftmode);

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_fftgain, 0);
	lv2_atom_forge_float(&ui->forge, robtk_dial_get_value (ui->spn_fftgain));

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_fftchan, 0);
	lv2_atom_forge_int(&ui->forge, robtk_select_get_value(ui->sel_chn));

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_dbscale, 0);
	lv2_atom_forge_float(&ui->forge, ui->ydBrange);

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_uiscale, 0);
	lv2_atom_forge_float(&ui->forge, ui->rw->widget_scale);

	lv2_atom_forge_property_head(&ui->forge, ui->uris.s_kbtuning, 0);
	lv2_atom_forge_float(&ui->forge, ui->tuning_fq);

	lv2_atom_forge_pop(&ui->forge, &frame);
	ui->write(ui->controller, FIL_ATOM_CONTROL, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

/*** knob & button callbacks ****/

// TODO separate handle and data, update single value only
// (no big deal, LV2 hosts ignore values if not changed, DSP backend does not care either)

static bool cb_btn_en (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	if (ui->disable_signals) return TRUE;
	for (uint32_t i = 0; i < NCTRL; ++i) {
		float val = robtk_cbtn_get_active(ui->btn_enable[i]) ? 1.f : 0.f;
		ui->write(ui->controller, IIR_LS_EN + i * 4, sizeof(float), 0, (const void*) &val);
	}
	update_filter_display (ui);
	return TRUE;
}

static bool cb_spn_freq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	for (uint32_t i = 0; i < NCTRL; ++i) {
		const float val = dial_to_freq(&freqs[i], robtk_dial_get_value (ui->spn_freq[i]));
		dial_annotation_hz (ui->btn_enable[i], i, val);
		if (ui->disable_signals) continue;
		ui->write(ui->controller, IIR_LS_FREQ + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static bool cb_spn_bw (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	if (ui->disable_signals) return TRUE;
	for (uint32_t i = 0; i < NCTRL; ++i) {
		const float val = dial_to_bw (robtk_dial_get_value (ui->spn_bw[i]));
		ui->write(ui->controller, IIR_LS_Q + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static bool cb_spn_gain (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	update_filters(ui);
	for (uint32_t i = 0; i < NCTRL; ++i) {
		const float val = robtk_dial_get_value (ui->spn_gain[i]);
		if (ui->disable_signals) continue;
		ui->write(ui->controller, IIR_LS_GAIN + i * 4, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

static void set_hipass_label (Fil4UI* ui) {
	char txt[32];
	if ( ui->hilo[0].f > 999) {
		snprintf(txt, 32, "%.1f KHz\nQ:%.2f",
				ui->hilo[0].f / 1000, ui->hilo[0].q);
	} else if ( ui->hilo[0].f >= 50) {
		snprintf(txt, 32, "%.0f Hz\nQ:%.2f",
				ui->hilo[0].f, ui->hilo[0].q);
	} else {
		snprintf(txt, 32, "%.1f Hz\nQ:%.2f",
				ui->hilo[0].f, ui->hilo[0].q);
	}
	robtk_lbl_set_text (ui->lbl_hilo[0], txt);
}

static void set_lopass_label (Fil4UI* ui) {
	char txt[32];
	if ( ui->hilo[1].f > 999) {
		snprintf(txt, 32, "%.1f KHz\nQ:%.2f",
				ui->hilo[1].f / 1000.f, ui->hilo[1].q);
	} else {
		snprintf(txt, 32, "%.0f Hz\nQ:%.2f",
				ui->hilo[1].f, ui->hilo[1].q);
	}
	robtk_lbl_set_text (ui->lbl_hilo[1], txt);
}

static bool cb_spn_g_hifreq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	float val = dial_to_freq (&lphp[0], robtk_dial_get_value (ui->spn_g_hifreq));
	ui->hilo[0].f = val;
	update_hilo (ui);
	update_filter_display (ui);
	set_hipass_label (ui);
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_HIFREQ, sizeof(float), 0, (const void*) &ui->hilo[0].f);
	return TRUE;
}

static bool cb_spn_g_hiq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float val = dial_to_hplp (robtk_dial_get_value (ui->spn_g_hiq));
	ui->hilo[0].q = val;
	update_hilo (ui);
	update_filter_display (ui);
	set_hipass_label (ui);
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_HIQ, sizeof(float), 0, (const void*) &val);
	return TRUE;
}

static bool cb_spn_g_lofreq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float val = dial_to_freq (&lphp[1], robtk_dial_get_value (ui->spn_g_lofreq));
	ui->hilo[1].f = val;
	update_hilo (ui);
	update_filter_display (ui);
	set_lopass_label (ui);
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_LOFREQ, sizeof(float), 0, (const void*) &ui->hilo[1].f);
	return TRUE;
}

static bool cb_spn_g_loq (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float val = dial_to_hplp (robtk_dial_get_value (ui->spn_g_loq));
	ui->hilo[1].q = val;
	update_hilo (ui);
	update_filter_display (ui);
	set_lopass_label (ui);
	if (ui->disable_signals) return TRUE;
	ui->write(ui->controller, FIL_LOQ, sizeof(float), 0, (const void*) &val);
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

static bool cb_peak_rest (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	if (ui->disable_signals) return TRUE;
	ui->peak_reset_val = ui->peak_reset_val ? 0 : 1;
	float val = ui->peak_reset_val;
	ui->write(ui->controller, FIL_PEAK_RESET, sizeof(float), 0, (const void*) &val);
	return TRUE;
}

static bool cb_fft_change (RobWidget *w, void* handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	const float mode = robtk_select_get_value(ui->sel_fft);
	if (mode == 3) {
		ui->fft_change = true;
	}
	if (ui->disable_signals) return TRUE;
	tx_state (ui);
	return TRUE;
}

static bool cb_japa (RobWidget* w, void *handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	recalc_scales (ui);
	if (ui->disable_signals) return TRUE;
	tx_state (ui);
	return TRUE;
}

static bool cb_set_fft (RobWidget* w, void *handle) {
	Fil4UI* ui = (Fil4UI*)handle;
	ui->fft_change = true;
	update_filter_display (ui);
	const float val = robtk_select_get_value(ui->sel_fft);
	robtk_dial_set_sensitive (ui->spn_fftgain, val > 0);
	robtk_select_set_sensitive (ui->sel_res, (val > 0 && val < 3));
	robtk_select_set_sensitive (ui->sel_spd, (val > 0 && val < 3));
	if (ui->disable_signals) return TRUE;
	tx_state (ui);
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

	write_text_full(cr, "dB", ui->font[0], x0 - 20, ui->m0_ym, M_PI * -.5, 8, c_ann);

	/* calculate grid
	 *
	 * 1) find max possible major [numerical] grid lines - depending on height
	 * 2) calc stride of numeric grid - depending on dB-range and (1)
	 * 3) set sub-grid stride
	 */
	const int m0_h = ui->m0_y1 - ui->m0_y0; // [px/dB]
	const int max_lines = MIN(8, MAX(2, floorf (8 * m0_h / (m0_h + 160.f))));
	const int numgrid = 3 * ceilf (ui->ydBrange / (float)(max_lines * 3)); // [dB]
	const int numspace = ceilf (9.75 / ui->m0_yr); // skip entries this close to the border [dB]
	const float griddist = (numgrid * ui->m0_yr); // numeric-grid spacing in [px].
	int subgrid = 0; // resulting subgrid stride [db]

	/* max density of sub-grid lines (px space based) */
	const int max_subgrid = floor (griddist / 10.0); // [number of lines]
	/* those lines need to span 'numgrid' dB ..*/
	if (max_subgrid > numgrid) {
		subgrid = 1; // yes we can.
	} else if (max_subgrid >= 1) {
		/* subgrid is spaced (numgrid / 2) rounded down to next mult of 3, or one */
		const float sg = MAX(1, 3 * floor(numgrid / 6.0)); // [db]
		/* does it fit? */
		if (numgrid <= max_subgrid * sg) {
			subgrid = sg;
		}
	}

#if 0 // DEBUG
	const float sgriddist = subgrid * ui->m0_yr;
	printf("GRID: H:%d[px] R:%.1f[db] -> #G:%d MX:%d || NG: %d SG: %d NS: %d [dB] || (px/db:%.3f) GRD: %.2f[px] SGRD: %.2f[px]\n",
			m0_h, ui->ydBrange, max_lines, max_subgrid,
			numgrid, subgrid, numspace,
			ui->m0_yr, griddist, sgriddist);
#endif

	if (subgrid > 0) {
		cairo_set_line_width(cr, .75);
		if (is_light_theme ()) {
			CairoSetSouerceRGBA(c_g90);
		} else {
			CairoSetSouerceRGBA(c_g20);
		}
		for (int i = subgrid; i <= ui->ydBrange; i += subgrid) {
			if (ui->ydBrange - i < subgrid) break;
			if (i % numgrid == 0) {
					continue;
			}
			GRID_DB_DOT(i);
			GRID_DB_DOT(-i);
		}
	}

	cairo_set_line_width(cr, 1.0);
	if (is_light_theme ()) {
		CairoSetSouerceRGBA(c_wht);
	} else {
		CairoSetSouerceRGBA(c_g30);
	}

	GRID_DB(0, "0");

	for (int i = numgrid; i < ui->ydBrange; i += numgrid) {
		if (ui->ydBrange - i < numspace) {
			GRID_DB_DOT(i);
			GRID_DB_DOT(-i);
			break;
		}
		char txt[16];
		snprintf (txt, sizeof(txt), "%+d", i);
		GRID_DB(i, txt);
		snprintf (txt, sizeof(txt), "%+d", -i);
		GRID_DB(-i, txt);
	}

	// outer limits
	{
		char txt[8];
		snprintf (txt, 8, "%+.0f", ui->ydBrange);
		GRID_DB(ui->ydBrange, txt);
		snprintf (txt, 8, "%+.0f", -ui->ydBrange);
		GRID_DB(-ui->ydBrange, txt);
	}

	if (is_light_theme ()) {
		CairoSetSouerceRGBA(c_wht);
	} else {
		CairoSetSouerceRGBA(c_g30);
	}

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

	/* piano keyboard */
	int py0 = ui->m0_y1 + PK_YOFFS;
	const double semitone_width = ceil (ui->m0_xw * logf(2.0) / logf (1000.0) / 12.f);

	cairo_save (cr);
	const float x20  = x_at_freq (20, ui->m0_xw) - (BOXRADIUS / 2.f);
	const float x20k = x_at_freq (20000, ui->m0_xw) + (BOXRADIUS / 2.f);
	cairo_rectangle (cr, x0 + x20, py0, x20k - x20, PK_WHITE);
	cairo_clip (cr);
	cairo_set_line_width(cr, 1.0);

	for (int note = 2; note < 149; ++note) {
		const size_t n = note % 12;
		double k0, kw;
		switch (n) {
			case 0:
			case 5:
				k0 = .5 * semitone_width;
				kw = semitone_width * 1.5;
				break;
			case 2:
			case 7:
			case 9:
				k0 = semitone_width;
				kw = semitone_width * 2.0;
				break;
			case 4:
			case 11:
				k0 = semitone_width;
				kw = semitone_width * 1.5;
				break;
			default:
				/* black-key */
				continue;
		}

		const float fq = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
		const float xx = x_at_freq (fq, ui->m0_xw) - .5f;
		cairo_rectangle (cr, round (x0 + xx - k0) - .5, py0, kw, PK_WHITE);
		if (note < 21 || note > 108) {
			/* outside default piano range, draw inverted color */
			cairo_set_source_rgba (cr, 0.4, 0.4, 0.4, 1.0);
		} else {
			cairo_set_source_rgba (cr, 0.7, 0.7, 0.7, 1.0);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
		cairo_stroke (cr);
	}

	/* draw black keys on top */
	for (int note = 2; note < 149; ++note) {
		const size_t n = note % 12;
		if (n == 0 || n == 2 || n == 4 || n == 5 || n == 7 || n == 9 || n == 11) {
			continue; // white-key
		}
		const float fq = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
		const float xx = x_at_freq (fq, ui->m0_xw) - .5f;
		cairo_rectangle (cr, round (x0 + xx - semitone_width * .5) - .5, py0, semitone_width, PK_BLACK);
		if (note < 21 || note > 108) {
			/* outside default piano range, draw inverted color */
			cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 1.0);
		} else {
			cairo_set_source_rgba (cr, 0.3, 0.3, 0.3, 1.0);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
		cairo_stroke (cr);
	}
	cairo_restore (cr);

	char tune[16];
	snprintf (tune, sizeof (tune), "A:%.0f", ui->tuning_fq);
	write_text_full (cr, tune, ui->font[0], x0 - 20, py0 + PK_WHITE, M_PI * -.5, 9, c_ann);


	write_text_full (cr,
			ui->nfo ? ui->nfo : "x42 fil4.LV2",
			ui->font[0], x1 + 2, ui->m0_y0, 1.5 * M_PI, 7, c_g30);

	cairo_destroy (cr);
}

static void start_solo (Fil4UI* ui) {
	// save solo state
	for (int i = 0; i < NCTRL; ++i) {
		ui->solo_state[i] = robtk_cbtn_get_active (ui->btn_enable[i]);
	}
	ui->solo_state [Ctrl_HPF] = robtk_ibtn_get_active (ui->btn_g_hipass);
	ui->solo_state [Ctrl_LPF] = robtk_ibtn_get_active (ui->btn_g_lopass);
	ui->solo_hplp [0] = robtk_dial_get_value (ui->spn_g_hifreq);
	ui->solo_hplp [1] = robtk_dial_get_value (ui->spn_g_lofreq);
	ui->solo_hplp [2] = robtk_dial_get_value (ui->spn_g_hiq);
	ui->solo_hplp [3] = robtk_dial_get_value (ui->spn_g_loq);

	ui->soloing = true;
	robtk_ibtn_set_sensitive (ui->btn_g_hipass, false);
	robtk_ibtn_set_sensitive (ui->btn_g_lopass, false);
	robtk_dial_set_sensitive (ui->spn_g_hifreq, false);
	robtk_dial_set_sensitive (ui->spn_g_lofreq, false);
	robtk_dial_set_sensitive (ui->spn_g_hiq, false);
	robtk_dial_set_sensitive (ui->spn_g_loq, false);
	for (int i = 0; i < NCTRL; ++i) {
		robtk_cbtn_set_sensitive (ui->btn_enable[i], false);
	}

	// start solo
	float hz = 0;
	for (int i = 0; i < NCTRL; ++i) {
		if (ui->dragging == i) {
			hz = dial_to_freq(&freqs[i], robtk_dial_get_value (ui->spn_freq[i]));
			robtk_cbtn_set_active (ui->btn_enable[i], true);
		} else {
			robtk_cbtn_set_active (ui->btn_enable[i], false);
		}
	}
	assert (hz != 0);
	robtk_dial_set_value (ui->spn_g_hiq, hplp_to_dial (1.0));
	robtk_dial_set_value (ui->spn_g_loq, hplp_to_dial (1.0));

	robtk_dial_set_value (ui->spn_g_hifreq, freq_to_dial (&lphp[0], hz));
	robtk_dial_set_value (ui->spn_g_lofreq, freq_to_dial (&lphp[1], hz));
	if (ui->dragging > 0) {
		robtk_ibtn_set_active (ui->btn_g_hipass, true);
	} else {
		robtk_ibtn_set_active (ui->btn_g_hipass, false);
	}
	if (ui->dragging < NCTRL - 1) {
		robtk_ibtn_set_active (ui->btn_g_lopass, true);
	} else {
		robtk_ibtn_set_active (ui->btn_g_lopass, false);
	}
}

static void end_solo (Fil4UI* ui) {
	if (!ui->soloing) {
		return;
	}
	robtk_ibtn_set_sensitive (ui->btn_g_hipass, true);
	robtk_ibtn_set_sensitive (ui->btn_g_lopass, true);
	robtk_dial_set_sensitive (ui->spn_g_hifreq, true);
	robtk_dial_set_sensitive (ui->spn_g_lofreq, true);
	robtk_dial_set_sensitive (ui->spn_g_hiq, true);
	robtk_dial_set_sensitive (ui->spn_g_loq, true);
	for (int i = 0; i < NCTRL; ++i) {
		robtk_cbtn_set_sensitive (ui->btn_enable[i], true);
	}

	for (int i = 0; i < NCTRL; ++i) {
		robtk_cbtn_set_active (ui->btn_enable[i], ui->solo_state[i]);
	}
	robtk_ibtn_set_active (ui->btn_g_lopass, ui->solo_state [Ctrl_LPF]);
	robtk_ibtn_set_active (ui->btn_g_hipass, ui->solo_state [Ctrl_HPF]);

	robtk_dial_set_value (ui->spn_g_hifreq, ui->solo_hplp [0]);
	robtk_dial_set_value (ui->spn_g_lofreq, ui->solo_hplp [1]);
	robtk_dial_set_value (ui->spn_g_hiq, ui->solo_hplp [2]);
	robtk_dial_set_value (ui->spn_g_loq, ui->solo_hplp [3]);

	ui->soloing = false;
}

/* callbacks and iteraction related to graph */

static void
m0_size_request (RobWidget* handle, int *w, int *h) {
	*w = 600;
	*h = 240;
}

static void
m0_size_allocate (RobWidget* handle, int w, int h) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	ui->m0_width = w;
	ui->m0_height = h;
	robwidget_set_size(ui->m0, w, h);

	update_grid (ui);

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
	ui->m0_ym = rintf((m0h - 10 - PK_WHITE) * .5f) - .5;
	ui->m0_yr = (m0h - 34 - PK_WHITE) / ceilf(2 * ui->ydBrange);
	ui->m0_y0 = floor (ui->m0_ym - ui->ydBrange * ui->m0_yr);
	ui->m0_y1 = ceil  (ui->m0_ym + ui->ydBrange * ui->m0_yr);

	//printf("Y0 %.1f YM %.1f Y1 %.1f YR %.3f\n", ui->m0_y0, ui->m0_ym, ui->m0_y1, ui->m0_yr);

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
	update_filter_display (ui);
}

static void y_axis_zoom (RobWidget* handle, float dbRange) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);

	if (dbRange >= 50) dbRange = 50;
	if (dbRange <= 12) dbRange = 12;
	if (ui->ydBrange == dbRange) {
		return;
	}
	ui->fft_change = true;
	ui->ydBrange = dbRange;
	m0_size_allocate (handle, ui->m0_width, ui->m0_height);

	if (ui->disable_signals) return;
	tx_state (ui);
}

static void piano_tuning (Fil4UI* ui, float tuning) {
	if (tuning < 220 || tuning > 880) {
		return;
	}
	ui->tuning_fq = tuning;
	update_grid (ui);

	if (ui->disable_signals) return;
	tx_state (ui);
}

static void maybe_snap_rtk (Fil4UI* ui, RobTkDial* fctl, FilterFreq const* ffq, int port_index) {
	const float freq = dial_to_freq (ffq, robtk_dial_get_value (fctl));
	const int note = rintf (12.f * log2f (freq / ui->tuning_fq) + 69.0);
	const float note_freq = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
	if (fabsf (freq - note_freq) < 0.05) {
		return;
	}
	if (note_freq < ffq->min ||  note_freq > ffq->max) {
		return;
	}
	if (ui->touch && port_index > 0) {
		ui->touch->touch (ui->touch->handle, port_index, true);
	}
	robtk_dial_set_value (fctl, freq_to_dial (ffq, note_freq));
	if (ui->touch && port_index > 0) {
		ui->touch->touch (ui->touch->handle, port_index, false);
	}
}

static void maybe_snap_ev (Fil4UI* ui, const int x, const int y) {
	/* snap all elements under the mouse on mouse-down */
	for (int i = 0; i < NCTRL; ++i) {
		if (!robtk_cbtn_get_active (ui->btn_enable[i])) {
			continue;
		}
		if (fabsf(x - ui->flt[i].x0) <= PK_RADIUS) {
			int port_index = (ui->dragging == i + Ctrl_Piano) ? -1 : IIR_LS_FREQ + i * 4;
			maybe_snap_rtk (ui, ui->spn_freq[i], &freqs[i], port_index);
		}
	}
	if (robtk_ibtn_get_active (ui->btn_g_hipass)) {
		if (fabsf(x - ui->hilo[0].x0) <= PK_RADIUS) {
			maybe_snap_rtk (ui, ui->spn_g_hifreq, &lphp[0], (ui->dragging == Ctrl_PHP) ? -1 : FIL_HIFREQ);
		}
	}
	if (robtk_ibtn_get_active (ui->btn_g_lopass)) {
		if (fabsf(x - ui->hilo[1].x0) <= PK_RADIUS) {
			maybe_snap_rtk (ui, ui->spn_g_lofreq, &lphp[1], (ui->dragging == Ctrl_PLP) ? -1 : FIL_LOFREQ);
		}
	}
}

static int find_control_point (Fil4UI* ui, const int x, const int y) {
	if (x > 8 && x < 29 && y > ui->m0_y0 && y < ui->m0_y1) {
		return Ctrl_Yaxis;
	}

	if (x > 8 && x < 29 && y > ui->m0_y1 + PK_YOFFS && y < ui->m0_y1 + PK_YOFFS + PK_WHITE) {
		return Ctrl_Tuning;
	}

	if (x > 30 && fabs (y - (ui->m0_y1 + PK_YOFFS + PK_WHITE / 2)) <= PK_RADIUS) {
		for (int i = 0; i < NCTRL; ++i) {
			if (!robtk_cbtn_get_active (ui->btn_enable[i])) {
				continue;
			}
			if (fabsf(x - ui->flt[i].x0) <= PK_RADIUS) {
				return Ctrl_Piano + i;
			}
		}
		if (robtk_ibtn_get_active (ui->btn_g_hipass)) {
			if (fabsf(x - ui->hilo[0].x0) <= PK_RADIUS) {
				return Ctrl_PHP;
			}
		}
		if (robtk_ibtn_get_active (ui->btn_g_lopass)) {
			if (fabsf(x - ui->hilo[1].x0) <= PK_RADIUS) {
				return Ctrl_PLP;
			}
		}
		return -1;
	}

	if (fabsf(y - ui->m0_ym) <= DOTRADIUS && fabsf(x - ui->hilo[0].x0) <= DOTRADIUS) {
		return Ctrl_HPF;
	}
	if (fabsf(y - ui->m0_ym) <= DOTRADIUS && fabsf(x - ui->hilo[1].x0) <= DOTRADIUS) {
		return Ctrl_LPF;
	}

	for (int i = 0; i < NCTRL; ++i) {
		if (fabsf(x - ui->flt[i].x0) <= DOTRADIUS && fabsf(y - ui->flt[i].y0) <= DOTRADIUS) {
			return i;
		}
	}
	return -1;
}

static RobWidget* m0_mouse_up (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	end_solo (ui);

	if (ui->dragging >= 0 && ui->dragging < NCTRL && ui->touch) {
		ui->touch->touch (ui->touch->handle, IIR_LS_FREQ + ui->dragging * 4, false);
		ui->touch->touch (ui->touch->handle, IIR_LS_GAIN + ui->dragging * 4, false);
	} else if (ui->dragging >= Ctrl_Piano && ui->dragging < Ctrl_PHP && ui->touch) {
		ui->touch->touch (ui->touch->handle, IIR_LS_FREQ + (ui->dragging - Ctrl_Piano) * 4, false);
	} else if (ui->dragging == Ctrl_LPF && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_LOFREQ, false);
	} else if (ui->dragging == Ctrl_HPF && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_HIFREQ, false);
	} else if (ui->dragging == Ctrl_PLP && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_LOFREQ, false);
	} else if (ui->dragging == Ctrl_PHP && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_HIFREQ, false);
	}

	ui->dragging = -1;
	update_filter_display (ui);
	return NULL;
}

static RobWidget* m0_mouse_scroll (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);

	RobTkDial *bwctl = NULL;

	int cp = find_control_point (ui, ev->x, ev->y);

	int port_index = -1;
	switch (cp) {
		case -1:
			return NULL;
			break;
		case Ctrl_Tuning:
			if (ev->direction == ROBTK_SCROLL_UP) {
				piano_tuning (ui, ui->tuning_fq + 1.0);
			} else {
				piano_tuning (ui, ui->tuning_fq - 1.0);
			}
			return NULL;
			break;
		case Ctrl_Yaxis:
			/* y-axis zoom in the header */
			if (ev->direction == ROBTK_SCROLL_UP) {
				y_axis_zoom (handle, ui->ydBrange + 1);
			}
			else if (ev->direction == ROBTK_SCROLL_DOWN) {
				y_axis_zoom (handle, ui->ydBrange - 1);
			}
			return NULL;
			break;
		case Ctrl_HPF:
			bwctl = ui->spn_g_hiq;
			port_index = FIL_HIQ;
			break;
		case Ctrl_LPF:
			bwctl = ui->spn_g_loq;
			port_index = FIL_LOQ;
			break;
		default:
			assert (cp >= 0);
			if (cp < NCTRL) {
				bwctl = ui->spn_bw[cp];
				port_index = IIR_LS_Q + cp * 4;
			}
			break;
	}

	if (!bwctl) {
		return NULL;
	}

	float v = robtk_dial_get_value (bwctl);
	const float delta = (ev->state & ROBTK_MOD_CTRL) ? bwctl->acc : bwctl->scroll_mult * bwctl->acc;

	if (port_index >= 0 && ui->touch) {
		ui->touch->touch (ui->touch->handle, port_index, true);
	}

	switch (ev->direction) {
		case ROBTK_SCROLL_RIGHT:
		case ROBTK_SCROLL_UP:
			v += delta;
			robtk_dial_set_value (bwctl, v);
			break;
		case ROBTK_SCROLL_LEFT:
		case ROBTK_SCROLL_DOWN:
			v -= delta;
			robtk_dial_set_value (bwctl, v);
			break;
		default:
			break;
	}

	if (port_index >= 0 && ui->touch) {
		ui->touch->touch (ui->touch->handle, port_index, false);
	}

	return NULL;
}

static RobWidget* m0_mouse_down (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	// TODO right-click -> toggle ??
	// double click -> en/disable band ??

	if (ui->dragging != -1) {
		return NULL;
	}

	int cp = find_control_point (ui, ev->x, ev->y);

	switch (cp) {
		case -1:
			return NULL;
			break;
		case Ctrl_Yaxis:
			if (ev->button == 3) {
				y_axis_zoom (handle, DEFAULT_YZOOM);
			}
			else if (ev->button == 1) {
				ui->dragging = Ctrl_Yaxis;
				ui->drag_y = ev->y;
				return handle;
			}
			return NULL;
			break;
		case Ctrl_Tuning:
			if (ev->button == 3) {
				piano_tuning (ui, 440);
			}
			return NULL;
			break;
		default:
#if 0
			if (ev->button == 3) {
				if (cp == Ctrl_LPF) {
					robtk_ibtn_set_active (ui->btn_g_lopass, !robtk_ibtn_get_active(ui->btn_g_lopass));
				}
				else if (cp == Ctrl_HPF) {
					robtk_ibtn_set_active (ui->btn_g_hipass, !robtk_ibtn_get_active(ui->btn_g_hipass));
				}
				else {
					assert(cp >= 0 && cp < NCTRL);
					robtk_cbtn_set_active (ui->btn_enable[cp], !robtk_cbtn_get_active(ui->btn_enable[cp]));
				}
				update_filter_display (ui);
				return NULL;
			}
#endif
			if (ev->button == 3 && cp < NCTRL) {
				ui->dragging = cp;
				start_solo (ui);
				break;
			}
			if (ev->button != 1) {
				return NULL;
			}
			update_filter_display (ui);
			ui->dragging = cp;
			break;
	}

	if (ev->state & ROBTK_MOD_SHIFT && ui->dragging == Ctrl_HPF) {
		robtk_dial_set_value (ui->spn_g_hifreq, ui->spn_g_hifreq->dfl);
		robtk_dial_set_value (ui->spn_g_hiq, ui->spn_g_hiq->dfl);
		ui->dragging = -1;
		update_filter_display (ui);
		return NULL;
	}

	if (ev->state & ROBTK_MOD_SHIFT && ui->dragging == Ctrl_LPF) {
		robtk_dial_set_value (ui->spn_g_lofreq, ui->spn_g_lofreq->dfl);
		robtk_dial_set_value (ui->spn_g_loq, ui->spn_g_loq->dfl);
		ui->dragging = -1;
		update_filter_display (ui);
		return NULL;
	}

	if (ev->state & ROBTK_MOD_SHIFT && ui->dragging >= 0 && ui->dragging < NCTRL) {
		// XXX dial needs an API for this
		robtk_dial_set_value (ui->spn_freq[ui->dragging], ui->spn_freq[ui->dragging]->dfl);
		robtk_dial_set_value (ui->spn_gain[ui->dragging], ui->spn_gain[ui->dragging]->dfl);
		robtk_dial_set_value (ui->spn_bw[ui->dragging], ui->spn_bw[ui->dragging]->dfl);
		ui->dragging = -1;
		update_filter_display (ui);
		return NULL;
	}

	if (ui->dragging >= 0 && ui->dragging < NCTRL && ui->touch) {
		ui->touch->touch (ui->touch->handle, IIR_LS_FREQ + ui->dragging * 4, true);
		ui->touch->touch (ui->touch->handle, IIR_LS_GAIN + ui->dragging * 4, true);
	} else if (ui->dragging == Ctrl_LPF && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_LOFREQ, true);
	} else if (ui->dragging == Ctrl_HPF && ui->touch) {
		ui->touch->touch (ui->touch->handle, FIL_HIFREQ, true);
	}

	if (ui->dragging >= 0 && ui->dragging >= Ctrl_Piano && ui->dragging <= Ctrl_PLP) {
		if (ui->touch) {
			switch (ui->dragging) {
				case Ctrl_PLP:
					ui->touch->touch (ui->touch->handle, FIL_LOFREQ, true);
					break;
				case Ctrl_PHP:
					ui->touch->touch (ui->touch->handle, FIL_HIFREQ, true);
					break;
				default:
					ui->touch->touch (ui->touch->handle, IIR_LS_FREQ + (ui->dragging - Ctrl_Piano) * 4, true);
			}
		}
		maybe_snap_ev (ui, ev->x, ev->y);
	}

	assert (ui->dragging >= 0);
	return handle;
}

static RobWidget* m0_mouse_move (RobWidget* handle, RobTkBtnEvent *ev) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);

	int hover = find_control_point (ui, ev->x, ev->y);
	if (hover != ui->hover) {
		ui->hover = hover;
		if (ui->dragging < 0) {
			update_filter_display (ui);
		}
	}

	if (ui->dragging < 0) return NULL;

	const float x0 = 30;
	const float x1 = x0 + ui->m0_xw;

#ifdef VISUAL_GAIN_OFFSET
	float g_gain = robtk_dial_get_value (ui->spn_g_gain);
#else
	float g_gain = 0;
#endif
	const int sect = ui->dragging;

	bool snap_to_note = false;
	RobTkDial *fctl = NULL;
	RobTkDial *gctl = NULL;
	FilterFreq const *ffq = NULL;

	if (sect == Ctrl_HPF) { //high pass special case
		fctl = ui->spn_g_hifreq;
		ffq = &lphp[0];
	} else if (sect == Ctrl_PHP) {
		fctl = ui->spn_g_hifreq;
		ffq = &lphp[0];
		snap_to_note = true;
	} else if (sect == Ctrl_LPF) { // low pass special case
		fctl = ui->spn_g_lofreq;
		ffq = &lphp[1];
	} else if (sect == Ctrl_PLP) {
		fctl = ui->spn_g_lofreq;
		ffq = &lphp[1];
		snap_to_note = true;
	} else if (sect < NCTRL) {
		fctl = ui->spn_freq[sect];
		gctl = ui->spn_gain[sect];
		ffq = &freqs[sect];
	} else if (sect < Ctrl_PHP) {
		assert (sect >= Ctrl_Piano);
		fctl = ui->spn_freq[sect - Ctrl_Piano];
		ffq = &freqs[sect - Ctrl_Piano];
		snap_to_note = true;
	} else if (sect == Ctrl_Yaxis) { // header y-zoom
		float delta = floor ((ui->drag_y - ev->y) / ui->m0_yr);
		if (delta != 0) {
			//if (ui->drag_y < ui->m0_ym) delta *= -1; // drag away from '0'
			y_axis_zoom (handle, ui->ydBrange + delta);
			ui->drag_y = ev->y;
		}
		return handle;
	} else {
		assert (0);
		return NULL;
	}

	if (ev->x < x0) {
		ev->x = x0;
	}
	if (ev->x > x1) {
		ev->x = x1;
	}

	if (fctl) {
		float hz = freq_at_x (ev->x - x0, ui->m0_xw);
		if (snap_to_note) {
			int note = rintf (12.f * log2f (hz / ui->tuning_fq) + 69.0);
			hz = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
			if (hz < ffq->min) {
				note = ceilf (12.f * log2f (ffq->min / ui->tuning_fq) + 69.0);
				hz = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
			}
			if (hz > ffq->max) {
				note = floorf (12.f * log2f (ffq->max / ui->tuning_fq) + 69.0);
				hz = ui->tuning_fq * powf (2.0, (note - 69.f) / 12.f);
			}
		}

		robtk_dial_set_value (fctl, freq_to_dial (ffq, hz));

		if (ui->soloing) {
			robtk_dial_set_value (ui->spn_g_hifreq, freq_to_dial (&lphp[0], hz));
			robtk_dial_set_value (ui->spn_g_lofreq, freq_to_dial (&lphp[1], hz));
		}
	}
	if (gctl) {
		const float db = (ui->m0_ym - ev->y) / ui->m0_yr;
		robtk_dial_set_value (gctl, db - g_gain);

		if (fabsf(robtk_dial_get_value(gctl)) + 1 >= ui->ydBrange) {
			y_axis_zoom (handle, ui->ydBrange + 1);
		}
	}
	return handle;
}

static void m0_leave_notify (RobWidget* handle) {
	Fil4UI* ui = (Fil4UI*)GET_HANDLE(handle);
	if (-1 != ui->hover) {
		ui->hover = -1;
		queue_draw(ui->m0);
	}
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
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

#ifdef VISUAL_GAIN_OFFSET
	float g_gain = robtk_dial_get_value (ui->spn_g_gain);
#else
	float g_gain = 0;
#endif
	if (!robtk_cbtn_get_active(ui->btn_g_enable)) {
		shade = 0.5;
	}
	const float xw = ui->m0_xw;
	const float ym = ui->m0_ym;
	const float yr = ui->m0_yr;
	const float x0 = 30;
	const float ny = x_at_freq(.5 * ui->samplerate, xw);

	/* draw dots for peaking EQ, boxes for shelves */
	if (is_light_theme ()) {
		cairo_set_operator (cr, CAIRO_OPERATOR_MULTIPLY);
	} else {
		cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	}
	cairo_set_line_width(cr, 1.0);
	for (int j = 0 ; j < NCTRL; ++j) {
		float fshade = shade;
		if (!robtk_cbtn_get_active(ui->btn_enable[j])) {
			fshade = .5;
		}
		const float fq = dial_to_freq(&freqs[j], robtk_dial_get_value (ui->spn_freq[j]));
		const float db = robtk_dial_get_value (ui->spn_gain[j]);

		const float xx = x_at_freq(fq, xw) - .5f;
		const float yy = rintf(ym + .5 - yr * (db + g_gain)) - .5;
		if (ui->dragging == j || (ui->dragging < 0 && ui->hover == j)) {
			cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], fshade);
		} else {
			cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], .6 * fshade);
		}
		if (j == 0 || j == NCTRL - 1) {
			cairo_rectangle (cr, xx - BOXRADIUS, yy - BOXRADIUS, 2 * BOXRADIUS, 2 * BOXRADIUS);
		} else {
			cairo_arc (cr, xx, yy, DOTRADIUS, 0, 2 * M_PI);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], .3 * fshade);
		cairo_stroke (cr);

		/* cache position (for drag) */
		ui->flt[j].x0 = x0 + xx;
		ui->flt[j].y0 = yy;
	}

	/* hi/low pass triangles */
	{
		const float xx = x_at_freq (ui->hilo[0].f, xw);
		cairo_move_to (cr, xx - .5            , ym + BOXRADIUS);
		cairo_line_to (cr, xx - .5 - BOXRADIUS, ym - BOXRADIUS);
		cairo_line_to (cr, xx - .5 + BOXRADIUS, ym - BOXRADIUS);
		cairo_close_path (cr);
		float fshade = shade;
		if (!robtk_ibtn_get_active(ui->btn_g_hipass)) {
			fshade = .5;
		}
		if (ui->dragging == Ctrl_HPF || (ui->dragging < 0 && ui->hover == Ctrl_HPF)) {
			cairo_set_source_rgba (cr, c_fil[NCTRL][0], c_fil[NCTRL][1], c_fil[NCTRL][2], fshade);
		} else {
			cairo_set_source_rgba (cr, c_fil[NCTRL][0], c_fil[NCTRL][1], c_fil[NCTRL][2], .6 * fshade);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, c_fil[NCTRL][0], c_fil[NCTRL][1], c_fil[NCTRL][2], .3 * fshade);
		cairo_stroke (cr);
		ui->hilo[0].x0 = x0 + xx;
	}

	{
		const float xx = x_at_freq (ui->hilo[1].f, xw);
		cairo_move_to (cr, xx - .5            , ym + BOXRADIUS);
		cairo_line_to (cr, xx - .5 - BOXRADIUS, ym - BOXRADIUS);
		cairo_line_to (cr, xx - .5 + BOXRADIUS, ym - BOXRADIUS);
		cairo_close_path (cr);
		float fshade = shade;
		if (!robtk_ibtn_get_active(ui->btn_g_lopass)) {
			fshade = .5;
		}
		if (ui->dragging == Ctrl_LPF || (ui->dragging < 0 && ui->hover == Ctrl_LPF)) {
			cairo_set_source_rgba (cr, c_fil[NCTRL+1][0], c_fil[NCTRL+1][1], c_fil[NCTRL+1][2], fshade);
		} else {
			cairo_set_source_rgba (cr, c_fil[NCTRL+1][0], c_fil[NCTRL+1][1], c_fil[NCTRL+1][2], .6 * fshade);
		}
		cairo_fill_preserve (cr);
		cairo_set_source_rgba (cr, c_fil[NCTRL+1][0], c_fil[NCTRL+1][1], c_fil[NCTRL+1][2], .3 * fshade);
		cairo_stroke (cr);
		ui->hilo[1].x0 = x0 + xx;
	}

	if (ny < xw) {
		cairo_rectangle (cr, 0, 0, ny, ui->m0_height);
		cairo_clip (cr);
	}

	/* draw filters , hi/lo first (only when dragging)*/

	if (is_light_theme ()) {
		cairo_set_operator (cr, CAIRO_OPERATOR_MULTIPLY);
	} else {
		cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	}
	cairo_set_line_width(cr, 1.0);

	{
		float fshade = shade;
		if (!robtk_ibtn_get_active(ui->btn_g_hipass)) {
			fshade = .5;
		}
		float yy = ym - yr * g_gain - yr * get_highpass_response (ui, freq_at_x(0, xw));
		cairo_move_to (cr, 0, yy);
		for (int i = 1 ; i < xw && i < ny; ++i) {
			const float xf = freq_at_x(i, xw);
			float y = yr * g_gain;
			y += yr * get_highpass_response (ui, xf);
			cairo_line_to (cr, i, ym - y);
		}
		cairo_set_source_rgba (cr, c_fil[NCTRL][0], c_fil[NCTRL][1], c_fil[NCTRL][2], fshade);
		if (ui->dragging == Ctrl_HPF) {
			cairo_stroke_preserve(cr);
			cairo_line_to (cr, xw, ym);
			cairo_line_to (cr, xw, ym + yr * ui->ydBrange);
			if (yy < ym + yr * ui->ydBrange) {
				cairo_line_to (cr, 0, ym + yr * ui->ydBrange);
			}
			cairo_set_source_rgba (cr, c_fil[NCTRL][0], c_fil[NCTRL][1], c_fil[NCTRL][2], .4 * fshade);
			cairo_fill (cr);
		} else {
			cairo_stroke(cr);
		}
	}
	{
		float fshade = shade;
		if (!robtk_ibtn_get_active(ui->btn_g_lopass)) {
			fshade = .5;
		}
		cairo_move_to (cr, 0, ym - yr * g_gain - yr * get_lowpass_response (ui, freq_at_x(0, xw)));
		for (int i = 1 ; i < xw && i < ny; ++i) {
			const float xf = freq_at_x(i, xw);
			float y = yr * g_gain;
			y += yr * get_lowpass_response (ui, xf);
			cairo_line_to (cr, i, ym - y);
		}
			cairo_set_source_rgba (cr, c_fil[NCTRL+1][0], c_fil[NCTRL+1][1], c_fil[NCTRL+1][2], fshade);
		if (ui->dragging == Ctrl_LPF) {
			cairo_stroke_preserve(cr);
			float yy = ym - yr * g_gain - yr * get_lowpass_response (ui, freq_at_x(xw, xw));
			if (yy < ym + yr * ui->ydBrange) {
				cairo_line_to (cr, xw, ym + yr * ui->ydBrange);
			}
			cairo_line_to (cr, 0, ym + yr * ui->ydBrange);
			cairo_line_to (cr, 0, ym);
			cairo_set_source_rgba (cr, c_fil[NCTRL+1][0], c_fil[NCTRL+1][1], c_fil[NCTRL+1][2], .4 * fshade);
			cairo_fill (cr);
		} else {
			cairo_stroke(cr);
		}
	}

	/* draw filters */
	for (int j = 0 ; j < NCTRL; ++j) {
		float fshade = shade;
		if (!robtk_cbtn_get_active(ui->btn_enable[j])) {
			fshade = .5;
		}

		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], fshade);

		for (int i = 0 ; i < xw && i < ny; ++i) {
			const float xf = freq_at_x(i, xw);
			float y = yr;
			if (j == 0) {
				y *= get_shelf_response (&ui->flt[j], xf);
			} else if (j == NCTRL -1) {
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
	if (is_light_theme ()) {
		CairoSetSouerceRGBA(c_g30);
	} else {
		CairoSetSouerceRGBA(c_g60);
	}
	cairo_move_to (cr, 0, ym - yr * g_gain);
	cairo_line_to (cr, xw -1 , ym - yr * g_gain);
	cairo_stroke(cr);

	/* draw total */
	cairo_set_line_width(cr, 2.0 * shade);
	if (is_light_theme ()) {
		cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, shade);
	} else {
		cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, shade);
	}
	for (int i = 0 ; i < xw && i < ny; ++i) {
		const float xf = freq_at_x(i, xw);
		float y = yr * g_gain;
		for (int j = 0 ; j < NCTRL; ++j) {
			if (!robtk_cbtn_get_active(ui->btn_enable[j])) continue;
			if (j == 0) {
				y += yr * get_shelf_response (&ui->flt[j], xf);
			} else if (j == NCTRL -1) {
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
			// TODO optimize '0'/moveto out of the loop
			cairo_move_to (cr, i, ym - y);
		} else {
			cairo_line_to (cr, i, ym - y);
		}
	}

	if (is_light_theme ()) {
		cairo_set_operator (cr, CAIRO_OPERATOR_MULTIPLY);
	} else {
		cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	}
	cairo_stroke_preserve(cr);

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

	rounded_rectangle (cr, 4, 4, ui->m0_width - 8 , ui->m0_height - 8, 9);

	if (is_light_theme ()) {
		CairoSetSouerceRGBA (c_g80);
	} else {
		CairoSetSouerceRGBA (c_blk);
	}
	cairo_fill (cr);

	const float xw = ui->m0_xw;
	const float ym = ui->m0_ym;
	const float yr = ui->m0_yr;
	const float x0 = 30;
	const float yp = ui->m0_y1 + PK_YOFFS + PK_WHITE / 2;

	if (!ui->m0_grid) {
		draw_grid (ui);
	}

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_surface(cr, ui->m0_grid, 0, 0);
	cairo_paint (cr);

	if (ui->dragging == Ctrl_Yaxis || (ui->dragging < 0 && ui->hover == Ctrl_Yaxis)) {
		rounded_rectangle (cr, 7, ui->m0_y0 - 4, 20, 9 + ui->m0_y1 - ui->m0_y0, 2);
		cairo_set_source_rgba (cr, 1, 1, 1, .25);
		cairo_fill (cr);
	}

	if ((ui->dragging < 0 && ui->hover == Ctrl_Tuning)) {
		rounded_rectangle (cr, 7, ui->m0_y1 + PK_YOFFS, 20, PK_WHITE, 2);
		cairo_set_source_rgba (cr, 1, 1, 1, .25);
		cairo_fill (cr);
	}

	const int fft_mode = robtk_select_get_value(ui->sel_fft);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

	if (fft_mode > 0) {
		update_fft_scale (ui);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_surface(cr, ui->fft_scale, x0 + xw, 0);
		cairo_paint (cr);
	}

	/* draw indicators on top of piano */
	cairo_save (cr);
	cairo_rectangle (cr, x0 - BOXRADIUS / 2, ui->m0_y1 + PK_YOFFS, xw + BOXRADIUS, PK_WHITE);
	cairo_clip(cr);

	cairo_set_line_width(cr, 1.0);
	for (int j = 0 ; j < NCTRL; ++j) {
		if (!robtk_cbtn_get_active(ui->btn_enable[j])) {
			continue;
		}
		const float fq = dial_to_freq(&freqs[j], robtk_dial_get_value (ui->spn_freq[j]));
		const float xx = x0 + x_at_freq(fq, xw);
		if (j == 0 || j == NCTRL - 1) {
			cairo_rectangle (cr, xx - PK_RADIUS, yp - PK_RADIUS, 2 * PK_RADIUS, 2 * PK_RADIUS);
		} else {
			cairo_arc (cr, xx, yp, PK_RADIUS, 0, 2 * M_PI);
		}
		if (ui->dragging == j + Ctrl_Piano || (ui->dragging < 0 && ui->hover == j + Ctrl_Piano)) {
			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		} else {
			cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
		}
		cairo_set_source_rgba (cr, c_fil[j][0], c_fil[j][1], c_fil[j][2], .8);
		cairo_fill_preserve (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		if (ui->dragging == j + Ctrl_Piano) {
			cairo_set_source_rgba (cr, 1, 1, 1, 1.0);
		} else {
			cairo_set_source_rgba (cr, 0, 0, 0, 1.0);
		}
		cairo_stroke (cr);
	}
	/* Hi/Lo */
	for (int j = 0 ; j < 2; ++j) {
		if (!robtk_ibtn_get_active(j == 1 ? ui->btn_g_lopass : ui->btn_g_hipass)) {
			continue;
		}
		const float xx = x0 + x_at_freq (ui->hilo[j].f, xw);
		cairo_move_to (cr, xx            , yp + PK_RADIUS);
		cairo_line_to (cr, xx - PK_RADIUS, yp - PK_RADIUS);
		cairo_line_to (cr, xx + PK_RADIUS, yp - PK_RADIUS);
		cairo_close_path (cr);
		if (ui->dragging == j + Ctrl_PHP || (ui->dragging < 0 && ui->hover == j + Ctrl_PHP)) {
			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		} else {
			cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
		}
		cairo_set_source_rgba (cr, c_fil[NCTRL + j][0], c_fil[NCTRL + j][1], c_fil[NCTRL + j][2], .8);
		cairo_fill_preserve (cr);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		if (ui->dragging == j + Ctrl_PHP) {
			cairo_set_source_rgba (cr, 1, 1, 1, 1.0);
		} else {
			cairo_set_source_rgba (cr, 0, 0, 0, 1.0);
		}
		cairo_stroke (cr);
	}
	cairo_restore (cr);

	cairo_rectangle (cr, x0, ui->m0_y0, xw, ui->m0_y1 - ui->m0_y0);
	cairo_clip (cr);


	if (fft_mode == 3 &&  ui->fft_hist_line >= 0) {
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
	else if (fft_mode > 0 && fft_mode < 3) {
		cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
		cairo_set_line_width(cr, 1.0);
		if (is_light_theme ()) {
			if (robtk_select_get_value(ui->sel_pos)) {
				cairo_set_source_rgba (cr, .1, .2, .5, .75);
			} else {
				cairo_set_source_rgba (cr, .5, .2, .1, .75);
			}
		} else {
			if (robtk_select_get_value(ui->sel_pos)) {
				cairo_set_source_rgba (cr, .5, .6, .7, .75);
			} else {
				cairo_set_source_rgba (cr, .7, .6, .5, .75);
			}
		}
		float *d = ui->japa->power ()->_data;
		if (!ui->scale_cached) {
			ui->scale_cached = true;
			for (int i = 0; i <= FFT_MAX; ++i) {
				ui->xscale[i] = x0 + x_at_freq(ui->_fscale[i] * ui->samplerate, xw) - .5;
			}
		}
		const float align = DEFAULT_YZOOM + robtk_dial_get_value (ui->spn_fftgain);
		if (fft_mode == 2) {
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

	if (is_light_theme ()) {
		cairo_set_operator (cr, CAIRO_OPERATOR_MULTIPLY);
	} else {
		cairo_set_operator (cr, CAIRO_OPERATOR_ADD);
	}

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
	robwidget_toplevel_enable_scaling (ui->rw);

	ui->font[0] = pango_font_description_from_string("Mono 9px");
	ui->font[1] = pango_font_description_from_string("Mono 10px");

	if (is_light_theme ()) {
		c_dlf[0] = c_dlf[1] = c_dlf[2] = 0.2;
	}

	prepare_faceplates (ui);

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
	robwidget_set_leave_notify (ui->m0, m0_leave_notify);

	ui->ctbl = rob_table_new (/*rows*/7, /*cols*/ 2 * NCTRL + 8, FALSE);

#define GBT_W(PTR) robtk_cbtn_widget(PTR)
#define GBI_W(PTR) robtk_ibtn_widget(PTR)
#define GSP_W(PTR) robtk_dial_widget(PTR)
#define GLB_W(PTR) robtk_lbl_widget(PTR)
#define GSL_W(PTR) robtk_select_widget(PTR)
#define GBP_W(PTR) robtk_pbtn_widget(PTR)

	int col = 0;

	/* Global section, far left*/
	ui->btn_g_enable = robtk_cbtn_new ("Enable", GBT_LED_LEFT, false);
	ui->spn_g_gain   = robtk_dial_new_with_size (-18, 18, .2,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	ui->lbl_g_gain  = robtk_lbl_new ("Output");
	ui->lbl_peak    = robtk_lbl_new ("Peak:");
	ui->btn_peak    = robtk_pbtn_new_with_colors ("-8888.8 dBFS\n", c_g20, c_wht);

	robtk_dial_annotation_callback(ui->spn_g_gain, dial_annotation_db, ui);
	robtk_cbtn_set_callback (ui->btn_g_enable, cb_btn_g_en, ui);
	robtk_dial_set_callback (ui->spn_g_gain,   cb_spn_g_gain, ui);
	robtk_pbtn_set_callback (ui->btn_peak,     cb_peak_rest, ui);

	if (ui->touch) {
		robtk_dial_set_touch (ui->spn_g_gain, ui->touch->touch, ui->touch->handle, FIL_GAIN);
	}

	robtk_cbtn_set_temporary_mode (ui->btn_g_enable, 1);
	robtk_cbtn_set_color_on(ui->btn_g_enable,  1.0, 1.0, 1.0);
	robtk_cbtn_set_color_off(ui->btn_g_enable, .2, .2, .2);
	robtk_pbtn_set_alignment(ui->btn_peak, .5, .5);

	robtk_dial_set_default(ui->spn_g_gain, 0.0);
	robtk_dial_set_scaled_surface_scale (ui->spn_g_gain, ui->dial_bg[0], 2.0);
	robtk_dial_set_detent_default (ui->spn_g_gain, true);
	robtk_dial_set_scroll_mult (ui->spn_g_gain, 5.f);

	rob_table_attach (ui->ctbl, GBT_W(ui->btn_g_enable), col, col+1, 0, 1, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_gain),   col, col+1, 1, 3, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GLB_W(ui->lbl_g_gain),   col, col+1, 3, 5, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GLB_W(ui->lbl_peak),     col, col+1, 5, 6, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GBP_W(ui->btn_peak),     col, col+1, 6, 7, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* separators */
	for (int i = 0; i < 4; ++i) {
		ui->sep_v[i] = robtk_sep_new(FALSE);
	}

	robtk_sep_set_dash (ui->sep_v[1], 2, 0);
	robtk_sep_set_dash (ui->sep_v[2], 2, 0);

	/* separator */
	++col;
	rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_v[0]), col, col+1, 0, 7);

	/* HPF & LPF */
	++col;
	ui->btn_g_hipass = robtk_ibtn_new (ui->hpf_btn[0], ui->hpf_btn[1]);
	ui->btn_g_lopass = robtk_ibtn_new (ui->lpf_btn[0], ui->lpf_btn[1]);
	ui->lbl_hilo[0]  = robtk_lbl_new ("XXXX Hz");
	ui->lbl_hilo[1]  = robtk_lbl_new ("XXXX Hz");

	robtk_ibtn_set_temporary_mode (ui->btn_g_hipass, 1);
	robtk_ibtn_set_temporary_mode (ui->btn_g_lopass, 1);
	robtk_ibtn_set_alignment(ui->btn_g_hipass, .5, 0);
	robtk_ibtn_set_alignment(ui->btn_g_lopass, .5, 0);

	robtk_lbl_set_alignment(ui->lbl_hilo[0], .5, 0);
	robtk_lbl_set_alignment(ui->lbl_hilo[1], .5, 0);

	ui->spn_g_hifreq = robtk_dial_new_with_size (0, 1, 1./160.,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
	ui->spn_g_lofreq = robtk_dial_new_with_size (0, 1, 1./160.,
			GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);

	ui->spn_g_hiq = robtk_dial_new_with_size (0, 1.0, 1./100.,
			GED_WIDTH, GED_HEIGHT + 4, GED_CX, GED_CY + 3, GED_RADIUS);
	ui->spn_g_loq = robtk_dial_new_with_size (0, 1.0, 1./100.,
			GED_WIDTH, GED_HEIGHT + 4, GED_CX, GED_CY + 3, GED_RADIUS);

	robtk_ibtn_set_callback (ui->btn_g_hipass, cb_btn_g_hi, ui);
	robtk_ibtn_set_callback (ui->btn_g_lopass, cb_btn_g_lo, ui);
	robtk_dial_set_callback (ui->spn_g_hifreq, cb_spn_g_hifreq, ui);
	robtk_dial_set_callback (ui->spn_g_lofreq, cb_spn_g_lofreq, ui);
	robtk_dial_set_callback (ui->spn_g_hiq, cb_spn_g_hiq, ui);
	robtk_dial_set_callback (ui->spn_g_loq, cb_spn_g_loq, ui);

	robtk_dial_annotation_callback(ui->spn_g_hifreq, dial_annotation_hifreq, ui);
	robtk_dial_annotation_callback(ui->spn_g_lofreq, dial_annotation_lofreq, ui);

	if (ui->touch) {
		robtk_dial_set_touch (ui->spn_g_hifreq, ui->touch->touch, ui->touch->handle, FIL_HIFREQ);
		robtk_dial_set_touch (ui->spn_g_lofreq, ui->touch->touch, ui->touch->handle, FIL_LOFREQ);
		robtk_dial_set_touch (ui->spn_g_hiq, ui->touch->touch, ui->touch->handle, FIL_HIQ);
		robtk_dial_set_touch (ui->spn_g_loq, ui->touch->touch, ui->touch->handle, FIL_LOQ);
	}

	/* trigger update of hi/lo labels */
	ui->disable_signals = true;
	robtk_dial_set_value (ui->spn_g_hifreq, freq_to_dial (&lphp[0], lphp[0].dflt));
	robtk_dial_set_value (ui->spn_g_lofreq, freq_to_dial (&lphp[1], lphp[1].dflt));
	robtk_dial_set_value (ui->spn_g_hiq, hplp_to_dial(.7));
	robtk_dial_set_value (ui->spn_g_loq, hplp_to_dial(1.0));
	ui->disable_signals = false;

	robtk_dial_set_constained (ui->spn_g_hifreq, false);
	robtk_dial_set_constained (ui->spn_g_lofreq, false);
	robtk_dial_set_default(ui->spn_g_hifreq, freq_to_dial (&lphp[0], lphp[0].dflt));
	robtk_dial_set_default(ui->spn_g_lofreq, freq_to_dial (&lphp[1], lphp[1].dflt));
	robtk_dial_set_default(ui->spn_g_hiq, hplp_to_dial(.7));
	robtk_dial_set_default(ui->spn_g_loq, hplp_to_dial(1.0));

	robtk_dial_set_scroll_mult (ui->spn_g_hifreq, 4.f);
	robtk_dial_set_scroll_mult (ui->spn_g_lofreq, 4.f);
	robtk_dial_set_scroll_mult (ui->spn_g_hiq, 5.f);
	robtk_dial_set_scroll_mult (ui->spn_g_loq, 5.f);

	robtk_dial_set_scaled_surface_scale (ui->spn_g_hifreq, ui->dial_hplp[0], 2.0);
	robtk_dial_set_scaled_surface_scale (ui->spn_g_lofreq, ui->dial_hplp[1], 2.0);
	robtk_dial_set_scaled_surface_scale (ui->spn_g_hiq, ui->dial_hplp[2], 2.0);
	robtk_dial_set_scaled_surface_scale (ui->spn_g_loq, ui->dial_hplp[3], 2.0);

	/* HPF on the left side */
	rob_table_attach (ui->ctbl, GBI_W(ui->btn_g_hipass), col, col+1, 0, 2, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GLB_W(ui->lbl_hilo[0]),  col, col+1, 2, 3, 5, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_hiq),    col, col+1, 3, 5, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_hifreq), col, col+1, 5, 7, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* LPF at the far right */
	rob_table_attach (ui->ctbl, GBI_W(ui->btn_g_lopass), col + NCTRL + 4, col + NCTRL + 5, 0, 2, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GLB_W(ui->lbl_hilo[1]),  col + NCTRL + 4, col + NCTRL + 5, 2, 3, 5, 0, RTK_EXANDF, RTK_EXANDF);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_loq),    col + NCTRL + 4, col + NCTRL + 5, 3, 5, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, GSP_W(ui->spn_g_lofreq), col + NCTRL + 4, col + NCTRL + 5, 5, 7, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* Filter bands */
	++col;
	for (int i = 0; i < NCTRL; ++i, ++col) {
		if (i == NCTRL - 1) {
			rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_v[1]), col, col+1, 1, 7);
			++col;
		}
		ui->btn_enable[i] = robtk_cbtn_new("88.8KHz", GBT_LED_LEFT, false);

		ui->spn_freq[i] = robtk_dial_new_with_size (0, 1, .00625,
				GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->spn_gain[i] = robtk_dial_new_with_size (-18, 18, .2, // .2 dB steps
				GED_WIDTH + 12, GED_HEIGHT + 20, GED_CX + 6, GED_CY + 15, GED_RADIUS);
		ui->spn_bw[i]   = robtk_dial_new_with_size (0, 1.0, 1./90,
				GED_WIDTH, GED_HEIGHT + 4, GED_CX, GED_CY + 3, GED_RADIUS);

		rob_table_attach (ui->ctbl, GBT_W(ui->btn_enable[i]), col, col+1, 0, 1, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_gain[i]),   col, col+1, 1, 3, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_bw[i]),     col, col+1, 3, 5, 0, 0, RTK_EXANDF, RTK_SHRINK);
		rob_table_attach (ui->ctbl, GSP_W(ui->spn_freq[i]),   col, col+1, 5, 7, 0, 0, RTK_EXANDF, RTK_SHRINK);

		robtk_dial_annotation_callback(ui->spn_gain[i], dial_annotation_db, ui);
		if (i > 0 && i < NCTRL - 1) {
			/* band's bandwidth */
			robtk_dial_annotation_callback(ui->spn_bw[i], dial_annotation_bw, ui);
		} else {
			robtk_dial_annotation_callback(ui->spn_bw[i], dial_annotation_q, ui);
		}

		robtk_dial_annotation_callback(ui->spn_freq[i], dial_annotation_fq, ui);

		robtk_dial_set_constained (ui->spn_freq[i], false);
		robtk_dial_set_default(ui->spn_freq[i], freq_to_dial (&freqs[i], freqs[i].dflt));
		robtk_dial_set_default(ui->spn_gain[i], 0.0);
		robtk_dial_set_default(ui->spn_bw[i], bw_to_dial(0.5));

		robtk_cbtn_set_callback (ui->btn_enable[i], cb_btn_en, ui);
		robtk_dial_set_callback (ui->spn_freq[i],   cb_spn_freq, ui);
		robtk_dial_set_callback (ui->spn_bw[i],     cb_spn_bw, ui);
		robtk_dial_set_callback (ui->spn_gain[i],   cb_spn_gain, ui);

		if (ui->touch) {
			robtk_cbtn_set_touch (ui->btn_enable[i], ui->touch->touch, ui->touch->handle, IIR_LS_EN + i * 4);
			robtk_dial_set_touch (ui->spn_freq[i], ui->touch->touch, ui->touch->handle, IIR_LS_FREQ + i * 4);
			robtk_dial_set_touch (ui->spn_gain[i], ui->touch->touch, ui->touch->handle, IIR_LS_GAIN + i * 4);
			robtk_dial_set_touch (ui->spn_bw[i], ui->touch->touch, ui->touch->handle, IIR_LS_Q + i * 4);
		}

		robtk_dial_set_alignment (ui->spn_freq[i], 0.0, .5);
		robtk_dial_set_alignment (ui->spn_bw[i], 1.0, .5);
		robtk_dial_set_alignment (ui->spn_gain[i], 0.0, .5);

		robtk_cbtn_set_color_on (ui->btn_enable[i],  c_fil[i][0], c_fil[i][1], c_fil[i][2]);
		robtk_cbtn_set_color_off (ui->btn_enable[i], c_fil[i][0] * .3, c_fil[i][1] * .3, c_fil[i][2] * .3);

		robtk_dial_set_scaled_surface_scale (ui->spn_gain[i], ui->dial_bg[0], 2.0);
		robtk_dial_set_scaled_surface_scale (ui->spn_freq[i], ui->dial_fq[i], 2.0);
		if (i == 0) {
			robtk_dial_set_scaled_surface_scale (ui->spn_bw[i],   ui->dial_bg[2], 2.0);
		} else if (i == NCTRL -1) {
			robtk_dial_set_scaled_surface_scale (ui->spn_bw[i],   ui->dial_bg[3], 2.0);
		} else {
			robtk_dial_set_scaled_surface_scale (ui->spn_bw[i],   ui->dial_bg[1], 2.0);
		}

		robtk_cbtn_set_temporary_mode (ui->btn_enable[i], 1);
		robtk_dial_set_detent_default (ui->spn_gain[i], true);
		robtk_dial_set_scroll_mult (ui->spn_freq[i], 4.f); // 24 clicks per octave
		robtk_dial_set_scroll_mult (ui->spn_gain[i], 5.f); // 1dB per click
		robtk_dial_set_scroll_mult (ui->spn_bw[i],   5.f); // 1/3 octave per click

		if (i == 0) {
			++col;
			rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_v[2]), col, col+1, 1, 7);
		}
	}

	/* shelf filter range */
	robtk_dial_update_range (ui->spn_bw[0], 0, 1, 1 / 90.f); // 3 clicks for 1:2
	robtk_dial_update_range (ui->spn_bw[NCTRL - 1], 0, 1, 1 / 100.f);
	robtk_dial_update_range (ui->spn_freq[0], 0, 1, 1 / 80.f); // 6 per octave
	robtk_dial_update_range (ui->spn_freq[NCTRL - 1], 0, 1, 1 / 80.f);
	robtk_dial_set_default(ui->spn_bw[0], bw_to_dial(1.00));
	robtk_dial_set_default(ui->spn_bw[NCTRL - 1], bw_to_dial(1.00));


	/* spectrum analysis */
	++col; // LPF
	++col;
	rob_table_attach_defaults (ui->ctbl, robtk_sep_widget(ui->sep_v[3]), col, col+1, 0, 7);
	++col;

	ui->lbl_fft = robtk_lbl_new ("Spectrum");
	robtk_lbl_set_alignment (ui->lbl_fft, .5, 0);

	ui->spn_fftgain  = robtk_dial_new_with_size (0, 60, 1.0,
				GED_WIDTH, GED_HEIGHT + 4, GED_CX, GED_CY + 3, GED_RADIUS);
	robtk_dial_set_scaled_surface_scale (ui->spn_fftgain, ui->dial_bg[4], 2.0);
	robtk_dial_set_value (ui->spn_fftgain, 0);
	robtk_dial_set_sensitive (ui->spn_fftgain, false);
	robtk_dial_set_callback (ui->spn_fftgain,  cb_fft_change, ui);
	robtk_dial_annotation_callback(ui->spn_fftgain, dial_annotation_db, ui);
	robtk_dial_set_default(ui->spn_fftgain, 0.0);
	robtk_dial_set_detent_default (ui->spn_fftgain, true);

	ui->sel_fft = robtk_select_new();
	robtk_select_add_item (ui->sel_fft, 0, "Off");  // 0x0
	//robtk_select_add_item (ui->sel_fft, 1, "Flat"); // 0x2
	//robtk_select_add_item (ui->sel_fft, 2, "Prop"); // 0x4
	robtk_select_add_item (ui->sel_fft, 2, "Spec"); // 0x4
	robtk_select_add_item (ui->sel_fft, 3, "Hist"); // 0x6

	robtk_select_set_default_item (ui->sel_fft, 0);
	robtk_select_set_value (ui->sel_fft, 0);
	robtk_select_set_callback(ui->sel_fft, cb_set_fft, ui);

	ui->sel_chn = robtk_select_new();
	robtk_select_add_item (ui->sel_chn, -1, "All");
	robtk_select_set_default_item (ui->sel_chn, 0);
	robtk_select_set_value (ui->sel_chn, -1);
	robtk_select_set_callback(ui->sel_chn, cb_fft_change, ui);

	if (ui->n_channels == 2) {
		robtk_select_add_item (ui->sel_chn, 0, "L");
		robtk_select_add_item (ui->sel_chn, 1, "R");
	}

	ui->sel_pos = robtk_select_new();
	robtk_select_add_item (ui->sel_pos, 0, "Pre");  // sel_fft | 0
	robtk_select_add_item (ui->sel_pos, 1, "Post"); // sel_fft | 1
	robtk_select_set_default_item (ui->sel_pos, 1);
	robtk_select_set_value (ui->sel_pos, 1);
	robtk_select_set_callback(ui->sel_pos, cb_fft_change, ui);

	ui->sel_spd = robtk_select_new(); // sel_fft << 8
	robtk_select_add_item (ui->sel_spd, 0, "Rpd."); // 0x000
	robtk_select_add_item (ui->sel_spd, 1, "Fast"); // 0x100
	robtk_select_add_item (ui->sel_spd, 2, "Mod."); // 0x200
	robtk_select_add_item (ui->sel_spd, 3, "Slow"); // 0x300
	robtk_select_add_item (ui->sel_spd, 4, "Ns.");  // 0x400
	robtk_select_set_default_item (ui->sel_spd, 2);
	robtk_select_set_value (ui->sel_spd, 2);
	robtk_select_set_callback(ui->sel_spd, cb_japa, ui);
	robtk_select_set_sensitive (ui->sel_spd, false);

	ui->sel_res = robtk_select_new(); // sel_fft << 12
	robtk_select_add_item (ui->sel_res, 0, "Bark"); // 0x0000
	robtk_select_add_item (ui->sel_res, 1, "Med."); // 0x1000
	robtk_select_add_item (ui->sel_res, 2, "High"); // 0x2000
	robtk_select_set_default_item (ui->sel_res, 1);
	robtk_select_set_value (ui->sel_res, 1);
	robtk_select_set_callback(ui->sel_res, cb_japa, ui);
	robtk_select_set_sensitive (ui->sel_res, false);

	ui->spbox = rob_vbox_new (FALSE, 0);

	rob_vbox_child_pack(ui->spbox, GSL_W(ui->sel_fft), TRUE, FALSE);
	rob_vbox_child_pack(ui->spbox, GSL_W(ui->sel_pos), TRUE, FALSE);
	if (ui->n_channels > 1) {
		rob_vbox_child_pack(ui->spbox, GSL_W(ui->sel_chn), TRUE, FALSE);
	}
	rob_vbox_child_pack(ui->spbox, GSP_W(ui->spn_fftgain), TRUE, FALSE);
	rob_vbox_child_pack(ui->spbox, GSL_W(ui->sel_res), TRUE, FALSE);
	rob_vbox_child_pack(ui->spbox, GSL_W(ui->sel_spd), TRUE, FALSE);

	rob_table_attach (ui->ctbl, GLB_W(ui->lbl_fft), col, col+1, 0, 1, 5, 0, RTK_EXANDF, RTK_SHRINK);
	rob_table_attach (ui->ctbl, ui->spbox,          col, col+1, 1, 7, 5, 0, RTK_EXANDF, RTK_SHRINK);

	/* top-level packing */
	rob_vbox_child_pack(ui->rw, ui->m0, TRUE, TRUE);
	rob_vbox_child_pack(ui->rw, ui->ctbl, FALSE, TRUE);
	return ui->rw;
}

static void gui_cleanup(Fil4UI* ui) {
	for (int i = 0; i < NCTRL; ++i) {
		robtk_cbtn_destroy (ui->btn_enable[i]);
		robtk_dial_destroy (ui->spn_bw[i]);
		robtk_dial_destroy (ui->spn_gain[i]);
		robtk_dial_destroy (ui->spn_freq[i]);
		cairo_surface_destroy (ui->dial_fq[i]);
	}

	robtk_cbtn_destroy (ui->btn_g_enable);
	robtk_dial_destroy (ui->spn_g_gain);

	robtk_ibtn_destroy (ui->btn_g_hipass);
	robtk_dial_destroy (ui->spn_g_hifreq);
	robtk_dial_destroy (ui->spn_g_hiq);
	robtk_ibtn_destroy (ui->btn_g_lopass);
	robtk_dial_destroy (ui->spn_g_lofreq);
	robtk_dial_destroy (ui->spn_g_loq);

	robtk_dial_destroy (ui->spn_fftgain);
	robtk_select_destroy(ui->sel_fft);
	robtk_select_destroy(ui->sel_pos);
	robtk_select_destroy(ui->sel_chn);
	robtk_select_destroy(ui->sel_res);
	robtk_select_destroy(ui->sel_spd);

	for (int i = 0; i < 4; ++i) {
		robtk_sep_destroy (ui->sep_v[i]);
	}
	robtk_lbl_destroy (ui->lbl_g_gain);
	robtk_lbl_destroy (ui->lbl_fft);
	robtk_lbl_destroy (ui->lbl_hilo[0]);
	robtk_lbl_destroy (ui->lbl_hilo[1]);

	robtk_lbl_destroy  (ui->lbl_peak);
	robtk_pbtn_destroy (ui->btn_peak);

	pango_font_description_free(ui->font[0]);
	pango_font_description_free(ui->font[1]);

	for (int i = 0; i < 5; ++i) {
		cairo_surface_destroy (ui->dial_bg[i]);
	}
	for (int i = 0; i < 4; ++i) {
		cairo_surface_destroy (ui->dial_hplp[i]);
	}
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
#ifdef USE_LOP_FFT
	fftx_free(ui->lopfft);
#endif
	fftx_free(ui->fa);
	free(ui->ffy);

	delete ui->japa;

	if (ui->m0_grid) {
		cairo_surface_destroy (ui->m0_grid);
	}
	if (ui->m0_filters) {
		cairo_surface_destroy (ui->m0_filters);
	}

	rob_box_destroy (ui->spbox);
	robwidget_destroy (ui->m0);
	rob_table_destroy (ui->ctbl);
	rob_box_destroy (ui->rw);
}

/******************************************************************************
 * RobTk + LV2
 */

#define LVGL_RESIZEABLE

static void ui_enable(LV2UI_Handle handle) {
	Fil4UI* ui = (Fil4UI*)handle;

	uint8_t obj_buf[64];
	lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_frame_time(&ui->forge, 0);
	LV2_Atom* msg = (LV2_Atom*)x_forge_object(&ui->forge, &frame, 1, ui->uris.ui_on);
	lv2_atom_forge_pop(&ui->forge, &frame);
	ui->write(ui->controller, FIL_ATOM_CONTROL, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void ui_disable(LV2UI_Handle handle) {
	Fil4UI* ui = (Fil4UI*)handle;

	tx_state (ui); // too late?

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
	Fil4UI* ui = (Fil4UI*) calloc(1, sizeof(Fil4UI));
	if (!ui) {
		return NULL;
	}

	if (!strcmp(plugin_uri, RTK_URI "mono")) {
		ui->n_channels = 1;
	} else if (!strcmp(plugin_uri, RTK_URI "stereo")) {
		ui->n_channels = 2;
	} else {
		free (ui);
		return NULL;
	}

	for (int i = 0; features[i]; ++i) {
		if (!strcmp(features[i]->URI, LV2_URID_URI "#map")) {
			ui->map = (LV2_URID_Map*)features[i]->data;
		}
		if (!strcmp(features[i]->URI, LV2_UI__touch)) {
			ui->touch = (LV2UI_Touch*)features[i]->data;
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
	ui->soloing    = false;
	ui->dragging   = -1;
	ui->hover      = -1;
	ui->samplerate = 48000;
	ui->ydBrange   = DEFAULT_YZOOM;
	ui->tuning_fq  = 440;
	ui->filter_redisplay = true;
#ifdef OPTIMIZE_FOR_BROKEN_HOSTS
	ui->last_peak = 9999;
#endif

	ui->hilo[0].f = lphp[0].dflt;
	ui->hilo[1].f = lphp[1].dflt;
	ui->hilo[0].q = .7;
	ui->hilo[1].q = .7;

	map_fil4_uris (ui->map, &ui->uris);
	lv2_atom_forge_init(&ui->forge, ui->map);

	*widget = toplevel(ui, ui_toplevel);
	samplerate_changed (ui);
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
			const LV2_Atom *a0 = NULL;
			const LV2_Atom *a1 = NULL;
			const LV2_Atom *a2 = NULL;
			if (
					/* handle raw-audio data objects */
					obj->body.otype == ui->uris.rawaudio
					/* retrieve properties from object and
					 * check that there the [here] two required properties are set.. */
					&& 3 == lv2_atom_object_get(obj, ui->uris.samplerate, &a0, ui->uris.channelid, &a1, ui->uris.audiodata, &a2, NULL)
					/* ..and non-null.. */
					&& a0 && a1 && a2
					/* ..and match the expected type */
					&& a0->type == ui->uris.atom_Float
					&& a1->type == ui->uris.atom_Int
					&& a2->type == ui->uris.atom_Vector
				 )
			{
				const float sr = ((LV2_Atom_Float*)a0)->body;
				const int chn = ((LV2_Atom_Int*)a1)->body;
				LV2_Atom_Vector* vof = (LV2_Atom_Vector*)LV2_ATOM_BODY(a2);
				assert (vof->atom.type == ui->uris.atom_Float);

				const size_t n_elem = (a2->size - sizeof(LV2_Atom_Vector_Body)) / vof->atom.size;
				const float *data = (float*) LV2_ATOM_BODY(&vof->atom);

				if (ui->samplerate != sr) {
					ui->samplerate = sr;
					samplerate_changed (ui);
				}
				handle_audio_data (ui, chn, n_elem, data);
			}
			else if (obj->body.otype == ui->uris.state) {
				ui->disable_signals = true;
				if (1 == lv2_atom_object_get(obj, ui->uris.samplerate, &a0, NULL) && a0) {
					const float sr = ((LV2_Atom_Float*)a0)->body;
					if (ui->samplerate != sr) {
						ui->samplerate = sr;
						samplerate_changed (ui);
					}
				}
				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_dbscale, &a0, NULL) && a0) {
					const float ys = ((LV2_Atom_Float*)a0)->body;
					y_axis_zoom (ui->m0, ys);
				}
				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_fftgain, &a0, NULL) && a0) {
					const float fa = ((LV2_Atom_Float*)a0)->body;
					robtk_dial_set_value (ui->spn_fftgain, fa);
				}
				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_fftmode, &a0, NULL) && a0) {
					const int fm = ((LV2_Atom_Int*)a0)->body;
					robtk_select_set_value (ui->sel_pos,  fm        & 0x1);
					robtk_select_set_value (ui->sel_fft, (fm >>  1) & 0xf);
					robtk_select_set_value (ui->sel_spd, (fm >>  8) & 0xf);
					robtk_select_set_value (ui->sel_res, (fm >> 12) & 0xf);
				}
				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_fftchan, &a0, NULL) && a0) {
					const int fm = ((LV2_Atom_Int*)a0)->body;
					robtk_select_set_value (ui->sel_chn, fm);
				}

				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_uiscale, &a0, NULL) && a0) {
					const float sc = ((LV2_Atom_Float*)a0)->body;
					if (sc != ui->rw->widget_scale && sc >= 1.0 && sc <= 2.0) {
						robtk_queue_scale_change (ui->rw, sc);
					}
				}

				a0 = NULL;
				if (1 == lv2_atom_object_get(obj, ui->uris.s_kbtuning, &a0, NULL) && a0) {
					const float fq = ((LV2_Atom_Float*)a0)->body;
					piano_tuning (ui, fq);
				}
				ui->disable_signals = false;
			}
		}
	}

	if (format != 0 || port_index < FIL_ENABLE || port_index > IIR_HS_GAIN) return;

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
	else if (port_index == FIL_HIQ) {
		robtk_dial_set_value (ui->spn_g_hiq, hplp_to_dial(v));
	}
	else if (port_index == FIL_LOPASS) {
		robtk_ibtn_set_active (ui->btn_g_lopass, v > 0 ? true : false);
	}
	else if (port_index == FIL_LOFREQ) {
		robtk_dial_set_value (ui->spn_g_lofreq, freq_to_dial (&lphp[1], v));
	}
	else if (port_index == FIL_LOQ) {
		robtk_dial_set_value (ui->spn_g_loq, hplp_to_dial(v));
	}
	else if (port_index == FIL_PEAK_RESET) {
		ui->peak_reset_val = (int)floorf(v);
	}
	else if (port_index == FIL_PEAK_DB) {
#ifdef OPTIMIZE_FOR_BROKEN_HOSTS
		if (ui->last_peak != v) {
			ui->last_peak = v;
#endif
			format_button_dbfs (ui->btn_peak, v);
#ifdef OPTIMIZE_FOR_BROKEN_HOSTS
		}
#endif
	}
	else if (port_index >= IIR_LS_EN && port_index <= IIR_HS_GAIN) {
		const int param = (port_index - IIR_LS_EN) % 4;
		const int sect = (port_index - IIR_LS_EN) / 4;
		assert (sect >= 0 && sect < NCTRL);
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
