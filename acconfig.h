@BOTTOM@
/* $Id: acconfig.h,v 1.2 2000/05/01 23:08:34 gunnar Exp $ */

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

#ifdef	HAVE_NETDB_H
#include <netdb.h>
#endif

/* The maximum length of a host name. */
#ifndef MAXHOSTNAMELEN
#undef MAXHOSTNAMELEN
#endif

/* Define if you have the Socket API. */
#ifndef	HAVE_SOCKETS
#undef	HAVE_SOCKETS
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

/* The number of signals in the system. */
#ifndef	NSIG
#undef	NSIG
#endif

#ifdef	HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

/* The maximum number of open files. */
#ifndef NOFILE
#undef	NOFILE
#endif

/* Nail's release date. */
#ifndef	REL_DATE
#undef	REL_DATE
#endif
