#	$Id: Makefile,v 1.3 2000/03/21 03:52:04 gunnar Exp $
#	OpenBSD: Makefile,v 1.8 1996/06/08 19:48:09 christos Exp
#	NetBSD: Makefile,v 1.8 1996/06/08 19:48:09 christos Exp

PROG=	nail
CC=gcc
DESTDIR=/usr/local

INCLUDE = $(shell test -d /usr/include/bsd && echo -I/usr/include/bsd)
CPPFLAGS=-D_BSD_SOURCE -D_GNU_SOURCE $(INCLUDE)
CFLAGS=-O2 -fno-strength-reduce -fomit-frame-pointer
LDFLAGS=
LIBS=

#CFLAGS = -g
#LIBS=-lefence

SRCS=	version.c aux.c cmd1.c cmd2.c cmd3.c cmdtab.c collect.c dotlock.c \
	edit.c fio.c getname.c head.c v7.local.c lex.c list.c main.c names.c \
	popen.c quit.c send.c strings.c temp.c tty.c vars.c base64.c mime.c

OBJS=$(SRCS:%.c=%.o)

SFILES=	nail.help nail.tildehelp
EFILES=	nail.rc
MFILE =	nail.1
MAILMFILE = Mail.1

default: all

all: $(PROG)
 
$(PROG): mailfiles.h $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
 
 .c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

mailfiles.h: Makefile
	echo -e "/* mail installes files */\
\n#define	_PATH_HELP	\"$(DESTDIR)/lib/nail.help\"\
\n#define	_PATH_TILDE	\"$(DESTDIR)/lib/nail.tildehelp\"\
\n#define	_PATH_MASTER_RC	\"$(DESTDIR)/etc/nail.rc\"" > mailfiles.h

clean:
	rm -f $(PROG) $(OBJS) *~ mailfiles.h dead.letter core

mail:
 
install: all
	mkdir -p $(DESTDIR)/bin $(DESTDIR)/man/man1 \
	$(DESTDIR)/etc $(DESTDIR)/lib
	install -s -m 0755 -o root -g root $(PROG) $(DESTDIR)/bin
	cd misc && install -c -m 0644 $(EFILES) $(DESTDIR)/etc
	cd misc && install -c -m 0644 $(SFILES) $(DESTDIR)/lib
	install -c -m 0644 $(MFILE) $(DESTDIR)/man/man1

mailinstall: install mail
	install -c -m 0644 $(MAILMFILE) $(DESTDIR)/man/man1
	ln -sf $(DESTDIR)/bin/$(PROG) /bin/mail

cmdtab.o:   cmdtab.c   def.h extern.h
aux.o:      aux.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
cmd1.o:     cmd1.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
cmd2.o:     cmd2.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
cmd3.o:     cmd3.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
collect.o:  collect.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
dotlock.o:  dotlock.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
edit.o:     edit.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
fio.o:      fio.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
getname.o:  getname.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
head.o:     head.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
lex.o:      lex.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
list.o:     list.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
main.o:     main.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
names.o:    names.c    rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
popen.o:    popen.c    rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
quit.o:     quit.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
send.o:     send.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
strings.o:  strings.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
temp.o:     temp.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
tty.o:      tty.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
v7.local.o: v7.local.c rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
vars.o:     vars.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
mime.o:     mime.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
