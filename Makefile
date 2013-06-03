#@ Makefile for S-nail.
#@ See the file INSTALL if you need help.

# General prefix
PREFIX		= /usr/local

BINDIR		= $(PREFIX)/bin
MANDIR		= $(PREFIX)/man
SYSCONFDIR	= $(PREFIX)/etc

# Prepended to all paths at installation time (for e.g. package building)
DESTDIR		=
# (For those who want to install S-nail(1) as nail(1), use an empty *SID*)
SID		= s-

MAILSPOOL	= /var/mail
SENDMAIL	= /usr/sbin/sendmail
SENDMAIL_PROGNAME = sendmail
SHELL		= /bin/sh
STRIP		= strip
INSTALL		= /usr/bin/install

# Note: this is an old codebase that originates in the 70s, when all those
# fancy compiler features, like constant data, restricted pointers, type
# aliasing etc., did not yet exist.  Until the codebase has been overhauled
# (and the overhauling has itself been debugged :) you may read INSTALL for
# hints but are otherwise on your own if you turn on fancy compiler features!
CFLAGS		= -O1
#CFLAGS		= -std=c89 -O2 -g -fstrict-aliasing
#WARN		= -Wall -Wextra -pedantic -Wbad-function-cast -Wcast-align \
#		-Winit-self -Wwrite-strings -Wcast-qual -Wunused
# Warnings that are not handled very well (yet)
#		-Wshadow
# The gcc(1) from NetBSD 6 produces a lot of errors with -fstrict-overflow,
# so that this needs to be revisited; it was enabled on OS X 6 and FreeBSD 9,
# and seemed to be good for gcc(1) and clang(1)
#		-fstrict-overflow -Wstrict-overflow=5
#LDFLAGS		=

##  --  >8  --  8<  --  ##

# To ease the life of forkers and packagers one may even adjust the "nail"
# of nail(1).  Note that $(SID)$(NAIL) must be longer than two characters.
# Two lines for a completely clean fork.  Ok, three with update-release:
NAIL		= nail
SYSCONFRC	= $(SYSCONFDIR)/$(SID)$(NAIL).rc

# Binaries builtin paths
PATHDEFS	= -DSYSCONFRC='"$(SYSCONFRC)"' -DMAILSPOOL='"$(MAILSPOOL)"' \
		-DSENDMAIL='"$(SENDMAIL)"' \
		-DSENDMAIL_PROGNAME='"$(SENDMAIL_PROGNAME)"'

OBJ = attachments.o auxlily.o cmd1.o cmd2.o cmd3.o cmdtab.o collect.o \
	dotlock.o edit.o fio.o head.o \
	imap.o imap_cache.o imap_search.o junk.o lex.o list.o lzw.o \
	macro.o maildir.o main.o md5.o mime.o mime_cte.o names.o \
	openssl.o pop3.o popen.o quit.o \
	send.o sendout.o smtp.o ssl.o strings.o thread.o tty.o \
	vars.o version.o

.SUFFIXES: .o .c .y
.c.o:
	$(CC) $(CFLAGS) $(WARN) $(PATHDEFS) `cat config.inc` -c $<

.c .y: ;

all: $(SID)$(NAIL)

$(SID)$(NAIL): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) `cat config.lib` -o $@

$(OBJ): config.h def.h extern.h glob.h rcv.h
imap.o: imap_gssapi.c
md5.o imap.o smtp.o auxlily.o pop3.o junk.o: md5.h
mime.o: mime_types.h

config.h: user.conf makeconfig Makefile
	$(SHELL) ./makeconfig

mime_types.h: mime.types
	< mime.types > $@ sed '/^#/d; /^$$/d; s/^/	"/; s/$$/",/'

mkman.1: nail.1
	_SYSCONFRC="$(SYSCONFRC)" _NAIL="$(SID)$(NAIL)" \
	< nail.1 > $@ awk 'BEGIN {written = 0} \
	/.\"--MKMAN-START--/, /.\"--MKMAN-END--/ { \
		if (written == 1) \
			next; \
		written = 1; \
		OFS = ""; \
		unail = toupper(ENVIRON["_NAIL"]); \
		lnail = tolower(unail); \
		cnail = toupper(substr(lnail, 1, 1)) substr(lnail, 2); \
		print ".ds UU \\\\%", unail; \
		print ".ds UA \\\\%", cnail; \
		print ".ds ua \\\\%", lnail; \
		print ".ds UR \\\\%", ENVIRON["_SYSCONFRC"]; \
		OFS = " "; \
		next \
	} \
	{print} \
	'

mkrc.rc: nail.rc
	_SYSCONFRC="$(SYSCONFRC)" _NAIL="$(SID)$(NAIL)" \
	< nail.rc > $@ awk 'BEGIN {written = 0} \
	/#--MKRC-START--/, /#--MKRC-END--/ { \
		if (written == 1) \
			next; \
		written = 1; \
		OFS = ""; \
		lnail = tolower(ENVIRON["_NAIL"]); \
		cnail = toupper(substr(lnail, 1, 1)) substr(lnail, 2); \
		print "# ", ENVIRON["_SYSCONFRC"]; \
		print "# Configuration file for ", cnail, "(1), a fork of"; \
		OFS = " "; \
		next \
	} \
	{print} \
	'

install: all mkman.1 mkrc.rc
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) -c $(SID)$(NAIL) $(DESTDIR)$(BINDIR)/$(SID)$(NAIL)
	$(STRIP) $(DESTDIR)$(BINDIR)/$(SID)$(NAIL)
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -c -m 644 mkman.1 $(DESTDIR)$(MANDIR)/man1/$(SID)$(NAIL).1
	test -d $(DESTDIR)$(SYSCONFDIR) || mkdir -p $(DESTDIR)$(SYSCONFDIR)
	test -f $(DESTDIR)$(SYSCONFRC) || \
		$(INSTALL) -c -m 644 mkrc.rc $(DESTDIR)$(SYSCONFRC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(SID)$(NAIL) \
		$(DESTDIR)$(MANDIR)/man1/$(SID)$(NAIL).1

clean:
	rm -f $(OBJ) $(SID)$(NAIL) mime_types.h mkman.1 mkrc.rc *~ core log

distclean: clean
	rm -f config.h config.log config.lib config.inc

update-version:
	[ -z "$${VERSION}" ] && eval VERSION="`git describe --tags`"; \
	echo > version.c \
	"char const *const uagent = \"$(SID)$(NAIL)\", \
	*const version = \"$${VERSION:-huih buh}\";"

update-release:
	echo 'Name of release tag:';\
	read REL;\
	echo "Is <$(SID)$(NAIL)-$${REL}> correct?  ENTER continues";\
	read i;\
	FREL=`echo $${REL} | sed 's/\./_/g'` && \
	$(MAKE) update-version && \
	git add version.c && \
	git commit -m "Bump $(SID)$(NAIL)-$${REL}" && \
	git tag -f "$(SID)$(NAIL)-$${REL}" && \
	$(MAKE) update-version && \
	git add version.c && git commit --amend && \
	git tag -f "$(SID)$(NAIL)-$${REL}" && \
	git archive --prefix="$(SID)$(NAIL)-$${REL}/" \
		-o "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.tar.gz" HEAD && \
	openssl md5 "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.tar.gz" \
		> "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.cksum" 2>&1 && \
	openssl sha1 "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.tar.gz" \
		>> "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.cksum" 2>&1 && \
	openssl sha256 "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.tar.gz" \
		>> "$(TMPDIR)/$(SID)$(NAIL)-$${FREL}.cksum" 2>&1 && \
	echo "-put $(TMPDIR)/$(SID)$(NAIL)-$${FREL}.tar.gz" | \
	sftp -b - sdaoden@frs.sourceforge.net:/home/frs/project/s-nail && \
	echo 'All seems fine' && \
	make distclean && \
	make WANT_ASSERTS=1 && \
	./s-nail -s "Announcing S-nail $${REL}" -c nail-cc nail && \
	echo 'Uff.'
