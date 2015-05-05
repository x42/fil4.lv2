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


#ifndef __FILTERS_H
#define __FILTERS_H

#include <math.h>
#include "ladspaplugin.h"


class Paramsect
{
public:

    enum { SECT, FREQ, BAND, GAIN };

    void init (void)
    {
        _f = 0.25f;
        _b = _g = 1.0f;
	_a = _s1 = _s2 = _z1 = _z2 = 0.0f;
    }
    
    void proc (int k, float *sig, float f, float b, float g)
    {
        float s1, s2, d1, d2, a, da, x, y;
        bool  u2 = false;

        s1 = _s1;        
        s2 = _s2;
        a = _a; 
        d1 = 0;
        d2 = 0;
        da = 0;

        if (f != _f)
	{
  	    if      (f < 0.5f * _f) f = 0.5f * _f;
            else if (f > 2.0f * _f) f = 2.0f * _f;
            _f = f;
            _s1 = -cosf (6.283185f * f);
            d1 = (_s1 - s1) / k;           
            u2 = true;
	}             
       
        if (g != _g)
	{
            if      (g < 0.5f * _g) g = 0.5f * _g;
            else if (g > 2.0f * _g) g = 2.0f * _g;
	    _g = g;
            _a = 0.5f * (g - 1.0f);
            da = (_a - a) / k;
            u2 = true;
	}

        if (b != _b)
	{
            if      (b < 0.5f * _b) b = 0.5f * _b;
            else if (b > 2.0f * _b) b = 2.0f * _b;
            _b = b; 
            u2 = true;
	}

        if (u2)
	{
            b *= 7 * f / sqrtf (g);         
            _s2 = (1 - b) / (1 + b);
            d2 = (_s2 - s2) / k;        
	}

        while (k--)
	{  
	    s1 += d1;
	    s2 += d2;
            a += da;
            x = *sig;
            y = x - s2 * _z2;
            *sig++ -= a * (_z2 + s2 * y - x);                           
            y -= s1 * _z1;
            _z2 = _z1 + s1 * y;
            _z1 = y + 1e-10f;
	}
    }
    
private:

    float  _f, _b, _g;
    float  _s1, _s2, _a;
    float  _z1, _z2;
};


class Ladspa_Paramfilt : public LadspaPlugin
{
public:

    enum { AIP, AOP, FILT, GAIN, SECT, NSECT = 4, NPORT = 20 };

    Ladspa_Paramfilt (unsigned long fsam) : LadspaPlugin (fsam) {}
    virtual void setport (unsigned long port, LADSPA_Data *data);  
    virtual void active  (bool act);  
    virtual void runproc (unsigned long len, bool add);
    virtual ~Ladspa_Paramfilt (void) {}  

private:

    float     *_port [NPORT];
    float      _gain;
    int        _fade;
    Paramsect  _sect [NSECT];
};


#endif
