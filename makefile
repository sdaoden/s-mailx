#@ Makefile for S-nail.
#@ Adjustments have to be made in make.rc -- or on the command line.
#@ See the file INSTALL if you need help.

.PHONY: ohno tangerine citron \
	all config build install uninstall clean distclean test \
	devel odevel

# These are targets of make-emerge.sh
CWDDIR=
TOPDIR=

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
d-v:
	@$(_prestop);\
	LC_ALL=C $${MAKE} -f mk-config.mk _update-version

# The test should inherit the user runtime environment!
test:
	@$(__prestop); cd .obj && LC_ALL=C $(MAKE) -f mk-config.mk test

d-okeys:
	perl mk/make-okey-map.pl
d-okeys-nv:
	perl mk/make-okey-map.pl noverbose
d-tcaps:
	perl mk/make-tcap-map.pl
d-tcaps-nv:
	perl mk/make-tcap-map.pl noverbose
d-errors:
	sh mk/make-errors.sh
d-errors-nv:
	sh mk/make-errors.sh noverbose

d-dox:
	doxygen mk/su-doxygen.rc
d-gettext:
	(cd src/mx &&\
	 LC_ALL=C xgettext --sort-by-file --strict --add-location \
		--from-code=UTF-8 --keyword --keyword=_ --keyword=N_ \
		--add-comments=I18N --foreign-user \
		-o messages.pot *.c *.h) &&\
	(cd src/mx &&\
	 LC_ALL=C xgettext --sort-by-file --strict --add-location \
		--from-code=UTF-8 --keyword --keyword=_ --keyword=N_ \
		--add-comments=I18N --foreign-user \
		-o messages.pot *.h)

_prego = if CWDDIR="$(CWDDIR)" TOPDIR="$(TOPDIR)" \
		SHELL="$(SHELL)" MAKE="$(MAKE)" CC="$(CC)" \
		CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
		$(SHELL) "$(TOPDIR)"mk/make-config.sh "$(MAKEFLAGS)"; then :;\
	else exit 1; fi
__prestop = if [ -f .obj/mk-config.mk ]; then :; else \
		echo 'Program not configured, nothing to do';\
		echo 'Use one of the targets: config, all, tangerine, citron';\
		exit 0;\
	fi
_prestop = $(__prestop); cd .obj && . ./mk-config.ev

# s-mk-mode
