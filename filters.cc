/*
    Copyright (C) 2004-2009 Fons Adriaensen <fons@kokkinizita.net>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#include <string.h>
#include "filters.h"


extern float exp2ap (float x);


void Ladspa_Paramfilt::setport (unsigned long port, LADSPA_Data *data)
{
    _port [port] = data;
}


void Ladspa_Paramfilt::active (bool act)
{
    int j;

    if (! act) return;
    _fade = 0;
    _gain = 1;
    for (j = 0; j < NSECT; j++) _sect [j].init ();   
}


void Ladspa_Paramfilt::runproc (unsigned long len, bool add)
{
    int   i, j, k;
    float *aip = _port [AIP];
    float *aop = _port [AOP];
    float *p, sig [48];
    float t, g, d;
    float fgain;
    float sfreq [NSECT];
    float sband [NSECT];
    float sgain [NSECT];

    fgain = exp2ap (0.1661 * _port [GAIN][0]);
    for (j = 0; j < NSECT; j++)
    {
	t = _port [SECT + 4 * j + Paramsect::FREQ][0] / _fsam; 
        if (t < 0.0002) t = 0.0002;
        if (t > 0.4998) t = 0.4998;
        sfreq [j] = t;        
        sband [j] = _port [SECT + 4 * j + Paramsect::BAND][0];
        if (_port [SECT + 4 * j + Paramsect::SECT][0] > 0) sgain [j] = exp2ap (0.1661 * _port [SECT + 4 * j + Paramsect::GAIN][0]);
        else sgain [j] = 1.0;
    }

    while (len)
    {
        k = (len > 48) ? 32 : len;
        
        t = fgain;
        g = _gain;
        if      (t > 1.25 * g) t = 1.25 * g;
        else if (t < 0.80 * g) t = 0.80 * g;
        _gain = t;
        d = (t - g) / k;
        for (i = 0; i < k; i++) 
	{
	    g += d;
            sig [i] = g * aip [i];
	}

        for (j = 0; j < NSECT; j++) _sect [j].proc (k, sig, sfreq [j], sband [j], sgain [j]);
                  
        j = _fade;
        g = j / 16.0;
	p = 0;
	if (_port [FILT][0] > 0)
	{
	    if (j == 16) p = sig;
	    else ++j;
	}
	else
	{
	    if (j == 0) p = aip;
	    else --j;
	}
	_fade = j;  
	if (p) memcpy (aop, p, k * sizeof (float));
	else
	{
	    d = (j / 16.0 - g) / k;
	    for (i = 0; i < k; i++)
	    {
		g += d;
		aop [i] = g * sig [i] + (1 - g) * aip [i];
	    }
	}
	aip += k;
	aop += k;
	len -= k;
    }
}

