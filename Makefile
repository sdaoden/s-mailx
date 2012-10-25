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
SHELL		= /bin/sh
STRIP		= strip
INSTALL		= /usr/bin/install

#CFLAGS		= -std=c89 -O2
#WARN		= -g -Wall -Wextra -pedantic -Wbad-function-cast -Wcast-align \
#		-Winit-self
# Warnings that are not handled very well (yet)
#		-Wshadow -Wcast-qual -Wwrite-strings
# The gcc(1) from NetBSD 6 produces a lot of errors with -fstrict-overflow,
# so that this needs to be revisited; it was enabled on OS X 6 and FreeBSD 9,
# and seemed to be good for gcc(1) and clang(1)
#		-fstrict-overflow -Wstrict-overflow=5
#LDFLAGS		=

##  --  >8  --  8<  --  ##

# To ease the life of forkers and packagers one may even adjust the "nail"
# of nail(1).  Note that $(SID)$(NAIL) must be longer than two characters.
# There you go.  Two lines for a completely clean fork.
NAIL		= nail
SYSCONFRC	= $(SYSCONFDIR)/$(SID)$(NAIL).rc

# Binaries builtin paths
PATHDEFS	= -DSYSCONFRC='"$(SYSCONFRC)"' -DMAILSPOOL='"$(MAILSPOOL)"' \
			-DSENDMAIL='"$(SENDMAIL)"'

OBJ = aux.o base64.o cache.o cmd1.o cmd2.o cmd3.o cmdtab.o collect.o \
	dotlock.o edit.o fio.o getname.o getopt.o head.o hmac.o \
	imap.o imap_search.o junk.o lex.o list.o lzw.o \
	macro.o maildir.o main.o md5.o mime.o names.o nss.o \
	openssl.o pop3.o popen.o quit.o \
	send.o sendout.o smtp.o ssl.o strings.o temp.o thread.o tty.o \
	v7.local.o vars.o \
	version.o

.SUFFIXES: .o .c .y
.c.o:
	$(CC) $(CFLAGS) $(WARN) $(PATHDEFS) `cat INCS` -c $<

.c .y: ;

all: $(SID)$(NAIL)

$(SID)$(NAIL): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) `cat LIBS` -o $@

$(OBJ): config.h def.h extern.h glob.h rcv.h
imap.o: imap_gssapi.c
md5.o imap.o hmac.o smtp.o aux.o pop3.o junk.o: md5.h
nss.o: nsserr.c

config.h: user.conf makeconfig Makefile
	$(SHELL) ./makeconfig

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
		print ".ds UU ", unail; \
		print ".ds uu ", cnail; \
		print ".ds UA \\\\fI", cnail, "\\\\fR"; \
		print ".ds ua \\\\fI", lnail, "\\\\fR"; \
		print ".ds ba \\\\fB", lnail, "\\\\fR"; \
		print ".ds UR ", ENVIRON["_SYSCONFRC"]; \
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
	rm -f $(OBJ) $(SID)$(NAIL) mkman.1 mkrc.rc *~ core log

distclean: clean
	rm -f config.h config.log LIBS INCS

update-version:
	[ -z "$${VERSION}" ] && eval VERSION="`git describe --dirty --tags`"; \
	echo > version.c \
	"char const *const uagent = \"$(SID)$(NAIL)\", \
	*const version = \"$${VERSION:-huih buh}\";"
