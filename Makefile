#!/usr/bin/make -f

OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only

PREFIX   ?= /usr/local
CXXFLAGS ?= -Wall -g -Wno-unused-function
STRIP    ?= strip
BUILDDIR ?= build/
APPBLD   ?= x42/
RW       ?= robtk/
LV2DIR   ?= $(PREFIX)/lib/lv2

###############################################################################
fil4_VERSION ?= $(shell (git describe --tags HEAD || echo "0") | sed 's/-g.*$$//;s/^v//')

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
targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)

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
  ifeq ($(shell test -f $(FFTWA) || echo no), no)
    FFTW_LIBS=`pkg-config --libs fftw3f`
  endif
  $(warning "**********************************************************")
  $(warning "           the fftw3 library is not thread-safe           ")
  $(warning "**********************************************************")
  $(info )
  $(info see https://github.com/FFTW/fftw3/issues/16 for further info.)
  $(info )
  $(info   run    ./static_fft.sh)
  $(info   prior to make to create compile static lib specific for this)
  $(info   plugin to avoid the issue.)
  $(info )
  $(warning "**********************************************************")
  $(info )
  $(eval FFTW_CFLAGS=`pkg-config --cflags fftw3f`)
  $(eval FFTW_LIB=$(FFTW_LIBS) -lm)
endif
export FFTW_CFLAGS
export FFTW_LIBS

ifeq ($(shell pkg-config --atleast-version=1.4.2 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.4.2 or later")
endif

ifeq ($(shell pkg-config --exists pango cairo $(PKG_GL_LIBS) || echo no), no)
  $(error "This plugin requires cairo pango $(PKG_GL_LIBS)")
endif

#ifeq ($(shell pkg-config --exists jack || echo no), no)
#  $(warning *** libjack from http://jackaudio.org is required)
#  $(error   Please install libjack-dev or libjack-jackd2-dev)
#endif

# check for LV2 idle thread
ifeq ($(shell pkg-config --atleast-version=1.4.2 lv2 && echo yes), yes)
  GLUICFLAGS+=-DHAVE_IDLE_IFACE
  LV2UIREQ+=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;
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

# add library dependent flags and libs
override CXXFLAGS += -fPIC $(OPTIMIZATIONS) -DVERSION="\"$(fil4_VERSION)\""
override CXXFLAGS += `pkg-config --cflags lv2`


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

all: submodule_check $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/;s/@UI_TYPE@/$(UI_TYPE)/;s/@LV2GUI@/$(LV2GUI)/g" \
		lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: Makefile lv2ttl/$(LV2NAME).ttl.in \
	lv2ttl/$(LV2NAME).ports.ttl.in lv2ttl/$(LV2NAME).mono.ttl.in lv2ttl/$(LV2NAME).stereo.ttl.in
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/" \
	    lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@URISUFFIX@/mono/;s/@NAMESUFFIX@/ Mono/;s/@CTLSIZE@/33120/" \
	    lv2ttl/$(LV2NAME).ports.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat lv2ttl/$(LV2NAME).mono.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@URISUFFIX@/stereo/;s/@NAMESUFFIX@/ Stereo/;s/@CTLSIZE@/65888/" \
	    lv2ttl/$(LV2NAME).ports.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	cat lv2ttl/$(LV2NAME).stereo.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl

DSP_SRC = src/lv2.c
DSP_DEPS = $(DSP_SRC) src/filters.h src/iir.h src/hip.h src/uris.h src/lop.h
GUI_DEPS = gui/analyser.cc gui/analyser.h gui/fft.c gui/fil4.c src/uris.h

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): $(DSP_DEPS) Makefile
	@mkdir -p $(BUILDDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(DSP_SRC) \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

jackapps: $(APPBLD)x42-fil4

$(eval x42_fil4_JACKSRC = -DX42_MULTIPLUGIN src/lv2.c)
x42_fil4_JACKGUI = gui/fil4.c
x42_fil4_LV2HTTL = lv2ttl/plugins.h
x42_fil4_JACKDESC = lv2ui_descriptor
$(APPBLD)x42-fil4$(EXE_EXT): $(DSP_DEPS) $(GUI_DEPS) \
	        $(x42_fil4_JACKGUI) $(x42_fil4_LV2HTTL)


BUILDGTK=no

-include $(RW)robtk.mk

$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/fil4.c

###############################################################################
# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(BUILDDIR)$(LV2NAME)$(LIB_EXT) $(BUILDDIR)$(LV2GUI)$(LIB_EXT) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

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

.PHONY: clean all install uninstall distclean jackapps
