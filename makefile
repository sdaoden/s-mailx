#@ Makefile for S-nail.
#@ Adjustments have to be made in make.rc -- or on the command line.
#@ See the file INSTALL if you need help.

.PHONY: ohno tangerine citron \
	all config build install uninstall clean distclean test \
	devel odevel

CWDDIR=./
SRCDIR=./

ohno: build
tangerine: config build test install
citron: config build install
all: config build

config:
	@$(_prego)
build:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk $(MAKEJOBS) all
install packager-install: build
	@$(_prestop);\
	LC_ALL=C $${MAKE} -f mk-config.mk DESTDIR="$(DESTDIR)" install
uninstall:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk uninstall

clean:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk clean
distclean:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk distclean

test:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk $(MAKEJOBS) test

devel:
	@CONFIG=DEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f mk-config.mk _update-version &&\
	LC_ALL=C $${MAKE} -f mk-config.mk $(MAKEJOBS) all
odevel:
	@CONFIG=ODEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f mk-config.mk _update-version &&\
	LC_ALL=C $${MAKE} -f mk-config.mk $(MAKEJOBS) all
d-b:
	@$(_prestop);\
	LC_ALL=C $${MAKE} -f mk-config.mk _update-version &&\
	LC_ALL=C $${MAKE} -f mk-config.mk $(MAKEJOBS) all

d-gettext:
	cd "$(SRCDIR)" &&\
	 LC_ALL=C xgettext --sort-by-file --strict --add-location \
		--from-code=UTF-8 --keyword --keyword=_ --keyword=N_ \
		--add-comments=I18N --foreign-user \
		-o messages.pot *.c *.h

_prego = SHELL="$(SHELL)" MAKE="$(MAKE)" CWDDIR="$(CWDDIR)" SRCDIR="$(SRCDIR)" \
	CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
	$(SHELL) "$(SRCDIR)"make-config.sh || exit 1
_prestop = if [ -f ./mk-config.mk ]; then :; else \
		echo 'Program not configured, nothing to do';\
		echo 'Use one of the targets: config, all, tangerine';\
		exit 1;\
	fi;\
	< ./mk-config.ev read __ev__; eval $${__ev__}; unset __ev__

# s-mk-mode
