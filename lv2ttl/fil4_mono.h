// generated by lv2ttl2c from
// http://gareus.org/oss/lv2/fil4#mono

extern const LV2_Descriptor* lv2_descriptor(uint32_t index);
extern const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index);

static const RtkLv2Description _plugin_mono = {
	&lv2_descriptor,
	&lv2ui_descriptor
	, 0 // uint32_t dsp_descriptor_id
	, 0 // uint32_t gui_descriptor_id
	, "x42-eq - Parametric Equalizer Mono" // const char *plugin_human_id
	, (const struct LV2Port[38])
	{
		{ "control", ATOM_IN, nan, nan, nan, "UI to plugin communication"},
		{ "notify", ATOM_OUT, nan, nan, nan, "Plugin to GUI communication"},
		{ "enable", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Enable"},
		{ "gain", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Gain"},
		{ "peak", CONTROL_OUT, nan, -120.000000, 0.000000, "Peak"},
		{ "peakreset", CONTROL_IN, 1.000000, 0.000000, 1.000000, "toggle to reset the peak"},
		{ "HighPass", CONTROL_IN, 0.000000, 0.000000, 1.000000, "Highpass"},
		{ "HPfreq", CONTROL_IN, 20.000000, 5.000000, 1250.000000, "Highpass Frequency"},
		{ "HPQ", CONTROL_IN, 0.700000, 0.000000, 1.400000, "HighPass Resonance"},
		{ "LowPass", CONTROL_IN, 0.000000, 0.000000, 1.000000, "Lowpass"},
		{ "LPfreq", CONTROL_IN, 20000.000000, 500.000000, 20000.000000, "Lowpass Frequency"},
		{ "LPQ", CONTROL_IN, 1.000000, 0.000000, 1.400000, "LowPass Resonance"},
		{ "LSsec", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Lowshelf"},
		{ "LSfreq", CONTROL_IN, 80.000000, 25.000000, 400.000000, "Lowshelf Frequency"},
		{ "LSq", CONTROL_IN, 1.000000, 0.062500, 4.000000, "Lowshelf Bandwidth"},
		{ "LSgain", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Lowshelf Gain"},
		{ "sec1", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Section 1"},
		{ "freq1", CONTROL_IN, 160.000000, 20.000000, 2000.000000, "Frequency 1"},
		{ "q1", CONTROL_IN, 0.500000, 0.062500, 4.000000, "Bandwidth 1"},
		{ "gain1", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Gain 1"},
		{ "sec2", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Section 2"},
		{ "freq2", CONTROL_IN, 397.000000, 40.000000, 4000.000000, "Frequency 2"},
		{ "q2", CONTROL_IN, 0.500000, 0.062500, 4.000000, "Bandwidth 2"},
		{ "gain2", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Gain 2"},
		{ "sec3", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Section 3"},
		{ "freq3", CONTROL_IN, 1250.000000, 100.000000, 10000.000000, "Frequency 3"},
		{ "q3", CONTROL_IN, 0.500000, 0.062500, 4.000000, "Bandwidth 3"},
		{ "gain3", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Gain 3"},
		{ "sec4", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Section 4"},
		{ "freq4", CONTROL_IN, 2500.000000, 200.000000, 20000.000000, "Frequency 4"},
		{ "q4", CONTROL_IN, 0.500000, 0.062500, 4.000000, "Bandwidth 4"},
		{ "gain4", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Gain 4"},
		{ "HSsec", CONTROL_IN, 1.000000, 0.000000, 1.000000, "Highshelf"},
		{ "HSfreq", CONTROL_IN, 8000.000000, 1000.000000, 16000.000000, "Highshelf Frequency"},
		{ "HSq", CONTROL_IN, 1.000000, 0.062500, 4.000000, "Highshelf Bandwidth"},
		{ "HSgain", CONTROL_IN, 0.000000, -18.000000, 18.000000, "Highshelf Gain"},
		{ "in", AUDIO_IN, nan, nan, nan, "In"},
		{ "out", AUDIO_OUT, nan, nan, nan, "Out"},
	}
	, 38 // uint32_t nports_total
	, 1 // uint32_t nports_audio_in
	, 1 // uint32_t nports_audio_out
	, 0 // uint32_t nports_midi_in
	, 0 // uint32_t nports_midi_out
	, 1 // uint32_t nports_atom_in
	, 1 // uint32_t nports_atom_out
	, 34 // uint32_t nports_ctrl
	, 33 // uint32_t nports_ctrl_in
	, 1 // uint32_t nports_ctrl_out
	, 65888 // uint32_t min_atom_bufsiz
	, false // bool send_time_info
	, UINT32_MAX // uint32_t latency_ctrl_port
};
