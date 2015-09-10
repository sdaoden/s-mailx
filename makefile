#@ Makefile for S-nail.
#@ Adjustments have to be made in `make.rc' -- or on the command line.
#@ See the file `INSTALL' if you need help.

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
	: $${UPLOAD:=sdaoden@frs.sourceforge.net:/home/frs/project/s-nail};\
	: $${ACCOUNT:=sn_sf};\
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
	grep=grep sed=sed cmp=cmp mv=mv \
		VERSION="$${REL}" $(MAKE) -f mk-mk.in _update-version &&\
	$(_version_from_header);\
	REL="$${VERSION}";\
	FREL=`echo $${REL} | sed -e 's/\./_/g'`;\
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
	git add version.h nail.1 nail.rc &&\
	git commit -m "Bump $${UUAGENT} v$${REL}" &&\
	git tag -s -f "v$${REL}" &&\
	\
	git update-ref refs/heads/next master &&\
	\
	git checkout timeline &&\
	git rm -rf '*' &&\
	git archive --format=tar.gz "v$${REL}" | tar -x -z -f - &&\
	git add --all &&\
	git commit -m "$${UAGENT} v$${REL}, $${DATE_ISO}" &&\
	\
	git checkout master &&\
	git log --no-walk --decorate --oneline --branches --remotes &&\
	git branch &&\
	echo "Push git(1) repo?  ENTER continues";\
	read i;\
	git push &&\
	\
	git archive --format=tar.gz --prefix="$${UAGENT}-$${REL}/" \
		-o "$${TMPDIR}/$${UAGENT}-$${FREL}.tar.gz" "v$${REL}" &&\
	cd "$${TMPDIR}" &&\
	tar -x -z -f "$${UAGENT}-$${FREL}.tar.gz" &&\
	rm -f "$${UAGENT}-$${FREL}.tar.gz" &&\
	( \
	cd "$${UAGENT}-$${REL}" &&\
	\
	rm -f .gitignore .mailmap \
	\
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
	tar -c -f "$${UAGENT}-$${FREL}.tar" "$${UAGENT}-$${REL}" &&\
	\
	openssl md5 "$${UAGENT}-$${FREL}.tar" > \
		"$${UAGENT}-$${FREL}.cksum" 2>&1 &&\
	openssl sha1 "$${UAGENT}-$${FREL}.tar" >> \
		"$${UAGENT}-$${FREL}.cksum" 2>&1 &&\
	openssl sha256 "$${UAGENT}-$${FREL}.tar" >> \
		"$${UAGENT}-$${FREL}.cksum" 2>&1 &&\
	gpg --detach-sign --armor "$${UAGENT}-$${FREL}.tar" 2>&1 &&\
	cat "$${UAGENT}-$${FREL}.tar.asc" >> \
		"$${UAGENT}-$${FREL}.cksum" 2>&1 &&\
	\
	< "$${UAGENT}-$${FREL}.tar" gzip > "$${UAGENT}-$${FREL}.tar.gz" &&\
	< "$${UAGENT}-$${FREL}.tar" xz -e -C sha256 > \
		"$${UAGENT}-$${FREL}.tar.xz" &&\
	\
	rm -f "$${UAGENT}-$${FREL}.tar" &&\
	\
	( echo "-put $${UAGENT}-$${FREL}.tar.gz";\
	  echo "-put $${UAGENT}-$${FREL}.tar.xz";\
	  echo "-put $${UAGENT}-$${FREL}.tar.asc" ) | \
	sftp -b - $${UPLOAD} &&\
	echo 'All seems fine';\
	\
	echo 'Really send announcement mail?  ENTER continues';\
	read i;\
	cd "$${UAGENT}-$${REL}" &&\
	make CONFIG=MAXIMAL &&\
	LC_ALL=${ORIG_LC_ALL} ./$${UAGENT} -A $${ACCOUNT} \
		-s "[ANNOUNCE] of $${UUAGENT} v$${REL}" \
		-b nail-announce-bcc nail-announce &&\
	cd .. &&\
	\
	echo "Really remove $${UAGENT}-$${REL} build dir?  ENTER continues";\
	read i;\
	rm -rf "$${UAGENT}-$${REL}" &&\
	echo 'Uff.'

# s-mk-mode
