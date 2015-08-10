IGNORE_HFILES=\
	$(top_srcdir)/gst/gettext.h \
	$(top_srcdir)/gst/glib-compat-private.h \
	$(top_srcdir)/gst/glib-compat.h \
	$(top_srcdir)/gst/gst-i18n-app.h \
	$(top_srcdir)/gst/gst-i18n-lib.h \
	$(top_srcdir)/gst/gst_private.h \
	$(top_srcdir)/gst/gstelementdetails.h \
	$(top_srcdir)/gst/gstmacros.h \
	$(top_srcdir)/gst/grammar.tab.h \
	$(top_srcdir)/gst/grammar.tab.pre.h \
	$(top_srcdir)/gst/math-compat.h \
	$(top_srcdir)/gst/types.h

ALLHFILES=$(wildcard $(top_srcdir)/gst/*.h)
HFILES=$(filter-out $(IGNORE_HFILES), $(ALLHFILES))

CFILES=$(wildcard $(top_srcdir)/gst/*.c)

MARKDOWN_FILES=$(wildcard $(srcdir)/markdown_files/*.markdown)
MARKDOWN_INDEX=$(srcdir)/markdown_files/index.markdown

CLANG_OPTIONS='-isystem /usr/include/glib-2.0 -isystem /usr/lib64/glib-2.0/include -I$(top_srcdir)/libs -I$(top_srcdir) -D_GNU_SOURCE -DGST_EXPORTS -DGST_API_VERSION=\\""1.0"\\" -DGST_DISABLE_DEPRECATED -DHAVE_CONFIG_H'

doc.stamp: $(HFILES) $(CFILES) $(MARKDOWN_FILES)
	@echo "Generating documentation with new tool"
	@CLANG_OPTIONS=$(CLANG_OPTIONS) bdt.py --filenames $(HFILES) $(CFILES)\
		      -i $(MARKDOWN_INDEX)\
		      --gobject-introspection-dump $(top_srcdir)/gst/tmp-introspect*/dump.xml\
		      -o html
	@touch doc.stamp

clean-local:
	@rm -f doc.stamp
	@rm -rf html

all-local: doc.stamp
