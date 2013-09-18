#@ Makefile for S-nail.
#@ Adjustments have to be made in `conf.rc' (or on the command line).
#@ See the file `INSTALL' if you need help.

.POSIX:
.PHONY: all install uninstall clean distclean

all:
	@$(_prego) && $(MAKE) -f mk.mk all
install:
	@$(_prego) && $(MAKE) -f mk.mk install
uninstall:
	@$(_prestop) && $(MAKE) -f mk.mk uninstall
clean:
	@$(_prestop) && $(MAKE) -f mk.mk clean
distclean:
	@$(_prestop) && $(MAKE) -f mk.mk distclean

_update-version:
	@$(_prego) && $(MAKE) -f mk.mk _update-version
_buh:
	@WANT_ASSERTS=1 WANT_NOALLOCA=1; export WANT_ASSERTS WANT_NOALLOCA;\
		$(_prego) && $(MAKE) -f mk.mk _update-version &&\
		$(MAKE) -f mk.mk all
_update-release:
	@$(_prego) && $(MAKE) -f mk.mk _update-release

_prego = $(SHELL) ./mk-conf.sh
_prestop = [ -f ./mk.mk ] || {\
	echo 'S-nail is not configured, nothing to do';\
	exit 1;\
	}

# vim:set fenc=utf-8 filetype=make:
