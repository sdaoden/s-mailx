PROG=	mail
SRCS=	version.c aux.c cmd1.c cmd2.c cmd3.c cmdtab.c collect.c dotlock.c \
	edit.c fio.c getname.c head.c v7.local.c lex.c list.c main.c names.c \
	popen.c quit.c send.c strings.c temp.c tty.c vars.c
SFILES=	mail.help mail.tildehelp
EFILES=	mail.rc
BINOWN = root
BINGRP = mail

all:
	gcc $(SRCS) -o $(PROG)

install:
	mkdir -p /usr/share/misc
	mkdir -p /usr/share/man/man1
	mkdir -p /etc
	mkdir -p /usr/bin
	install -o $(BINOWN) -g $(BINGRP) -m 2755 $(PROG) /usr/bin
	install -o root -g root -m 644 mail.1 /usr/share/man/man1
	cd misc; install -c -o ${BINOWN} -g ${BINGRP} \
	    -m 444 ${SFILES} /usr/share/misc
	cd misc; install -c -o root -g root \
	    -m 644 ${EFILES} /etc

