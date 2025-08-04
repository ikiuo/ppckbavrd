# -*- Makefile-gmake -*-

NAME     = ppckbavrd

MAJOR_VERSION = 0
MINOR_VERSION = 1
DEBUG_VERSION = 0
VERSION = $(MAJOR_VERSION).$(MINOR_VERSION).$(DEBUG_VERSION)

SBIN_DIR = $(DESTDIR)/usr/sbin
INIT_DIR = $(DESTDIR)/etc/init.d
SUPP_DIR = $(DESTDIR)/etc/$(NAME)

SUPPS    = allevent # event-??

CC       = gcc

CDEFS   += -DMAJOR_VERSION=$(MAJOR_VERSION)
CDEFS   += -DMINOR_VERSION=$(MINOR_VERSION)
CDEFS   += -DDEBUG_VERSION=$(DEBUG_VERSION)
CDEFS   += -DVERSION=\"$(VERSION)\"

CWARNS  += -W -Wall

COPTS   += -Os

CGENS   += -fPIC

CFLAGS   = $(CDEFS) $(CWARNS) $(COPTS)

PROGRAM  = $(NAME)

.PHONY: all build clean install

all: build

clean:
	rm -f $(PROGRAM)

build: $(PROGRAM)

install:
	@[ $$EUID -eq 0 ] && exit 0 || (echo "you must be root to install software."; exit 2)
	mkdir -p $(SBIN_DIR) $(INIT_DIR) $(SUPP_DIR)
	install -m 755 $(PROGRAM) $(SBIN_DIR)
	install -m 755 script/$(NAME) $(INIT_DIR)
	install -m 755 $(SUPPS:%=script/%) $(SUPP_DIR)

$(PROGRAM): ppckbavrd.c
	$(CC) $(CFLAGS) $< -o $@

DEBSRCDIR = ppckbavrd-$(VERSION)

deb-pkg: deb-pkg-build
deb-pkg-archive: clean
	rm -rf ../$(DEBSRCDIR)
	mkdir -p ../$(DEBSRCDIR)
	cp -rp . ../$(DEBSRCDIR)
	rm ../$(DEBSRCDIR)/Makefile.local
	cd ..; tar zcvf $(DEBSRCDIR).tar.gz $(DEBSRCDIR)
deb-pkg-build: deb-pkg-archive
	cd ../$(DEBSRCDIR);\
	echo -e "\n" | dh_make -s -c gpl -e seiichi@ikiuo.dev;\
	rm -f debian/*.ex;\
	rm -f debian/ex.*;\
	rm -f debian/README.Debian;\
	rm -f debian/emacsen-*;\
	cp -p ../ppckbavrd/script/ppckbavrd debian/init;\
	cp -p ../ppckbavrd/script/debian-conffiles debian/conffiles;\
	cp -p ../ppckbavrd/script/debian-control debian/control;\
	cp -p ../ppckbavrd/script/debian-dirs debian/dirs;\
	cp -p ../ppckbavrd/script/debian-preinst debian/preinst;\
	cp -p ../ppckbavrd/script/debian-postinst debian/postinst;\
	cp -p ../ppckbavrd/script/debian-rules debian/rules;\
	debuild -us -uc

-include Makefile.local
