check_PROGRAMS = test_installwatch
test_installwatch_SOURCES =test-installwatch.c
test_installwatch_LDFLAGS = @installwatch_libs@

installcheck-local: insttest_installwatch.sh
	$(SHELL) insttest_installwatch.sh

insttest_installwatch.sh: insttest_installwatch.sh_in $(top_builddir)/config.status ../src/libinstallwatch_libpath.sh test_installwatch
	$(top_builddir)/config.status --file=$(@:.sh=.sh_tmp):$<
	$(SHELL) ../src/libinstallwatch_libpath.sh $(@:.sh=.sh_tmp) > $@
	rm -f $(@:.sh=.sh_tmp)
	chmod +x $@
