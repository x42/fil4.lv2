function (event) {

	/* constants */

	var log1k = Math.log (1000.0);
	var rate  = 48000;

	/* some helper functions */

	function freq_at_x (x, width) {
		/* log-scale 20..20K */
		return 20 * Math.pow (1000, x / width);
	}

	function x_at_freq (f, width) {
		return width * Math.log (f / 20.0) / log1k;
	}

	function y_at_db (db) {
		return 50 - 50 * db / 20;
	}

	/** calculate combined transfer function for all filters
	 *
	 * @param f  frequency in Hz
	 * @returns dB (gain,attenuation) at given frequency
	 */
	function response (dsp, f) {
		var db = 0;
		/* calc omega and sin/cos once */
		var w = 2 * Math.PI * f / dsp[0].rate;
		var c = Math.cos (w);
		var s = Math.sin (w);
		for (var i = 0; i < Object.keys (dsp).length; i++) {
			db += dsp[i].dBAtFreq (f, w, c, s);
		}
		return db;
	}

	/* the actual SVG drawing function */
	function x42_draw_tf (tf) {
		var ds = tf.data ('xModPorts');
		var dsp = tf.data ('xModDSP');
		var svg = tf.svg ('get');
		if (!dsp || !svg) {
			return;
		}
		if (Object.keys (dsp).length < 8) {
			return;
		}

		var width = 119;
		svg.clear ();

		/* grid */
		var g = svg.group ({stroke: 'gray', strokeWidth: 0.5, fill: 'none'});
		var yg = y_at_db (0);
		svg.line (g, 0, yg, width, yg);

		g = svg.group ({stroke: 'gray', strokeWidth: 0.25, fill: 'none'});
		yg = .5 + Math.round (y_at_db (  6)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db ( -6)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db ( 12)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-12)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db ( 18)); svg.line (g, 0, yg, width, yg);
		yg = .5 + Math.round (y_at_db (-18)); svg.line (g, 0, yg, width, yg);

		var xg;
		g = svg.group ({stroke: 'darkgray', strokeWidth: 0.25, strokeDashArray: '1, 3', fill: 'none'});
		xg = Math.round (x_at_freq (   50, width)); svg.line (g, xg, 0, xg, 100);
		xg = Math.round (x_at_freq (  200, width)); svg.line (g, xg, 0, xg, 100);
		xg = Math.round (x_at_freq (  500, width)); svg.line (g, xg, 0, xg, 100);
		xg = Math.round (x_at_freq ( 2000, width)); svg.line (g, xg, 0, xg, 100);
		xg = Math.round (x_at_freq ( 5000, width)); svg.line (g, xg, 0, xg, 100);
		xg = Math.round (x_at_freq (15000, width)); svg.line (g, xg, 0, xg, 100);

		var tg = svg.group ({stroke: 'gray', fontSize: '8px', textAnchor: 'end', fontFamily: 'Monospace', strokeWidth: 0.5});
		var to; var tr;
		g = svg.group ({stroke: 'gray', strokeWidth: 0.25, strokeDashArray: '3, 2'});
		xg = Math.round (x_at_freq (  100, width)); svg.line (g, xg, 0, xg, 105);
		to = svg.group (tg, {transform: 'translate ('+xg+', 103)'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "100");

		xg = Math.round (x_at_freq ( 1000, width)); svg.line (g, xg, 0, xg, 105);
		to = svg.group (tg, {transform: 'translate ('+xg+', 103)'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "1K");

		xg = Math.round (x_at_freq (10000, width)); svg.line (g, xg, 0, xg, 105);
		to = svg.group (tg, {transform: 'translate ('+xg+', 103)'});
		tr = svg.group (to, {transform: 'rotate (-90, 3, 0)'});
		svg.text (tr, 0, 0, "10K");

		/* transfer function */
		var clp = svg.clipPath (null, 'tfClip');
		svg.rect (clp, -1, 0, width + 3, 100);

		var color = 'white';
		if (!ds['enable'] || 1 == ds[':bypass']) {
			color = '#444444';
		}

		var path = [];
		g = svg.group ({stroke: color, strokeWidth: 1.0, fill: 'none'});
		for (var x = 0; x < width; x++) {
			path.push ([x, y_at_db (response (dsp, freq_at_x (x, width)))]);
		}
		svg.polyline (g, path, {clipPath: 'url(#tfClip)'});
		path.push ([width + 1, 50]);
		path.push ([0, 50]);
		g = svg.group ({stroke: 'none', fill: color, fillOpacity: '0.35'});
		svg.polyline (g, path, {clipPath: 'url(#tfClip)'});
	}

	/* test if all transfer-function relevant
	 * port-parameters are known. Cache the result.
	 */
	function check_information (tf) {
		var ok = tf.data ('xModOK');
		if (ok) {
			return true;
		}
		var ds = tf.data ('xModPorts');
		/* 33 plugin control inputs + MOD bypass */
		if (34 > Object.keys (ds).length) {
			return false;
		}

		var pname = [
			'sec1', 'gain1', 'freq1', 'q1',
			'sec2', 'gain2', 'freq2', 'q2',
			'sec3', 'gain3', 'freq3', 'q3',
			'sec4', 'gain4', 'freq4', 'q4',
			'HSsec', 'HSgain', 'HSfreq', 'HSq',
			'LSsec', 'LSgain', 'LSfreq', 'LSq',
			'HighPass', 'HPfreq', 'HPQ',
			'LowPass',  'LPfreq', 'LPQ',
			'enable', ':bypass' // 32
			// gain, reset-peak
		];

		ok = true;
		for (var p in pname) {
			if (typeof ds[pname[p]] === 'undefined') {
				ok = false;
				break;
			}
		}
		tf.data ('xModOK', ok);
		return ok;
	}

	/* Call when an input parameter changes, update the
	 * corresponding filter
	 *
	 * @return 0 when successful and a re-expose of the Transfer function
	 * is needed, -1 otherwise
	 */
	function set_filter (tf, symbol) {
		var ds = tf.data ('xModPorts');
		var dsp = tf.data ('xModDSP');
		switch (symbol) {
			case 'sec1':
			case 'gain1':
			case 'freq1':
			case 'q1':
				dsp[0] = new X42EQBandPass (ds['sec1'], ds['gain1'], ds['freq1'], ds['q1'], rate);
				break;
			case 'sec2':
			case 'gain2':
			case 'freq2':
			case 'q2':
				dsp[1] = new X42EQBandPass (ds['sec2'], ds['gain2'], ds['freq2'], ds['q2'], rate);
				break;
			case 'sec3':
			case 'gain3':
			case 'freq3':
			case 'q3':
				dsp[2] = new X42EQBandPass (ds['sec3'], ds['gain3'], ds['freq3'], ds['q3'], rate);
				break;
			case 'sec4':
			case 'gain4':
			case 'freq4':
			case 'q4':
				dsp[3] = new X42EQBandPass (ds['sec4'], ds['gain4'], ds['freq4'], ds['q4'], rate);
				break;
			case 'LSsec':
			case 'LSgain':
			case 'LSfreq':
			case 'LSq':
				dsp[4] = new X42EQShelf (ds['LSsec'], ds['LSgain'], ds['LSfreq'], ds['LSq'], 0, rate);
				break;
			case 'HSsec':
			case 'HSgain':
			case 'HSfreq':
			case 'HSq':
				dsp[5] = new X42EQShelf (ds['HSsec'], ds['HSgain'], ds['HSfreq'], ds['HSq'], 1, rate);
				break;
			case 'HighPass':
			case 'HPfreq':
			case 'HPQ':
				dsp[6] = new X42EQHighPass (ds['HighPass'], ds['HPfreq'], ds['HPQ'], rate);
				break;
			case 'LowPass':
			case 'LPfreq':
			case 'LPQ':
				dsp[7] = new X42EQLowPass (ds['LowPass'], ds['LPfreq'], ds['LPQ'], rate);
				break;
			case 'enable':
			case ':bypass':
				return 0;
			default:
				return -1;
				break;
		}
		tf.data ('xModDSP', dsp);
		return 0;
	}

	/* wrapper around the above, set parameter,
	 * test that all parameters are known before updating
	 */
	function set_x42_eq_param (tf, symbol, value) {
		var ds = tf.data ('xModPorts');
		ds[symbol] = value;
		tf.data ('xModPorts', ds);

		if (check_information (tf)) {
			if (0 == set_filter (tf, symbol)) {
				x42_draw_tf (tf);
			}
		}
	}


	/* top-level entry, called from mod-ui */
	if (event.type == 'start') {
		/* initialize */
		var tf = event.icon.find ('[mod-role=transfer-function]');
		tf.svg ();

		var svg = tf.svg ('get');
		svg.configure ({width: '118px'}, false);
		svg.configure ({height: '130px'}, false);
		svg.clear ();
		var tg = svg.group ({stroke: '#cccccc', fontSize: '11px', textAnchor: 'middle', strokeWidth: 0.5});
		svg.text (tg, 59, 65, "Transfer function", {dy: '-1.5em'});
		svg.text (tg, 59, 65, "display needs");
		svg.text (tg, 59, 65, "MOD v0.15.0 or later.", {dy: '1.5em'});

		var ds = {};
		var ports = event.ports;

		for (var p in ports) {
			ds[ports[p].symbol] = ports[p].value;
		}

		tf.data ('xModDSP', []);
		tf.data ('xModPorts', ds);
		tf.data ('xModOK', false);

		if (!check_information (tf)) {
			/* MOD < v0.15.0  does not set initial values */
			return;
		}

		set_filter (tf, 'sec1');
		set_filter (tf, 'sec2');
		set_filter (tf, 'sec3');
		set_filter (tf, 'sec4');
		set_filter (tf, 'LSsec');
		set_filter (tf, 'HSsec');
		set_filter (tf, 'HighPass');
		set_filter (tf, 'LowPass');
		x42_draw_tf (tf);
	}
	else if (event.type == 'change') {
		/* update parameters, redraw transfer function if needed */
		var tf = event.icon.find ('[mod-role=transfer-function]');
		set_x42_eq_param (tf, event.symbol, event.value);
	}
}

/* GLOBAL fn & classes */

function x42_hypot (x, y) {
	return Math.sqrt (x * x + y * y);
}

function x42_square (x) {
	return x * x
}

var X42EQShelf = function (en, gain, freq, bandw, type, rate) {
	this.en = en;
	this.rate = rate;
	if (!this.en) { return; }

	var freq_ratio = freq / rate;
	var q = .2129 + bandw / 2.25;
	if (freq_ratio < 0.0004) { freq_ratio = 0.0004; }
	if (freq_ratio > 0.4700) { freq_ratio = 0.4700; }
	if (q < .25) { q = .25; }
	if (q > 2.0) { q = 2.0; }

	var w0 = 2. * Math.PI * freq_ratio;
	var _cosW = Math.cos (w0);

	var A  = Math.pow (10, .025 * gain); // sqrt(gain_as_coeff)
	var As = Math.sqrt (A);
	var a  = Math.sin (w0) / 2 * (1 / q);

	if (type) { // high shelf
		var b0 =  A *      ((A + 1) + (A - 1) * _cosW + 2 * As * a);
		var b1 = -2 * A  * ((A - 1) + (A + 1) * _cosW);
		var b2 =  A *      ((A + 1) + (A - 1) * _cosW - 2 * As * a);
		var a0 = (A + 1) -  (A - 1) * _cosW + 2 * As * a;
		var a1 =  2 *      ((A - 1) - (A + 1) * _cosW);
		var a2 = (A + 1) -  (A - 1) * _cosW - 2 * As * a;

		var _b0 = b0 / a0;
		var _b2 = b2 / a0;
		var _a2 = a2 / a0;

		this.A  = _b0 + _b2;
		this.B  = _b0 - _b2;
		this.C  = 1.0 + _a2;
		this.D  = 1.0 - _a2;
		this.A1 = a1 / a0;
		this.B1 = b1 / a0;
	} else { // low shelf
		var b0 =  A *      ((A + 1) - (A - 1) * _cosW + 2 * As * a);
		var b1 =  2 * A  * ((A - 1) - (A + 1) * _cosW);
		var b2 =  A *      ((A + 1) - (A - 1) * _cosW - 2 * As * a);
		var a0 = (A + 1) +  (A - 1) * _cosW + 2 * As * a;
		var a1 = -2 *      ((A - 1) + (A + 1) * _cosW);
		var a2 = (A + 1) +  (A - 1) * _cosW - 2 * As * a;

		var _b0 = b0 / a0;
		var _b2 = b2 / a0;
		var _a2 = a2 / a0;

		this.A  = _b0 + _b2;
		this.B  = _b0 - _b2;
		this.C  = 1.0 + _a2;
		this.D  = 1.0 - _a2;
		this.A1 = a1 / a0;
		this.B1 = b1 / a0;
	}
}

X42EQShelf.prototype.dBAtFreq = function (f, w, c1, s1) {
	if (!this.en) { return 0; }
	/*
	var w = 2 * Math.PI * f / this.rate;
	var c1 = Math.cos (w);
	var s1 = Math.sin (w);
	*/

	var A = this.A * c1 + this.B1;
	var B = this.B * s1;
	var C = this.C * c1 + this.A1;
	var D = this.D * s1;
	return 20 * Math.log10 (Math.sqrt ((x42_square (A) + x42_square (B)) * (x42_square (C) + x42_square (D))) / (x42_square (C) + x42_square (D)));
}

var X42EQBandPass = function (en, gain, freq, bandw, rate) {
	this.en = en;
	this.rate = rate;
	if (!this.en) { return; }

	var freq_ratio = freq / rate;
	if (freq_ratio < 0.0002) { freq_ratio = 0.0002; }
	if (freq_ratio > 0.4998) { freq_ratio = 0.4998; }
	var g = Math.pow (10, 0.05 * gain);
	var b = 7 * bandw * freq_ratio / Math.sqrt (g);

	this.s2 = (1 - b) / (1 + b);
	this.s1 = -1 * Math.cos (2 * Math.PI * freq_ratio);
	this.s1 *= (1 + this.s2);
	this.gain_db = 0.5 * (g - 1) * (1 - this.s2);
};

X42EQBandPass.prototype.dBAtFreq = function (f, w, c1, s1) {
	if (!this.en) { return 0; }
	/*
	var w =  2 * Math.PI * f / this.rate;
	var c1 = Math.cos (w);
	var s1 = Math.sin (w);
	*/
	var c2 = Math.cos (2 * w);
	var s2 = Math.sin (2 * w);

	var x = c2 + this.s1 * c1 + this.s2;
	var y = s2 + this.s1 * s1;

	var t1 = x42_hypot (x, y);

	x += this.gain_db * (c2 - 1);
	y += this.gain_db * s2;

	var t2 = x42_hypot (x, y);

	return 20 * Math.log10 (t2 / t1);
}

var X42EQHighPass = function (en, freq, q, rate) {
	this.en = en;
	this.rate = rate;
	if (!this.en) { return; }

	this.freq = freq;
	this.q = q;
}

X42EQHighPass.prototype.dBAtFreq = function (f) {
	if (!this.en) { return 0; }
	if (f < 5) { f = 5; }
	else if (f > this.rate / 12) { f = this.rate / 12; }
	// this is only an approx.
	var wr = this.freq / f;
	var q;
	var r = (0.7 + 0.78 * Math.tanh (1.82 * (this.q - .8))); // RESHP
	if (r < 1.3) {
		q = 3.01 * Math.sqrt (r / (r + 2));
	} else {
		q = Math.sqrt (4 - 0.09 / (r - 1.09));
	}
	return -10 * Math.log10 (x42_square (1 + x42_square (wr)) - x42_square (q * wr));
}

var X42EQLowPass = function (en, freq, q, rate) {
	this.en = en;
	this.rate = rate;
	if (!this.en) { return; }
	if (freq < rate * 0.0002) {
		freq = rate * 0.0002;
	}
	else if (freq > rate * 0.4998) {
		freq = rate * 0.4998;
	}
	this.fb = 3. * Math.pow (q, 3.20772);
	if (this.fb < 0) { this.fb = 0; }
	if (this.fb > 9) { this.fb = 9; }

	this.wc = Math.sin (Math.PI * freq / rate);
	this.q  = Math.sqrt (4 * this.fb / (1 + this.fb));
}

X42EQLowPass.prototype.dBAtFreq = function (f) {
	if (!this.en) { return 0; }
	// this is only an approx. w/o additional shelf
	var w  = Math.sin (Math.PI * f / this.rate);
	return -10 * Math.log10 (x42_square (1 + x42_square (w / this.wc)) - x42_square (this.q * w / this.wc));
}

/* vim: set sw=2 ts=2: */
