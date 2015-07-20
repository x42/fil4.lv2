#!/usr/bin/make -f

# these can be overridden using make variables. e.g.
#   make CFLAGS=-O2
#   make install DESTDIR=$(CURDIR)/debian/meters.lv2 PREFIX=/usr
#
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1
# see http://lv2plug.in/pages/filesystem-hierarchy-standard.html, don't use libdir
LV2DIR ?= $(PREFIX)/lib/lv2

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only -DNDEBUG
CXXFLAGS ?= -Wall -g -Wno-unused-function
STRIP  ?= strip

BUILDOPENGL?=yes
BUILDJACKAPP?=yes

fil4_VERSION ?= $(shell (git describe --tags HEAD || echo "0") | sed 's/-g.*$$//;s/^v//')

###############################################################################

BUILDDIR ?= build/
APPBLD   ?= x42/
RW       ?= robtk/

###############################################################################

LOADLIBES=-lm
LV2NAME=fil4
LV2GUI=fil4UI_gl
BUNDLE=fil4.lv2
targets=

STRIPFLAGS=-s
GLUICFLAGS=-I.

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXE_EXT=
  UI_TYPE=ui:CocoaUI
  PUGL_SRC=$(RW)pugl/pugl_osx.m
  PKG_GL_LIBS=
  GLUILIBS=-framework Cocoa -framework OpenGL -framework CoreFoundation
  STRIPFLAGS=-u -r -arch all -s $(RW)lv2syms
  EXTENDED_RE=-E
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed
  LIB_EXT=.so
  EXE_EXT=
  UI_TYPE=ui:X11UI
  PUGL_SRC=$(RW)pugl/pugl_x11.c
  PKG_GL_LIBS=glu gl
  GLUILIBS=-lX11
  GLUICFLAGS+=`pkg-config --cflags glu`
  STRIPFLAGS= -s
  EXTENDED_RE=-r
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  CXX=$(XWIN)-g++
  STRIP=$(XWIN)-strip
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed
  LIB_EXT=.dll
  EXE_EXT=.exe
  PUGL_SRC=$(RW)pugl/pugl_win.cpp
  PKG_GL_LIBS=
  UI_TYPE=
  GLUILIBS=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
  GLUICFLAGS=-I.
  override LDFLAGS += -static-libgcc -static-libstdc++
endif


ifeq ($(UI_TYPE),)
  UI_TYPE=kx:Widget
  LV2UIREQ+=lv2:requiredFeature kx:Widget;
  override CXXFLAGS += -DXTERNAL_UI
endif

targets+=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(BUILDOPENGL), no)
  targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)
endif

###############################################################################
# extract versions
LV2VERSION=$(fil4_VERSION)
include git2lv2.mk

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifneq ($(shell test -f fftw-3.3.4/.libs/libfftw3f.a || echo no), no)
  FFTW_CFLAGS=-Ifftw-3.3.4/api fftw-3.3.4/.libs/libfftw3f.a -lm
  FFTW_LIBS=fftw-3.3.4/.libs/libfftw3f.a -lm
else
  ifeq ($(shell pkg-config --exists fftw3f || echo no), no)
    $(error "fftw3f library was not found")
  endif
  FFTW_LIBS=`pkg-config --variable=libdir fftw3f`/libfftw3f.a
  ifeq ($(shell test -f $(FFTW_LIBS) || echo no), no)
    FFTW_LIBS=`pkg-config --libs fftw3f`
  endif
  $(warning "**********************************************************")
  $(warning "           the fftw3 library is not thread-safe           ")
  $(warning "**********************************************************")
  $(info )
  $(info see https://github.com/FFTW/fftw3/issues/16 for further info.)
  $(info )
  ifneq ("$(wildcard static_fft.sh)","")
  $(info   run    ./static_fft.sh)
  $(info   prior to make to create compile static lib specific for this)
  $(info   plugin to avoid the issue.)
  $(info )
  endif
  $(warning "**********************************************************")
  $(info )
  $(eval FFTW_CFLAGS=`pkg-config --cflags fftw3f`)
  $(eval FFTW_LIB=$(FFTW_LIBS) -lm)
endif
export FFTW_CFLAGS
export FFTW_LIBS

ifeq ($(shell pkg-config --atleast-version=1.6.0 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.6.0 or later")
endif

ifeq ($(shell pkg-config --exists pango cairo $(PKG_GL_LIBS) || echo no), no)
  $(error "This plugin requires cairo pango $(PKG_GL_LIBS)")
endif

ifneq ($(BUILDJACKAPP), no)
 ifeq ($(shell pkg-config --exists jack || echo no), no)
  $(warning *** libjack from http://jackaudio.org is required)
  $(error   Please install libjack-dev or libjack-jackd2-dev)
 endif
 JACKAPP=$(APPBLD)x42-fil4$(EXE_EXT)
endif


# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CXXFLAGS += -DHAVE_LV2_1_8
endif

ifneq ($(MAKECMDGOALS), submodules)
  ifeq ($(wildcard $(RW)robtk.mk),)
    $(warning "**********************************************************")
    $(warning This plugin needs https://github.com/x42/robtk)
    $(warning "**********************************************************")
    $(info )
    $(info set the RW environment variale to the location of the robtk headers)
    ifeq ($(wildcard .git),.git)
      $(info or run 'make submodules' to initialize robtk as git submodule)
    endif
    $(info )
    $(warning "**********************************************************")
    $(error robtk not found)
  endif
endif

# LV2 idle >= lv2-1.6.0
GLUICFLAGS+=-DHAVE_IDLE_IFACE
LV2UIREQ+=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;

# add library dependent flags and libs
override CXXFLAGS += $(OPTIMIZATIONS) -DVERSION="\"$(fil4_VERSION)\""
override CXXFLAGS += `pkg-config --cflags lv2`
ifeq ($(XWIN),)
override CXXFLAGS += -fPIC -fvisibility=hidden
else
override CXXFLAGS += -DPTW32_STATIC_LIB
endif


GLUICFLAGS+=`pkg-config --cflags cairo pango` $(value FFTW_CFLAGS) $(CXXFLAGS)
GLUILIBS+=`pkg-config $(PKG_UI_FLAGS) --libs cairo pango pangocairo $(PKG_GL_LIBS)` $(value FFTW_LIBS)

ifneq ($(XWIN),)
GLUILIBS+=-lpthread -lusp10
endif

GLUICFLAGS+=$(LIC_CFLAGS)
GLUILIBS+=$(LIC_LOADLIBES)

#GLUICFLAGS+=-DUSE_GUI_THREAD
ifeq ($(GLTHREADSYNC), yes)
  GLUICFLAGS+=-DTHREADSYNC
endif

ROBGL+= Makefile

JACKCFLAGS=-I. $(CXXFLAGS) $(LIC_CFLAGS)
JACKCFLAGS+=`pkg-config --cflags jack lv2 pango pangocairo $(PKG_GL_LIBS)`
JACKLIBS=-lm $(GLUILIBS) $(LIC_LOADLIBES)


# build target definitions
default: all

submodule_pull:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_pull

submodule_update:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_update

submodule_check:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_check

submodules:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodules

all: submodule_check $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets) $(JACKAPP)

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/;s/@LV2GUI@/$(LV2GUI)/g" \
		lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: Makefile lv2ttl/$(LV2NAME).ttl.in \
	lv2ttl/$(LV2NAME).ports.ttl.in lv2ttl/$(LV2NAME).mono.ttl.in lv2ttl/$(LV2NAME).stereo.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/" \
	    lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@URISUFFIX@/mono/;s/@NAMESUFFIX@/ Mono/;s/@CTLSIZE@/65888/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
	    lv2ttl/$(LV2NAME).ports.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat lv2ttl/$(LV2NAME).mono.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@URISUFFIX@/stereo/;s/@NAMESUFFIX@/ Stereo/;s/@CTLSIZE@/131424/;s/@VERSION@/lv2:microVersion $(LV2MIC) ;lv2:minorVersion $(LV2MIN) ;/g" \
	    lv2ttl/$(LV2NAME).ports.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl

DSP_SRC = src/lv2.c
DSP_DEPS = $(DSP_SRC) src/filters.h src/iir.h src/hip.h src/uris.h src/lop.h
GUI_DEPS = gui/analyser.cc gui/analyser.h gui/fft.c gui/fil4.c src/uris.h src/lop.h

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): $(DSP_DEPS) Makefile
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DSP_SRC) \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

jackapps: $(JACKAPP)

$(eval x42_fil4_JACKSRC = -DX42_MULTIPLUGIN src/lv2.c)
x42_fil4_JACKGUI = gui/fil4.c
x42_fil4_LV2HTTL = lv2ttl/plugins.h
x42_fil4_JACKDESC = lv2ui_descriptor
$(APPBLD)x42-fil4$(EXE_EXT): $(DSP_DEPS) $(GUI_DEPS) \
	        $(x42_fil4_JACKGUI) $(x42_fil4_LV2HTTL)

-include $(RW)robtk.mk

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/fil4.c

###############################################################################
# install/uninstall/clean target definitions

install: install-bin install-man

uninstall: uninstall-bin uninstall-man

install-bin: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
ifneq ($(BUILDOPENGL), no)
	install -m755 $(BUILDDIR)$(LV2GUI)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
endif
ifneq ($(BUILDJACKAPP), no)
	install -d $(DESTDIR)$(BINDIR)
	install -m755 $(APPBLD)x42-fil4$(EXE_EXT) $(DESTDIR)$(BINDIR)
endif

uninstall-bin:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	rm -f $(DESTDIR)$(BINDIR)/x42-fil4$(EXE_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	-rmdir $(DESTDIR)$(BINDIR)

install-man:
ifneq ($(BUILDJACKAPP), no)
	install -d $(DESTDIR)$(MANDIR)
	install -m644 x42-fil4.1 $(DESTDIR)$(MANDIR)
endif

uninstall-man:
	rm -f $(DESTDIR)$(MANDIR)/x42-fil4.1
	-rmdir $(DESTDIR)$(MANDIR)

man: $(APPBLD)x42-fil4
	help2man -N -o x42-fil4.1 -n "x42 JACK Parametric Equalizer" $(APPBLD)x42-fil4

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)$(LV2GUI)$(LIB_EXT)
	rm -rf $(BUILDDIR)*.dSYM
	rm -rf $(APPBLD)x42-*
	-test -d $(APPBLD) && rmdir $(APPBLD) || true
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean jackapps man \
        install-bin uninstall-bin install-man uninstall-man \
        submodule_check submodules submodule_update submodule_pull
