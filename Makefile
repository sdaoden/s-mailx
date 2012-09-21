#@ Makefile for s-nail
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

# Define compiler, preprocessor, and linker flags here.
# Note that some Linux/glibc versions need -D_GNU_SOURCE in CPPFLAGS, or
# wcwidth() will not be available and multibyte characters will not be
# displayed correctly.
#CFLAGS		=
#CPPFLAGS	=
#LDFLAGS		=
#WARN		= -W -Wall -Wno-parentheses -Werror

# If you know that the IPv6 functions work on your machine, you can enable
# them here.
#IPv6		= -DHAVE_IPv6_FUNCS

##  --  >8  --  8<  --  ##

###########################################################################
###########################################################################
# You should really know what you do if you change anything below this line
###########################################################################
###########################################################################

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

.SUFFIXES: .o .c .x
.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FEATURES) \
		`grep '^[^#]' INCS` $(INCLUDES) $(WARN) -c $<

.c.x:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FEATURES) $(INCLUDES) $(WARN) -E $< >$@

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FEATURES) $(INCLUDES) $(WARN) \
		$(LDFLAGS) $< `grep '^[^#]' LIBS` $(LIBS) -o $@

all: $(SID)nail

$(SID)nail: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) `grep '^[^#]' LIBS` $(LIBS) -o $@

$(OBJ): config.h def.h extern.h glob.h rcv.h
imap.o: imap_gssapi.c
md5.o imap.o hmac.o smtp.o aux.o pop3.o junk.o: md5.h
nss.o: nsserr.c
version.o: version.h

#version.h: $(OBJ:.o=.c)
version.h:
	eval VERSION=`git describe --dirty --tags`; \
	echo > version.h \
		"#define V \"<12.5 7/5/10; $${VERSION:-S-nail spooned}>\""

config.h: user.conf makeconfig
	$(SHELL) ./makeconfig

install: all
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) -c $(SID)nail $(DESTDIR)$(BINDIR)/$(SID)nail
	$(STRIP) $(DESTDIR)$(BINDIR)/$(SID)nail
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -c -m 644 mailx.1 $(DESTDIR)$(MANDIR)/man1/$(SID)nail.1
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

