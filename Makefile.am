# Makefile.am for nail, a mail user agent.
# Process this file with automake to produce Makefile.in

# $Id: Makefile.am,v 1.4 2000/08/08 20:10:39 gunnar Exp $

# Initially written by Jacob Moorman.
# Changes by Gunnar Ritter.

# automake generates dependencies that fail on non-GNU systems.
AUTOMAKE_OPTIONS=no-dependencies foreign

bin_PROGRAMS = nail

nail_SOURCES = version.c aux.c base64.c cmd1.c cmd2.c cmd3.c \
	cmdtab.c collect.c dotlock.c edit.c fio.c getname.c head.c \
	v7.local.c lex.c list.c main.c mime.c names.c popen.c \
	quit.c send.c sendout.c smtp.c strings.c temp.c tty.c vars.c 

nail_MANS = nail.1

EXTRA_DIST = I18N nail.1 def.h extern.h glob.h rcv.h

SUBDIRS = misc

SUFFIXES = .1.html 

man1dir = $(mandir)/man1
man1_DATA = nail.1

mailinstall: install
	ln -sf $(bindir)/$(bin_PROGRAMS) $(bindir)/Mail
	ln -sf $(bindir)/$(bin_PROGRAMS) $(bindir)/mail
	ln -sf $(bindir)/$(bin_PROGRAMS) $(bindir)/mailx


.1.html:
	rman -r off -f HTML $< > $@
