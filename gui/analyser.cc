// -------------------------------------------------------------------------
//
//  Copyright (C) 2004-2013 Fons Adriaensen <fons@linuxaudio.org>
//    
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// -------------------------------------------------------------------------


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "analyser.h"


Trace::Trace (int size) : _valid (false)
{
    _data = new float [size];
} 


Trace::~Trace (void)
{
    delete[] _data;
}


Analyser::Analyser (int ipsize, int fftmax, float fsamp) :
    _ipsize (ipsize),
    _icount (0),
    _fftmax (fftmax),
    _fftlen (0),
    _fftplan (0),
    _fsamp (fsamp),
    _wfact (0.0f),
    _speed (1.0f)
{
    _ipdata = new float [ipsize];
    _warped = (float *) fftwf_malloc ((fftmax + 1) * sizeof (float));
    _trdata = (fftwf_complex *) fftwf_malloc ((fftmax / 2 + 9) * sizeof (fftwf_complex));
    _power = new Trace (fftmax + 1);
    _peakp = new Trace (fftmax + 1);
}


Analyser::~Analyser (void)
{
    if (_fftplan) fftwf_destroy_plan (_fftplan);
    delete _power;
    delete _peakp;
    fftwf_free (_trdata);
    fftwf_free (_warped);
    delete[] _ipdata;
}


void Analyser::set_fftlen (int fftlen)
{
    if (fftlen > _fftmax) fftlen = _fftmax;
    if (_fftlen != fftlen)
    {
        if (_fftplan) fftwf_destroy_plan (_fftplan);
        _fftlen = fftlen;
        _fftplan = fftwf_plan_dft_r2c_1d (_fftlen, _warped, _trdata + 4, FFTW_ESTIMATE);
        set_wfact (_wfact);
        set_speed (_speed);
        clr_peak ();
    }
}


void Analyser::set_wfact (float wfact)
{
    _wfact = wfact;
    _pmax = 1e-20f;
    memset (_warped, 0, (_fftlen + 1) * sizeof (float));
    _power->_valid = false;
    _peakp->_valid = false;
    memset (_power->_data,  0, (_fftlen + 1) * sizeof (float));
    memset (_peakp->_data,  0, (_fftlen + 1) * sizeof (float));
}


void Analyser::set_speed (float speed)
{
    _speed = speed;
}


void Analyser::clr_peak (void)
{
    _peakp->_valid = false;
    memset (_peakp->_data,  0, (_fftlen + 1) * sizeof (float));
}


float Analyser::conv0 (fftwf_complex *v)
{
    float x, y;
    x =  v [0][0]
        - 0.677014f * (v [-1][0] + v [1][0])
        + 0.195602f * (v [-2][0] + v [2][0])
        - 0.019420f * (v [-3][0] + v [3][0])
        + 0.000741f * (v [-4][0] + v [4][0]);
    y =  v [0][1]
        - 0.677014f * (v [-1][1] + v [1][1])
        + 0.195602f * (v [-2][1] + v [2][1])
        - 0.019420f * (v [-3][1] + v [3][1])
        + 0.000741f * (v [-4][1] + v [4][1]);
    return x * x + y * y;
}


float Analyser::conv1 (fftwf_complex *v)
{
    float x, y;
    x =   0.908040f * (v [ 0][0] - v [1][0])
	- 0.409037f * (v [-1][0] - v [2][0])
	+ 0.071556f * (v [-2][0] - v [3][0])
	- 0.004085f * (v [-3][0] - v [4][0]);
    y =   0.908040f * (v [ 0][1] - v [1][1])
	- 0.409037f * (v [-1][1] - v [2][1])
	+ 0.071556f * (v [-2][1] - v [3][1])
	- 0.004085f * (v [-3][1] - v [4][1]);
    return x * x + y * y;
}


void Analyser::process (int iplen, bool holdp)
{
    int    i, j, k, l;
    float  a, b, c, d, m, p, s, w, z;
    float  *p1, *p2;

    w = -_wfact;
    l = _fftlen / 2;

    for (k = 0; k < iplen; k += l)
    {
        p1 = _ipdata + _icount;
	_icount += l;
	if (_icount == _ipsize) _icount = 0;

        for (j = 0; j < l; j += 4)
	{ 
            a = _warped [0];
            b = *p1++ + 1e-20f;
            c = *p1++ - 1e-20f;
            d = *p1++ + 1e-20f;
            _warped [0] = z = *p1++ - 1e-20f;
            for (i = 0; i < _fftlen; i += 4)
	    {
                s = _warped [i + 1];
		a += w * (b - s);
		b += w * (c - a);
		c += w * (d - b);
                _warped [i + 1] = z = d + w * (z - c);
                d = s;
                s = _warped [i + 2];
		d += w * (a - s);
		a += w * (b - d);
		b += w * (c - a);
                _warped [i + 2] = z = c + w * (z - b);
                c = s;
                s = _warped [i + 3];
		c += w * (d - s);
		d += w * (a - c);
		a += w * (b - d);
                _warped [i + 3] = z = b + w * (z - a);
                b = s;
                s = _warped [i + 4];
		b += w * (c - s);
		c += w * (d - b);
		d += w * (a - c);
                _warped [i + 4] = z = a + w * (z - d);
                a = s;
	    }
	}

        fftwf_execute (_fftplan);
        for (i = 1; i <= 4; i++)
	{
	    _trdata [4 - i][0] =  _trdata [4 + i][0];
	    _trdata [4 - i][1] = -_trdata [4 + i][1];
	    _trdata [4 + l + i][0] =  _trdata [4 + l - i][0];
	    _trdata [4 + l + i][1] = -_trdata [4 + l - i][1];
	}

        a = 1.0f - powf (0.1f, l / (_fsamp * _speed)); 
        b = 4.0f / ((float)_fftlen * (float)_fftlen);
        s = 0;
        m = 0;
        p1 = _power->_data;
        for (i = 0; i < l; i++)
	{
	    p = b * conv0 (_trdata + 4 + i) + 1e-20f;
            if (m < p) m = p;
            s += p;
            *p1 += a * (p - *p1);
            p1++;
	    p = b * conv1 (_trdata + 4 + i) + 1e-20f;
            if (m < p) m = p;
            s += p;
            *p1 += a * (p - *p1);
            p1++;  
	}
        p = b * conv0 (_trdata + 4 + i) + 1e-20f;
        s += p;
        *p1 += a * (p - *p1);
        _power->_valid = true;

        if (_pmax < m) _pmax = m;
        else _pmax *= 0.95f;
        _ptot = s;
        
        if (holdp)
	{ 
            p1 = _power->_data;
            p2 = _peakp->_data;
	    for (i = 0; i <= 2 * l; i++)
	    {
		if (p2 [i] < p1 [i]) p2 [i] = p1 [i];
	    }
            _peakp->_valid = true;
	}
    }
}

