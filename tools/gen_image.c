#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <cairo/cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

/* colors */
static const float c_blk[4] = {0.0, 0.0, 0.0, 1.0};
static const float c_wht[4] = {1.0, 1.0, 1.0, 1.0};
static const float c_dlf[4] = {0.8, 0.8, 0.8, 1.0};
static const float c_ann[4] = {0.5, 0.5, 0.5, 1.0}; // text annotation color

/* filter parameters */
typedef struct {
	float min;
	float max;
	float dflt;
	float warp;
} FilterFreq;

/* frequency mapping */
static const FilterFreq freqs[] = {
	/*min    max   dflt*/
	{  25,   400,    80,  16}, // LS
	{  20,  2000,   160, 100},
	{  40,  4000,   397, 100},
	{ 100, 10000,  1250, 100},
	{ 200, 20000,  2500, 100},
	{1000, 16000,  8000,  16}, // HS
};

static const FilterFreq lphp[2] = {
	{   10,  1000,   20,  100}, // HP
	{  630, 20000, 20000,  32}, // LP
};

static const double GED_CX = 47;
static const double GED_CY = 43.5;
static const double GED_RADIUS = 28;
static const double GED_WIDTH = 93;
static const double GZLINE = 75 - 10;
static const double SF_W = 93;
static const double SF_H = 85;

static float dial_to_freq (const FilterFreq *m, float f) {
	return m->min + (m->max - m->min) * (pow((1. + m->warp), f) - 1.) / m->warp;
}

static float bw_to_dial (float v) {
	if (v < .0625) return 0.f;
	if (v >  4.0) return 1.f;
	return log2f (16.f * v) / 6.f;
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


static void write_text_full (
		cairo_t* cr,
		const char *txt,
		PangoFontDescription *font,
		const float x, const float y,
		const float ang, const int align,
		const float * const col) {
	int tw, th;
	cairo_save(cr);

	PangoLayout * pl = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(pl, font);
	if (strncmp(txt, "<markup>", 8)) {
		pango_layout_set_text(pl, txt, -1);
	} else {
		pango_layout_set_markup(pl, txt, -1);
	}
	pango_layout_get_pixel_size(pl, &tw, &th);
	cairo_translate (cr, rintf(x), rintf(y));
	if (ang != 0) { cairo_rotate (cr, ang); }
	switch(abs(align)) {
		case 1:
			cairo_translate (cr, -tw, ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 2:
			cairo_translate (cr, ceil(tw/-2.0), ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 3:
			cairo_translate (cr, 0, ceil(th/-2.0));
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		case 4:
			cairo_translate (cr, -tw, -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 5:
			cairo_translate (cr, ceil(tw/-2.0), -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 6:
			cairo_translate (cr, 0, -th);
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		case 7:
			cairo_translate (cr, -tw, 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_RIGHT);
			break;
		case 8:
			cairo_translate (cr, ceil(tw/-2.0), 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_CENTER);
			break;
		case 9:
			cairo_translate (cr, 0, 0);
			pango_layout_set_alignment (pl, PANGO_ALIGN_LEFT);
			break;
		default:
			break;
	}
	if (align < 0) {
		cairo_set_source_rgba (cr, .0, .0, .0, .5);
		cairo_rectangle (cr, 0, 0, tw, th);
		cairo_fill (cr);
	}
#if 1
	cairo_set_source_rgba (cr, col[0], col[1], col[2], col[3]);
	pango_cairo_show_layout(cr, pl);
#else
	cairo_set_source_rgba (cr, col[0], col[1], col[2], col[3]);
	pango_cairo_layout_path(cr, pl);
	cairo_fill(cr);
#endif
	g_object_unref(pl);
	cairo_restore(cr);
	cairo_new_path (cr);
}

#define CairoSetSouerceRGBA(COL) \
  cairo_set_source_rgba (cr, (COL)[0], (COL)[1], (COL)[2], (COL)[3])


#define DIALDOTS(V, XADD, YADD) \
	float ang = (-.75 * M_PI) + (1.5 * M_PI) * (V); \
	xlp = GED_CX + XADD + sinf (ang) * (GED_RADIUS); \
	ylp = GED_CY + YADD - cosf (ang) * (GED_RADIUS); \
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); \
	CairoSetSouerceRGBA(c_dlf); \
	cairo_set_line_width(cr, 2.5); \
	cairo_move_to(cr, rint(xlp)-.5, rint(ylp)-.5); \
	cairo_close_path(cr); \
	cairo_stroke(cr);


#define RESPLABLEL(V) \
	{ \
	DIALDOTS(V, 0, 0) \
	xlp = GED_CX + sinf (ang) * (GED_RADIUS + 6.5); \
	ylp = GED_CY - cosf (ang) * (GED_RADIUS + 6.5); \
	}


static void gain_knob (cairo_t* cr)
{
	float xlp, ylp;
	PangoFontDescription* font = pango_font_description_from_string("Mono 9px");

	RESPLABLEL(0.00);
	write_text_full(cr, "-18", font, xlp, ylp,      0, 1, c_dlf);
	RESPLABLEL(.16);
	write_text_full(cr, "-12", font, xlp, ylp,      0, 1, c_dlf);
	RESPLABLEL(.33);
	write_text_full(cr,  "-6", font, xlp, ylp,      0, 1, c_dlf);
	RESPLABLEL(0.5);
	write_text_full(cr, "0dB", font, xlp-1, ylp-1,  0, 2, c_dlf);
	RESPLABLEL(.66);
	write_text_full(cr,  "+6", font, xlp-3, ylp,    0, 3, c_dlf);
	RESPLABLEL(.83);
	write_text_full(cr, "+12", font, xlp-3, ylp,    0, 3, c_dlf);
	RESPLABLEL(1.0);
	write_text_full(cr, "+18", font, xlp-3, ylp,    0, 3, c_dlf);
	pango_font_description_free (font);
}

#define BWX 10

static void bw_knob (cairo_t* cr)
{
	const double GED_CY = 33.5;
	float xlp, ylp;
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);
	cairo_move_to (cr,  1 + BWX, GZLINE);
	cairo_line_to (cr,  8 + BWX, GZLINE);
	cairo_line_to (cr, 10 + BWX, GZLINE - 4);
	cairo_line_to (cr, 12 + BWX, GZLINE);
	cairo_line_to (cr, 19 + BWX, GZLINE);
	cairo_move_to (cr, 12 + BWX, GZLINE);
	cairo_line_to (cr, 10 + BWX, GZLINE + 4);
	cairo_line_to (cr,  8 + BWX, GZLINE);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - BWX -  1, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX -  4, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 10, GZLINE - 4);
	cairo_line_to (cr, GED_WIDTH - BWX - 16, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 19, GZLINE);
	cairo_move_to (cr, GED_WIDTH - BWX - 16, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 10, GZLINE + 4);
	cairo_line_to (cr, GED_WIDTH - BWX -  4, GZLINE);
	cairo_stroke (cr);

	{ DIALDOTS(bw_to_dial (powf(2.f,  4 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f,  2 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f,  0 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -2 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -4 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -6 / 2.f)), .0, .5) }
	{ DIALDOTS(bw_to_dial (powf(2.f, -8 / 2.f)), .0, .5) }
}

static void bw_ls_knob (cairo_t* cr)
{
	const double GED_CY = 33.5;
	float xlp, ylp;
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);
	cairo_move_to (cr,  1 + BWX, GZLINE - 3);
	cairo_line_to (cr,  4 + BWX, GZLINE - 3);
	cairo_line_to (cr, 14 + BWX, GZLINE);
	cairo_line_to (cr, 18 + BWX, GZLINE);
	cairo_move_to (cr, 14 + BWX, GZLINE);
	cairo_line_to (cr,  4 + BWX, GZLINE + 3);
	cairo_line_to (cr,  1 + BWX, GZLINE + 3);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - BWX -  1, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX -  7, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 10, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH - BWX - 18, GZLINE - 3);
	cairo_move_to (cr, GED_WIDTH - BWX -  7, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 10, GZLINE + 3);
	cairo_line_to (cr, GED_WIDTH - BWX - 18, GZLINE + 3);
	cairo_stroke (cr);

	CairoSetSouerceRGBA(c_ann);
	cairo_set_line_width (cr, 1.0);
	cairo_arc (cr, GED_CX + 0, GED_CY, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);
	cairo_arc (cr, GED_CX - 1, GED_CY, GED_RADIUS + 2.0, 0.75 * M_PI, 1.25 * M_PI);
	cairo_stroke (cr);
	CairoSetSouerceRGBA(c_dlf);

	{ DIALDOTS(  0.0, .0, .5) }
	{ DIALDOTS(1/6.f, .0, .5) }
	{ DIALDOTS(2/6.f, .0, .5) }
	{ DIALDOTS(3/6.f, .0, .5) }
	{ DIALDOTS(4/6.f, .0, .5) }
	{ DIALDOTS(5/6.f, .0, .5) }
	{ DIALDOTS(  1.0, .0, .5) }
}

static void bw_hs_knob (cairo_t* cr)
{
	const double GED_CY = 33.5;
	float xlp, ylp;
	CairoSetSouerceRGBA(c_dlf);
	cairo_set_line_width(cr, 1.25);
	cairo_move_to (cr, 18 + BWX, GZLINE - 3);
	cairo_line_to (cr, 15 + BWX, GZLINE - 3);
	cairo_line_to (cr,  5 + BWX, GZLINE);
	cairo_line_to (cr,  1 + BWX, GZLINE);
	cairo_move_to (cr,  5 + BWX, GZLINE);
	cairo_line_to (cr, 15 + BWX, GZLINE + 3);
	cairo_line_to (cr, 18 + BWX, GZLINE + 3);
	cairo_stroke (cr);

	cairo_move_to (cr, GED_WIDTH - BWX - 18, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX - 12, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX -  8, GZLINE - 3);
	cairo_line_to (cr, GED_WIDTH - BWX -  1, GZLINE - 3);
	cairo_move_to (cr, GED_WIDTH - BWX - 12, GZLINE);
	cairo_line_to (cr, GED_WIDTH - BWX -  9, GZLINE + 3);
	cairo_line_to (cr, GED_WIDTH - BWX -  1, GZLINE + 3);
	cairo_stroke (cr);

	CairoSetSouerceRGBA(c_ann);
	cairo_set_line_width (cr, 1.0);
	cairo_arc (cr, GED_CX + 0, GED_CY, GED_RADIUS + 2.0, -.25 * M_PI, .25 * M_PI);
	cairo_stroke (cr);
	cairo_arc (cr, GED_CX - 1, GED_CY, GED_RADIUS + 2.0, 0.75 * M_PI, 1.25 * M_PI);
	cairo_stroke (cr);
	CairoSetSouerceRGBA(c_dlf);

	{ DIALDOTS(  0.0, .0, .5) }
	{ DIALDOTS(1/6.f, .0, .5) }
	{ DIALDOTS(2/6.f, .0, .5) }
	{ DIALDOTS(3/6.f, .0, .5) }
	{ DIALDOTS(4/6.f, .0, .5) }
	{ DIALDOTS(5/6.f, .0, .5) }
	{ DIALDOTS(  1.0, .0, .5) }
}


#define HLX 3 // Hi/Low pass icon x-offset

static void bw_hp_knob (cairo_t* cr)
{
	float xlp, ylp;
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

	{ DIALDOTS(  0.0,             .0, .5) }
	{ DIALDOTS(hplp_to_dial(.71), .0, .5) }
	{ DIALDOTS(  0.5,             .0, .5) }
	{ DIALDOTS(hplp_to_dial(1.0), .0, .5) }
	{ DIALDOTS(  1.0,             .0, .5) }
}

static void bw_lp_knob (cairo_t* cr)
{
	float xlp, ylp;
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

	{ DIALDOTS(  0.0,             .0, .5) }
	{ DIALDOTS(hplp_to_dial(.71), .0, .5) }
	{ DIALDOTS(  0.5,             .0, .5) }
	{ DIALDOTS(hplp_to_dial(1.0), .0, .5) }
	{ DIALDOTS(  1.0,             .0, .5) }
}


static void freq_knob (cairo_t* cr, FilterFreq const * const f)
{
	float xlp, ylp;
	char tfq[8];

	PangoFontDescription* font = pango_font_description_from_string("Mono 9px");

	print_hz(tfq, dial_to_freq(f, 0));
	RESPLABLEL(0.00); write_text_full(cr, tfq, font, xlp, ylp, 0, 1, c_dlf);

	print_hz(tfq, dial_to_freq(f, .25));
	RESPLABLEL(0.25); write_text_full(cr, tfq, font, xlp, ylp, 0, 1, c_dlf);

	print_hz(tfq, dial_to_freq(f, .50));
	RESPLABLEL(0.50); write_text_full(cr, tfq, font, xlp, ylp, 0, 2, c_dlf);

	print_hz(tfq, dial_to_freq(f, .75));
	RESPLABLEL(0.75); write_text_full(cr, tfq, font, xlp-2, ylp, 0, 3, c_dlf);

	print_hz(tfq, dial_to_freq(f, 1.0));
	RESPLABLEL(1.00); write_text_full(cr, tfq, font, xlp-2, ylp, 0, 3, c_dlf);

	pango_font_description_free (font);
}

#define SF_NEW(W,H) \
	cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H); \
	cr = cairo_create(cs);

#define SF_END(NAME) \
	cairo_surface_write_to_png(cs, NAME); \
	cairo_destroy (cr); \
	cairo_surface_destroy (cs);

int main(int argc, char **argv) {
	cairo_surface_t* cs;
	cairo_t* cr;

	SF_NEW (SF_W, SF_H);
	gain_knob (cr);
	SF_END ("../modgui/x42-eq-gain.png");

	SF_NEW (SF_W, SF_H - 10);
	bw_knob (cr);
	SF_END ("../modgui/x42-eq-bw.png");

	SF_NEW (SF_W, SF_H - 10);
	bw_hs_knob (cr);
	SF_END ("../modgui/x42-eq-bwhs.png");

	SF_NEW (SF_W, SF_H - 10);
	bw_ls_knob (cr);
	SF_END ("../modgui/x42-eq-bwls.png");

	SF_NEW (SF_W, SF_H);
	bw_lp_knob (cr);
	SF_END ("../modgui/x42-eq-bwlp.png");

	SF_NEW (SF_W, SF_H);
	bw_hp_knob (cr);
	SF_END ("../modgui/x42-eq-bwhp.png");

	for (int i = 0; i < 6; ++i) {
		char name[64];
		sprintf(name, "../modgui/x42-eq-f%d.png",i);
		SF_NEW (SF_W, SF_H);
		freq_knob (cr, &freqs[i]);
		SF_END (name);
	}

	SF_NEW (SF_W, SF_H);
	freq_knob (cr, &lphp[0]);
	SF_END ("../modgui/x42-eq-fh.png");

	SF_NEW (SF_W, SF_H);
	freq_knob (cr, &lphp[1]);
	SF_END ("../modgui/x42-eq-fl.png");

	return 0;
}
