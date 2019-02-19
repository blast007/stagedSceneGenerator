lib_LTLIBRARIES = stagedSceneGenerator.la

stagedSceneGenerator_la_SOURCES = stagedSceneGenerator.cpp
stagedSceneGenerator_la_CPPFLAGS= -I$(top_srcdir)/include -I$(top_srcdir)/plugins/plugin_utils
stagedSceneGenerator_la_LDFLAGS = -module -avoid-version -shared
stagedSceneGenerator_la_LIBADD = $(top_builddir)/plugins/plugin_utils/libplugin_utils.la

AM_CPPFLAGS = $(CONF_CPPFLAGS)
AM_CFLAGS = $(CONF_CFLAGS)
AM_CXXFLAGS = $(CONF_CXXFLAGS)

EXTRA_DIST = \
	README.stagedSceneGenerator.txt \
	stagedSceneGenerator.cfg \
	stagedSceneGenerator.sln \
	stagedSceneGenerator.vcxproj \
	stagedSceneGenerator.vcxproj.filters

MAINTAINERCLEANFILES =	\
	Makefile.in
