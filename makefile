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
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk $(MAILJOBS) all
install packager-install: build
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk DESTDIR="$(DESTDIR)" install
uninstall:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk uninstall

clean:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk clean
distclean:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk distclean

test:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk test

devel:
	@CONFIG=DEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAILJOBS) all
odevel:
	@CONFIG=ODEVEL; export CONFIG; $(_prego); $(_prestop);\
	LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAILJOBS) all
d-b:
	@$(_prestop); LC_ALL=C $${MAKE} -f ./mk.mk _update-version &&\
	LC_ALL=C $${MAKE} -f ./mk.mk $(MAILJOBS) all

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

# Corresponds to same thing in mk-mk.in! (Except it is sed not $(sed))
_version_from_header = VERSION="`< version.h sed \
		-e '/ VERSION /b X' -e d -e ':X' \
		-e 's/[^\"]*\"v\([^\"]\{1,\}\)\"/\1/'`"

# This is pretty specific
_update-release:
	@ORIG_LC_ALL=${LC_ALL};\
	LC_ALL=C; export LC_ALL;\
	: $${UAGENT:=s-nail};\
	: $${UUAGENT:=S-nail};\
	: $${UPLOAD:=steffen@sdaoden.eu:/var/www/localhost/downloads};\
	: $${ACCOUNT:=ich};\
	DATE_MAN="`date -u +'%b %d, %Y'`";\
	DATE_ISO="`date -u +%Y-%m-%d`";\
	if [ "`git rev-parse --verify HEAD`" != \
			"`git rev-parse --verify master`" ]; then \
		echo >&2 'Not on the [master] branch';\
		exit 1;\
	fi;\
	if [ "`git status --porcelain --ignored |\
			awk 'BEGIN{no=0}{++no}END{print no}'`" -ne 0 ]; then\
		echo >&2 'Directory not clean, see git status --ignored';\
		exit 2;\
	fi;\
	echo 'Name of release tag:';\
	read REL;\
	echo "Is $${UAGENT} <v$${REL}> correct?  ENTER continues";\
	read i;\
	\
	git show announce > "$${TMPDIR}/$${UAGENT}-$${REL}.ann.mail" &&\
	\
	grep=grep sed=sed cmp=cmp mv=mv \
		VERSION="$${REL}" $(MAKE) -f mk-mk.in _update-version &&\
	$(_version_from_header);\
	REL="$${VERSION}";\
	\
	< nail.1 > nail.1x awk '\
		BEGIN { written = 0 }\
		/\.\\"--MKREL-START--/, /\.\\"--MKREL-END--/ {\
			if (written++ != 0)\
				next;\
			print ".\\\"--MKREL-START--";\
			print ".\\\" '"$${UUAGENT}"'(1): v'"$${REL}"'" \
				" / '"$${DATE_ISO}"'";\
			print ".Dd '"$${DATE_MAN}"'";\
			print ".ds VV \\\\%v'"$${REL}"'";\
			print ".\\\"--MKREL-END--";\
			next\
		}\
		{print}\
	' &&\
	mv -f nail.1x nail.1 &&\
	\
	< nail.rc > nail.rcx awk '\
		BEGIN { written = 0 }\
		/^#--MKREL-START--/, /^#--MKREL-END--/ {\
			if (written++ != 0)\
				next;\
			print "#--MKREL-START--";\
			print \
		"# '"$${UUAGENT}"'(1): v'"$${REL}"' / '"$${DATE_ISO}"'";\
			print "#--MKREL-END--";\
			next\
		}\
		{print}\
	' && \
	mv -f nail.rcx nail.rc &&\
	\
	./mk-okey-map.pl&&\
	./mk-tcap-map.pl&&\
	\
	git add version.h nail.1 nail.rc okeys.h tcaps.h&&\
	LC_ALL=${ORIG_LC_ALL} git commit -S -m "Bump $${UUAGENT} v$${REL}" &&\
	LC_ALL=${ORIG_LC_ALL} git tag -s -f "v$${REL}" &&\
	\
	git update-ref refs/heads/next master &&\
	\
	git checkout timeline &&\
	git rm -rf '*' &&\
	git archive --format=tar "v$${REL}" | tar -x -f - &&\
	\
	( \
	rm -f .gitignore .mailmap TODO &&\
	sed -E -e '/^\.\\"--MKREL-(START|END)--/d' \
		-e '/--BEGINSTRIP--/,$$ {' \
			-e '/^\.[[:space:]]*$$/d' -e '/^\.[[:space:]]*\\"/d' \
		-e '}' \
		-e '/^\.$$/d' < nail.1 > nail.1x &&\
	mv -f nail.1x nail.1 &&\
	if command -v mdocmx.sh >/dev/null 2>&1; then \
		mdocmx.sh < nail.1 > nail.1x &&\
		mv -f nail.1x nail.1;\
	fi; \
	sed -Ee '/^#--MKREL-(START|END)--/d' \
		< nail.rc > nail.rcx &&\
	mv -f nail.rcx nail.rc \
	) &&\
	\
	./mk-okey-map.pl noverbose &&\
	./mk-tcap-map.pl noverbose &&\
	\
	git add --all &&\
	LC_ALL=${ORIG_LC_ALL} \
		git commit -S -m "$${UUAGENT} v$${REL}, $${DATE_ISO}" &&\
	LC_ALL=${ORIG_LC_ALL} git tag -s -f "v$${REL}.ar" &&\
	\
	git checkout master &&\
	git log --no-walk --decorate --oneline --branches --remotes &&\
	git branch &&\
	echo "Push git(1) repo?  ENTER continues";\
	read i;\
	git push &&\
	\
	git archive --format=tar --prefix="$${UAGENT}-$${REL}/" "v$${REL}.ar" |\
		( cd "$${TMPDIR}" && tar -x -f - ) &&\
	cd "$${TMPDIR}" &&\
	\
	tar -c -f "$${UAGENT}-$${REL}.tar" "$${UAGENT}-$${REL}" &&\
	openssl sha1 "$${UAGENT}-$${REL}.tar" >> \
		"$${UAGENT}-$${REL}.cksum" 2>&1 &&\
	openssl sha256 "$${UAGENT}-$${REL}.tar" >> \
		"$${UAGENT}-$${REL}.cksum" 2>&1 &&\
	openssl sha512 "$${UAGENT}-$${REL}.tar" >> \
		"$${UAGENT}-$${REL}.cksum" 2>&1 &&\
	gpg --detach-sign --armor "$${UAGENT}-$${REL}.tar" 2>&1 &&\
	cat "$${UAGENT}-$${REL}.tar.asc" >> \
		"$${UAGENT}-$${REL}.cksum" 2>&1 &&\
	< "$${UAGENT}-$${REL}.tar" gzip > "$${UAGENT}-$${REL}.tar.gz" &&\
	< "$${UAGENT}-$${REL}.tar" xz -e -C sha256 > \
		"$${UAGENT}-$${REL}.tar.xz" &&\
	\
	(\
	echo "-put $${UAGENT}-$${REL}.tar";\
	echo "-rm $${UAGENT}-latest.tar";\
	echo "-ln $${UAGENT}-$${REL}.tar $${UAGENT}-latest.tar";\
	echo "-put $${UAGENT}-$${REL}.tar.gz";\
	echo "-rm $${UAGENT}-latest.tar.gz";\
	echo "-ln $${UAGENT}-$${REL}.tar.gz $${UAGENT}-latest.tar.gz";\
	echo "-put $${UAGENT}-$${REL}.tar.xz";\
	echo "-rm $${UAGENT}-latest.tar.xz";\
	echo "-ln $${UAGENT}-$${REL}.tar.xz $${UAGENT}-latest.tar.xz";\
	echo "-put $${UAGENT}-$${REL}.tar.asc";\
	echo "-rm $${UAGENT}-latest.tar.asc";\
	echo "-ln $${UAGENT}-$${REL}.tar.asc $${UAGENT}-latest.tar.asc";\
	echo "-chmod 0644 $${UAGENT}-$${REL}.tar*";\
	) | \
	sftp -b - $${UPLOAD} &&\
	echo 'All seems fine';\
	\
	echo 'Really send announcement mail?  ENTER continues';\
	read i;\
	cd "$${UAGENT}-$${REL}" &&\
	make CONFIG=MAXIMAL all &&\
	< "$${TMPDIR}/$${UAGENT}-$${REL}.ann.mail" \
		LC_ALL=${ORIG_LC_ALL} ./$${UAGENT} -A $${ACCOUNT} \
				-Snofollowup-to \
			-s "[ANN]ouncing $${UUAGENT} v$${REL}" \
			-t &&\
	echo 'Uff.'

# s-mk-mode
