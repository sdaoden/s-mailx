/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of attachments.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "rcv.h"

#include <errno.h>
#include <unistd.h>

#include "extern.h"

/* We use calloc() for struct attachment */
CTA(AC_DEFAULT == 0);

/* Fill in some attachment fields; don't be interactive if number==0n */
static struct attachment *	_fill_in(struct attachment *ap,
					char const *file, ui_it number);

/* Ask the user to edit file names and other data for the given attachment */
static struct attachment *	_read_attachment_data(struct attachment *ap,
					ui_it number);

/* Try to create temporary charset converted version */
#ifdef HAVE_ICONV
static int			_attach_iconv(struct attachment *ap);
#endif

static struct attachment *
_fill_in(struct attachment *ap, char const *file, ui_it number)
{
	/*
	 * XXX The "attachment-ask-content-*" variables are left undocumented
	 * since they are for RFC connoisseurs only.
	 */
	char prefix[80 * 2];

	ap->a_name = file;
	if ((file = strrchr(file, '/')) != NULL)
		++file;
	else
		file = ap->a_name;

	ap->a_content_type = mime_classify_content_type_by_fileext(file);
	if (number > 0 && value("attachment-ask-content-type")) {
		snprintf(prefix, sizeof prefix, "#%u\tContent-Type: ", number);
		ap->a_content_type = readtty(prefix, ap->a_content_type);
	}

	if (number > 0 && value("attachment-ask-content-disposition")) {
		snprintf(prefix, sizeof prefix,
			"#%u\tContent-Disposition: ", number);
		ap->a_content_disposition = readtty(prefix,
				ap->a_content_disposition);
	}
	if (ap->a_content_disposition == NULL)
		ap->a_content_disposition = "attachment";

	if (number > 0 && value("attachment-ask-content-id")) {
		snprintf(prefix, sizeof prefix, "#%u\tContent-ID: ", number);
		ap->a_content_id = readtty(prefix, ap->a_content_id);
	}

	if (number > 0 && value("attachment-ask-content-description")) {
		snprintf(prefix, sizeof prefix,
			"#%u\tContent-Description: ", number);
		ap->a_content_description = readtty(prefix,
				ap->a_content_description);
	}
	return ap;
}

static struct attachment *
_read_attachment_data(struct attachment *ap, ui_it number)
{
	char prefix[80 * 2];
	char const *cslc, *cp, *defcs;

	if (ap == NULL)
		ap = csalloc(1, sizeof *ap);
	else if (ap->a_msgno) {
		printf(tr(159, "#%u\tmessage %u\n"), number, ap->a_msgno);
		goto jleave;
	} else if (ap->a_conv == AC_TMPFILE) {
		Fclose(ap->a_tmpf);
		ap->a_conv = AC_DEFAULT;
	}

	snprintf(prefix, sizeof prefix, tr(50, "#%u\tfilename: "), number);
	for (;;) {
		if ((ap->a_name = readtty(prefix, ap->a_name)) == NULL) {
			ap = NULL;
			goto jleave;
		}
		if ((cp = file_expand(ap->a_name)) == NULL)
			continue;
		ap->a_name = cp;
		if (access(cp, R_OK) == 0)
			break;
		perror(cp);
	}

	ap = _fill_in(ap, cp, number);

	/*
	 * Character set of attachments: enum attach_conv
	 */
	cslc = charset_get_lc();
#ifdef HAVE_ICONV
	if (! (options & OPT_INTERACTIVE))
		goto jcs;
	if ((cp = ap->a_content_type) != NULL &&
			ascncasecmp(cp, "text/", 5) != 0 &&
			! yorn(tr(162, "Filename doesn't indicate text "
			"content - want to edit charsets? "))) {
		ap->a_conv = AC_DEFAULT;
		goto jleave;
	}

	charset_iter_reset(NULL);
jcs:
#endif
	snprintf(prefix, sizeof prefix, tr(160, "#%u\tinput charset: "),
		number);
	if ((defcs = ap->a_input_charset) == NULL)
		defcs = cslc;
	cp = ap->a_input_charset = readtty(prefix, defcs);
#ifdef HAVE_ICONV
	if (! (options & OPT_INTERACTIVE)) {
#endif
		ap->a_conv = (cp != NULL) ? AC_FIX_INCS : AC_DEFAULT;
#ifdef HAVE_ICONV
		goto jleave;
	}

	snprintf(prefix, sizeof prefix, tr(161, "#%u\toutput (send) charset: "),
		number);
	if ((defcs = ap->a_charset) == NULL)
		defcs = charset_iter_next();
	defcs = ap->a_charset = readtty(prefix, defcs);

	if (cp != NULL && defcs == NULL) {
		ap->a_conv = AC_FIX_INCS;
		goto jleave;
	}
	if (cp == NULL && defcs == NULL) {
		ap->a_conv = AC_DEFAULT;
		ap->a_input_charset = cslc;
		ap->a_charset = charset_iter_current();
	} else if (cp == NULL && defcs != NULL) {
		ap->a_conv = AC_FIX_OUTCS;
		ap->a_input_charset = cslc;
	} else
		ap->a_conv = AC_TMPFILE;

	printf(tr(197, "Trying conversion from %s to %s\n"),
		ap->a_input_charset, ap->a_charset);
	if (_attach_iconv(ap))
		ap->a_conv = AC_TMPFILE;
	else {
		ap->a_input_charset = cp;
		ap->a_charset = defcs;
		goto jcs;
	}
#endif
jleave:
	return ap;
}

#ifdef HAVE_ICONV
static int
_attach_iconv(struct attachment *ap)
{
	struct str oul = {NULL, 0}, inl = {NULL, 0};
	FILE *fo = NULL, *fi = NULL;
	size_t count, lbsize;
	iconv_t icp;

	if ((icp = n_iconv_open(ap->a_charset, ap->a_input_charset))
			== (iconv_t)-1) {
		if (errno == EINVAL)
			goto jeconv;
		else
			perror("iconv_open");
		goto jerr;
	}

	if ((fi = Fopen(ap->a_name, "r")) == NULL) {
		perror(ap->a_name);
		goto jerr;
	}
	count = fsize(fi);

	inl.s = NULL;
	if ((fo = Ftemp(&inl.s, "aiconv", "w+", 0600, 1)) == NULL) {
		perror(tr(51, "temporary mail file"));
		inl.s = NULL;
		goto jerr;
	}
	unlink(inl.s);
	Ftfree(&inl.s);

	for (inl.s = NULL, lbsize = 0;;) {
		if (fgetline(&inl.s, &lbsize, &count, &inl.l, fi, 0) == NULL) {
			if (! count)
				break;
			perror(tr(195, "I/O read error occurred"));
			goto jerr;
		}

		if (n_iconv_str(icp, &oul, &inl, NULL, FAL0) != 0)
			goto jeconv;
		if ((inl.l=fwrite(oul.s, sizeof *oul.s, oul.l, fo)) != oul.l) {
			perror(tr(196, "I/O write error occurred"));
			goto jerr;
		}
	}
	fflush_rewind(fo);

	ap->a_tmpf = fo;
jleave:
	if (inl.s != NULL)
		free(inl.s);
	if (oul.s != NULL)
		free(oul.s);
	if (fi != NULL)
		Fclose(fi);
	if (icp != (iconv_t)-1)
		n_iconv_close(icp);
	return (fo != NULL);
jeconv:
	fprintf(stderr, tr(179, "Cannot convert from %s to %s\n"),
		ap->a_input_charset, ap->a_charset);
jerr:
	if (fo != NULL)
		Fclose(fo);
	fo = NULL;
	goto jleave;
}
#endif /* HAVE_ICONV */

struct attachment *
add_attachment(struct attachment *aphead, char *file, struct attachment **newap)
{
	struct attachment *nap = NULL, *ap;

	if ((file = file_expand(file)) == NULL)
		goto jleave;
	if (access(file, R_OK) != 0)
		goto jleave;

	nap = _fill_in(csalloc(1, sizeof *nap), file, 0);
	if (newap != NULL)
		*newap = nap;
	if (aphead != NULL) {
		for (ap = aphead; ap->a_flink != NULL; ap = ap->a_flink)
			;
		ap->a_flink = nap;
		nap->a_blink = ap;
	} else {
		nap->a_blink = NULL;
		aphead = nap;
	}
	nap = aphead;
jleave:
	return nap;
}

struct attachment *
append_attachments(struct attachment *aphead, char *names)
{
	char *cp;
	struct attachment *xaph, *nap;

	while ((cp = strcomma(&names, 1)) != NULL) {
		if ((xaph = add_attachment(aphead, cp, &nap)) != NULL) {
			aphead = xaph;
			if (options & OPT_INTERACTIVE)
				printf(tr(19, "~@: added attachment \"%s\"\n"),
					nap->a_name);
		} else
			perror(cp);
	}
	return aphead;
}

struct attachment *
edit_attachments(struct attachment *aphead)
{
	struct attachment *ap, *nap;
	ui_it attno = 1;

	/* Modify already present ones? */
	for (ap = aphead; ap != NULL; ap = ap->a_flink) {
		if (_read_attachment_data(ap, attno) != NULL) {
			++attno;
			continue;
		}
		nap = ap->a_flink;
		if (ap->a_blink != NULL)
			ap->a_blink->a_flink = nap;
		else
			aphead = nap;
		if (nap != NULL)
			nap->a_blink = ap->a_blink;
		else
			goto jleave;
	}

	/* Add some more? */
	while ((nap = _read_attachment_data(NULL, attno)) != NULL) {
		if ((ap = aphead) != NULL) {
			while (ap->a_flink != NULL)
				ap = ap->a_flink;
			ap->a_flink = nap;
		}
		nap->a_blink = ap;
		nap->a_flink = NULL;
		if (aphead == NULL)
			aphead = nap;
		++attno;
	}
jleave:
	return aphead;
}
