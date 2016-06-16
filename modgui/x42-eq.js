function (event) {

	function response (dsp, f) {
		var db = 0;
			// TODO optimize, calc omega and sin/cos once here for f.
		for (var i = 0; i < Object.keys (dsp).length; i++) {
			db += dsp[i].dBAtFreq (f);
		}
		return db;
	}

	function freq_at_x (x, width) {
		// 20..20K
		return 20 * Math.pow (1000, x / width);
	}

	function x_at_freq (f, width) {
		return width * Math.log (f / 20.0) / Math.log (1000.0);
	}

	function y_at_db (db) {
		return 50 - 50 * db / 20;
	}

	function x42_draw_tf (tf, data) {
		var dsp = tf.data ('xModDSP');
		var svg = tf.svg ('get');
		if (!dsp || !svg) {
			return;
		}
		if (Object.keys (dsp).length < 8) {
			return;
		}
		var width = 118;
		var y0 = 50;
		var yr = 50;

		svg.clear ();

		/* grid */
		var g = svg.group ({stroke: 'gray', strokeWidth: 0.5});
		var yg = y_at_db (0);
		svg.line (g, 0, yg, width + 1, yg);

		g = svg.group ({stroke: 'gray', strokeWidth: 0.25});
		yg = y_at_db (  6); svg.line (g, 0, yg, width + 1, yg);
		yg = y_at_db ( -6); svg.line (g, 0, yg, width + 1, yg);
		yg = y_at_db ( 12); svg.line (g, 0, yg, width + 1, yg);
		yg = y_at_db (-12); svg.line (g, 0, yg, width + 1, yg);
		yg = y_at_db ( 18); svg.line (g, 0, yg, width + 1, yg);
		yg = y_at_db (-18); svg.line (g, 0, yg, width + 1, yg);

		g = svg.group ({stroke: 'darkgray', strokeWidth: 0.25, strokeDashArray: '1,3'});
		var xg;
		xg = x_at_freq (   50, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq (  200, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq (  500, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq ( 2000, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq ( 5000, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq (15000, width); svg.line (g, xg, 0, xg, 100);

		g = svg.group ({stroke: 'gray', strokeWidth: 0.25, strokeDashArray: '3,2'});
		xg = x_at_freq (  100, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq ( 1000, width); svg.line (g, xg, 0, xg, 100);
		xg = x_at_freq (10000, width); svg.line (g, xg, 0, xg, 100);

		/* tranfer function */
		g = svg.group ({stroke: 'white', strokeWidth: 1.5});
		var yp = y_at_db (response (dsp, freq_at_x (0, width)));
		for (var x = 1; x < width; x++) {
			var y = y_at_db (response (dsp, freq_at_x (x, width)));
			svg.line (g, x, yp, x + 1, y);
			yp = y;
		}
	}

	function set_filter (tf, symbol) {
		var ds = tf.data ('xModPorts');
		var dsp = tf.data ('xModDSP');
		switch (symbol) {
			case 'sec1':
			case 'gain1':
			case 'freq1':
			case 'q1':
				dsp[0] = new X42EQBandPass (ds['sec1'], ds['gain1'], ds['freq1'], ds['q1'], 48000);
				break;
			case 'sec2':
			case 'gain2':
			case 'freq2':
			case 'q2':
				dsp[1] = new X42EQBandPass (ds['sec2'], ds['gain2'], ds['freq2'], ds['q2'], 48000);
				break;
			case 'sec3':
			case 'gain3':
			case 'freq3':
			case 'q3':
				dsp[2] = new X42EQBandPass (ds['sec3'], ds['gain3'], ds['freq3'], ds['q3'], 48000);
				break;
			case 'sec4':
			case 'gain4':
			case 'freq4':
			case 'q4':
				dsp[3] = new X42EQBandPass (ds['sec4'], ds['gain4'], ds['freq4'], ds['q4'], 48000);
				break;
			case 'LSsec':
			case 'LSgain':
			case 'LSfreq':
			case 'LSq':
				dsp[4] = new X42EQShelf (ds['LSsec'], ds['LSgain'], ds['LSfreq'], ds['LSq'], 0, 48000);
				break;
			case 'HSsec':
			case 'HSgain':
			case 'HSfreq':
			case 'HSq':
				dsp[5] = new X42EQShelf (ds['HSsec'], ds['HSgain'], ds['HSfreq'], ds['HSq'], 1, 48000);
				break;
			case 'HighPass':
			case 'HPfreq':
			case 'HPQ':
				dsp[6] = new X42EQHighPass (ds['HighPass'], ds['HPfreq'], ds['HPQ'], 48000);
				break;
			case 'LowPass':
			case 'LPfreq':
			case 'LPQ':
				dsp[7] = new X42EQLowPass (ds['LowPass'], ds['LPfreq'], ds['LPQ'], 48000);
				break;
			default:
				return -1;
				break;
		}
		tf.data ('xModDSP', dsp);
		return 0;
	}

	function set_x42_eq_param (tf, symbol, value) {
		var ds = tf.data ('xModPorts');
		ds[symbol] = value;
		tf.data ('xModPorts', ds);

		/* 33 plugin control inputs + MOD .bypass */
		if (34 >= Object.keys (ds).length) {
			if (0 == set_filter (tf, symbol)) {
				x42_draw_tf (tf, ds);
			}
		}
	}

	if (event.type == 'start') {
		var tf = event.icon.find ('[mod-role=transfer-function]');
		tf.svg ();
		var svg = tf.svg ('get');
		//svg.configure ({viewBox: '0 0 120 100'}, true);
		svg.configure ({width: '118px'}, false);
		svg.configure ({height: '100px'}, false);
		var ports = event.ports;
		var ds = {};
		for (var p in ports){
			ds[ports[p].symbol] = ports[p].value;
		}
		tf.data ('xModDSP', []);
		tf.data ('xModPorts', ds);
		/* initialize */
		set_filter (tf, 'sec1');
		set_filter (tf, 'sec2');
		set_filter (tf, 'sec3');
		set_filter (tf, 'sec4');
		set_filter (tf, 'LSsec');
		set_filter (tf, 'HSsec');
		set_filter (tf, 'HighPass');
		set_filter (tf, 'LowPass');
	}
	else if (event.type == 'change') {
		var tf = event.icon.find ('[mod-role=transfer-function]');
		set_x42_eq_param (tf, event.symbol, event.value);
	}
}

function x42_hypot (x,y) {
	return Math.sqrt ( x * x + y * y);
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

X42EQShelf.prototype.dBAtFreq = function (f) {
	if (!this.en) { return 0; }
	var w = 2 * Math.PI * f / this.rate;
	var c1 = Math.cos (w);
	var s1 = Math.sin (w);
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

X42EQBandPass.prototype.dBAtFreq = function (f) {
	if (!this.en) { return 0; }

	var w =  2 * Math.PI * f / this.rate;
	var c1 = Math.cos (w);
	var s1 = Math.sin (w);
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
	// TODO clamp freq
	// this is only an approx.
	var wr = this.freq / f;
	var q;
	var r = (0.7 + 0.78 * Math.tanh (1.82 * ((this.q) -.8))); // RESHP
	if (r < 1.3) {
		q = 3.01 * Math.sqrt (r / (r + 2));
	} else {
		// clamp pole
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
	if (freq > rate * 0.4998) {
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
