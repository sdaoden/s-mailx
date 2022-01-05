/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Names (read: addresses), including `alternates' and `alias' lists.
 *@ TODO It should be solely that, parsing etc. should be in header.c,
 *@ TODO or rfc5322.c or something like this.
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define mx_NAMES_H /* XXX a lie - it is rather n_ yet */

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_name;

enum mx_name_flags{
   mx_NAME_SKINNED = 1u<<0, /* Has been skin()ned */
   mx_NAME_IDNA = 1u<<1, /* IDNA has been applied */
   mx_NAME_NAME_SALLOC = 1u<<2, /* .n_name in detached memory */

   mx_NAME_ADDRSPEC_ISFILE = 1u<<3, /* ..is a file path */
   mx_NAME_ADDRSPEC_ISPIPE = 1u<<4, /* ..is a command for piping */
   mx_NAME_ADDRSPEC_ISFILEORPIPE = mx_NAME_ADDRSPEC_ISFILE |
         mx_NAME_ADDRSPEC_ISPIPE,
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
   mx_NAME_ADDRSPEC_INVALID = mx_NAME_ADDRSPEC_ERR_EMPTY |
         mx_NAME_ADDRSPEC_ERR_ATSEQ | mx_NAME_ADDRSPEC_ERR_CHAR |
         mx_NAME_ADDRSPEC_ERR_IDNA | mx_NAME_ADDRSPEC_ERR_NAME,

   /* Error storage (we must fit in 31-bit!) */
   mx__NAME_SHIFTWC = 14,
   mx__NAME_MAXWC = 0x1FFFF,
   mx__NAME_MASKWC = mx__NAME_MAXWC << mx__NAME_SHIFTWC
   /* Bit 31 (32) == S32_MIN temporarily used */
};

struct mx_name{
   struct mx_name *n_flink;
   struct mx_name *n_blink;
   enum gfield n_type; /* Header field this comes from */
   u32 n_flags; /* enum mx_name_flags */
   char *n_name;
   char *n_fullname; /* .n_name, unless GFULL: +comments, etc */
   char *n_fullextra; /* GFULL, without address */
};

/* In the !_ERR_EMPTY case, the failing character can be queried */
INLINE s32 mx_name_flags_get_err_wc(u32 flags){
   return (((flags & mx__NAME_MASKWC) >> mx__NAME_SHIFTWC) & mx__NAME_MAXWC);
}

/* ..where err is mx_name_flags (mix) */
INLINE u32 mx_name_flags_set_err(u32 flags, u32 err, s32 e_wc){
   return ((flags & ~(mx_NAME_ADDRSPEC_INVALID | mx__NAME_MASKWC)) |
      S(u32,err) | ((S(u32,e_wc) & mx__NAME_MAXWC) << mx__NAME_SHIFTWC));
}

/* Allocate a single element of a name list, initialize its name field to the
 * passed name and return it.
 * May return NULL with GNULL_OK (only, unfortunately) */
EXPORT struct mx_name *nalloc(char const *str, enum gfield ntype);

/* Alloc an Fcc: entry TODO temporary only i hope */
EXPORT struct mx_name *nalloc_fcc(char const *file);

/* Like nalloc(), but initialize from content of np */
EXPORT struct mx_name *ndup(struct mx_name *np, enum gfield ntype);

/* Concatenate the two passed name lists, return the result */
EXPORT struct mx_name *cat(struct mx_name *n1, struct mx_name *n2);

/* Duplicate np */
EXPORT struct mx_name *n_namelist_dup(struct mx_name const *np,
      enum gfield ntype);

/* Determine the number of undeleted elements in a name list and return it;
 * the latter also doesn't count file and pipe addressees in addition */
EXPORT u32 count(struct mx_name const *np);
EXPORT u32 count_nonlocal(struct mx_name const *np);

/* Extract a list of names from a line, and make a list of names from it.
 * Return the list or NULL if none found */
EXPORT struct mx_name *extract(char const *line, enum gfield ntype);

/* Like extract() unless line contains anyof ",\"\\(<|", in which case
 * comma-separated list extraction is used instead */
EXPORT struct mx_name *lextract(char const *line, enum gfield ntype);

/* Interprets the entire line as one address: identical to extract() and
 * lextract() but only returns one (or none) name.
 * GSKIN will be added to ntype as well as GNULL_OK: may return NULL! */
EXPORT struct mx_name *n_extract_single(char const *line, enum gfield ntype);

/* Turn a list of names into a string of the same names */
EXPORT char *detract(struct mx_name *np, enum gfield ntype);

/* Get a lextract() list via go_input_cp(), reassigning to *np* */
EXPORT struct mx_name *grab_names(u32/*mx_go_input_flags*/ gif,
      char const *field, struct mx_name *np, int comma, enum gfield gflags);

/* Check whether n1 & n2 are the same address, effectively.
 * Takes *allnet* into account */
EXPORT boole mx_name_is_same_address(struct mx_name const *n1,
      struct mx_name const *n2);

/* Check whether n1 & n2 share the domain name */
EXPORT boole mx_name_is_same_domain(struct mx_name const *n1,
      struct mx_name const *n2);

/* Check all addresses in np and delete invalid ones; if set_on_error is not
 * NULL it'll be set to TRU1 for error or -1 for "hard fail" error */
EXPORT struct mx_name *checkaddrs(struct mx_name *np,
      enum expand_addr_check_mode eacm, s8 *set_on_error);

/* Vaporise all duplicate addresses in hp (.h_(to|cc|bcc)) so that an address
 * that "first" occurs in To: is solely in there, ditto Cc:, after expanding
 * aliases etc.  eacm and set_on_error are passed to checkaddrs().
 * After updating hp to the new state this returns a flat list of all
 * addressees, which may be NIL */
EXPORT struct mx_name *n_namelist_vaporise_head(struct header *hp,
      boole metoo, boole strip_alternates,
      enum expand_addr_check_mode eacm, s8 *set_on_error);

/* Map all of the aliased users in the invoker's mailrc file and insert them
 * into the list */
EXPORT struct mx_name *usermap(struct mx_name *names, boole force_metoo);

/* Remove all of the duplicates from the passed name list by insertion sorting
 * them, then checking for dups.  Return the head of the new list */
EXPORT struct mx_name *elide(struct mx_name *names);

/* `(un)?alias' */
EXPORT int c_alias(void *vp);
EXPORT int c_unalias(void *vp);

/* Is name a valid alias name (as opposed to: "is an alias") */
EXPORT boole mx_alias_is_valid_name(char const *name);

/* `(un)?alternates' deal with the list of alternate names */
EXPORT int c_alternates(void *vp);
EXPORT int c_unalternates(void *vp);

/* If keep_single is set one alternates member will be allowed in np */
EXPORT struct mx_name *mx_alternates_remove(struct mx_name *np,
      boole keep_single);

/* Likewise, is name an alternate in broadest sense? */
EXPORT boole mx_name_is_mine(char const *name);

#include <su/code-ou.h>
#endif /* mx_NAMES_H */
/* s-it-mode */
