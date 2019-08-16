fil4.lv2 - Parametric Equalizer
===============================

fil4.lv2 is a 4 band parametric equalizer with additional low+high shelf
filters, Low and High-pass, as well as an optional, custom GUI displaying
the transfer function and realtime signal spectrum or spectrogram.

It is available as [LV2 plugin](http://lv2plug.in/) and standalone
[JACK](http://jackaudio.org/)-application.


Usage
-----

The parameters can be set by moving the nodes in the graph or directly
via control knobs:

*   Shift + click: reset to default
*   Right-click on knob: toggle current value with default, 2nd click restore.
*   Right-click on button: temporarily toggle, until release

The Ctrl key allows for fine-grained control when dragging or
using the mouse-wheel on a knob.

Mouse-wheel granularity:
*   Gain: 1dB (fine: 0.2dB)
*   Frequency: 1/6 octave (fine: 1/24 octave)
*   Bandwidth: 1/3 octave (fine: 15 steps for a ratio 1:2)

All switches and controls are internally smoothed, so they can be
used 'live' without any clicks or zipper noises. This should make
this plugin a good candidate for use in systems that allow automation
of plugin control ports, such as Ardour, or for stage use.


Install
-------

Binaries for Intel-platform (GNU/Linux, OSX and Windows) are available
for [releases](https://github.com/x42/fil4.lv2/releases). Most GNU/Linux
distributions include this fil4 as part of the x42-plugins collection.


Compiling fil4 requires the LV2 SDK, jack-headers, gnu-make, a c++-compiler,
libpango, libcairo and openGL (sometimes called: glu, glx, mesa).

```bash
  git clone git://github.com/x42/fil4.lv2.git
  cd fil4.lv2
  make submodules
  make
  sudo make install PREFIX=/usr
```

Note to packagers: the Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CXXLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CXXFLAGS`), also
see the first 10 lines of the Makefile.
You really want to package the superset of [x42-plugins](https://github.com/x42/x42-plugins).


Screenshots
-----------

![screenshot](https://raw.github.com/x42/fil4.lv2/master/img/fil4_v6.png "Fil4 GUI")


Details
-------

Fil4 is based on fil-plugins LADSPA by Fons Adriaensen.

Fil4 consists of four 2nd order resonant filters using a Regalia-Mitra
style lattice filter, which has the nice property of being stable
even while parameters are being changed.

The high/low-shelf filters are standard 2nd order biquad/IIR filters.

High and Low pass are 2nd order resonant filters (-12dB/octave).
*   Quality 0.0: -6dB at cutoff-freq (no feedback)
*   Quality 0.7: -3dB at cutoff-freq
*   Quality 1.0:  0dB at cutoff (resonant)

All filters are zero latency with correct equivalent analog gain at Nyquist
(signal phase-shift at Nyquist frequency is zero).


Why another EQ?
---------------

Because I was unhappy with all existing ones: they are either not portable
(OSX, Windows, BSD, GNU/Linux,..), or simply unprofessional textbook biquad
filters (phase-shifts, not decramped, comb-filter effect when values are
changed), or lack important attention to the detail (control knob granularity),
or are not available as LV2, or a combination of those issues.
