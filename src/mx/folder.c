/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Folder (mailbox) initialization, newmail announcement and related.
 *
 * Copyright (c) 2012 - 2025 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#define su_FILE folder
#define mx_SOURCE
#define mx_SOURCE_FOLDER

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/path.h>

#include "mx/fexpand.h"
#include "mx/file-streams.h"
#include "mx/okeys.h"
#include "mx/ui-str.h"

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Update mailname (if name != NIL) and displayname, return whether displayname was large enough to swallow mailname */
static boole a_folder_update_mailname(char const *name, boole rdonly);

/**/
static void a_folder_info(void);

static boole
a_folder_update_mailname(char const *name, boole rdonly){ /* TODO 2MUCH work */
	char const *foldp;
	char *mailp, *mailbp, *dispp;
	uz i, j, maillen, foldlen;
	boole rv;
	NYD_IN;

	/* Do not realpath(3) if it's only an update request */
	if(name != NIL){
#ifdef mx_HAVE_REALPATH
		char const *adjname;
		enum protocol p;

		/* v15-compat: which_protocol(): no auto-completion */
		p = which_protocol(name, TRU1, TRU1, &adjname);

		if(p == n_PROTO_FILE || p == n_PROTO_MAILDIR || p == n_PROTO_EML ||
				p == n_PROTO_SMBOX || p == n_PROTO_XMBOX){
			name = adjname;
			if(realpath(name, mailname) == NIL){
				if(su_err_by_errno() != su_ERR_NOENT)
					n_err(_("Cannot canonicalize %s\n"), n_shexp_quote_cp(name, FAL0));
				goto jdocopy;
			}
		}else
jdocopy:
#endif
			su_cs_pcopy_n(mailname, name, sizeof(mailname));
	}
	ASSERT(mailname[0] != '\0');

	mailp = mailname;

	/* Do not display an absolute path but "+FOLDER" if within *folder* */
	n_pstate &= ~n_PS_MAILNAME_WITHIN_FOLDER;
	if(*(foldp = n_folder_query()) != '\0'){
		foldlen = su_cs_len(foldp);
		if(su_cs_cmp_n(foldp, mailp, foldlen))
			foldlen = 0;
		else
			n_pstate |= n_PS_MAILNAME_WITHIN_FOLDER;
	}else
		foldlen = 0;

	/* Never include any paths in displayname when reproducible */
	if(su_state_has(su_STATE_REPRODUCIBLE))
		mailp = n_filename_to_repro(mailp);

	maillen = su_cs_len(mailp);

	/* basename */
	mailbp = &mailp[maillen];
	/*if(maillen > 0)*/
		while(--mailbp > mailp)
			if(*mailbp == '/'){ /* DIRSEP */
				if(mailbp[1] != '\0')
					++mailbp;
				break;
			}

	/* We want to see the name of the folder .. on the screen */
	dispp = displayname;

	if(foldlen > 0){
		dispp[0] = '+';
		dispp[1] = '[';
		dispp += 2;
	}

	j = mx_field_detect_clip(sizeof(displayname) - 16/* user prompt */ - 3 - n_mb_cur_max -1, mailp, maillen);
	if(j != maillen){
		dispp[0] = dispp[1] = '.';
		dispp += 2;
		i = su_cs_len(mailbp);
		j = mx_field_detect_clip(sizeof(displayname) - 3 - 2 - 1 - n_mb_cur_max -1, mailbp, i);
		if(j == i){
			*dispp++ = '/';
			su_mem_copy(dispp, mailbp, i);
			dispp += i;
		}
		rv = FAL0;
	}else{
		su_mem_copy(dispp, mailp, maillen);
		dispp += maillen;
		rv = TRU1;
	}

	if(foldlen > 0)
		*dispp++ = ']';
	*dispp = '\0';

	n_PS_ROOT_BLOCK((
		ok_vset(mailbox_basename, mailbp),
		ok_vset(mailbox_display, displayname),
		ok_vset(mailbox_resolved, mailname),
		(rdonly ? ok_bset(mailbox_read_only) : ok_bclear(mailbox_read_only))
	));

	NYD_OU;
	return rv;
}

static void
a_folder_info(void){
	struct message *mp;
	int u, n, d, s, hidden, moved;
	NYD2_IN;

	if(mb.mb_type == MB_VOID){
		fprintf(n_stdout, _("(Currently no active mailbox)"));
		goto jleave;
	}

	s = d = hidden = moved = 0;
	for(mp = message, n = 0, u = 0; PCMP(mp, <, &message[msgCount]); ++mp){
		if(mp->m_flag & MNEW)
			++n;
		if((mp->m_flag & MREAD) == 0)
			++u;
		if((mp->m_flag & (MDELETED | MSAVED)) == (MDELETED | MSAVED))
			++moved;
		if((mp->m_flag & (MDELETED | MSAVED)) == MDELETED)
			++d;
		if((mp->m_flag & (MDELETED | MSAVED)) == MSAVED)
			++s;
		if(mp->m_flag & MHIDDEN)
			++hidden;
	}

	/* With truncated displayname there is no option to see mailbox full pathname: print at least for '? fi' */
	/* C99 */{
		char const *cp;

		cp = a_folder_update_mailname(NIL, (mb.mb_perm == 0)) ? displayname : mailname;
		if(su_state_has(su_STATE_REPRODUCIBLE))
			cp = n_filename_to_repro(cp);

		fprintf(n_stdout, "%s: ", n_shexp_quote_cp(cp, FAL0));
	}
	if(msgCount == 1)
		fprintf(n_stdout, _("1 message"));
	else
		fprintf(n_stdout, _("%d messages"), msgCount);
	if(n > 0)
		fprintf(n_stdout, _(" %d new"), n);
	if(u-n > 0)
		fprintf(n_stdout, _(" %d unread"), u);
	if(d > 0)
		fprintf(n_stdout, _(" %d deleted"), d);
	if(s > 0)
		fprintf(n_stdout, _(" %d saved"), s);
	if(moved > 0)
		fprintf(n_stdout, _(" %d moved"), moved);
	if(hidden > 0)
		fprintf(n_stdout, _(" %d hidden"), hidden);
	if(mb.mb_perm == 0)
		fprintf(n_stdout, _(" [Read-only]"));
#ifdef mx_HAVE_IMAP
	if(mb.mb_type == MB_CACHE)
		fprintf(n_stdout, _(" [Disconnected]"));
#endif

jleave:
	putc('\n', n_stdout);
	NYD2_OU;
}

FL int
newmailinfo(int omsgCount){
	int mdot, i;
	NYD_IN;

	for(i = 0; i < omsgCount; ++i)
		message[i].m_flag &= ~MNEWEST;

	if(msgCount > omsgCount){
		for(i = omsgCount; i < msgCount; ++i)
			message[i].m_flag |= MNEWEST;
		fprintf(n_stdout, _("New mail has arrived.\n"));
		if((i = msgCount - omsgCount) == 1)
			fprintf(n_stdout, _("Loaded 1 new message.\n"));
		else
			fprintf(n_stdout, _("Loaded %d new messages.\n"), i);
	}else
		fprintf(n_stdout, _("Loaded %d messages.\n"), msgCount);

	mdot = getmdot(1);

	if(ok_blook(header) && (i = omsgCount + 1) <= msgCount){
#ifdef mx_HAVE_IMAP
		if(mb.mb_type == MB_IMAP)
			imap_getheaders(i, msgCount); /* TODO not here */
#endif
		for(omsgCount = 0; i <= msgCount; ++omsgCount, ++i)
			n_msgvec[omsgCount] = i;
		n_msgvec[omsgCount] = 0;
		print_headers(n_msgvec, FAL0, FAL0);
	}

	mx_temporary_on_mailbox_event(mx_ON_MAILBOX_EVENT_NEWMAIL);

	NYD_OU;
	return mdot;
}

FL void
setmsize(int size){
	NYD_IN;

	if(n_msgvec != NIL)
		su_FREE(n_msgvec);
	n_msgvec = su_TCALLOC(int, S(uz,size) + 1);

	NYD_OU;
}

FL void
print_header_summary(char const *Larg){
	uz i;
	NYD_IN;

	getmdot(0);
#ifdef mx_HAVE_IMAP
	if(mb.mb_type == MB_IMAP)
		imap_getheaders(0, msgCount); /* TODO not here */
#endif
	ASSERT(n_msgvec != NIL);

	if(Larg != NIL){
		/* Avoid any messages XXX add a make_mua_silent() and use it? */
		if((n_poption & (n_PO_V | n_PO_EXISTONLY)) == n_PO_EXISTONLY){
			n_stdout = freopen(su_path_null, "w", stdout);
			n_stderr = freopen(su_path_null, "w", stderr);
		}
		i = (n_getmsglist(mx_SCOPE_NONE, FAL0, n_shexp_quote_cp(Larg, FAL0), n_msgvec, 0, NIL) <= 0);
		if(n_poption & n_PO_EXISTONLY)
			n_exit_status = S(int,i);
		else if(i == 0)
			print_headers(n_msgvec, TRU1, FAL0); /* TODO should be iterator! */
	}else{
		i = 0;
		if(!mb.mb_threaded){
			for(; UCMP(z, i, <, msgCount); ++i)
				n_msgvec[i] = i + 1;
		}else{
			struct message *mp;

			for(mp = threadroot; mp; ++i, mp = next_in_thread(mp))
				n_msgvec[i] = S(int,P2UZ(mp - message + 1));
		}
		print_headers(n_msgvec, FAL0, TRU1); /* TODO should be iterator! */
	}

	NYD_OU;
}

FL void
n_folder_announce(enum n_announce_flags af){
	int vec[2], mdot;
	NYD_IN;

	mdot = (mb.mb_type == MB_VOID) ? 1 : getmdot(0);
	dot = &message[mdot - 1];

	if(af != n_ANNOUNCE_NONE && ok_blook(header) &&
			((af & n_ANNOUNCE_MAIN_CALL) || ((af & n_ANNOUNCE_CHANGE) && !ok_blook(posix))))
		af |= n_ANNOUNCE_STATUS | n__ANNOUNCE_HEADER;

	if(af & n_ANNOUNCE_STATUS){
		a_folder_info();
		af |= n__ANNOUNCE_ANY;
	}

	if(af & n__ANNOUNCE_HEADER){
		if(!(af & n_ANNOUNCE_MAIN_CALL) && ok_blook(bsdannounce))
			n_OBSOLETE(_("*bsdannounce* is now default behavior"));
		vec[0] = mdot;
		vec[1] = 0;
		print_header_group(vec); /* XXX errors? */
		af |= n__ANNOUNCE_ANY;
	}

	if(af & n__ANNOUNCE_ANY)
		fflush(n_stdout);

	NYD_OU;
}

FL int
getmdot(int nmail){
	struct message *mp;
	char *cp;
	int mdot;
	BITENUM(u32,mflag) avoid;
	NYD_IN;

	avoid = MHIDDEN | MDELETED;

	if(!nmail){
		if(ok_blook(autothread)){
			n_OBSOLETE(_("please use *autosort=thread* instead of *autothread*"));
			c_thread(NIL);
		}else if((cp = ok_vlook(autosort)) != NIL){
			if(mb.mb_sorted != NIL)
				su_FREE(mb.mb_sorted);
			mb.mb_sorted = su_cs_dup(cp, 0);
			c_sort(NIL);
		}
	}

	if(mb.mb_type == MB_VOID){
		mdot = 1;
		goto jleave;
	}

	if(nmail)
		for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp)
			if((mp->m_flag & (MNEWEST | avoid)) == MNEWEST)
				break;

	if(!nmail || PCMP(mp, >=, &message[msgCount])){
		if(mb.mb_threaded){
			for(mp = threadroot; mp != NIL; mp = next_in_thread(mp))
				if((mp->m_flag & (MNEW | avoid)) == MNEW)
					break;
		}else for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp)
			if((mp->m_flag & (MNEW | avoid)) == MNEW)
				break;
	}

	if((mb.mb_threaded ? (mp == NIL) : PCMP(mp, >=, &message[msgCount]))){
		if(mb.mb_threaded){
			for(mp = threadroot; mp != NIL; mp = next_in_thread(mp))
				if(mp->m_flag & MFLAGGED)
					break;
		}else{
			for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp)
				if(mp->m_flag & MFLAGGED)
					break;
		}
	}

	if((mb.mb_threaded ? (mp == NIL) : PCMP(mp, >=, &message[msgCount]))){
		if(mb.mb_threaded){
			for(mp = threadroot; mp != NIL; mp = next_in_thread(mp))
				if(!(mp->m_flag & (MREAD | avoid)))
					break;
		}else{
			for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp)
				if(!(mp->m_flag & (MREAD | avoid)))
					break;
		}
	}

	if(nmail && (mb.mb_threaded ? (mp != NIL) : PCMP(mp, <, &message[msgCount])))
		mdot = S(int,P2UZ(mp - message + 1));
	else if(ok_blook(showlast)){
		if(mb.mb_threaded){
			for(mp = this_in_thread(threadroot, -1); mp; mp = prev_in_thread(mp))
				if(!(mp->m_flag & avoid))
					break;
			mdot = (mp != NIL) ? S(int,P2UZ(mp - message + 1)) : msgCount;
		}else{
			for(mp = message + msgCount - 1; mp >= message; --mp)
				if(!(mp->m_flag & avoid))
					break;
			mdot = (mp >= message) ? S(int,P2UZ(mp - message + 1)) : msgCount;
		}
	}else if(!nmail && (mb.mb_threaded ? (mp != NIL) : PCMP(mp, <, &message[msgCount])))
		mdot = S(int,P2UZ(mp - message + 1));
	else if(mb.mb_threaded){
		for(mp = threadroot; mp; mp = next_in_thread(mp))
			if(!(mp->m_flag & avoid))
				break;
		mdot = (mp != NIL) ? S(int,P2UZ(mp - message + 1)) : 1;
	}else{
		for(mp = message; PCMP(mp, <, &message[msgCount]); ++mp)
			if(!(mp->m_flag & avoid))
				break;
		mdot = PCMP(mp, <, &message[msgCount]) ? S(int,P2UZ(mp - message + 1)) : 1;
	}

jleave:
	NYD_OU;
	return mdot;
}

FL void
n_initbox(char const *name, boole rdonly){
	struct mx_fs_tmp_ctx *fstcp;
	boole err;
	NYD_IN;

	if(mb.mb_type != MB_VOID)
		su_cs_pcopy_n(prevfile, mailname, PATH_MAX);

	/* TODO name always NE mailname (but goes away for objects anyway)
	 * TODO Well, not true no more except that in parens */
	a_folder_update_mailname((name != mailname ? name : NIL), rdonly);

	err = FAL0;
	if((mb.mb_otf = mx_fs_tmp_open(NIL, "tmpmbox",
				(mx_FS_O_WRONLY | mx_FS_O_HOLDSIGS | mx_FS_O_NOREGISTER), &fstcp)) == NIL){
		n_perr(_("initbox: temporary mail message file, writer"), 0);
		err = TRU1;
	}else if((mb.mb_itf = mx_fs_open(fstcp->fstc_filename, (mx_FS_O_RDONLY | mx_FS_O_NOREGISTER))) == NIL){
		n_perr(_("initbox: temporary mail message file, reader"), 0);
		err = TRU1;
	}
	mx_fs_tmp_release(fstcp);
	if(err)
		exit(su_EX_ERR); /* TODO no! */

	mx_message_reset();
	mb.mb_active = MB_NONE;
	mb.mb_threaded = 0;
#ifdef mx_HAVE_IMAP
	mb.mb_flags = MB_NOFLAGS;
#endif
	if(mb.mb_sorted != NIL){
		su_FREE(mb.mb_sorted);
		mb.mb_sorted = NIL;
	}
	dot = prevdot = threadroot = NIL;
	n_pstate &= ~n_PS_DID_PRINT_DOT;

	NYD_OU;
}

FL char const *
n_folder_query(void){
	struct n_string s_b, *s;
	enum protocol proto;
	char *cp;
	char const *rv, *adjcp;
	boole err;
	NYD_IN;

	s = n_string_creat_auto(&s_b);

	/* *folder* is linked with *folder_resolved*: we only use the latter */
	for(err = FAL0;;){
		if((rv = ok_vlook(folder_resolved)) != NIL)
			break;

		/* POSIX says:
		 *	If directory does not start with a <slash> ('/'), the contents
		 *	of HOME shall be prefixed to it.
		 * And:
		 *	If folder is unset or set to null, [.] filenames beginning with
		 *	'+' shall refer to files in the current directory.
		 * We may have the result already.
		 * P.S.: that "or set to null" seems to be a POSIX bug, V10 mail and BSD
		 * Mail since 1982 work differently, follow suit */
		rv = su_empty;
		err = FAL0;

		if((cp = ok_vlook(folder)) == NIL)
			goto jset;

		/* Expand the *folder*; skip %: prefix for simplicity of use */
		if(cp[0] == '%' && cp[1] == ':')
			cp += 2;
		cp = mx_fexpand(cp, (mx_FEXP_NSPECIAL | mx_FEXP_NFOLDER | mx_FEXP_NSHELL | mx_FEXP_NGLOB));
		if((err = ((cp == NIL) /*|| *cp == '\0'*/)))
			goto jset;
		else{
			uz i;

			for(i = su_cs_len(cp);;){
				if(--i == 0)
					goto jset;
				if(cp[i] != '/'){
					cp[++i] = '\0';
					break;
				}
			}
		}

		switch((proto = which_protocol(cp, FAL0, FAL0, &adjcp))){
		case n_PROTO_POP3:
			n_err(_("*folder*: cannot use the POP3 protocol\n"));
			err = TRU1;
			goto jset;
		case n_PROTO_IMAP:
#ifdef mx_HAVE_IMAP
			rv = cp;
			if(!su_cs_cmp(rv, protbase(rv)))
				rv = savecatsep(rv, '/', n_empty);
#else
			n_err(_("*folder*: IMAP support not compiled in\n"));
			err = TRU1;
#endif
			goto jset;
		default:
			/* Further expansion desired */
			break;
		}

		/* Prefix HOME as necessary */
		if(*adjcp != '/'){ /* XXX path_is_absolute() */
			uz l1, l2;
			char const *home;

			home = ok_vlook(HOME);
			l1 = su_cs_len(home);
			ASSERT(l1 > 0); /* (checked VIP variable) */
			l2 = su_cs_len(cp);

			s = n_string_reserve(s, l1 + 1 + l2 +1);

			if(cp != adjcp){
				uz i;

				s = n_string_push_buf(s, cp, i = P2UZ(adjcp - cp));
				cp += i;
				l2 -= i;
			}

			s = n_string_push_buf(s, home, l1);
			if(l2 > 0){
				s = n_string_push_c(s, '/');
				s = n_string_push_buf(s, cp, l2);
			}
			cp = n_string_cp(s);
			s = n_string_drop_ownership(s);
		}

		/* TODO Since visual mailname is resolved via realpath(3) if available
		 * TODO to avoid that we loose track of our currently open folder in case
		 * TODO we chdir away, but still checks the leading path portion against
		 * TODO folder_query() to be able to abbreviate to the +FOLDER syntax if
		 * TODO possible, we need to realpath(3) the folder, too */
		rv = cp;
#ifdef mx_HAVE_REALPATH
		ASSERT(s->s_len == 0 && s->s_dat == NIL);
		s = n_string_reserve(s, PATH_MAX +1);
		if(realpath(cp, s->s_dat) != NIL)
			rv = s->s_dat;
		else{
			if(su_err_by_errno() != su_ERR_NOENT){
				n_err(_("Cannot canonicalize *folder*: %s\n"), n_shexp_quote_cp(cp, FAL0));
				err = TRU1;
				rv = su_empty;
			}
		}
		s = n_string_drop_ownership(s);
#endif

		/* Always append a solidus to our result path upon success */
		if(!err){
			uz i;

			i = su_cs_len(rv);
			if(rv[i - 1] != '/'){
				s = n_string_reserve(s, i + 1 +1);
				s = n_string_push_buf(s, rv, i);
				s = n_string_push_c(s, '/');
				rv = n_string_cp(s);
				s = n_string_drop_ownership(s);
			}
		}

jset:
		n_PS_ROOT_BLOCK(ok_vset(folder_resolved, rv));
	}

	if(err){
		n_err(_("*folder* is not resolvable, using CWD\n"));
		ASSERT(rv != NIL && *rv == '\0');
	}

	NYD_OU;
	return rv;
}

FL void
n_folder_mbox_setptr(FILE *ibuf, off_t offset, enum protocol proto, boole maybepipe){
	enum{
		a_RFC4155 = 1u<<0,
		a_HAD_BAD_FROM_ = 1u<<1,
		a_HAD_DATA = 1u<<2,
		a_HAD_ONE = 1u<<3,
		a_MAYBE = 1u<<4,
		a_CREATE = 1u<<5,
		a_INHEAD = 1u<<6,
		a_COMMIT = 1u<<7,
		a_ISEML = 1u<<16
	};

	struct message self, commit;
	u32 f;
	uz linesize, fulnsz, fulnlen, filesize, cnt;
	char *linebuf, *fulnbp, *cp;
int msgcnt_base;
	NYD_IN;

	STRUCT_ZERO(struct message, &self);
	self.m_flag = MVALID | MNEW | MNEWEST;

	mx_fs_linepool_aquire(&linebuf, &linesize);
	mx_fs_linepool_aquire(&fulnbp, &fulnsz);
	fulnlen = 0;
	filesize = S(uz,mailsize - offset); /* XXX overflow <> fgetline() */
	f = ((proto == n_PROTO_EML) ? (a_ISEML | a_HAD_DATA) : (a_MAYBE | (ok_blook(mbox_rfc4155) ? a_RFC4155 : 0)));

	offset = ftell(mb.mb_otf);
msgcnt_base=msgCount;
	for(;;){
		/* Ensure space for terminating LF, so do append it */
		if(UNLIKELY(fgetline(&linebuf, &linesize, (maybepipe ? NIL : &filesize), &cnt, ibuf, TRU1) == NIL))
			break;

		/* TODO Normalize away line endings, we will place (readded) \n */
		/* TODO v15 MIME layer rewrite: we need to parse this into a tree of
		 * TODO objects, and the "Part" object must recognize the used line
		 * TODO ending; this is in fact standard, RFC 2045 etc; ie, we may need
		 * TODO to reencode a part when writing a RFC 5322 eml, or when writing
		 * TODO a RFC 4155 MBOX, in order to keep the part-native line-endings
		 * TODO alive; in particular RFC 2045 about CRLF regarding this!! */
		if(cnt >= 2 && linebuf[cnt - 2] == '\r')
			linebuf[--cnt] = '\0';
		linebuf[--cnt] = '\0';
		/* We cannot use this ASSERTion since it will trigger for example when the Linux kernel crashes
		 * and the log files (which i saw containing NULs, then; ArchLinux, systemd) are sent out via email! */
		/*ASSERT(linebuf[0] != '\0' || cnt == 0);*/

		if(UNLIKELY(cnt == 0)){
			if(!(f & a_ISEML))
				f |= a_MAYBE;

			if(LIKELY(!(f & a_CREATE)))
				f &= ~a_INHEAD;
			else{
				f |= a_HAD_BAD_FROM_;
				f &= ~(a_CREATE | a_INHEAD | a_COMMIT);
				commit.m_size += self.m_size;
				commit.m_lines += self.m_lines;
				commit.m_flag |= MBADFROM_;
				self = commit;
			}
			goto jputln;
		}else{
			boole from_;

/*
fprintf(stderr, "%s\n",linebuf);*/
			if(UNLIKELY((f & a_MAYBE) &&
					(from_ = (linebuf[0] == 'F')) && (from_ = is_head(linebuf, cnt, TRU1)) &&
					(from_ == TRU1 || !(f & a_RFC4155)))){
				/* TODO char date[n_FROM_DATEBUF];
				 * TODO extract_date_from_from_(linebuf, cnt, date);
				 * TODO self.m_time = 10000; */
				self.m_xsize = self.m_size;
				self.m_xlines = self.m_lines;
				self.m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
				commit = self;

				f |= a_CREATE | a_INHEAD;
				if(f & a_ISEML)
					f |= a_COMMIT;
				else if(!(f & a_COMMIT) && (f & (a_HAD_DATA | a_HAD_ONE)) == a_HAD_DATA){
#if 1
/*
fprintf(stderr, "HIER, %d %d\n",msgcnt_base,msgCount);*/
					if(msgcnt_base == msgCount)
						mx_message_append(&commit);
					else
						message[msgCount - 1] = commit;
#endif


					commit.m_flag &= ~MVALID;
/*
					mx_message_append(&commit);
					*/
				}
				f &= ~a_MAYBE;
				f |= a_HAD_DATA;

				if(from_ == TRUM1){
					f |= a_HAD_BAD_FROM_;
					/* TODO MBADFROM_ causes the From_ line to be replaced entirely
					 * TODO when the message is newly written via e.g. `copy'.
					 * TODO Instead this From_ should be an object which can fix
					 * TODO the parts which are missing or are faulty, such that
					 * TODO good info can be kept; this is possible after the main
					 * TODO message header has been fully parsed. For now we're
					 * TODO stuck and fail eg in a_header_extract_date_from_from_()
					 * TODO (which should not exist as such btw) */
					self.m_flag = MVALID | MNEW | MNEWEST | MBADFROM_;
				}else
					self.m_flag = MVALID | MNEW | MNEWEST | (f & a_ISEML ? MNOFROM : 0);
				self.m_size = 0;
				self.m_lines = 0;
				self.m_block = mailx_blockof(offset);
				self.m_offset = mailx_offsetof(offset);
				goto jputln;
			}

			f &= ~a_MAYBE;
			if(LIKELY(!(f & a_INHEAD))){
				if(UNLIKELY(!(f & (a_ISEML | a_HAD_ONE | a_HAD_DATA)))){
					for(cp = &linebuf[cnt]; cp-- != linebuf;)
						if(!su_cs_is_space(*cp)){
							f |= a_HAD_DATA;
							self.m_flag |= MNOFROM;
							break;
						}
				}
				goto jputln;
			}
		}

		if(LIKELY((cp = su_mem_find(linebuf, ':', cnt)) != NIL)){
			u32 mf;
			char *cps, *cpe, c;

			f |= a_HAD_DATA;
			if(f & a_CREATE)
				f |= a_COMMIT;

			mf = self.m_flag;

			for(cps = linebuf; su_cs_is_blank(*cps); ++cps){
			}
			for(cpe = cp; cpe > cps && su_cs_is_blank(cpe[-1]); --cpe){
			}
			switch(P2UZ(cpe - cps)){
			case sizeof("status") -1:
				if(!su_cs_cmp_case_n(cps, "status", sizeof("status") -1)){
					for(;;){
						if((c = *++cp) == '\0')
							break;
						switch(c){
						case 'R': mf |= MREAD; break;
						case 'O': mf &= ~MNEW; break;
						}
					}
				}
				break;
			case sizeof("x-status") -1:
				if(!su_cs_cmp_case_n(cps, "x-status", sizeof("x-status") -1)){
					for(;;){
						if((c = *++cp) == '\0')
							break;
						switch(c){
						case 'F': mf |= MFLAGGED; break;
						case 'A': mf |= MANSWERED; break;
						case 'T': mf |= MDRAFTED; break;
						}
					}
				}
				break;
			}

			self.m_flag = mf;
		}else if(!su_cs_is_blank(linebuf[0])){
			/* So either this is a false detection (nothing but From_ line yet, and is not a valid MBOX
			 * according to POSIX and RFC 5322), or no separating empty line in between header/body!
			 * In the latter case, add one! */
			f |= a_HAD_DATA;
			if(!(f & a_CREATE)){
				if(putc('\n', mb.mb_otf) == EOF){
					n_perr(_("/tmp"), su_err_by_errno());
					exit(su_EX_ERR); /* TODO no! */
				}
				++offset;
				++self.m_size;
				++self.m_lines;
			}else{
				commit.m_size += self.m_size;
				commit.m_lines += self.m_lines;
				self = commit;
				f &= ~(a_CREATE | a_INHEAD | a_COMMIT);
			}
		}else if(!(f & (a_HAD_ONE | a_HAD_DATA))){
			for(cp = &linebuf[cnt]; cp-- != linebuf;)
				if(!su_cs_is_space(*cp)){
					f |= a_HAD_DATA;
					break;
				}
		}

jputln:
		if(f & a_COMMIT){
			f &= ~(a_CREATE | a_COMMIT);
			if(f & a_HAD_ONE)
				mx_message_append(&commit);
			f |= a_HAD_ONE | a_HAD_DATA;
		}
		linebuf[cnt++] = '\n';
		ASSERT(linebuf[cnt] == '\0');
		fwrite(linebuf, sizeof *linebuf, cnt, mb.mb_otf);

		if(ferror(mb.mb_otf)){
			n_perr(_("/tmp"), su_err_by_errno());
			exit(su_EX_ERR); /* TODO no! */
		}
		offset += cnt;
		self.m_size += cnt;
		++self.m_lines;
	}

	/* TODO We are not prepared for error here */
	if(f & (a_HAD_DATA | a_HAD_ONE)){
		if(f & a_CREATE){
			commit.m_size += self.m_size;
			commit.m_lines += self.m_lines;
		}else
			commit = self;
		commit.m_xsize = commit.m_size;
		commit.m_xlines = commit.m_lines;
		commit.m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
		if(!(f & (a_ISEML | a_HAD_ONE)))
			commit.m_flag &= ~MVALID;
		if(f & a_ISEML)
			commit.m_flag |= MNOFROM;
		mx_message_append(&commit);
	}
	mx_message_append_nil();

	if(f & a_HAD_BAD_FROM_){
		/*if(!(mb.mb_active & MB_BAD_FROM_))*/{
			mb.mb_active |= MB_BAD_FROM_;
			/* TODO mbox-rfc4155 does NOT fix From_ line!
			 * And, TODO: of course MBOXO cannot be it!  We need C-T-E!! */
			n_err(_(
"MBOX with non-conforming From_ line(s), or message(s) without headers!\n"
"  Message boundaries may have been misdetected!\n"
"  Setting *mbox-rfc4155* and reopening _may_ improve the result.\n"
"  Recreating the mailbox will perform MBOXO quoting: \"copy * SOME-FILE\".\n"
"  (This is TODO: in v15 we will be able to apply MIME re-encoding instead!)\n"
"  (Then unset *mbox-rfc4155* again.)\n")
			);
		}
	}

	mx_fs_linepool_release(fulnbp, fulnsz);
	mx_fs_linepool_release(linebuf, linesize);

	NYD_OU;
}

FL int
n_folder_mbox_prepare_append(FILE *fout, boole post, struct su_pathinfo *pip_or_nil){
	/* TODO n_folder_mbox_prepare_append -> Mailbox->append() */
	struct su_pathinfo pi;
	char buf[2];
	int rv;
	boole needsep;
	NYD2_IN;

	if(fseek(fout, -2L, SEEK_END) == 0 && fread(buf, sizeof *buf, 2, fout) == 2)
		needsep = (buf[0] != '\n' || buf[1] != '\n');
	else{
		rv = su_err_by_errno();
		if(pip_or_nil == NIL){
			pip_or_nil = &pi;
			if(!su_pathinfo_fstat(pip_or_nil, fileno(fout))){
				rv = su_err();
				goto jleave;
			}
		}

		if(pip_or_nil->pi_size >= 2){
			if(rv == su_ERR_PIPE)
				rv = su_ERR_NONE; /* XXX Just cannot do our job with ESPIPE ??? */
			goto jleave;
		}
		if(!post){
			if(pip_or_nil->pi_size == 0){
				clearerr(fout);
				rv = su_ERR_NONE;
				goto jleave;
			}
		}

		if(fseek(fout, -1L, SEEK_END) != 0)
			goto jerrno;
		if(fread(buf, sizeof *buf, 1, fout) != 1)
			goto jerrno;
		needsep = (buf[0] != '\n');
	}

	rv = su_ERR_NONE;
	if((needsep && (fseek(fout, 0L, SEEK_END) || putc('\n', fout) == EOF)) || fflush(fout) == EOF)
jerrno:
		rv = su_err_by_errno();

jleave:
	NYD2_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FOLDER
/* s-itt-mode */
