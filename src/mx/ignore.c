/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of ignore.h.
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE ignore
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/termios.h"

#include "mx/ignore.h"
#include "su/code-in.h"

struct a_ignore_type{
   u32 it_count; /* Entries in .it_ht (and .it_re) */
   boole it_all; /* _All_ fields ought to be _type_ (ignore/retain) */
   u8 it__dummy[3];
   struct a_ignore_field{
      struct a_ignore_field *if_next;
      char if_field[VFIELD_SIZE(0)]; /* Header field */
   } *it_ht[3]; /* TODO make hashmap dynamic */
#ifdef mx_HAVE_REGEX
   struct a_ignore_re{
      struct a_ignore_re *ir_next;
      regex_t ir_regex;
      char ir_input[VFIELD_SIZE(0)]; /* Regex input text (for showing it) */
   } *it_re, *it_re_tail;
#endif
};

struct mx_ignore{
   struct a_ignore_type i_retain;
   struct a_ignore_type i_ignore;
   boole i_auto; /* In auto-reclaimed, not heap memory */
   boole i_bltin; /* Is a built-in IGNORE* type */
   u8 i_ibm_idx; /* If .i_bltin: a_ignore_bltin_map[] idx */
   u8 i__dummy[5];
};

struct a_ignore_bltin_map{
   struct mx_ignore *ibm_ip;
   char const ibm_name[8];
};

static struct a_ignore_bltin_map const a_ignore_bltin_map[] = {
   {mx_IGNORE_TYPE, "type"},
   {mx_IGNORE_SAVE, "save"},
   {mx_IGNORE_FWD, "forward\0"},
   {mx_IGNORE_TOP, "top"},

   {mx_IGNORE_TYPE, "print\0"},
   {mx_IGNORE_FWD, "fwd"}
};
#ifdef mx_HAVE_DEVEL /* Avoid gcc warn cascade "mx_ignore is defined locally" */
CTAV(-mx__IGNORE_TYPE - mx__IGNORE_ADJUST == 0);
CTAV(-mx__IGNORE_SAVE - mx__IGNORE_ADJUST == 1);
CTAV(-mx__IGNORE_FWD - mx__IGNORE_ADJUST == 2);
CTAV(-mx__IGNORE_TOP - mx__IGNORE_ADJUST == 3);
CTAV(mx__IGNORE_MAX == 3);
#endif

static struct mx_ignore *a_ignore_bltin[mx__IGNORE_MAX + 1];

/* Almost everyone uses `ignore'/`retain', put _TYPE in BSS */
static struct mx_ignore a_ignore_type;

/* Return real self, which is xself unless that is a built-in special,
 * in which case NIL is returned if nonexistent and docreate is false.
 * The other statics assume self has been resolved (unless noted) */
static struct mx_ignore *a_ignore_resolve_self(struct mx_ignore *xself,
      boole docreate);

/* Lookup whether a mapping is contained: TRU1=retained, TRUM1=ignored.
 * If retain is _not_ TRUM1 then only the retained/ignored slot is inspected,
 * and regular expressions are not executed but instead their .ir_input is
 * text-compared against len bytes of dat.
 * Note it doesn't handle the .it_all "all fields" condition */
static boole a_ignore_lookup(struct mx_ignore const *self, boole retain,
      char const *dat, uz len);

/* Delete all retain( else ignor)ed members */
static void a_ignore_del_allof(struct mx_ignore *ip, boole retain);

/* Try to map a string to one of the built-in types */
static struct a_ignore_bltin_map const *a_ignore_resolve_bltin(char const *cp);

/* Logic behind `headerpick T T' (a.k.a. `retain'+) */
static boole a_ignore_addcmd_mux(struct mx_ignore *ip, char const **list,
      boole retain);

static void a_ignore__show(struct mx_ignore const *ip, boole retain);

/* Logic behind `unheaderpick T T' (a.k.a. `unretain'+) */
static boole a_ignore_delcmd_mux(struct mx_ignore *ip, char const **list,
      boole retain);

static boole a_ignore__delone(struct mx_ignore *ip, boole retain,
      char const *field);

static struct mx_ignore *
a_ignore_resolve_self(struct mx_ignore *xself, boole docreate){
   up suip;
   struct mx_ignore *self;
   NYD2_IN;

   self = xself;
   suip = -R(up,self) - mx__IGNORE_ADJUST;

   if(suip <= mx__IGNORE_MAX){
      if((self = a_ignore_bltin[suip]) == NIL && docreate){
         if(xself == mx_IGNORE_TYPE){
            self = &a_ignore_type;
            /* LIB: su_mem_set(self, 0, sizeof *self);*/
         }else
            self = mx_ignore_new(FAL0);
         self->i_bltin = TRU1;
         self->i_ibm_idx = S(u8,suip);
         a_ignore_bltin[suip] = self;
      }
   }

   NYD2_OU;
   return self;
}

static boole
a_ignore_lookup(struct mx_ignore const *self, boole retain,
      char const *dat, uz len){
   boole rv;
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   u32 hi;
   NYD2_IN;

   if(len == UZ_MAX)
      len = su_cs_len(dat);
   hi = su_cs_hash_case_cbuf(dat, len) % NELEM(self->i_retain.it_ht);

   /* Again: does not handle .it_all conditions! */
   /* (Inner functions would be nice, again) */
   if(retain && self->i_retain.it_count > 0){
      rv = TRU1;
      for(ifp = self->i_retain.it_ht[hi]; ifp != NIL; ifp = ifp->if_next)
         if(!su_cs_cmp_case_n(ifp->if_field, dat, len) &&
               ifp->if_field[len] == '\0')
            goto jleave;
#ifdef mx_HAVE_REGEX
      if(dat[len - 1] != '\0')
         dat = savestrbuf(dat, len);
      for(irp = self->i_retain.it_re; irp != NIL; irp = irp->ir_next)
         if((retain == TRUM1
               ? (regexec(&irp->ir_regex, dat, 0,NIL, 0) != REG_NOMATCH)
               : (!su_cs_cmp_n(irp->ir_input, dat, len) &&
                  irp->ir_input[len] == '\0')))
            goto jleave;
#endif
      rv = (retain == TRUM1) ? TRUM1 : FAL0;
   }else if((retain == TRUM1 || !retain) && self->i_ignore.it_count > 0){
      rv = TRUM1;
      for(ifp = self->i_ignore.it_ht[hi]; ifp != NIL; ifp = ifp->if_next)
         if(!su_cs_cmp_case_n(ifp->if_field, dat, len) &&
               ifp->if_field[len] == '\0')
            goto jleave;
#ifdef mx_HAVE_REGEX
      if(dat[len - 1] != '\0')
         dat = savestrbuf(dat, len);
      for(irp = self->i_ignore.it_re; irp != NIL; irp = irp->ir_next)
         if((retain == TRUM1
               ? (regexec(&irp->ir_regex, dat, 0,NIL, 0) != REG_NOMATCH)
               : (!su_cs_cmp_n(irp->ir_input, dat, len) &&
                  irp->ir_input[len] == '\0')))
            goto jleave;
#endif
      rv = (retain == TRUM1) ? TRU1 : FAL0;
   }else
      rv = FAL0;

jleave:
   NYD2_OU;
   return rv;
}

static void
a_ignore_del_allof(struct mx_ignore *ip, boole retain){
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   struct a_ignore_type *itp;
   NYD2_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

   if(!ip->i_auto){
      uz i;

      for(i = 0; i < NELEM(itp->it_ht); ++i)
         for(ifp = itp->it_ht[i]; ifp != NIL;){
            struct a_ignore_field *x;

            x = ifp;
            ifp = ifp->if_next;
            su_FREE(x);
         }
   }

#ifdef mx_HAVE_REGEX
   for(irp = itp->it_re; irp != NIL;){
      struct a_ignore_re *x;

      x = irp;
      irp = irp->ir_next;
      regfree(&x->ir_regex);
      if(!ip->i_auto)
         su_FREE(x);
   }
#endif

   su_mem_set(itp, 0, sizeof *itp);
   NYD2_OU;
}

static struct a_ignore_bltin_map const *
a_ignore_resolve_bltin(char const *cp){
   struct a_ignore_bltin_map const *ibmp;
   NYD2_IN;

   for(ibmp = &a_ignore_bltin_map[0];;)
      if(!su_cs_cmp_case(cp, ibmp->ibm_name))
         break;
      else if(++ibmp == &a_ignore_bltin_map[NELEM(a_ignore_bltin_map)]){
         ibmp = NIL;
         break;
      }

   NYD2_OU;
   return ibmp;
}

static boole
a_ignore_addcmd_mux(struct mx_ignore *ip, char const **list, boole retain){
   char const **ap;
   boole rv;
   NYD2_IN;

   ip = a_ignore_resolve_self(ip, rv = (*list != NIL));

   if(!rv){
      if(ip != NIL && ip->i_bltin)
         a_ignore__show(ip, retain);
      rv = TRU1;
   }else{
      for(ap = list; *ap != 0; ++ap)
         switch(mx_ignore_insert_cp(ip, retain, *ap)){
         case FAL0:
            n_err(_("Invalid field name cannot be %s: %s\n"),
               (retain ? _("retained") : _("ignored")), *ap);
            rv = FAL0;
            break;
         case TRUM1:
            if(n_poption & n_PO_D_V)
               n_err(_("Field already %s: %s\n"),
                  (retain ? _("retained") : _("ignored")), *ap);
            /* FALLTHRU */
         case TRU1:
            break;
         }
   }

   NYD2_OU;
   return rv;
}

static void
a_ignore__show(struct mx_ignore const *ip, boole retain){
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   uz i, sw;
   char const **ap, **ring;
   struct a_ignore_type const *itp;
   NYD2_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

   do{
      char const *pre, *attr;

      if(itp->it_all)
         pre = n_empty, attr = n_star;
      else if(itp->it_count == 0)
         pre = n_ns, attr = _("currently covers no fields");
      else
         break;
      fprintf(n_stdout, _("%sheaderpick %s %s %s\n"),
         pre, a_ignore_bltin_map[ip->i_ibm_idx].ibm_name,
         (retain ? "retain" : "ignore"), attr);
      goto jleave;
   }while(0);

   ring = n_autorec_alloc((itp->it_count +1) * sizeof *ring);
   for(ap = ring, i = 0; i < NELEM(itp->it_ht); ++i)
      for(ifp = itp->it_ht[i]; ifp != NIL; ifp = ifp->if_next)
         *ap++ = ifp->if_field;
   *ap = NIL;

   su_sort_shell_vpp(su_S(void const**,ring), P2UZ(ap - ring),
      su_cs_toolbox_case.tb_compare);

   i = fprintf(n_stdout, "headerpick %s %s",
      a_ignore_bltin_map[ip->i_ibm_idx].ibm_name,
      (retain ? "retain" : "ignore"));
   sw = mx_termios_dimen.tiosd_width;

   for(ap = ring; *ap != NIL; ++ap){
      /* These fields are all ASCII, no visual width needed */
      uz len;
      char const *cp;

      cp = n_shexp_quote_cp(*ap, FAL0);
      len = su_cs_len(cp) + 1;
      if(UCMP(z, len, >=, sw - i)){
         fputs(" \\\n ", n_stdout);
         i = 1;
      }
      i += len;
      putc(' ', n_stdout);
      fputs(cp, n_stdout);
   }

   /* Regular expression in FIFO order */
#ifdef mx_HAVE_REGEX
   for(irp = itp->it_re; irp != NIL; irp = irp->ir_next){
      uz len;
      char const *cp;

      cp = n_shexp_quote_cp(irp->ir_input, FAL0);
      len = su_cs_len(cp) + 1;
      if(UCMP(z, len, >=, sw - i)){
         fputs(" \\\n ", n_stdout);
         i = 1;
      }
      i += len;
      putc(' ', n_stdout);
      fputs(cp, n_stdout);
   }
#endif

   putc('\n', n_stdout);

jleave:
   fflush(n_stdout);
   NYD2_OU;
}

static boole
a_ignore_delcmd_mux(struct mx_ignore *ip, char const **list, boole retain){
   char const *cp;
   struct a_ignore_type *itp;
   boole rv;
   NYD2_IN;

   ip = a_ignore_resolve_self(ip, rv = (*list != NIL));
   itp = retain ? &ip->i_retain : &ip->i_ignore;

   if(itp->it_count == 0 && !itp->it_all)
      n_err(_("No fields currently being %s\n"),
         (retain ? _("retained") : _("ignored")));
   else{
      while((cp = *list++) != NIL)
         if(cp[0] == '*' && cp[1] == '\0')
            a_ignore_del_allof(ip, retain);
         else if(!a_ignore__delone(ip, retain, cp)){
            n_err(_("Field not %s: %s\n"),
               (retain ? _("retained") : _("ignored")), cp);
            rv = FAL0;
         }
   }

   NYD2_OU;
   return rv;
}

static boole
a_ignore__delone(struct mx_ignore *ip, boole retain, char const *field){
   struct a_ignore_type *itp;
   NYD_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

#ifdef mx_HAVE_REGEX
   if(n_is_maybe_regex(field)){
      struct a_ignore_re **lirp, *irp;

      for(irp = *(lirp = &itp->it_re); irp != NIL;
            lirp = &irp->ir_next, irp = irp->ir_next)
         if(!su_cs_cmp(field, irp->ir_input)){
            *lirp = irp->ir_next;
            if(irp == itp->it_re_tail)
               itp->it_re_tail = irp->ir_next;

            regfree(&irp->ir_regex);
            if(!ip->i_auto)
               su_FREE(irp);
            --itp->it_count;
            goto jleave;
         }
   }else
#endif /* mx_HAVE_REGEX */
        {
      struct a_ignore_field **ifpp, *ifp;
      u32 hi;

      hi = su_cs_hash_case_cbuf(field, UZ_MAX) % NELEM(itp->it_ht);

      for(ifp = *(ifpp = &itp->it_ht[hi]); ifp != NIL;
            ifpp = &ifp->if_next, ifp = ifp->if_next)
         if(!su_cs_cmp_case(ifp->if_field, field)){
            *ifpp = ifp->if_next;
            if(!ip->i_auto)
               su_FREE(ifp);
            --itp->it_count;
           goto jleave;
         }
   }

   ip = NIL;
jleave:
   NYD_OU;
   return (ip != NIL);
}

int
c_headerpick(void *vp){
   boole retain;
   struct a_ignore_bltin_map const *ibmp;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;
   argv = vp;

   /* Without arguments, show all settings of all contexts */
   if(*argv == NIL){
      rv = 0;
      for(ibmp = &a_ignore_bltin_map[0];
            ibmp <= &a_ignore_bltin_map[mx__IGNORE_MAX]; ++ibmp){
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, TRU1);
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, FAL0);
      }
      goto jleave;
   }

   if((ibmp = a_ignore_resolve_bltin(*argv)) == NIL){
      n_err(_("headerpick: invalid context: %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   /* With only <context>, show all settings of it */
   if(*argv == NIL){
      rv = 0;
      rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, TRU1);
      rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, FAL0);
      goto jleave;
   }

   if(su_cs_starts_with_case("retain", *argv))
      retain = TRU1;
   else if(su_cs_starts_with_case("ignore", *argv))
      retain = FAL0;
   else{
      n_err(_("headerpick: invalid type (retain, ignore): %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   /* With only <context> and <type>, show its settings */
   if(*argv == NIL){
      rv = !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, retain);
      goto jleave;
   }

   rv = !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, retain);

jleave:
   NYD_OU;
   return rv;
}

int
c_unheaderpick(void *vp){
   boole retain;
   struct a_ignore_bltin_map const *ibmp;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;
   argv = vp;

   if((ibmp = a_ignore_resolve_bltin(*argv)) == NIL){
      n_err(_("unheaderpick: invalid context: %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   if(su_cs_starts_with_case("retain", *argv))
      retain = TRU1;
   else if(su_cs_starts_with_case("ignore", *argv))
      retain = FAL0;
   else{
      n_err(_("unheaderpick: invalid type (retain, ignore): %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   rv = !a_ignore_delcmd_mux(ibmp->ibm_ip, argv, retain);

jleave:
   NYD_OU;
   return rv;
}

int
c_retain(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_TYPE, vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_ignore(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_TYPE, vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_unretain(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_TYPE, vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_unignore(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_TYPE, vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_saveretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_SAVE, v, TRU1);

   NYD_OU;
   return rv;
}

int
c_saveignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_SAVE, v, FAL0);

   NYD_OU;
   return rv;
}

int
c_unsaveretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_SAVE, v, TRU1);

   NYD_OU;
   return rv;
}

int
c_unsaveignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_SAVE, v, FAL0);

   NYD_OU;
   return rv;
}

int
c_fwdretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_FWD, v, TRU1);

   NYD_OU;
   return rv;
}

int
c_fwdignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(mx_IGNORE_FWD, v, FAL0);

   NYD_OU;
   return rv;
}

int
c_unfwdretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_FWD, v, TRU1);

   NYD_OU;
   return rv;
}

int
c_unfwdignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(mx_IGNORE_FWD, v, FAL0);

   NYD_OU;
   return rv;
}

struct mx_ignore *
mx_ignore_new(boole isauto){
   struct mx_ignore *self;
   NYD_IN;

   self = isauto ? n_autorec_calloc(1, sizeof *self)
         : su_CALLOC_N(1, sizeof *self);
   self->i_auto = isauto;

   NYD_OU;
   return self;
}

void
mx_ignore_del(struct mx_ignore *self){
   NYD_IN;

   a_ignore_del_allof(self, TRU1);
   a_ignore_del_allof(self, FAL0);
   if(!self->i_auto)
      su_FREE(self);

   NYD_OU;
}

boole
mx_ignore_is_any(struct mx_ignore const *self){
   boole rv;
   NYD_IN;

   self = a_ignore_resolve_self(n_UNCONST(self), FAL0);
   rv = (self != NIL &&
         (self->i_retain.it_count != 0 || self->i_retain.it_all ||
          self->i_ignore.it_count != 0 || self->i_ignore.it_all));

   NYD_OU;
   return rv;
}

boole
mx_ignore_insert(struct mx_ignore *self, boole retain,
      char const *dat, uz len){
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
   boole isre;
#endif
   struct a_ignore_field *ifp;
   struct a_ignore_type *itp;
   boole rv;
   NYD_IN;

   retain = !!retain; /* Make true bool, TRUM1 has special _lookup meaning */
   rv = FAL0;
   self = a_ignore_resolve_self(self, TRU1);

   if(len == UZ_MAX)
      len = su_cs_len(dat);

   /* Request to ignore or retain _anything_?  That is special-treated */
   if(len == 1 && dat[0] == '*'){
      itp = retain ? &self->i_retain : &self->i_ignore;
      if(itp->it_all)
         rv = TRUM1;
      else{
         itp->it_all = TRU1;
         a_ignore_del_allof(self, retain);
         rv = TRU1;
      }
      goto jleave;
   }

   /* Check for regular expression or valid fieldname */
#ifdef mx_HAVE_REGEX
   if(!(isre = n_is_maybe_regex_buf(dat, len)))
#endif
   {
      char c;
      uz i;

      for(i = 0; i < len; ++i){
         c = dat[i];
         if(!fieldnamechar(c))
            goto jleave;
      }
   }

   rv = TRUM1;
   if(a_ignore_lookup(self, retain, dat, len) == (retain ? TRU1 : TRUM1))
      goto jleave;

   itp = retain ? &self->i_retain : &self->i_ignore;

   if(itp->it_count == U32_MAX){
      n_err(_("Header selection size limit reached, cannot insert: %.*s\n"),
         S(int,MIN(len, S32_MAX)), dat);
      rv = FAL0;
      goto jleave;
   }

   rv = TRU1;
#ifdef mx_HAVE_REGEX
   if(isre){
      struct a_ignore_re *x;
      int s;
      uz i;

      i = VSTRUCT_SIZEOF(struct a_ignore_re, ir_input) + ++len;
      irp = self->i_auto ? n_autorec_alloc(i) : su_ALLOC(i);
      su_mem_copy(irp->ir_input, dat, --len);
      irp->ir_input[len] = '\0';

      if((s = regcomp(&irp->ir_regex, irp->ir_input,
            REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
         n_err(_("Invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(irp->ir_input, FAL0),
            n_regex_err_to_doc(NIL, s));
         if(!self->i_auto)
            su_FREE(irp);
         rv = FAL0;
         goto jleave;
      }

      irp->ir_next = NIL;
      if((x = itp->it_re_tail) != NIL)
         x->ir_next = irp;
      else
         itp->it_re = irp;
      itp->it_re_tail = irp;
   }else
#endif /* mx_HAVE_REGEX */
        {
      u32 hi;
      uz i;

      i = VSTRUCT_SIZEOF(struct a_ignore_field, if_field) + len + 1;
      ifp = self->i_auto ? n_autorec_alloc(i) : su_ALLOC(i);
      su_mem_copy(ifp->if_field, dat, len);
      ifp->if_field[len] = '\0';
      hi = su_cs_hash_case_cbuf(dat, len) % NELEM(itp->it_ht);
      ifp->if_next = itp->it_ht[hi];
      itp->it_ht[hi] = ifp;
   }
   ++itp->it_count;

jleave:
   NYD_OU;
   return rv;
}

boole
mx_ignore_lookup(struct mx_ignore const *self, char const *dat, uz len){
   boole rv;
   NYD_IN;

   if(self == mx_IGNORE_ALL)
      rv = TRUM1;
   else if(len == 0 ||
         (self = a_ignore_resolve_self(UNCONST(struct mx_ignore*,self), FAL0)
            ) == NIL)
      rv = FAL0;
   else if(self->i_retain.it_all)
      rv = TRU1;
   else if(self->i_retain.it_count == 0 && self->i_ignore.it_all)
      rv = TRUM1;
   else
      rv = a_ignore_lookup(self, TRUM1, dat, len);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
