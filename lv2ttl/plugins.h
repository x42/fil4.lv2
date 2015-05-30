#define MULTIPLUGIN 1
#define X42_MULTIPLUGIN_NAME "Parametric EQ"
#define X42_MULTIPLUGIN_URI "http://gareus.org/oss/lv2/fil4"

#include "lv2ttl/fil4_mono.h"
#include "lv2ttl/fil4_stereo.h"

static const RtkLv2Description _plugins[] = {
	_plugin_stereo,
	_plugin_mono,
};

