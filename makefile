#@ Makefile for S-nail.
#@ Adjustments have to be made in make.rc -- or on the command line.
#@ See the file INSTALL if you need help.

# (Targets of ./make-emerge.sh)
CWDDIR=
TOPDIR=
OBJDIR=.obj

##  --  >8  --  8<  --  ##

# For make(1)s which not honour POSIX special treatment
SHELL = /bin/sh

.PHONY: ohno tangerine citron \
	all config build install uninstall clean distclean \
	devel odevel \
	test testnj
.NOTPARALLEL:
.WAIT: # Luckily BSD make supports specifying this as target, too

ohno: build
tangerine: config .WAIT build .WAIT test .WAIT install
citron: config .WAIT build .WAIT install
all: config .WAIT build

config:
	@$(_prego)
build:
	@$(_prestop); LC_ALL=C $${MAKE} -f mk-config.mk all
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
	$${SHELL} "$${TOPDIR}"mk/make-version.sh create &&\
	LC_ALL=C $${MAKE} -f mk-config.mk all
odevel:
	@CONFIG=ODEVEL; export CONFIG; $(_prego); $(_prestop);\
	$${SHELL} "$${TOPDIR}"mk/make-version.sh create &&\
	LC_ALL=C $${MAKE} -f mk-config.mk all

# (Test should inherit user runtime environ, at least a bit)
test:
	@$(__prestop); cd "$(OBJDIR)" && LC_ALL=C $(MAKE) -f mk-config.mk test
testnj:
	@$(__prestop); cd "$(OBJDIR)" &&\
	LC_ALL=C $(MAKE) -f mk-config.mk testnj

d-cross-build:
	@DEVEL_ORIG_CC=$(CC); export DEVEL_ORIG_CC;\
	[ -n "$(TOPDIR)" ] && CC="$(TOPDIR)"mk/pcb-cc.sh || \
		CC="`pwd`/mk/pcb-cc.sh";\
	$(MAKE) OPT_CROSS_BUILD=y OPT_DEVEL=1 VERBOSE=1 CC="$${CC}" config;\
	$(MAKE) distclean
d-b:
	@$(_prestop);\
	$${SHELL} "$${TOPDIR}"mk/make-version.sh create &&\
	LC_ALL=C $${MAKE} -f mk-config.mk all
d-v:
	@$(_prestop);\
	$${SHELL} "$${TOPDIR}"mk/make-version.sh create
d-cmd-tab:
	sh mk/make-cmd-tab.sh
d-cmd-tab-nv:
	sh mk/make-cmd-tab.sh noverbose
d-cs-ctype:
	sh mk/su-make-cs-ctype.sh
d-cs-ctype-nv:
	sh mk/su-make-cs-ctype.sh noverbose
d-errors:
	sh mk/su-make-errors.sh
d-errors-nv:
	sh mk/su-make-errors.sh noverbose
d-okeys:
	perl mk/make-okey-map.pl
d-okeys-nv:
	perl mk/make-okey-map.pl noverbose
d-tcaps:
	perl mk/make-tcap-map.pl
d-tcaps-nv:
	perl mk/make-tcap-map.pl noverbose

d-dox:
	doxygen mk/su-doxygen.rc
d-gettext:
	LC_ALL=C xgettext --sort-by-file --strict --add-location \
		--from-code=UTF-8 --keyword --keyword=_ --keyword=N_ \
		--add-comments=I18N --foreign-user \
		-o messages.pot src/mx/*.c src/mx/*.h src/su/*.c src/su/*.h

_prego = if CWDDIR="$(CWDDIR)" TOPDIR="$(TOPDIR)" \
		SHELL="$(SHELL)" MAKE="$(MAKE)" CC="$(CC)" \
		CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
		$(SHELL) "$(TOPDIR)"mk/make-config.sh "$(MAKEFLAGS)"; then :;\
	else exit 1; fi
__prestop = if [ -f "$(OBJDIR)"/mk-config.mk ]; then :; else \
		echo 'Program not configured, nothing to do';\
		echo 'Use one of the targets: config, all, tangerine, citron';\
		exit 0;\
	fi
_prestop = $(__prestop); cd "$(OBJDIR)" && . ./mk-config.env

# s-mk-mode
