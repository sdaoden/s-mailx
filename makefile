#@ Makefile for S-nail.
#@ Adjustments have to be made in `conf.rc' (or on the command line).
#@ See the file `INSTALL' if you need help.

.PHONY: all install uninstall clean distclean config build test

all: config
	@LC_ALL=C $(MAKE) -f mk.mk all
install: all
	@LC_ALL=C $(MAKE) -f mk.mk install
uninstall:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk uninstall
clean:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk clean
distclean:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk distclean

config:
	@$(_prego)
build:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk all
test:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk test
packager-install:
	@$(_prestop) && LC_ALL=C $(MAKE) -f mk.mk install

_update-version:
	@$(_prego) && LC_ALL=C $(MAKE) -f mk.mk _update-version
_buh:
	@WANT_GSSAPI=1 WANT_DEBUG=1 WANT_NOALLOCA=1;\
	export WANT_GSSAPI WANT_DEBUG WANT_NOALLOCA;\
	$(_prego) && LC_ALL=C $(MAKE) -f mk.mk _update-version &&\
	LC_ALL=C $(MAKE) -f mk.mk all
_update-release:
	@$(_prego) && LC_ALL=C $(MAKE) -f mk.mk _update-release

_prego = $(SHELL) ./mk-conf.sh
_prestop = [ -f ./mk.mk ] || {\
	echo 'S-nail is not configured, nothing to do';\
	exit 1;\
	}

# vim:set fenc=utf-8 filetype=make:
