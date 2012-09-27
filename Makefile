#@ Makefile for S-nail.
#@ See the file INSTALL if you need help.

PREFIX		= /usr/local
# Prepended to all paths at installation time (for e.g. package building)
DESTDIR		=

# (For those who want to install S-nail(1) as nail(1), use an empty *SID*)
SID		= s-

# Not uninstalled via uninstall: rule
MAILRC		= $(SYSCONFDIR)/$(SID)nail.rc

BINDIR		= $(PREFIX)/bin
MANDIR		= $(PREFIX)/man
SYSCONFDIR	= $(PREFIX)/etc

MAILSPOOL	= /var/mail
SENDMAIL	= /usr/sbin/sendmail
SHELL		= /bin/sh
STRIP		= strip
INSTALL		= /usr/bin/install

#CFLAGS		=
#WARN		= -W -Wall -pedantic
#LDFLAGS		=

##  --  >8  --  8<  --  ##

FEATURES	= -DMAILRC='"$(MAILRC)"' -DMAILSPOOL='"$(MAILSPOOL)"' \
			-DSENDMAIL='"$(SENDMAIL)"' $(IPv6)

OBJ = aux.o base64.o cache.o cmd1.o cmd2.o cmd3.o cmdtab.o collect.o \
	dotlock.o edit.o fio.o getname.o getopt.o head.o hmac.o \
	imap.o imap_search.o junk.o lex.o list.o lzw.o \
	macro.o maildir.o main.o md5.o mime.o names.o nss.o \
	openssl.o pop3.o popen.o quit.o \
	send.o sendout.o smtp.o ssl.o strings.o temp.o thread.o tty.o \
	v7.local.o vars.o \
	version.o

.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) $(CFLAGS) $(WARN) $(FEATURES) `cat INCS` -c $<

.c.x:
	$(CC) $(CFLAGS) $(WARN) $(FEATURES) -E $< >$@

.c .y: ;

all: $(SID)nail

$(SID)nail: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) `cat LIBS` -o $@

$(OBJ): config.h def.h extern.h glob.h rcv.h
imap.o: imap_gssapi.c
md5.o imap.o hmac.o smtp.o aux.o pop3.o junk.o: md5.h
nss.o: nsserr.c

new-version:
	eval VERSION=`git describe --dirty --tags`; \
	echo > version.c \
	"const char *version = \"<12.5 7/5/10; S-nail $${VERSION:-spooned}>\";"

config.h: user.conf makeconfig
	$(SHELL) ./makeconfig

install: all
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) -c $(SID)nail $(DESTDIR)$(BINDIR)/$(SID)nail
	$(STRIP) $(DESTDIR)$(BINDIR)/$(SID)nail
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -c -m 644 s-nail.1 $(DESTDIR)$(MANDIR)/man1/$(SID)nail.1
	test -d $(DESTDIR)$(SYSCONFDIR) || mkdir -p $(DESTDIR)$(SYSCONFDIR)
	test -f $(DESTDIR)$(MAILRC) || \
		$(INSTALL) -c -m 644 nail.rc $(DESTDIR)$(MAILRC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(SID)nail \
		$(DESTDIR)$(MANDIR)/man1/$(SID)nail.1

clean:
	rm -f $(OBJ) $(SID)nail *~ core log

distclean: clean
	rm -f config.h config.log LIBS INCS

