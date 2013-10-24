SUBDIRS = elements tracers

DIST_SUBDIRS = elements tracers

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:SUBDIR $(patsubst %,plugins/%, $(SUBDIRS)) \
	> $@
