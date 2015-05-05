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

//-----------------------------------------------------------------------------------
// Common definitions


#include "filters.h"

#define NMODS 1
#define VERSION "0.3.0"


static const char* maker = "Fons Adriaensen <fons@kokkinizita.net>";
static const char* copyr = "GPL";


static void pconnect (LADSPA_Handle H, unsigned long port, LADSPA_Data *data)
{
    ((LadspaPlugin *)H)->setport (port, data);
}

static void activate (LADSPA_Handle H)
{
    ((LadspaPlugin *)H)->active (true);
}

static void runplugin (LADSPA_Handle H, unsigned long k)
{
    ((LadspaPlugin *)H)->runproc (k, false);
}
/*
static void runadding (LADSPA_Handle H, unsigned long k)
{
    ((LadspaPlugin *)H)->runproc (k, true);
}

static void setadding (LADSPA_Handle H, LADSPA_Data gain)
{
    ((LadspaPlugin *)H)->setgain (gain);
}
*/
static void deactivate (LADSPA_Handle H)
{
    ((LadspaPlugin *)H)->active (false);
}

static void cleanup (LADSPA_Handle H)
{
    delete (LadspaPlugin *) H;
}

//-----------------------------------------------------------------------------------
// Plugin definitions


static const char* label0 = "Parametric1";
static const char* name0  = "4-band parametric filter";


static LADSPA_Handle instant0 (const struct _LADSPA_Descriptor *desc, unsigned long rate)
{
    return new Ladspa_Paramfilt (rate);
}


static const LADSPA_PortDescriptor pdesc0 [Ladspa_Paramfilt::NPORT] = 
{
    LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL
};


static const char * const pname0  [Ladspa_Paramfilt::NPORT] = 
{
  "Input",
  "Output",
  "Filter",
  "Gain",
  "Section 1",
  "Frequency 1",
  "Bandwidth 1",
  "Gain 1",
  "Section 2",
  "Frequency 2",
  "Bandwidth 2",
  "Gain 2",
  "Section 3",
  "Frequency 3",
  "Bandwidth 3",
  "Gain 3",
  "Section 4",
  "Frequency 4",
  "Bandwidth 4",
  "Gain 4"
};


static const LADSPA_PortRangeHint phint0 [Ladspa_Paramfilt::NPORT] = 
{
  { 0, 0, 0 },
  { 0, 0, 0 },
  { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0, 1 },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -20, 20 },
  { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0, 1 },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE, 2e1f, 2e3f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_1, 0.125f, 8.0f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -20, 20 },
  { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0, 1 },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE, 4e1f, 4e3f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_1, 0.125f, 8.0f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -20, 20 },
  { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0, 1 },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE, 1e2f, 1e4f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_1, 0.125f, 8.0f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -20, 20 },
  { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0, 1 },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_MIDDLE, 2e2f, 2e4f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_1, 0.125f, 8.0f },
  { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE | LADSPA_HINT_DEFAULT_0, -20, 20 }
};


static const LADSPA_Descriptor moddescr [NMODS] =
{
  {
    1970,
    label0,
    LADSPA_PROPERTY_REALTIME | LADSPA_PROPERTY_HARD_RT_CAPABLE,
    name0,
    maker,
    copyr,
    Ladspa_Paramfilt::NPORT,
    pdesc0,
    pname0,
    phint0,
    0,
    instant0,
    pconnect,
    activate,
    runplugin,
    0,
    0,
    deactivate,
    cleanup
  }  
};


extern "C" const LADSPA_Descriptor *ladspa_descriptor (unsigned long i)
{
  if (i >= NMODS) return 0;
  return moddescr + i;
}

//-----------------------------------------------------------------------------------
