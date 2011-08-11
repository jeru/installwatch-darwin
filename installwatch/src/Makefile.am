lib_LTLIBRARIES = libinstallwatch.la
libinstallwatch_la_SOURCES = installwatch.c
libinstallwatch_la_CFLAGS = -D_GNU_SOURCE -D_REENTRANT
libinstallwatch_la_LDFLAGS = @installwatch_libs@

bin_SCRIPTS = installwatch
nodist_bin_SCRIPTS = libinstallwatch_libpath.sh

installwatch: installwatch.sh_in $(top_builddir)/config.status libinstallwatch_libpath.sh
	$(top_builddir)/config.status --file=installwatch.sh_tmp:$<
	$(SHELL) libinstallwatch_libpath.sh installwatch.sh_tmp > $@
	rm -f installwatch.sh_tmp

libinstallwatch_libpath.sh: libinstallwatch.la
	cat $< > libinstallwatch_libpath.tmp
	echo "echo \$$libdir/\$$dlname" >> libinstallwatch_libpath.tmp
	echo "LIBPATH=\"`$(SHELL) libinstallwatch_libpath.tmp`\"" > $@
	echo "$(SED) s,@INSTALLWATCH_LIBPATH@,\"\\'\$$LIBPATH\\'\",g \"\$$1\"" >> $@
	rm -f libinstallwatch_libpath.tmp
