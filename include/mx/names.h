/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Names (read: address(-list)?e?s and msg-id's).
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_NAMES_H
#define mx_NAMES_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_name;

enum mx_expand_addr_flags{
	mx_EAF_NONE = 0, /* -> EAF_NOFILE | EAF_NOPIPE */
	mx_EAF_RESTRICT = 1u<<0, /* "restrict" (do unless interactive / -[~#]) */
	mx_EAF_FAIL = 1u<<1, /* "fail" */
	mx_EAF_FAILINVADDR = 1u<<2, /* "failinvaddr" */
	mx_EAF_DOMAINCHECK = 1u<<3, /* "domaincheck" <-> *expandaddr-domaincheck* */
	mx_EAF_NAMETOADDR = 1u<<4, /* "nametoaddr": expand valid name to NAME@HOST */
	mx_EAF_SHEXP_PARSE = 1u<<5, /* shexp_parse() the address first is allowed */
	/* Bits reused by enum expand_addr_check_mode! */
	mx_EAF_FCC = 1u<<8, /* +"fcc" umbrella */
	mx_EAF_FILE = 1u<<9, /* +"file" targets */
	mx_EAF_PIPE = 1u<<10, /* +"pipe" command pipe targets */
	mx_EAF_NAME = 1u<<11, /* +"name"s (non-address) names / MTA aliases */
	mx_EAF_ADDR = 1u<<12, /* +"addr" network address (contain "@") */

	mx_EAF_TARGET_MASK = mx_EAF_FCC | mx_EAF_FILE | mx_EAF_PIPE | mx_EAF_NAME | mx_EAF_ADDR,
	mx_EAF_RESTRICT_TARGETS = mx_EAF_NAME | mx_EAF_ADDR /* (default set if not set) */
	/* TODO HACK!  In pre-v15 we have a control flow problem (it is a general
	 * TODO design problem): if n_collect() calls makeheader(), e.g., for -t or
	 * TODO because of ~e diting, then that will checkaddr() and that will
	 * TODO remove invalid headers.  However, this code path does not know
	 * TODO about keeping track of senderrors unless a pointer has been passed,
	 * TODO but which it doesn't for ~e, and shall not, too.  Thus, invalid
	 * TODO addresses may be automatically removed, silently, and no one will
	 * TODO ever know, in particular not regarding "failinvaddr".
	 * TODO The hacky solution is this bit -- which can ONLY be used for fields
	 * TODO which will be subject to namelist_vaporise_head() later on!! --,
	 * TODO if it is set (by n_header_extract()) then checkaddr() will NOT strip
	 * TODO invalid headers off IF it deals with a NULL senderror pointer */
	,mx_EAF_MAYKEEP = 1u<<15
};

enum mx_expand_addr_check_mode{
	mx_EACM_NONE = 0u, /* Do not care about *expandaddr* */
	mx_EACM_NORMAL = 1u<<0, /* Use our normal *expandaddr* checking */
	mx_EACM_STRICT = 1u<<1, /* Never allow any file or pipe addressee */
	mx_EACM_MODE_MASK = 0x3u, /* _NORMAL and _STRICT are mutual! */

	mx_EACM_NOLOG = 1u<<2, /* Do not log check errors */

	/* Some special overwrites of EAF_TARGETs.
	 * May NOT clash with EAF_* bits which may be ORd to these here! */

	mx_EACM_NONAME = 1u<<16,
	mx_EACM_NONAME_OR_FAIL = 1u<<17,
	mx_EACM_DOMAINCHECK = 1u<<18 /* Honour it! */
};

enum mx_name_flags{
	mx_NAME_SKINNED = 1u<<0, /* Has been *fullnames* skinned (plain address) */
	mx_NAME_IDNA = 1u<<1, /* IDNA has been applied */
	mx_NAME_NAME_SALLOC = 1u<<2, /* .n_name in detached memory */

	mx_NAME_ADDRSPEC_ISFILE = 1u<<3, /* ..is a file path */
	mx_NAME_ADDRSPEC_ISPIPE = 1u<<4, /* ..is a command for piping */
	mx_NAME_ADDRSPEC_ISFILEORPIPE = mx_NAME_ADDRSPEC_ISFILE | mx_NAME_ADDRSPEC_ISPIPE,
	mx_NAME_ADDRSPEC_ISNAME = 1u<<5, /* ..is an alias name */
	mx_NAME_ADDRSPEC_ISADDR = 1u<<6, /* ..is a mail network address.. */
	mx_NAME_ADDRSPEC_ISMASK = su_BITENUM_MASK(3,6),
	mx_NAME_ADDRSPEC_WITHOUT_DOMAIN = 1u<<7, /* ISADDR: without domain name */

	/* Bits not values for easy & testing */
	mx_NAME_ADDRSPEC_ERR_EMPTY = 1u<<9, /* An empty string (or NIL) */
	mx_NAME_ADDRSPEC_ERR_ATSEQ = 1u<<10, /* Weird @ sequence */
	mx_NAME_ADDRSPEC_ERR_CHAR = 1u<<11, /* Invalid character */
	mx_NAME_ADDRSPEC_ERR_IDNA = 1u<<12, /* IDNA conversion failed */
	mx_NAME_ADDRSPEC_ERR_NAME = 1u<<13, /* Alias with invalid content */
	mx_NAME_ADDRSPEC_INVALID = mx_NAME_ADDRSPEC_ERR_EMPTY | mx_NAME_ADDRSPEC_ERR_ATSEQ |
			mx_NAME_ADDRSPEC_ERR_CHAR | mx_NAME_ADDRSPEC_ERR_IDNA | mx_NAME_ADDRSPEC_ERR_NAME,

	/* Error storage (we must fit in 31-bit!) */
	mx__NAME_SHIFTWC = 14,
	mx__NAME_MAXWC = 0x1FFFF,
	mx__NAME_MASKWC = mx__NAME_MAXWC << mx__NAME_SHIFTWC
	/* Bit 31 (32) == S32_MIN temporarily used */
};

struct mx_name{
	struct mx_name *n_flink;
	struct mx_name *n_blink;
	BITENUM(u32,gfield) n_type; /* Header field this comes from */
	u32 n_flags; /* enum mx_name_flags */
	char *n_name;
	char *n_fullname; /* .n_name, plus +comments, etc */
	char *n_fullextra; /* fullname without address */
};

/* `addrcodec' */
EXPORT int c_addrcodec(void *vp);

/* */

/* In the !_ERR_EMPTY case, the failing character can be queried */
INLINE s32 mx_name_flags_get_err_wc(u32 flags){
	return (((flags & mx__NAME_MASKWC) >> mx__NAME_SHIFTWC) & mx__NAME_MAXWC);
}

/* ..where err is mx_name_flags (mix) */
INLINE u32 mx_name_flags_set_err(u32 flags, u32 err, s32 e_wc){
	return ((flags & ~(mx_NAME_ADDRSPEC_INVALID | mx__NAME_MASKWC)) | S(u32,err) |
			((S(u32,e_wc) & mx__NAME_MAXWC) << mx__NAME_SHIFTWC));
}

/* Extract a list of names; returns NIL on error */
EXPORT struct mx_name *mx_name_parse(char const *line, BITENUM(u32,gfield) ntype);

/* Interprets entire line as one address: identical to name_parse() but only returns one (or none) name (or NIL) */
EXPORT struct mx_name *mx_name_parse_as_one(char const *line, BITENUM(u32,gfield) ntype);

/* Alloc an Fcc: entry */
EXPORT struct mx_name *mx_name_parse_fcc(char const *file);

/* Initialize from content of np */
EXPORT struct mx_name *ndup(struct mx_name *np, enum gfield ntype);

/* Check if an address is invalid, either because it is malformed or, if not,
 * according to eacm.  Return FAL0 when it looks good, TRU1 if it is invalid
 * but the error condition was not covered by a 'hard "fail"ure', else -1 */
EXPORT s8 mx_name_is_invalid(struct mx_name *np, enum mx_expand_addr_check_mode eacm);

/* Does *NP* point to a file or pipe addressee? */
#define mx_name_is_fileorpipe(NP) (((NP)->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE) != 0)

/* Check whether n1 & n2 are the same address, effectively.
 * Takes *allnet* into account */
EXPORT boole mx_name_is_same_address(struct mx_name const *n1, struct mx_name const *n2);

/* Check whether n1 & n2 share the domain name */
EXPORT boole mx_name_is_same_domain(struct mx_name const *n1, struct mx_name const *n2);

/* */

/* Start of a "comment".  Ignore it */
EXPORT char const *mx_name_skip_comment_cp(char const *cp);

/* Return the start of a route-addr (address in angle brackets), if present */
EXPORT char const *mx_name_routeaddr_cp(char const *name);

/* Fetch the real name from an internet mail address field */
EXPORT char *mx_name_real_cp(char const *name);

/* Same name, while taking care for *allnet*?
 * If isallnet_or_nil is not NIL it is used as *allnet*; if TRUM1 *allnet* is assigned */
EXPORT boole mx_name_is_same_cp(char const *n1, char const *n2, boole *isallnet_or_nil);

/* An "alternate[s]" in broadest sense? */
EXPORT boole mx_name_is_metoo_cp(char const *name, boole check_reply_to);

/* Hash one, or compare two IDs; surrounding angle brackets are optional and ignored */
EXPORT uz mx_name_msgid_hash_cp(char const *id);
EXPORT sz mx_name_msgid_cmp_cp(char const *id1, char const *id2);

/* namelist */

/* Get a name_parse() list via go_input_cp(), reassigning to *np* */
EXPORT struct mx_name *mx_namelist_grab(u32/*mx_go_input_flags*/ gif, char const *field, struct mx_name *np, int comma,
		BITENUM(u32,gfield) gflags, boole not_a_list);

/* Duplicate np */
EXPORT struct mx_name *n_namelist_dup(struct mx_name const *np, BITENUM(u32,gfield) ntype);

/* Concatenate the two passed name lists, return the result */
EXPORT struct mx_name *cat(struct mx_name *n1, struct mx_name *n2);

/* Determine the number of undeleted elements in a name list and return it; the latter also does not count file and
 * pipe addressees in addition */
EXPORT u32 count(struct mx_name const *np);
EXPORT u32 count_nonlocal(struct mx_name const *np);

/* Check all addresses in np and delete invalid ones; if set_on_error is not NIL it will be set to TRU1 for error,
 * or -1 for "hard fail" error */
EXPORT struct mx_name *mx_namelist_check(struct mx_name *np, enum mx_expand_addr_check_mode eacm, s8 *set_on_error);

/* Vaporise all duplicate addresses in hp (.h_(to|cc|bcc)) so that an address that "first" occurs in To: is solely in
 * there, ditto Cc:, after expanding aliases etc.
 * eacm and set_on_error are passed to namelist_check().
 * After updating hp to the new state this returns a flat list of all addressees, which may be NIL */
EXPORT struct mx_name *n_namelist_vaporise_head(struct header *hp, enum mx_expand_addr_check_mode eacm, s8 *set_on_error);

/* Remove all of the duplicates from the passed name list by insertion sorting them, then checking for dups.
 * Return the head of the new list */
EXPORT struct mx_name *mx_namelist_elide(struct mx_name *names);

/* Turn a list of names into a string of the same names */
EXPORT char *mx_namelist_detract(struct mx_name const *np, BITENUM(u32,gfield) ntype);

#include <su/code-ou.h>
#endif /* mx_NAMES_H */
/* s-itt-mode */
