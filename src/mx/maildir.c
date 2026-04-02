/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of maildir.h.
 *@ FIXME rewrite - why do we chdir(2)??
 *@ FIXME Simply truncating paths isn't really it.
 *@ TODO Effective size limit 31-bit.
 *@
 *@ In the below COURIER refers to the document of the Courier mail server:
 *@	maildir - E-mail directory
 *@ References to "Dovecot" mean behavior of the according software in 2017.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2004 Gunnar Ritter.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *		notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *		notice, this list of conditions and the following disclaimer in the
 *		documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *		must display the following acknowledgement:
 *		This product includes software developed by Gunnar Ritter
 *		and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *		may be used to endorse or promote products derived from this software
 *		without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define su_FILE maildir
#define mx_SOURCE
#define mx_SOURCE_MAILDIR

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_MAILDIR

#include <dirent.h>

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>
#include <su/prime.h>
#include <su/time.h>

#include "mx/file-streams.h"
#include "mx/okeys.h"
#include "mx/sigs.h"
#include "mx/time.h"
#include "mx/tty.h"

#include "mx/maildir.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/*
 * Maildir as of DJB (cr.yp.to/proto/maildir.html):
 * ------------------------------------------------

Modern delivery identifiers are created by concatenating enough of the
following strings to guarantee uniqueness:

  * #/n/, where /n/ is (in hexadecimal) the output of the operating
    system's unix_sequencenumber() system call, which returns a number
    that increases by 1 every time it is called, starting from 0 after
    reboot.
  * X/n/, where /n/ is (in hexadecimal) the output of the operating
    system's unix_bootnumber() system call, which reports the number of
    times that the system has been booted. Together with #, this
    guarantees uniqueness; unfortunately, most operating systems don't
    support unix_sequencenumber() and unix_bootnumber.
  * R/n/, where /n/ is (in hexadecimal) the output of the operating
    system's unix_cryptorandomnumber() system call, or an equivalent
    source such as /dev/urandom. Unfortunately, some operating systems
    don't include cryptographic random number generators.
  * I/n/, where /n/ is (in hexadecimal) the UNIX inode number of this
    file. Unfortunately, inode numbers aren't always available through NFS.
  * V/n/, where /n/ is (in hexadecimal) the UNIX device number of this
    file. Unfortunately, device numbers aren't always available through
    NFS. (Device numbers are also not helpful with the standard UNIX
    filesystem: a maildir has to be within a single UNIX device for
    link() and rename() to work.)
  * M/n/, where /n/ is (in decimal) the microsecond counter from the
    same gettimeofday() used for the left part of the unique name.
  * P/n/, where /n/ is (in decimal) the process ID.
  * Q/n/, where /n/ is (in decimal) the number of deliveries made by
    this process.

Old-fashioned delivery identifiers use the following formats:

  * /n/, where /n/ is the process ID, and where this process has been
    forked to make one delivery. Unfortunately, some foolish operating
    systems repeat process IDs quickly, breaking the standard time+pid
    combination.
  * /n/_/m/, where /n/ is the process ID and /m/ is the number of
    deliveries made by this process.

[...]

When you move a file from *new* to *cur*, you have to change its name
from /uniq/ to /uniq:info/. Make sure to preserve the /uniq/ string, so
that separate messages can't bump into each other.

/info/ is morally equivalent to the Status field used by mbox readers.
It'd be useful to have MUAs agree on the meaning of /info/, so I'm
keeping a list of /info/ semantics. Here it is.

/info/ starting with "1,": Experimental semantics.

/info/ starting with "2,": Each character after the comma is an
independent flag.

  * Flag "P" (passed): the user has resent/forwarded/bounced this
    message to someone else.
  * Flag "R" (replied): the user has replied to this message.
  * Flag "S" (seen): the user has viewed this message, though perhaps he
    didn't read all the way through it.
  * Flag "T" (trashed): the user has moved this message to the trash;
    the trash will be emptied by a later user action.
  * Flag "D" (draft): the user considers this message a draft; toggled
    at user discretion.
  * Flag "F" (flagged): user-defined flag; toggled at user discretion.

New flags may be defined later. Flags must be stored in ASCII order:
e.g., "2,FRS".




Maildir, COURIER:
-----------------

A new unique filename is created using one of two possible forms: “time.MusecPpid.host”,
or “time.MusecPpid_unique.host”.
..
The name of the file in new should be "time.MusecPpidVdevIino.host,S=cnt", or
"time.MusecPpidVdevIino_unique.host,S=cnt".
[.] "cnt" is the message's size, in bytes.


Maildir, dovecot:
-----------------

E.g., 1276528487.M364837P9451.kurkku,S=1355,W=1394:2,
      1035478339.27041_118.foo.org,S=1000,W=1030:2,S

. There may be more fields before ‘:’ character
..

The standard filename definition is: |<base filename>:2,<flags>|.
Dovecot has extended the |<flags>| field to be |<flags>[,<non-standard
fields>]|. This means that if Dovecot sees a comma in the |<flags>|
field while updating flags in the filename, it doesn’t touch anything
after the comma. However other Maildir MUAs may mess them up, so it’s
still not such a good idea to do that.
[.]
Dovecot supports reading a few fields from the |<base filename>|:

  * |,S=<size>|: |<size>| contains the file size. Getting the size from
    the filename avoids doing a system |stat()| call, which may improve
    the performance. This is especially useful with Quota Backend:
    maildir <https://doc.dovecot.org/2.3/configuration_manual/quota/
    quota_maildir/#quota-backend-maildir>.

  * |,W=<vsize>|: |<vsize>| contains the file’s RFC822.SIZE, i.e., the
    file size with linefeeds being CR+LF characters. If the message was
    stored with CR+LF linefeeds, |<size>| and |<vsize>| are the same.
    Setting this may give a small speedup because now Dovecot doesn’t
    need to calculate the size itself.



 */




/* a_maildir_tbl should be a hash-indexed array of trees! */
/*

FIXME use a cs_dict, store it in

mailbox.
   void *mb_maildir_data;

delete that in maildir_quit()


drop chdir stuff, move all of src/mx/path.c to obs-imap.c
always work on full paths.


get rid of jumps thus?!?!?!

*/

/* Guideline on what to expect as maximum filename (for initial allocations) */
#define a_MAILDIR_XPECT_ENTLEN 64

static char const a_maildir_sds[3][4] = {"cur", "new", "tmp"};
enum a_maildir_sdsn {a_MAILDIR_SDS_CUR, a_MAILDIR_SDS_NEW, a_MAILDIR_SDS_TMP, a__MAILDIR_SDS_MAX};
enum {a_MAILDIR_SDS_LEN = 3};

static struct message **a_maildir_tbl, **a_maildir_tbl_top;
static u32 a_maildir_tbl_prime, a_maildir_tbl_maxdist;

static sigjmp_buf _maildir_jmp; /* TODO drop this mess */
static void __maildircatch(int s);
static void __maildircatch_hold(int s);

/* Remove stale tmp/ garbage */
static void a_maildir_cleantmp(void);

static int a_maildir_setfile1(char const *name, enum fedit_mode fm, int omsgCount);

static int a_maildir_cmp(void const *a, void const *b);

static boole a_maildir_subdir(char const *name, char const *sub, enum fedit_mode fm);

static void a_maildir_append(char const *name, char const *sub, char const *fn);

/* Read one Maildir file into our temporary MBOX/mp */
static boole a_maildir_readin(char const *name, struct message *mp, char **buf, uz *bufsize);

/* Return: whether maildir shall NOT be kept thereafter */
static boole a_maildir_quit(void);

static void a_maildir_move(struct su_timespec const *tsp, struct message *m);

static char *a_maildir_mkname(struct su_timespec const *tsp, enum mflag f, char const *pref);

static boole a_maildir_append1(struct su_timespec const *tsp, char const *name,
		FILE *fp, off_t off1, long size, enum mflag flag, boole realstat);

static boole a_maildir_mkmaildir(char const *name);

static struct message *a_maildir_mdlook(char const *name, struct message *data);

static void a_maildir_mktable(void);

static boole a_maildir_rmsubdir(struct n_string *sp, ZIPENUM(u8,a_maildir_sdsn) sub);

static void
__maildircatch(int s){
	NYD; /* Signal handler */
	siglongjmp(_maildir_jmp, s);
}

static void
__maildircatch_hold(int s){
	NYD; /* Signal handler */
	UNUSED(s);
	/* TODO no STDIO in signal handler, no _() tr's -- pre-translate interrupt
	 * TODO globally; */
	n_err_sighdl(_("\nImportant operation in progress: "
		"interrupt again to forcefully abort\n"));
	safe_signal(SIGINT, &__maildircatch);
}

static void
a_maildir_cleantmp(void){
	struct su_pathinfo pi;
	struct n_string s_b, *s;
	s64 now;
	DIR *dirp;
	struct dirent *dp;
	NYD_IN;

	if((dirp = opendir(a_maildir_sds[a_MAILDIR_SDS_TMP])) == NIL)
		goto jleave;

	/* COURIER: delete any files in there that are at least 36 hours old */
	now = mx_time_now(FAL0)->ts_sec - (36 * su_TIME_HOUR_SECS);

	s = n_string_creat_auto(&s_b); /* XXX only for setfile(); but linepool + owner take/drop?? */
	s = n_string_reserve(s, 64); /* xxx "sufficient" */
	s = n_string_push_buf(s, a_maildir_sds[a_MAILDIR_SDS_TMP], a_MAILDIR_SDS_LEN);
	s = n_string_push_c(s, su_PATH_SEP_C);

	while((dp = readdir(dirp)) != NIL){
		if(dp->d_name[0] == '.')
			continue;

		s = n_string_trunc(s, a_MAILDIR_SDS_LEN + 1);
		s = n_string_push_cp(s, dp->d_name);

		if(su_pathinfo_stat(&pi, n_string_cp(s)) && pi.pi_atime.ts_sec <= now)
			su_path_rm(s->s_dat); /* xxx debug/verbose log? */
	}

	closedir(dirp);

jleave:
	NYD_OU;
}

static int
a_maildir_setfile1(char const *name, enum fedit_mode fm, int omsgCount){
	int i;
	NYD_IN;

/*
FIXME
*/
i = -1;

	if(!(fm & FEDIT_NEWMAIL))
		a_maildir_cleantmp();

	mb.mb_perm = (fm & FEDIT_RDONLY) ? 0 : MB_DELE;

	if(!a_maildir_subdir(name, a_maildir_sds[a_MAILDIR_SDS_CUR], fm) ||
			!a_maildir_subdir(name, a_maildir_sds[a_MAILDIR_SDS_NEW], fm))
		goto jleave;
	/* (Ensure NILified last entry) */
	mx_message_append_nil();

	/* C99 */{
		uz bufsize;
		char *buf;

		mx_fs_linepool_aquire(&buf, &bufsize);
		su_mem_bag_auto_snap_create(su_MEM_BAG_SELF); /* substdate().. */

		for(i = ((fm & FEDIT_NEWMAIL) ? omsgCount : 0); i < msgCount; ++i){
			if(!a_maildir_readin(name, &message[i], &buf, &bufsize)){
				i = -1;
				break;
			}
			su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);
		}

		su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);
		mx_fs_linepool_release(buf, bufsize);
	}
	if(i < 0)
		goto jleave;

#if 1
#if 0
FIXME
#endif
	if(fm & FEDIT_NEWMAIL){
		if(msgCount > omsgCount)
			qsort(&message[omsgCount], msgCount - omsgCount, sizeof *message, &a_maildir_cmp);
	}else if(msgCount > 0)
		qsort(message, msgCount, sizeof *message, &a_maildir_cmp);
#endif

	i = msgCount;
jleave:
	NYD_OU;
	return i;
}

static int
a_maildir_cmp(void const *xa, void const *xb){
	char const *cpa, *cpa_pid, *cpb, *cpb_pid;
	union {struct message const *mp; char const *cp;} a, b;
	s64 at, bt;
	int rv;
	NYD2_IN;

	a.mp = xa;
	b.mp = xb;

	/* We could have parsed the time somewhen in the past, do a quick shot */
	at = S(s64,a.mp->m_time);
	bt = S(s64,b.mp->m_time);
	if(at != 0 && bt != 0 && (at -= bt) != 0)
		goto jret;

	/* Otherwise we need to parse the name */
	a.cp = &a.mp->m_maildir_file[4];
	b.cp = &b.mp->m_maildir_file[4];

	/* Interpret time stored in name, and use it for comparison */
	if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE ||
			*cpa != '.' || a.cp == cpa)
		goto jm1; /* Fishy */
	if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE ||
			*cpb != '.' || b.cp == cpb)
		goto j1; /* Fishy */

	if((at -= bt) != 0)
		goto jret;

	/* If the seconds part does not work, go deeper.
	 * COURIER, as also used by, e.g., Dovecot: sec.MusecPpid.hostname:2,flags.
	 * However, a different name convention exists which uses sec.pid_counter.hostname:2,flags.
	 * First go for usec/counter, then pid.
	 * TODO We do not support the COURIER stated sec.MusecPpidVdevIino[_unique].host,S=cnt */

	/* A: exact "standard"? */
	cpa_pid = NIL;
	a.cp = ++cpa;
	if((rv = *a.cp) == 'M'){
	}
	/* Known compat? */
	else if(su_cs_is_digit(rv)){
		cpa_pid = a.cp++;
		while((rv = *a.cp) != '\0' && rv != '_')
			++a.cp;
		if(rv == '\0')
			goto jm1; /* Fishy */
	}
	/* Compatible to what dovecot does, it surely does not do so for nothing, but no idea, and too stupid to ask */
	else for(;; rv = *++a.cp){
		if(rv == 'M')
			break;
		if(rv == '\0' || rv == '.' || rv == n_MAILDIR_SEPARATOR)
			goto jm1; /* Fishy */
	}
	++a.cp;
	if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
		goto jm1; /* Fishy */

	/* B: as above */
	cpb_pid = NIL;
	b.cp = ++cpb;
	if((rv = *b.cp) == 'M'){
	}else if(su_cs_is_digit(rv)){
		cpb_pid = b.cp++;
		while((rv = *b.cp) != '\0' && rv != '_')
			++b.cp;
		if(rv == '\0')
			goto j1;
	}else for(;; rv = *++b.cp){
		if(rv == 'M')
			break;
		if(rv == '\0' || rv == '.' || rv == n_MAILDIR_SEPARATOR)
			goto jm1;
	}
	++b.cp;
	if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
		goto j1;

	if((at -= bt) != 0)
		goto jret;

	/* So this gets hairy: sort by PID, then hostname */
	if(cpa_pid != NIL){
		a.cp = cpa_pid;
		xa = cpa;
	}else{
		a.cp = cpa;
		if(*a.cp++ != 'P')
			goto jm1; /* Fishy */
	}
	if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
		goto jm1; /* Fishy */

	if(cpb_pid != NIL){
		b.cp = cpb_pid;
		xb = cpb;
	}else{
		b.cp = cpb;
		if(*b.cp++ != 'P')
			goto j1; /* Fishy */
	}
	if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
		goto jm1; /* Fishy */

	if((at -= bt) != 0)
		goto jret;

	/* Hostname */
	a.cp = (cpa_pid != NIL) ? xa : cpa;
	b.cp = (cpb_pid != NIL) ? xb : cpb;
	for(;; ++a.cp, ++b.cp){
		char ac, bc;

		ac = *a.cp;
		at = (ac != '\0' && ac != n_MAILDIR_SEPARATOR);
		bc = *b.cp;
		bt = (bc != '\0' && bc != n_MAILDIR_SEPARATOR);
		if((at -= bt) != 0)
			break;
		at = ac;
		if((at -= bc) != 0)
			break;
		if(ac == '\0')
			break;
	}

jret:
	rv = (at == 0 ? 0 : (at < 0 ? -1 : 1));
jleave:
	NYD2_OU;
	return rv;
jm1:
	rv = -1;
	goto jleave;
j1:
	rv = 1;
	goto jleave;
}

static boole
a_maildir_subdir(char const *name, char const *sub, enum fedit_mode fm){
	DIR *dirp;
	struct dirent *dp;
	boole rv;
	NYD_IN;

	rv = FAL0;

	if((dirp = opendir(sub)) == NIL){
		n_err(_("Cannot open directory %s\n"), n_shexp_quote_cp(savecatsep(name, '/', sub), FAL0));
		goto jleave;
	}

	if(access(sub, W_OK) == -1)
		mb.mb_perm = 0;

	while((dp = readdir(dirp)) != NIL){
		/*if(dp->d_name[0] == '.' && (dp->d_name[1] == '\0' || (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
		 *	continue;*/
		if(dp->d_name[0] == '.')
			continue;
		if(!(fm & FEDIT_NEWMAIL) || a_maildir_mdlook(dp->d_name, NIL) == NIL)
			a_maildir_append(name, sub, dp->d_name);
	}
	closedir(dirp);

	rv = TRU1;
jleave:
	NYD_OU;
	return rv;
}

static void
a_maildir_append(char const *name, char const *sub, char const *fn){
	struct message *mp;
	char const *cp, *xp;
	s64 t;
	BITENUM(u32,mflag) f;
	NYD_IN;
	ASSERT(sub != NIL && fn != NIL);
	UNUSED(name);

	f = MVALID | MNOFROM | MNEWEST;
	t = 0;

	if(!su_cs_cmp(sub, a_maildir_sds[a_MAILDIR_SDS_NEW]))
		f |= MNEW;

	(void)/*TODO*/su_idec_s64_cp(&t, fn, 10, &xp);

	if((cp = su_cs_rfind_c(xp, ',')) != NIL && PCMP(cp, >, xp + 2) &&
			cp[-1] == '2' && cp[-2] == n_MAILDIR_SEPARATOR){
		while(*++cp != '\0'){
			switch(*cp){
			case 'F': f |= MFLAGGED; break;
			case 'R': f |= MANSWERED; break;
			case 'S': f |= MREAD; break;
			case 'T': f |= MDELETED; break;
			case 'D': f |= MDRAFT; break;
			}
		}
	}

	/* Ensure room + init */
	mx_message_append_nil();

	mp = &message[msgCount++];
	/* C99 */{
		char *tmp;
		uz i, j;

		i = su_cs_len(fn) +1;
		j = su_cs_len(sub);
		mp->m_maildir_file = tmp = su_ALLOC(j + 1 + i);
		su_mem_copy(tmp, sub, j);
		tmp[j++] = su_PATH_SEP_C;
		su_mem_copy(&tmp[j], fn, i);
	}
	mp->m_time = t;
	mp->m_flag = f;
	mp->m_maildir_hash = su_cs_hash(fn);

	NYD_OU;
}

static boole
a_maildir_readin(char const *name, struct message *mp, char **buf, uz *bufsize){
	long size, lines;
	off_t offset;
	char const *emsg;
	FILE *fp;
	uz cnt, buflen;
	boole rv, b;
	NYD_IN;

	rv = FAL0;

	if((fp = mx_fs_open(mp->m_maildir_file, mx_FS_O_RDONLY)) == NIL){
		emsg = N_("Cannot read %s for message %lu\n");
		goto jerr;
	}
	emsg = N_("I/O error reading %s for message %lu\n");

	offset = ftell(mb.mb_otf);
	cnt = fsize(fp);

	b = FAL0;
	size = lines = 0;
	while(fgetline(buf, bufsize, &cnt, &buflen, fp, TRU1) != NIL){
		/* TODO Since we simply copy over data without doing any transfer
		 * TODO encoding reclassification/adjustment we *have* to perform
		 * TODO RFC 4155 compliant From_ quoting here
		 * (TODO Note COURIER forbids *any* quoted ^From_, but how could that possibly work?) */
		if(b && is_head(*buf, buflen, FAL0)){
			if(putc('>', mb.mb_otf) == EOF)
				goto jerr;
			++size;
		}
		if(fwrite(*buf, 1, buflen, mb.mb_otf) != buflen)
			goto jerr;
		size += buflen;
		b = (**buf == '\n');
		++lines;
	}
	if(ferror(fp))
		goto jerr;

	if(!b){
		/* TODO we need \n\n for mbox format.
		 * TODO That is to say we do it wrong here in order to get it right
		 * TODO when send.c stuff or with MBOX handling, even though THIS
		 * TODO line is solely a property of the MBOX database format! */
		if(putc('\n', mb.mb_otf) == EOF)
			goto jerr;
		++lines;
		++size;
	}

	if(fflush(mb.mb_otf) == EOF)
		goto jerr;

	mp->m_content_info = CI_HAVE_MASK /* FIXME CI_NOTHING*/;
	mp->m_size = mp->m_xsize = size;
	mp->m_lines = mp->m_xlines = lines;
	mp->m_block = mailx_blockof(offset);
	mp->m_offset = mailx_offsetof(offset);
	substdate(mp);

	rv = TRU1;
jleave:
	if(fp != NIL)
		mx_fs_close(fp);

	NYD_OU;
	return rv;
jerr:
	n_err(V_(emsg), n_shexp_quote_cp(savecatsep(name, '/', mp->m_maildir_file), FAL0),
		S(ul,P2UZ(mp - message + 1)));
	goto jleave;
}

static boole
a_maildir_quit(void){
	struct message *mp;
	uz gotcha, held, modflags;
	struct su_timespec const *tsp;
	boole rv;
	NYD_IN;

	rv = FAL0;

	if(!mx_quit_automove_mbox(TRU1) || mb.mb_perm == 0)
		goto jleave;

	tsp = mx_time_now(TRU1); /* TODO FAL0, eventloop update! */

	su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);
	gotcha = held = modflags = 0;
	for(mp = message; mp < &message[msgCount]; ++mp){
		if((n_pstate & n_PS_EDIT) ? ((mp->m_flag & MDELETED) != 0)
				: !((mp->m_flag & MPRESERVE) || !(mp->m_flag & MTOUCH))){
			if(su_path_rm(mp->m_maildir_file))
				++gotcha;
			else
				n_err(_("Cannot delete file %s for message %lu\n"),
					n_shexp_quote_cp(savecatsep(mailname, '/', mp->m_maildir_file), FAL0),
					S(ul,P2UZ(mp - message + 1)));
		}else{
			++held;
			if((mp->m_flag & (MREAD | MSTATUS)) == (MREAD | MSTATUS) ||
					(mp->m_flag & (MNEW | MBOXED | MSAVED | MSTATUS | MFLAG |
						MUNFLAG | MANSWER | MUNANSWER | MDRAFT | MUNDRAFT))){
				a_maildir_move(tsp, mp);
				++modflags;
			}
		}
		su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);
	}
	su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);

	if((gotcha || modflags) && (n_pstate & n_PS_EDIT)){
		fprintf(n_stdout, "%s %s\n",
			n_shexp_quote_cp(displayname, FAL0),
			((ok_blook(bsdcompat) || ok_blook(bsdmsgs)) ? _("complete") : _("updated")));
	}else if(held > 0 && !(n_pstate & n_PS_EDIT)){
		fprintf(n_stdout, _("Held %" PRIuZ " %s in %s\n"),
			held, (held == 1 ? _("message") : _("messages")), displayname);
	}else
		rv = (held == 0 && !ok_blook(keep) && (!(n_pstate & n_PS_EDIT) || ok_blook(posix)));

	fflush(n_stdout);

jleave:
	for(mp = message; mp < &message[msgCount]; ++mp)
		su_FREE(UNCONST(char*,mp->m_maildir_file));
	if(mb.mb_maildir_data != NIL){
		struct su_cs_dict *csdp;

		csdp = S(struct su_cs_dict*,mb.mb_maildir_data);
		mb.mb_maildir_data = NIL;
		su_cs_dict_gut(csdp);
		su_FREE(csdp);
	}

	NYD_OU;
	return rv;
}

static void
a_maildir_move(struct su_timespec const *tsp, struct message *m){
	char *fn, *newfn;
	NYD_IN;

	fn = a_maildir_mkname(tsp, m->m_flag, &m->m_maildir_file[4]);
	newfn = savecat("cur/", fn);/* FIXME */
	if(!su_cs_cmp(m->m_maildir_file, newfn))
		goto jleave;

	if(!su_path_link(newfn, m->m_maildir_file)){
		n_err(_("Cannot link %s to %s: message %lu not touched\n"),
			n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0),
			n_shexp_quote_cp(savecatsep(mailname, '/', newfn), FAL0),
			S(ul,P2UZ(m - message + 1)));
		goto jleave;
	}

	if(!su_path_rm(m->m_maildir_file))
		n_err(_("Cannot remove %s\n"), n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0));

jleave:
	NYD_OU;
}

static char *
a_maildir_mkname(struct su_timespec const *tsp, enum mflag f, char const *pref){
	static char *node;
	static struct su_timespec ts;

	char *cp;
	int size, n, i;
	NYD_IN;

	if(pref == NIL){
		s64 s;

		if(n_pid == 0)
			n_pid = getpid();

		if(node == NIL){
			cp = n_nodename(FAL0);
			n = size = 0;
			do {
				if(UCMP(32, n, <, size + 8))
					node = su_REALLOC(node, size += 20);
				switch(*cp){
				case '/':
					node[n++] = '\\', node[n++] = '0',
					node[n++] = '5', node[n++] = '7';
					break;
				case ':':
					node[n++] = '\\', node[n++] = '0',
					node[n++] = '7', node[n++] = '2';
					break;
				default:
					node[n++] = *cp;
				}
			}while(*cp++ != '\0');
		}

		/* COURIER spec uses microseconds, not nanoseconds */
		/* XXX timespec calculation! */
		if((s = tsp->ts_sec) > ts.ts_sec){
			ts.ts_sec = s;
			ts.ts_nano = tsp->ts_nano / (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MICROS);
		}else{
			s = tsp->ts_nano / (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MICROS);
			if(s <= ts.ts_nano)
				s = ts.ts_nano + 1;
			if(s < su_TIMESPEC_SEC_NANOS)
				ts.ts_nano = s;
			else{
				++ts.ts_sec;
				ts.ts_nano = 0;
			}
		}

		/* Create a name according to Courier spec */
		size = 60 + su_cs_len(node);
		cp = su_AUTO_ALLOC(size);
		n = snprintf(cp, size, "%" PRId64 ".M%" PRIdZ "P%ld.%s:2,", ts.ts_sec, ts.ts_nano, (long)n_pid, node);
	} else {
		size = (n = su_cs_len(pref)) + 13;
		cp = su_AUTO_ALLOC(size);
		su_mem_copy(cp, pref, n +1);
		for(i = n; i > 3; --i)
			if(cp[i - 1] == ',' && cp[i - 2] == '2' && cp[i - 3] == n_MAILDIR_SEPARATOR){
				n = i;
				break;
			}
		if(i <= 3){
			su_mem_copy(cp + n, ":2,", 3 +1);
			n += 3;
		}
	}

	if(n < size - 7){
		if(f & MDRAFTED)
			cp[n++] = 'D';
		if(f & MFLAGGED)
			cp[n++] = 'F';
		if(f & MANSWERED)
			cp[n++] = 'R';
		if(f & MREAD)
			cp[n++] = 'S';
		if(f & MDELETED)
			cp[n++] = 'T';
		cp[n] = '\0';
	}

	NYD_OU;
	return cp;
}

static boole
a_maildir_append1(struct su_timespec const *tsp, char const *name, FILE *fp, off_t off1, long size,
		enum mflag flag, boole realstat){
	FILE *op;
	char buf[su_PAGE_SIZE], *tfn, *nfn, *fn, *cp;
	uz nlen, flen;
	boole rv;
	NYD_IN;
	ASSERT_NYD_EXEC(size > 0, rv = FAL0);

	rv = FAL0;
	nlen = su_cs_len(name);
	tfn = nfn = NIL;

	/* Create a unique temporary file */
	/* C99 */{
		int cnt;

		for(cnt = mx_FS_TMP_OPEN_TRIES;;){
			s32 e;

			fn = a_maildir_mkname(tsp, flag, NIL); /* TODO no AUTO: LOFI; and all around */
			flen = su_cs_len(fn);
			cp = tfn = su_LOFI_ALLOC(nlen + flen + 1 + a_MAILDIR_SDS_LEN + 1 +1);
			cp = su_cs_pcopy(cp, name);
			*cp++ = su_PATH_SEP_C;
			cp = su_cs_pcopy(cp, a_maildir_sds[a_MAILDIR_SDS_TMP]);
			*cp++ = su_PATH_SEP_C;
			cp = su_cs_pcopy(cp, fn);
jeintr:
			op = mx_fs_open(tfn, (mx_FS_O_WRONLY | mx_FS_O_CREATE | mx_FS_O_EXCL));
			if(op != NIL)
				break;

			e = su_err();
			if(e == su_ERR_INTR)
				goto jeintr;

			su_LOFI_FREE(tfn);
			tfn = NIL;

			--cnt;
			if(e == su_ERR_ACCES || e == su_ERR_ROFS || e == su_ERR_NAMETOOLONG)
				cnt = 0;

			if(cnt <= mx_FS_TMP_OPEN_TRIES / 2){
				char const *edsc, *emsg;

				edsc = su_err_doc(e);
				emsg = (cnt == 0) ? N_("giving up") : N_("sleeping to reduce system pressure");

				n_err(_("Maildir: cannot create a file in %s/%s: %s: %s\n"),
					n_shexp_quote_cp(name, FAL0), a_maildir_sds[a_MAILDIR_SDS_TMP], V_(emsg), edsc);

				if(cnt == 0)
					goto jleave;
				su_time_msleep(250, FAL0);
			}
		}
	}

	if(fseek(fp, off1, SEEK_SET) != -1) do{
		uz i;

		i = UCMP(z, size, >, sizeof buf) ? sizeof buf : S(uz,size);
		if(fread(buf, 1, i, fp) != i || fwrite(buf, 1, i, op) != i)
			break;
		size -= i;
	}while(size > 0);

	mx_fs_close(op);

	if(size != 0){
		n_err(_("Maildir: error writing to %s\n"), n_shexp_quote_cp(tfn, FAL0));
		goto jleave;
	}

	cp = nfn = su_LOFI_ALLOC(nlen + flen + 5 +1);
	cp = su_cs_pcopy(cp, name);
	*cp++ = su_PATH_SEP_C;
	cp = su_cs_pcopy(cp, a_maildir_sds[(!realstat || (flag & MNEW)) ? a_MAILDIR_SDS_NEW : a_MAILDIR_SDS_CUR]);
	*cp++ = su_PATH_SEP_C;
	cp = su_cs_pcopy(cp, fn);

	if(!su_path_link(nfn, tfn)){
		n_err(_("Maildir: cannot link %s to %s\n"),
			n_shexp_quote_cp(tfn, FAL0), n_shexp_quote_cp(nfn, FAL0));
		goto jleave;
	}

	rv = TRU1;
jleave:
	if(nfn != NIL)
		su_LOFI_FREE(nfn);
	if(tfn != NIL){
		if(!su_path_rm(tfn))
			n_err(_("Maildir: cannot remove %s\n"), n_shexp_quote_cp(tfn, FAL0));
		su_LOFI_FREE(tfn);
	}

	NYD_OU;
	return rv;
}

static boole
a_maildir_mkmaildir(char const *name){
	char *np, *ep;
	uz i;
	NYD_IN;

	i = su_cs_len(name);
	np = su_LOFI_ALLOC(i + 1 + a_MAILDIR_SDS_LEN +1);

	su_mem_copy(np, name, i +1);
	ep = &np[i];

	for(i = a__MAILDIR_SDS_MAX;;){
		if(LIKELY(su_path_mkdir(np, 0777, FAL0, su_STATE_ERR_NOPASS))){
			if(i-- == 0)
				break;
			ep[0] = su_PATH_SEP_C;
			su_cs_pcopy(&ep[1], a_maildir_sds[i]);

		}else{
			s32 e;

			e = su_err();
			n_err(_("Cannot create directory %s: %s\n"), n_shexp_quote_cp(np, FAL0), su_err_doc(e));

			/* (xxx Will try to remove upper layer even if lower cannot be cleaned up cpl) */
			while(*ep != '\0'){
				if(++i < a__MAILDIR_SDS_MAX){
					ep[0] = su_PATH_SEP_C;
					su_cs_pcopy(&ep[1], a_maildir_sds[i]);
				}else
					*ep = '\0';
				if(!su_path_rmdir(np)){
					e = su_err();
					n_err(_("Unable to remove stale directory %s: %s\n"),
						n_shexp_quote_cp(np, FAL0), su_err_doc(e));
				}
			}

			ep = NIL;
			break;
		}
	}

	su_LOFI_FREE(np);

	NYD_OU;
	return (ep != NIL);
}

static struct message *
a_maildir_mdlook(char const *name, struct message *data){
	struct message **mpp, *mp;
	u32 h, i;
	NYD_IN;

	if(data != NIL)
		i = data->m_maildir_hash;
	else
		i = su_cs_hash(name);
	h = i;
	mpp = &a_maildir_tbl[i %= a_maildir_tbl_prime];

	for(i = 0;;){
		if((mp = *mpp) == NIL){
			if(UNLIKELY(data != NIL)){
				*mpp = mp = data;
				if(i > a_maildir_tbl_maxdist)
					a_maildir_tbl_maxdist = i;
			}
			break;
		}else if(mp->m_maildir_hash == h && !su_cs_cmp(&mp->m_maildir_file[4], name))
			break;

		if(UNLIKELY(mpp++ == a_maildir_tbl_top))
			mpp = a_maildir_tbl;
		if(++i > a_maildir_tbl_maxdist && UNLIKELY(data == NIL)){
			mp = NIL;
			break;
		}
	}

	NYD_OU;
	return mp;
}

static void
a_maildir_mktable(void){
	struct message *mp;
	uz i;
	NYD_IN;

	i = a_maildir_tbl_prime = msgCount;
	i <<= 1;
	do
		a_maildir_tbl_prime = su_prime_lookup_next(a_maildir_tbl_prime);
	while(a_maildir_tbl_prime < i);

	a_maildir_tbl = su_CALLOC_N(sizeof *a_maildir_tbl, a_maildir_tbl_prime);
	a_maildir_tbl_top = &a_maildir_tbl[a_maildir_tbl_prime - 1];
	a_maildir_tbl_maxdist = 0;

	for(mp = message, i = msgCount; i-- != 0; ++mp)
		a_maildir_mdlook(&mp->m_maildir_file[4], mp);

	NYD_OU;
}

static boole
a_maildir_rmsubdir(struct n_string *sp, ZIPENUM(u8,a_maildir_sdsn) sub){
	struct dirent *dp;
	DIR *dirp;
	NYD_IN;

	sp = n_string_push_buf(sp, a_maildir_sds[sub], a_MAILDIR_SDS_LEN);

	if((dirp = opendir(n_string_cp_const(sp))) == NIL)
		goto jerrno;

	sp = n_string_push_c(sp, su_PATH_SEP_C);

	while((dp = readdir(dirp)) != NIL){
		uz dl;

		/*if(dp->d_name[0] == '.' && (dp->d_name[1] == '\0' || (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
		 *	continue;*/
		if(dp->d_name[0] == '.')
			continue;

		dl = su_cs_len(dp->d_name);
		sp = n_string_push_buf(sp, dp->d_name, dl);

		if(!su_path_rm(n_string_cp_const(sp)))
			goto jerr;

		sp = n_string_trunc(sp, sp->s_len - dl);
	}

	sp = n_string_trunc(sp, sp->s_len - 1);

	if(!su_path_rmdir(n_string_cp_const(sp)))
		goto jerr;

jleave:
	if(dirp != NIL)
		closedir(dirp);

	NYD_OU;
	return (sp != NIL);
jerrno:
	su_err_by_errno();
jerr:
/*
FIXME
*/
	n_perr(n_string_cp_const(sp), 0);
	sp = NIL;
	goto jleave;
}

int
maildir_setfile(char const *who, char const * volatile name, enum fedit_mode fm){
	n_sighdl_t volatile saveint;
	struct cw cw;
	char const *emsg;
	int omsgCount;
	int volatile i = -1;
	NYD_IN;

	omsgCount = msgCount;
	if(cwget(&cw) == STOP){
		n_alert(_("Cannot open current directory"));
		goto jleave;
	}

	if(!(fm & FEDIT_NEWMAIL) && !quit(FAL0))
		goto jleave;

	saveint = safe_signal(SIGINT, SIG_IGN);

	if(!(fm & FEDIT_NEWMAIL)){
		if(fm & FEDIT_SYSBOX)
			n_pstate &= ~n_PS_EDIT;
		else
			n_pstate |= n_PS_EDIT;
		if(mb.mb_itf){
			fclose(mb.mb_itf);
			mb.mb_itf = NIL;
		}
		if(mb.mb_otf){
			fclose(mb.mb_otf);
			mb.mb_otf = NIL;
		}
		n_initbox(name, ((fm & FEDIT_RDONLY) != 0));
		mb.mb_type = MB_MAILDIR;
		mb.mb_maildir_data = NIL;
	}

	while(!su_path_chdir(name)){
		if(su_err() == su_ERR_NOENT && !(fm & FEDIT_RDONLY) &&
				mx_tty_yesorno(_("Maildir non-existent, create it"), FAL0)){
			if(!a_maildir_mkmaildir(name)){
				emsg = N_("Cannot create maildir://%s\n");
				goto jerr;
			}
			continue;
		}
		emsg = N_("Cannot enter maildir://%s\n");
jerr:
		n_err(V_(emsg), n_shexp_quote_cp(name, FAL0));
		UNUSED(emsg);
		mb.mb_type = MB_VOID;
		*mailname = '\0';
		msgCount = 0;
		cwrelse(&cw);
		safe_signal(SIGINT, saveint);
		goto jleave;
	}

	a_maildir_tbl = NIL;
	if(sigsetjmp(_maildir_jmp, 1) == 0){
		if(fm & FEDIT_NEWMAIL)
			a_maildir_mktable();
		if(saveint != SIG_IGN)
			safe_signal(SIGINT, &__maildircatch);
		if(a_maildir_setfile1(name, fm, omsgCount) < 0){
			if((fm & FEDIT_NEWMAIL) && a_maildir_tbl != NIL)
				su_FREE(a_maildir_tbl);
			emsg = N_("Cannot setup maildir://%s\n");
			goto jerr;
		}
	}
	if((fm & FEDIT_NEWMAIL) && a_maildir_tbl != NIL)
		su_FREE(a_maildir_tbl);

	safe_signal(SIGINT, saveint);

	if(cwret(&cw) == STOP)
		n_panic(_("Cannot change back to current directory"));
	cwrelse(&cw);

	n_folder_setmsize(n_msgno);
	if((fm & FEDIT_NEWMAIL) && mb.mb_sorted && msgCount > omsgCount){
		mb.mb_threaded = 0;
		c_sort((void*)-1);
	}

	if(!(fm & FEDIT_NEWMAIL)){
		n_pstate &= ~n_PS_SAW_COMMAND;
		n_pstate |= n_PS_SETFILE_OPENED;
	}

	if((n_poption & n_PO_EXISTONLY) && !(n_poption & n_PO_HEADERLIST)){
		i = (msgCount == 0);
		goto jleave;
	}

	if(!(fm & FEDIT_NEWMAIL) && (fm & FEDIT_SYSBOX) && msgCount == 0){
		if(mb.mb_type == MB_MAILDIR /* XXX ?? */ && !ok_blook(emptystart))
			n_err(_("No mail for %s at %s\n"), who, n_shexp_quote_cp(name, FAL0));
		su_err_set(su_ERR_NODATA);
		i = 1;
		goto jleave;
	}

	if((fm & FEDIT_NEWMAIL) && msgCount > omsgCount)
		n_folder_newmailinfo(omsgCount);

	i = 0;
jleave:
	NYD_OU;
	return i;
}

boole
maildir_quit(boole hold_sigs_on){
	n_sighdl_t saveint;
	struct cw cw;
	boole volatile remove;
	boole rv;
	NYD_IN;

	if(hold_sigs_on)
		rele_sigs();

	rv = FAL0;

	if(cwget(&cw) == STOP){
		n_alert(_("Cannot open current directory"));
		goto jleave;
	}

	saveint = safe_signal(SIGINT, SIG_IGN);

	if(chdir(mailname) == -1){
		n_err(_("Cannot change directory to %s\n"), n_shexp_quote_cp(mailname, FAL0));
		cwrelse(&cw);
		safe_signal(SIGINT, saveint);
		goto jleave;
	}

	remove = FAL0;
	if(sigsetjmp(_maildir_jmp, 1) == 0){
		if(saveint != SIG_IGN)
			safe_signal(SIGINT, &__maildircatch_hold);

		remove = a_maildir_quit();
	}

	safe_signal(SIGINT, saveint);

	if(cwret(&cw) == STOP)
		n_panic(_("Cannot change back to current directory"));
	cwrelse(&cw);

	if(remove)
		maildir_remove(mailname);

	rv = TRU1;
jleave:
	if(hold_sigs_on)
		hold_sigs();

	NYD_OU;
	return rv;
}

boole
maildir_append(char const *name, FILE *fp, s64 offset, boole realstat){
	enum {a_NONE = 0, a_INHEAD = 1u<<0, a_NLSEP = 1u<<1} state;
	int flag;
	struct su_timespec const *tsp;
	char *buf, *bp, *lp;
	uz bufsize, buflen, cnt;
	long size;
	off_t off1, offs;
	NYD_IN;

	off1 = -1;

	if(!a_maildir_mkmaildir(name))
		goto jleave;

	buflen = 0;
	cnt = fsize(fp);
	offs = offset /* BSD will move due to O_APPEND! ftell(fp) */;
	size = 0;
	tsp = mx_time_now(TRU1); /* TODO -> eventloop */

	mx_fs_linepool_aquire(&buf, &bufsize);
	su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);

	for(flag = MNEW, state = a_NLSEP;;){
		bp = fgetline(&buf, &bufsize, &cnt, &buflen, fp, TRU1);

		if(bp == NIL || ((state & (a_INHEAD | a_NLSEP)) == a_NLSEP && is_head(buf, buflen, FAL0))){
			if(off1 != S(off_t,-1) && size > 0){
				if(!a_maildir_append1(tsp, name, fp, off1, size, flag, realstat))
					goto jfree;

				if(fseek(fp, offs + buflen, SEEK_SET) == -1)
					goto jfree;

				su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);
			}

			off1 = offs + buflen;
			size = 0;
			state = a_INHEAD;
			flag = MNEW;

			if(bp == NIL){
				if(ferror(fp))
					goto jfree;
				break;
			}
		}else
			size += buflen;
		offs += buflen;

		state &= ~a_NLSEP;
		if(buf[0] == '\n'){
			state &= ~a_INHEAD;
			state |= a_NLSEP;
		}else if(state & a_INHEAD){
			if(!su_cs_cmp_case_n(buf, "status", 6)){
				lp = buf + 6;
				while(su_cs_is_white(*lp))
					++lp;
				if(*lp == ':')
					while(*++lp != '\0')
						switch(*lp){
						case 'R': flag |= MREAD; break;
						case 'O': flag &= ~MNEW; break;
						}
			}else if(!su_cs_cmp_case_n(buf, "x-status", 8)){
				lp = buf + 8;
				while(su_cs_is_white(*lp))
					++lp;
				if(*lp == ':'){
					while(*++lp != '\0')
						switch(*lp){
						case 'F': flag |= MFLAGGED; break;
						case 'A': flag |= MANSWERED; break;
						case 'T': flag |= MDRAFTED; break;
						}
				}
			}
		}
	}

	fp = NIL;
jfree:
	su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);
	mx_fs_linepool_release(buf, bufsize);

jleave:
	NYD_OU;
	return (fp == NIL);
}

boole
maildir_remove(char const *name){
	struct n_string s_b, *sp;
	ZIPENUM(u8,a_maildir_sdsn) sdsn;
	uz sl;
	NYD_IN;
	ASSERT(name != NIL);

	sp = n_string_creat_auto(&s_b);
	sl = su_cs_len(name);
	sp = n_string_book(sp, sl + 1 + a_MAILDIR_SDS_LEN + 1 + a_MAILDIR_XPECT_ENTLEN /*+1*/);
	sp = n_string_push_buf(sp, name, sl);
	sp = n_string_push_c(sp, su_PATH_SEP_C);
	sl = sp->s_len;

	sdsn = 0;
	do if(!a_maildir_rmsubdir(n_string_trunc(sp, sl), sdsn))
		goto jleave;
	while(++sdsn < a__MAILDIR_SDS_MAX);

	if(!su_path_rmdir(name)){
		n_perr(name, 0);
		goto jleave;
	}

	name = NIL;
jleave:
	/*n_string_gut(sp);*/

	NYD_OU;
	return (name  == NIL);
}

boole
mx_maildir_lazy_load_header(struct mailbox *mbp, u32 lo, u32 hi){
	boole rv;
	NYD_IN;
	ASSERT(lo >= 1 && UCMP(32, hi, <=, n_msgno));
UNUSED(mbp);

rv = TRU1;
#if 0
FIXME
for(i = bot; i < topp; i++) {
if ((message[i-1].m_content_info & CI_HAVE_HEADER) ||
    getcache(&mb, &message[i-1], NEED_HEADER) == OKAY)
 bot = i+1;
else
 break;
}

	 msgCount);
#endif
	 NYD_OU;
	 return rv;
}

boole
mx_maildir_msg_lazy_load(struct mailbox *mbp, struct message *mp, enum needspec ns){
	boole rv;
	NYD_IN;

/*
FIXME
*/
UNUSED(mbp);
UNUSED(mp);
UNUSED(ns);
rv = TRU1;

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_MAILDIR */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MAILDIR
/* s-itt-mode */
