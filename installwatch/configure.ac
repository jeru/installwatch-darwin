AC_INIT([installwatch-port], [0.7.0beta5])
AM_INIT_AUTOMAKE([-Wall -Werror])


######################
# check for programs #
######################

AC_PROG_AWK
AC_PROG_CC
AC_PROG_EGREP
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_SED
AC_PROG_LIBTOOL

AC_CHECK_TOOL([OTOOL], [otool], [:])
AC_CHECK_TOOL([LDD], [ldd], [:])
if test "$OTOOL" = ":" && test "$LDD" = ":"; then
	AC_MSG_ERROR([none of otool or ldd is found.])
fi


##########################
# check for header files #
##########################

AC_CHECK_HEADERS([ \
	sys/param.h \
	sys/stat.h \
	sys/types.h \
	dirent.h \
	dlfcn.h \
	errno.h \
	fcntl.h \
	libgen.h \
	stdarg.h \
	stdio.h \
	stdlib.h \
	string.h \
	time.h \
	unistd.h \
	utime.h \
])
AC_CHECK_HEADERS([ \
	syslog.h \
])


###############################
# check for library functions #
###############################

# Installwatch requires dlopen() and dlsym().
installwatch_libs=
LIBS=
AC_SEARCH_LIBS(dlopen, [dl])
AC_SEARCH_LIBS(dlsym, [dl])
installwatch_libs="$installwatch_libs $LIBS"
LIBS=
AC_SUBST(installwatch_libs)

# Injected functions should be load by dlopen() and dlsym(). Find the source
# file of dlopen().
# TODO(Cheng Sheng): Check whether "libc.so" (in Linux) or "libSystem.dylib"
#                    (in Darwin) are the only possibibilities.
AC_LANG([C])
AH_TEMPLATE([LIBC_PATH], [Path to libc.so or libSystem.dylib])
AC_LINK_IFELSE([AC_LANG_PROGRAM()], [
	if test "$OTOOL" = ":"; then
		$LDD conftest$EXEEXT > conftest.shlist
	else
		$OTOOL -L conftest$EXEEXT > conftest.shlist
	fi
	$EGREP ['(libc|libSystem)[^_a-zA-Z0-9].*\.(so|dylib)'] conftest.shlist \
		| $AWK '{print $1}' > conftest.shres
	LIBC_PATH=`cat conftest.shres`
], [AC_MSG_FAILURE([Unexpected failure to compile.])])
AC_SUBST_FILE([LIBC_PATH])
AC_DEFINE_UNQUOTED([LIBC_PATH], ["${LIBC_PATH}"])

# POSIX io functions
AC_CHECK_FUNCS([ \
	access \
	chdir \
	chmod \
	chown \
	chroot \
	creat \
	fchmod \
	fchown \
	fopen \
	ftruncate \
	getcwd \
	lchown \
	link \
	mkdir \
	mknod \
	open \
	opendir \
	readdir \
	readlink \
	realpath \
	rename \
	rmdir \
	scandir \
	stat \
	lstat \
	symlink \
	time \
	truncate \
	unlink \
	utime \
	utimes \
])

# Linux only:
# __xstat, __lxstat, __fxstat, __xmknod, etc.
AC_CHECK_FUNCS([ \
	__xstat \
	__lxstat \
	__fxstat \
	__xmknod \
])

# Large file support:
AC_CHECK_FUNCS([ \
	creat64 \
	fopen64 \
	ftruncate64 \
	open64 \
	readdir64 \
	scandir64 \
	stat64 \
	lstat64 \
	xstat64 \
	lxstat64 \
	truncate64 \
])


#################
# Miscellaneous #
#################

if test "X`uname`" = "XDarwin"; then
	INJECT_ENV='DYLD_INSERT_LIBRARIES=@INSTALLWATCH_LIBPATH@ DYLD_FORCE_FLAT_NAMESPACE=1'
else
	INJECT_ENV='LD_PRELOAD=@INSTALLWATCH_LIBPATH@'
fi
AC_SUBST(INJECT_ENV)

##########
# output #
##########

AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_FILES([
	Makefile
	src/Makefile
	tests/Makefile
])
AC_OUTPUT
