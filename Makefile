#############################################################################
# NAIL - A MAIL USER AGENT
#############################################################################

#############################################################################
# Change the following to match your system's configuration
#############################################################################

# the destination directory, with subdirectories bin, etc, man, lib
DESTDIR		= /usr/local

# uncomment if on linux
CC		= gcc
CFLAGS		= -O2 -fno-strength-reduce -fomit-frame-pointer
CPPFLAGS	= -D_BSD_SOURCE -D_GNU_SOURCE

# uncomment if on System V Release 4
# do NOT use /usr/ucb/cc !
#CC		= cc
#CFLAGS		= -O
#CPPFLAGS	= -DSYSVR4 -I/usr/ucbinclude
#LIBS		= -YP,:/usr/ucblib:/usr/ccs/lib:/usr/lib \
#			-Bstatic -lucb -Bdynamic -lc

# in case you need it
#LDFLAGS	=
#LIBS		=

# for debugging
#CFLAGS		= -g
#LIBS		= -lefence

##############################################################################
# There is nothing to change beyond this line.
##############################################################################

PROG = nail

SRCS=	version.c aux.c base64.c cmd1.c cmd2.c cmd3.c cmdtab.c \
	collect.c dotlock.c edit.c fio.c getname.c head.c \
	v7.local.c lex.c list.c main.c mime.c names.c popen.c \
	quit.c send.c strings.c temp.c tty.c vars.c

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

mailfiles.h: Makefile mailfiles.H
	sed -e "s+DESTDIR+$(DESTDIR)+" mailfiles.H > mailfiles.h

clean:
	rm -f $(PROG) $(OBJS) *~ mailfiles.h dead.letter core

install: all
	mkdir -p $(DESTDIR)/bin $(DESTDIR)/man/man1 \
	$(DESTDIR)/etc $(DESTDIR)/lib
	install -s -m 0755 -o root -g root $(PROG) $(DESTDIR)/bin
	cd misc && install -c -m 0644 $(EFILES) $(DESTDIR)/etc
	cd misc && install -c -m 0644 $(SFILES) $(DESTDIR)/lib
	install -c -m 0644 $(MFILE) $(DESTDIR)/man/man1

mailinstall: install
	install -c -m 0644 $(MAILMFILE) $(DESTDIR)/man/man1
	ln -sf $(DESTDIR)/bin/$(PROG) $(DESTDIR)/bin/Mail

checkin:
	ci -mcomplete_checkin `ls RCS | sed s/,v$$//`

checkout:
	co `ls RCS | sed s/,v$$//` </dev/null

cmdtab.o:   cmdtab.c   def.h extern.h mailfiles.h
aux.o:      aux.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
base64.o:   base64.c   rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
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
mime.o:     mime.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
names.o:    names.c    rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
popen.o:    popen.c    rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
quit.o:     quit.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
send.o:     send.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
strings.o:  strings.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
temp.o:     temp.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
tty.o:      tty.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
v7.local.o: v7.local.c rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
vars.o:     vars.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
