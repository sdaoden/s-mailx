#@ Makefile for S-nail.
#@ Adjustments have to be made in make.rc -- or on the command line.
#@ See the file INSTALL if you need help.

.PHONY: ohno tangerine all config build install uninstall clean distclean test \
	devel odevel

ohno: build
tangerine: config build test install
all: config build

config:
	@$(_prego)
build:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk $(MAKEJOBS) all
install packager-install: build
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk DESTDIR="$(DESTDIR)" install
uninstall:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk uninstall

clean:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk clean
distclean:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk distclean

test:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk $(MAKEJOBS) test

devel:
	@CONFIG=DEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAKEJOBS) all
odevel:
	@CONFIG=ODEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAKEJOBS) all
d-b:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAKEJOBS) all

d-gettext:
	LC_ALL=C xgettext --sort-by-file --strict --add-location \
		--from-code=UTF-8 --keyword --keyword=_ --keyword=N_ \
		--add-comments=I18N --foreign-user \
		-o messages.pot *.c *.h

_prego = SHELL="$(SHELL)" MAKE="$(MAKE)" \
	CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
	$(SHELL) ./mk-conf.sh || exit 1
_prestop = if [ -f ./mk.mk ]; then :; else \
		echo 'Program not configured, nothing to do';\
		echo 'Use one of the targets: config, all, tangerine';\
		exit 1;\
	fi;\
	< ./config.ev read __ev__; eval $${__ev__}; unset __ev__

# s-mk-mode
