# Makefile for installwatch
# $Id: Makefile,v 0.7.0.5 2006/11/01 06:39:28 izto Exp $

# Well, the only configurable part is the following variable.
# Make sure the directory you specify exists.

PREFIX?=/usr/local

# End of configurable part

OS:=$(shell uname)
ifeq ($(OS),Darwin)
SHLIB_SUFFIX=.dylib
LD_SHLIB_FLAGS=-dylib
INSTALL_FLAGS=
else
SHLIB_SUFFIX=.so
LD_SHLIB_FLAGS=-shared
INSTALL_FLAGS=-s
CFLAGS+=-D_GNU_SOURCE
endif

VERSION=0.7.0beta5

BINDIR=$(PREFIX)/bin
LIBDIR=$(PREFIX)/lib
INSTW_LIB=$(LIBDIR)/installwatch$(SHLIB_SUFFIX)

all: installwatch$(SHLIB_SUFFIX)

installwatch$(SHLIB_SUFFIX): installwatch.o
	ld $(LD_SHLIB_FLAGS) -o $@ installwatch.o -ldl -lc

installwatch.o: installwatch.c localdecls.h
	gcc -g -Wall -c $(CFLAGS) -DPIC -fPIC -D_REENTRANT -DVERSION=\"$(VERSION)\" installwatch.c

localdecls.h:  create-localdecls*
	./create-localdecls

install: all
	mkdir -p $(LIBDIR)
	mkdir -p $(BINDIR)
	if [ -r $(LIBDIR)/installwatch$(SHLIB_SUFFIX) ]; then \
		rm -f  $(LIBDIR)/installwatch$(SHLIB_SUFFIX); \
	fi
	install $(INSTALL_FLAGS) installwatch$(SHLIB_SUFFIX) $(INSTW_LIB)
	
	sed -e "s|#PREFIX#|$(PREFIX)|" < installwatch > $(BINDIR)/installwatch
	chmod 755 $(BINDIR)/installwatch

uninstall:
	rm -f $(LIBDIR)/installwatch$(SHLIB_SUFFIX)
	rm -f $(BINDIR)/installwatch
	
clean:
	rm -f *~ *.bak *.o installwatch$(SHLIB_SUFFIX) core localdecls.h libctest test-installwatch

tarball: clean
	tar -czvC .. -f ../installwatch-$(VERSION).tar.gz installwatch-$(VERSION)

test: install test-installwatch.c
	gcc -g -Wall -DVERSION=\"$(VERSION)\" -DLIB=\"$(INSTW_LIB)\" -o test-installwatch test-installwatch.c -ldl
	$(PREFIX)/bin/installwatch ./test-installwatch
