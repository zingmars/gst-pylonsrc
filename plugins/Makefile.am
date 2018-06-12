plugin_LTLIBRARIES = libgstpylonsrc.la libgstfpsfilter.la

# sources used to compile this plug-in
libgstpylonsrc_la_SOURCES = gstpylonsrc.c gstpylonsrc.h
libgstfpsfilter_la_SOURCES = gstfpsfilter.c gstfpsfilter.h

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstpylonsrc_la_CFLAGS = $(GST_CFLAGS)
libgstpylonsrc_la_LIBADD = $(GST_LIBS) 
libgstpylonsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpylonsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstfpsfilter_la_CFLAGS = $(GST_CFLAGS)
libgstfpsfilter_la_LIBADD = $(GST_LIBS) 
libgstfpsfilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfpsfilter_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)