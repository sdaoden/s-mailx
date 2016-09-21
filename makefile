#@ Makefile for S-nail.
#@ Adjustments have to be made in "make.rc" -- or on the command line.
#@ See the file "INSTALL" if you need help.

.PHONY: all install uninstall clean distclean config build test

_not_all_: build
all: config
	@LC_ALL=C $(MAKE) -f ./mk.mk all
install: all
	@LC_ALL=C $(MAKE) -f ./mk.mk install
uninstall:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk uninstall
clean:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk clean
distclean:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk distclean

config:
	@$(_prego)
build:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk all
test:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk test
doinstall packager-install:
	@$(_prestop) && LC_ALL=C $(MAKE) -f ./mk.mk DESTDIR="$(DESTDIR)" install

devel:
	@CONFIG=DEVEL; export CONFIG;\
	$(_prego) && LC_ALL=C $(MAKE) -f ./mk.mk _update-version &&\
	LC_ALL=C $(MAKE) -f ./mk.mk all
odevel:
	@CONFIG=ODEVEL; export CONFIG;\
	$(_prego) && LC_ALL=C $(MAKE) -f ./mk.mk _update-version &&\
	LC_ALL=C $(MAKE) -f ./mk.mk all

_prego = SHELL="$(SHELL)" MAKE="$(MAKE)" \
	CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
	$(SHELL) ./mk-conf.sh
_prestop = if [ -f ./mk.mk ]; then :; else \
		echo 'Program not configured, nothing to do';\
		echo 'The following targets will work: config, [all], install';\
		exit 1;\
	fi

# s-mk-mode
