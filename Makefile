#############################################################################
# NAIL - A MAIL USER AGENT
#############################################################################

#############################################################################
# Change the following to match your system's configuration
#############################################################################

# The destination directory, with subdirectories bin, etc, man, lib.
DESTDIR		= /usr/local

# Uncomment on any system providing gcc.
CC		= gcc
CFLAGS		= -O2 -fno-strength-reduce -fomit-frame-pointer

# Uncomment on any system without gcc.
# Do NOT use /usr/ucb/cc !
#CC		= cc
#CFLAGS		= -O

# If you are in doubt, try without the following first.

# Many, notably commercial systems do not provide the file <paths.h> .
# You may also want to select this if you edit pathnames.h .
#CPPFLAGS	+= -DNO_PATHS_H

# On SysV systems without strcasecmp() in the regular libc,
# you also have to define this.
# An example is UnixWare 2.1.2.
#CPPFLAGS	+= -DNEED_STRCASECMP

# If your system does not provide snprint(), uncomment this.
# Since the sprintf() function then used can lead to buffer
# overflows, you should not run nail suid/sgid then.
# Claim your vendor for snprintf - the free OSs have it.
#CPPFLAG	+= -DNO_SNPRINTF

# POSIX.1 does not know the NSIG value, giving the number of signals
# provided by the operating system. It is defined on all systems known
# to me, though. In case your system really does not have it, check
# e.g. signal(7) for the correct value. On most architectures, this
# is the identical to the integer register size.
#CPPFLAGS	+= -DNSIG=32

# If your system does not provide implementations of tempnam() or getopt(),
# you are on your own. This should happen very rarely.

# In case you need it.
#LDFLAGS	=
#LIBS		=

# For debugging purposes.
#CFLAGS		= -g
#CFLAGS		+= -Wall -Wno-unused
#CPPFLAGS	+= -D_POSIX_SOURCE
#LIBS		= -lefence

##############################################################################
# There is nothing to change beyond this line.
##############################################################################

PROG = nail

SRCS=	version.c aux.c base64.c cmd1.c cmd2.c cmd3.c cmdtab.c \
	collect.c dotlock.c edit.c fio.c getname.c head.c \
	v7.local.c lex.c list.c main.c mime.c names.c popen.c \
	quit.c send.c sendout.c strings.c temp.c tty.c vars.c

OBJS=$(SRCS:%.c=%.o)

SFILES=	nail.help nail.tildehelp
EFILES=	nail.rc
MFILE =	nail.1

default: all

all: $(PROG)
 
$(PROG): mailfiles.h $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
 
.SUFFIXES: .o .c
.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

.SUFFIXES: .html .1
.1.html:
	rman -r off -f HTML $< > $@

mailfiles.h: Makefile mailfiles.H
	sed -e "s:DESTDIR:$(DESTDIR):" mailfiles.H > mailfiles.h

htmlman: nail.html

clean:
	rm -f $(PROG) $(OBJS) *~ mailfiles.h core $(MFILE:%.1=%.html)

install: all
	mkdir -p $(DESTDIR)/bin $(DESTDIR)/man/man1 \
		$(DESTDIR)/etc $(DESTDIR)/lib
	install -s -m 0755 -o root -g root $(PROG) $(DESTDIR)/bin
	cd misc && install -c -m 0644 $(EFILES) $(DESTDIR)/etc
	cd misc && install -c -m 0644 $(SFILES) $(DESTDIR)/lib
	install -c -m 0644 $(MFILE) $(DESTDIR)/man/man1

mailinstall: install
	ln -sf $(DESTDIR)/bin/$(PROG) $(DESTDIR)/bin/Mail
	ln -sf $(DESTDIR)/bin/$(PROG) $(DESTDIR)/bin/mail
	ln -sf $(DESTDIR)/bin/$(PROG) $(DESTDIR)/bin/mailx
	ln -sf $(DESTDIR)/bin/$(PROG) /bin/mail

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
sendout.o:  sendout.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
strings.o:  strings.c  rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
temp.o:     temp.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
tty.o:      tty.c      rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
v7.local.o: v7.local.c rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
version.o:  version.c  mailfiles.h
vars.o:     vars.c     rcv.h def.h glob.h extern.h pathnames.h mailfiles.h
