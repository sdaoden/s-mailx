@BOTTOM@
/* $Id: acconfig.h,v 1.4 2000/05/29 01:30:29 gunnar Exp $ */

/* The C shell's path. */
#ifndef _PATH_CSHELL
#undef _PATH_CSHELL
#endif

/* Path to `more'. */
#ifndef _PATH_MORE
#undef _PATH_MORE
#endif

/* Path to `ex'. */
#ifndef _PATH_EX
#undef _PATH_EX
#endif

/* Path to `vi'. */
#ifndef _PATH_VI
#undef _PATH_VI
#endif

/* Path to `sendmail'. */
#ifndef _PATH_SENDMAIL
#undef _PATH_SENDMAIL
#endif

/* Path to nail's support files. */
#undef _PATH_HELP
#undef _PATH_TILDE
#undef _PATH_MASTER_RC

/* The mail spool directory. */
#ifndef _PATH_MAILDIR
#undef _PATH_MAILDIR
#endif

/* The temporary directory. */
#ifndef _PATH_TMP
#undef _PATH_TMP
#endif

#include <sys/types.h>

#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef	HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef	HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef	HAVE_NETDB_H
#include <netdb.h>
#endif

/* Define if you have the Socket API. */
#ifndef	HAVE_SOCKETS
#undef	HAVE_SOCKETS
#endif

#ifdef	HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef	HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

/* Define if we need libnsl. */
#ifndef	HAVE_LIBNSL
#undef	HABE_LIBNSL
#endif

/* Define if we need libsocket. */
#ifndef	HAVE_LIBSOCKET
#undef	HABE_LIBSOCKET
#endif

#ifdef	HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef	TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef	HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#include <termios.h>
#ifdef	HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef	HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef	STDC_HEADERS
#include <stdlib.h>
#include <string.h>
#endif
#include <stdio.h>
#include <ctype.h>
/* The number of signals in the system. */
#ifndef	NSIG
#undef	NSIG
#endif

/* The maximum number of open files. */
#ifndef NOFILE
#undef	NOFILE
#endif

/* Nail's release date. */
#ifndef	REL_DATE
#undef	REL_DATE
#endif

/* The maximum length of a host name. */
#ifndef MAXHOSTNAMELEN
#undef MAXHOSTNAMELEN
#endif
