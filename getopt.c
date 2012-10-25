/*
 * getopt() - command option parsing
 *
 * Gunnar Ritter, Freiburg i. Br., Germany, March 2002.
 */

#include "config.h"

#ifdef HAVE_GETOPT
typedef int avoid_empty_file_compiler_warning;
#else

# include "rcv.h"

# ifndef HAVE_SSIZE_T
typedef	long	ssize_t;
# endif

/*
 * One should not think that re-implementing this is necessary, but
 *
 * - Some libcs print weird messages.
 *
 * - GNU libc getopt() is totally brain-damaged, as it requires special
 *   care _not_ to reorder parameters and can't be told to work correctly
 *   with ':' as first optstring character at all.
 */

char	*optarg;
int	optind = 1;
int	opterr = 1;
int	optopt;

static void
error(const char *s, int c)
{
	/*
	 * Avoid including <unistd.h>, in case its getopt() declaration
	 * conflicts.
	 */
	extern ssize_t	write(int, const void *, size_t);
	const char	*msg = 0;
	char	*buf, *bp;

	switch (c) {
	case '?':
		msg = ": illegal option -- ";
		break;
	case ':':
		msg = ": option requires an argument -- ";
		break;
	}
	bp = buf = ac_alloc(strlen(s) + strlen(msg) + 2);
	while (*s)
		*bp++ = *s++;
	while (*msg)
		*bp++ = *msg++;
	*bp++ = optopt;
	*bp++ = '\n';
	write(2, buf, bp - buf);
	ac_free(buf);
}

int
getopt(int argc, char *const argv[], const char *optstring)
{
	int	colon;
	static const char	*lastp;
	const char	*curp;

	if (optstring[0] == ':') {
		colon = 1;
		optstring++;
	} else
		colon = 0;
	if (lastp) {
		curp = lastp;
		lastp = 0;
	} else {
		if (optind >= argc || argv[optind] == 0 ||
				argv[optind][0] != '-' ||
				argv[optind][1] == '\0')
			return -1;
		if (argv[optind][1] == '-' && argv[optind][2] == '\0') {
			optind++;
			return -1;
		}
		curp = &argv[optind][1];
	}
	optopt = curp[0] & 0377;
	while (optstring[0]) {
		if (optstring[0] == ':') {
			optstring++;
			continue;
		}
		if ((optstring[0] & 0377) == optopt) {
			if (optstring[1] == ':') {
				if (curp[1] != '\0') {
					optarg = (char *)&curp[1];
					optind++;
				} else {
					if ((optind += 2) > argc) {
						if (!colon && opterr)
							error(argv[0], ':');
						return colon ? ':' : '?';
					}
					optarg = argv[optind - 1];
				}
			} else {
				if (curp[1] != '\0')
					lastp = &curp[1];
				else
					optind++;
				optarg = 0;
			}
			return optopt;
		}
		optstring++;
	}
	if (!colon && opterr)
		error(argv[0], '?');
	if (curp[1] != '\0')
		lastp = &curp[1];
	else
		optind++;
	optarg = 0;
	return '?';
}

# ifdef __APPLE__
/*
 * Starting with Mac OS 10.5 Leopard, <unistd.h> turns getopt()
 * into getopt$UNIX2003() by default. Consequently, this function
 * is called instead of the one defined above. However, optind is
 * still taken from this file, so in effect, options are not
 * properly handled. Defining an own getopt$UNIX2003() function
 * works around this issue.
 */
int
getopt$UNIX2003(int argc, char *const argv[], const char *optstring)
{
	return getopt(argc, argv, optstring);
}
# endif	/* __APPLE__ */
#endif /* HAVE_GETOPT */
