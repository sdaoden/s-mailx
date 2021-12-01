/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of ignore.h.
 *@ XXX debug+: on-exit cleanup
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE ignore
#define mx_SOURCE
#define mx_SOURCE_IGNORE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/sort.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/cmd.h"
#include "mx/go.h"
#include "mx/termios.h"

#include "mx/ignore.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_ignore_new_flags{
   a_IGNORE_NONE,
   a_IGNORE_BLTIN_MASK = 0x1F, /* Mask for index into _bltin_map */
   a_IGNORE_BLTIN = 1u<<5, /* Redundant: could be (X&BLTIN_MASK)!=0 */
   a_IGNORE_CLEANUP = 1u<<6
};

struct a_ignore_type{
   u32 it_count; /* Entries in .it_ht (and .it_re) */
   boole it_all; /* _All_ fields ought to be _type_ (ignore/retain) */
   u8 it__dummy[3];
   struct a_ignore_field{
      struct a_ignore_field *if_next;
      uz if_len; /* Of .if_field */
      char if_field[VFIELD_SIZE(0)]; /* Header field */
   } *it_ht[5]; /* TODO make hashmap dynamic */
#ifdef mx_HAVE_REGEX
   struct a_ignore_re{
      struct a_ignore_re *ir_next;
      struct su_re ir_re;
      uz ir_len; /* Of .ir_input */
      char ir_input[VFIELD_SIZE(0)]; /* Regex input text (for showing it) */
   } *it_re, *it_re_tail;
#endif
};

struct mx_ignore{
   struct mx_go_cleanup_ctx i_gcc; /* Place first so it IS self */
   struct mx_ignore *i_next; /* Only for non-builtin ones */
   struct a_ignore_type i_retain;
   struct a_ignore_type i_ignore;
   u8 i_flags;
   char i_name[VFIELD_SIZE(7)];
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
CTA(S(u32,mx__IGNORE_MAX) <= S(u32,a_IGNORE_BLTIN_MASK), "Bit range excessed");

static struct mx_ignore *a_ignore_bltin[mx__IGNORE_MAX + 1];
static struct mx_ignore *a_ignore_list;

/* */
static struct mx_ignore *a_ignore_new(char const *name,
      BITENUM_IS(u32,a_ignore_new_flags) f);

/* Return real self, which is xself unless that is a built-in special,
 * in which case NIL is returned if nonexistent and docreate is false.
 * The other statics assume self has been resolved (unless noted) */
static struct mx_ignore *a_ignore_resolve_self(struct mx_ignore *xself,
      boole docreate);

/* Delete all retain( else ignor)ed members */
static void a_ignore_del_allof(struct mx_ignore *ip, boole retain);

/* "assign" / "join" */
static boole a_ignore_assijoin(struct mx_ignore *self,
      struct mx_ignore const *tp, boole isjoin);

/* Logic behind `headerpick' */
static boole a_ignore_addcmd_mux(struct mx_ignore *ip, char const **list,
      boole retain);

static void a_ignore__show(struct mx_ignore const *ip, boole retain);

/* Logic behind `unheaderpick' */
static boole a_ignore_delcmd_mux(struct mx_ignore *ip, char const **list,
      boole retain);

static boole a_ignore__delone(struct mx_ignore *ip, boole retain,
      char const *field);

/* Lookup whether a mapping is contained: TRU1=retained, FAL0=ignored,
 * TRUM1=both; if retain is _not_ TRUM1 then regular expressions are not
 * executed but instead their .ir_input is byte-compared against len bytes of
 * dat.  dat[len] is NUL.
 * If isre=TRUM1 we just look.
 * Note it does not handle the .it_all "all fields" condition */
static boole a_ignore_lookup(struct mx_ignore const *self, boole retain,
      char const *dat, uz len, boole isre);

/* dat[len] is NUL */
static boole a_ignore_insert(struct a_ignore_type *itp,
      char const *dat, uz len, boole isre);

static struct mx_ignore *
a_ignore_new(char const *name, BITENUM_IS(u32,a_ignore_new_flags) f){
   struct mx_ignore *self;
   uz l;
   NYD2_IN;

   l = su_cs_len(name) +1;

   self = su_CALLOC(VSTRUCT_SIZEOF(struct mx_ignore,i_name) + l);
   self->i_flags = S(u8,f);

   if(f & a_IGNORE_BLTIN)
      a_ignore_bltin[f & a_IGNORE_BLTIN_MASK] = self;
   else{
      self->i_next = a_ignore_list;
      a_ignore_list = self;
   }

   su_mem_copy(self->i_name, name, l);

   if(f & a_IGNORE_CLEANUP){
      self->i_gcc.gcc_fun = R(su_del_fun,&mx_ignore_del);
      mx_go_ctx_cleanup_push(&self->i_gcc);
   }

   NYD2_OU;
   return self;
}

static struct mx_ignore *
a_ignore_resolve_self(struct mx_ignore *xself, boole docreate){
   up suip;
   struct mx_ignore *self;
   NYD2_IN;

   self = xself;
   suip = -R(up,self) - mx__IGNORE_ADJUST;

   if(suip <= mx__IGNORE_MAX &&
         (self = a_ignore_bltin[suip]) == NIL && docreate)
      self = a_ignore_new(a_ignore_bltin_map[suip].ibm_name,
            (S(u8,suip) | a_IGNORE_BLTIN));

   NYD2_OU;
   return self;
}

static void
a_ignore_del_allof(struct mx_ignore *ip, boole retain){
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   uz i;
   struct a_ignore_type *itp;
   NYD2_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

   for(i = 0; i < NELEM(itp->it_ht); ++i)
      for(ifp = itp->it_ht[i]; ifp != NIL;){
         struct a_ignore_field *x;

         x = ifp;
         ifp = ifp->if_next;
         su_FREE(x);
      }

#ifdef mx_HAVE_REGEX
   for(irp = itp->it_re; irp != NIL;){
      struct a_ignore_re *x;

      x = irp;
      irp = irp->ir_next;
      su_re_gut(&x->ir_re);
      su_FREE(x);
   }
#endif

   su_mem_set(itp, 0, sizeof *itp);
   NYD2_OU;
}

static boole
a_ignore_assijoin(struct mx_ignore *self, struct mx_ignore const *tp,
      boole isjoin){
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *tirp;
#endif
   struct a_ignore_field const *tifp;
   uz i;
   struct a_ignore_type *itp;
   struct a_ignore_type const *titp;
   boole rv, retain;
   NYD_IN;

   rv = FAL0;
   if((self = a_ignore_resolve_self(self, TRU1)) == NIL)
      goto jleave;

   rv = TRU1;
jedel:
   if(!rv || !isjoin){
      a_ignore_del_allof(self, TRU1);
      a_ignore_del_allof(self, FAL0);
   }
   if(!rv)
      goto jleave;

   if((tp = a_ignore_resolve_self(UNCONST(struct mx_ignore*,tp), TRU1)
         ) == NIL){
      rv = FAL0;
      goto jleave;
   }

   retain = TRU1;
jreign:
   itp = retain ? &self->i_retain : &self->i_ignore;
   titp = retain ? &tp->i_retain : &tp->i_ignore;

   for(i = 0; i < NELEM(titp->it_ht); ++i)
      for(tifp = titp->it_ht[i]; tifp != NIL; tifp = tifp->if_next){
         if(!(rv = a_ignore_lookup(self, retain, tifp->if_field, tifp->if_len,
               FAL0)) && !(rv = a_ignore_insert(itp, tifp->if_field,
                  tifp->if_len, FAL0)))
            goto jedel;
      }

#ifdef mx_HAVE_REGEX
   for(tirp = titp->it_re; tirp != NIL; tirp = tirp->ir_next){
      if(!(rv = a_ignore_lookup(self, retain, tirp->ir_input, tirp->ir_len,
            TRU1)) && !(rv = a_ignore_insert(itp, tirp->ir_input, tirp->ir_len,
               TRU1)))
         goto jedel;
   }
#endif

   if(retain){
      retain = FAL0;
      goto jreign;
   }

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

static boole
a_ignore_addcmd_mux(struct mx_ignore *ip, char const **list, boole retain){
   char const **ap;
   boole rv;
   NYD2_IN;

   ip = a_ignore_resolve_self(ip, rv = (*list != NIL));

   if(!rv){
      if(ip != NIL)
         a_ignore__show(ip, retain);
      rv = TRU1;
   }else{
      for(ap = list; *ap != 0; ++ap)
         switch(mx_ignore_insert(ip, retain, *ap)){
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
         pre, ip->i_name, (retain ? "retain" : "ignore"), attr);
      goto jleave;
   }while(0);

   ring = su_LOFI_ALLOC((itp->it_count +1) * sizeof *ring);

   for(ap = ring, i = 0; i < NELEM(itp->it_ht); ++i)
      for(ifp = itp->it_ht[i]; ifp != NIL; ifp = ifp->if_next)
         *ap++ = ifp->if_field;
   *ap = NIL;

   su_sort_shell_vpp(su_S(void const**,ring), P2UZ(ap - ring),
      su_cs_toolbox_case.tb_cmp);

   i = fprintf(n_stdout, "headerpick %s %s",
         ip->i_name, (retain ? "retain" : "ignore"));
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

   su_LOFI_FREE(ring);

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
   if(n_re_could_be_one_cp(field)){
      struct a_ignore_re **lirp, *irp;

      for(irp = *(lirp = &itp->it_re); irp != NIL;
            lirp = &irp->ir_next, irp = irp->ir_next)
         if(!su_cs_cmp(field, irp->ir_input)){
            *lirp = irp->ir_next;
            if(irp == itp->it_re_tail)
               itp->it_re_tail = irp->ir_next;

            su_re_gut(&irp->ir_re);
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
            ifpp = &ifp->if_next, ifp = ifp->if_next){
         if(!su_cs_cmp_case(ifp->if_field, field)){
            *ifpp = ifp->if_next;

            su_FREE(ifp);

            --itp->it_count;
           goto jleave;
         }
      }
   }

   ip = NIL;
jleave:
   NYD_OU;
   return (ip != NIL);
}

static boole
a_ignore_lookup(struct mx_ignore const *self, boole retain,
      char const *dat, uz len, boole isre){
   boole rv;
#ifdef mx_HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   u32 hi;
   NYD2_IN;

   if(len == 0){
      rv = FAL0;
      goto jleave;
   }
   ASSERT(dat[len] == '\0');

   hi = su_cs_hash_case_cbuf(dat, len) % NELEM(self->i_retain.it_ht);

   /* Again: does not handle .it_all conditions! */
   /* (Inner functions would be nice, again) */
   if(retain && self->i_retain.it_count > 0){
      rv = TRU1;

      if(!isre || isre == TRUM1){
         for(ifp = self->i_retain.it_ht[hi]; ifp != NIL; ifp = ifp->if_next)
            if(!su_cs_cmp_case_n(ifp->if_field, dat, len) &&
                  ifp->if_field[len] == '\0')
               goto jleave;
      }

#ifdef mx_HAVE_REGEX
      if(isre){
         for(irp = self->i_retain.it_re; irp != NIL; irp = irp->ir_next)
            if((retain == TRUM1
                  ? su_re_eval_cp(&irp->ir_re, dat, su_RE_EVAL_NONE)
                  : (!su_cs_cmp_n(irp->ir_input, dat, len) &&
                     irp->ir_input[len] == '\0')))
               goto jleave;
      }
#endif

      rv = (retain == TRUM1) ? TRUM1 : FAL0;
   }else if((retain == TRUM1 || !retain) && self->i_ignore.it_count > 0){
      rv = TRUM1;

      if(!isre || isre == TRUM1){
         for(ifp = self->i_ignore.it_ht[hi]; ifp != NIL; ifp = ifp->if_next)
            if(!su_cs_cmp_case_n(ifp->if_field, dat, len) &&
                  ifp->if_field[len] == '\0')
               goto jleave;
      }
#ifdef mx_HAVE_REGEX

      if(isre){
         for(irp = self->i_ignore.it_re; irp != NIL; irp = irp->ir_next)
            if((retain == TRUM1
                  ? su_re_eval_cp(&irp->ir_re, dat, su_RE_EVAL_NONE)
                  : (!su_cs_cmp_n(irp->ir_input, dat, len) &&
                     irp->ir_input[len] == '\0')))
               goto jleave;
      }
#endif

      rv = (retain == TRUM1) ? TRU1 : FAL0;
   }else
      rv = FAL0;

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_ignore_insert(struct a_ignore_type *itp, char const *dat, uz len,
      boole isre){
   boole rv;
   NYD_IN;
   UNUSED(isre);

   if(len == 0){
      rv = FAL0;
      goto jleave;
   }
   ASSERT(dat[len] == '\0');
   ++len;

   rv = TRU1;
#ifdef mx_HAVE_REGEX
   if(isre){
      struct su_re *rep;
      struct a_ignore_re *irp, *x;

      irp = su_ALLOC(VSTRUCT_SIZEOF(struct a_ignore_re,ir_input) + len);
      irp->ir_len = len -1;
      su_mem_copy(irp->ir_input, dat, len);

      rep = su_re_create(&irp->ir_re);

      if(su_re_setup_cp(rep, irp->ir_input, (su_RE_SETUP_EXT |
            su_RE_SETUP_ICASE | su_RE_SETUP_TEST_ONLY)) != su_RE_ERROR_NONE){
         n_err(_("Invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(irp->ir_input, FAL0), su_re_error_doc(rep));
         su_re_gut(rep);
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
      struct a_ignore_field *ifp;

      ifp = su_ALLOC(VSTRUCT_SIZEOF(struct a_ignore_field,if_field) + len);
      su_mem_copy(ifp->if_field, dat, len);
      ifp->if_len = --len;
      hi = su_cs_hash_case_cbuf(dat, len) % NELEM(itp->it_ht);
      ifp->if_next = itp->it_ht[hi];
      itp->it_ht[hi] = ifp;
   }

   ++itp->it_count;
jleave:
   NYD_OU;
   return rv;
}

int
c_headerpick(void *vp){
   boole retain;
   struct mx_ignore *self, *xself;
   char const **argv;
   int rv;
   NYD_IN;

   argv = vp;

   /* Without arguments, show all settings of all contexts */
   if(*argv == NIL){
      struct a_ignore_bltin_map const *ibmp;

      rv = su_EX_OK;

      for(ibmp = &a_ignore_bltin_map[0];
            ibmp <= &a_ignore_bltin_map[mx__IGNORE_MAX]; ++ibmp){
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, TRU1);
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, FAL0);
      }

      for(self = a_ignore_list; self != NIL; self = self->i_next){
         rv |= !a_ignore_addcmd_mux(self, argv, TRU1);
         rv |= !a_ignore_addcmd_mux(self, argv, FAL0);
      }

      goto jleave;
   }

   /* Special modes: create/remove/assign */
   if(su_cs_starts_with_case("create", *argv)){
      if(argv[1] == NIL || argv[2] != NIL)
         goto jesyn;
      ++argv;

      if(!n_shexp_is_valid_varname(*argv, FAL0) ||
            su_cs_starts_with_case("create", *argv) ||
            su_cs_starts_with_case("remove", *argv) ||
            su_cs_starts_with_case("assign", *argv) ||
            su_cs_starts_with_case("join", *argv)){
jecreatname:
         n_err(_("headerpick: create: invalid context name: %s\n"), *argv);
         rv = su_EX_ERR;
         goto jleave;
      }else{
         struct a_ignore_bltin_map const *ibmp;

         for(ibmp = &a_ignore_bltin_map[0];
               ibmp <= &a_ignore_bltin_map[mx__IGNORE_MAX]; ++ibmp)
            if(!su_cs_cmp_case(ibmp->ibm_name, *argv))
               goto jecreatname;
      }

      for(xself = a_ignore_list; xself != NIL; xself = xself->i_next)
         if(!su_cs_cmp_case(xself->i_name, *argv)){
            n_err(_("headerpick: create: context exists: %s\n"), *argv);
            rv = su_EX_ERR;
            goto jleave;
         }

      self = mx_ignore_new(*argv, FAL0);
      rv = su_EX_OK;
      goto jleave;
   }

   xself = NIL;
   if((rv = -1, su_cs_starts_with_case("assign", *argv)) ||
         (rv = -2, su_cs_starts_with_case("join", *argv))){
      if(argv[1] == NIL || argv[2] == NIL || argv[3] != NIL)
         goto jesyn;
      ++argv;
      if((xself = mx_ignore_by_name(*argv)) == NIL)
         goto jectx;
      ++argv;
   }else if((rv = su_cs_starts_with_case("remove", *argv))){
      if(argv[1] == NIL || argv[2] != NIL)
         goto jesyn;
      ++argv;
   }

   /* Back to normal flow */
   if((self = mx_ignore_by_name(*argv)) == NIL){
jectx:
      n_err(_("headerpick: invalid context: %s\n"), *argv);
      rv = su_EX_ERR;
      goto jleave;
   }
   ++argv;

   /* Was it an "assign", "join" or "remove" request? */
   if(rv != 0){
      if(rv > 0){
         if((self = a_ignore_resolve_self(self, FAL0)) != NIL)
            mx_ignore_del(self);
         rv = su_EX_OK;
      }else
         rv = a_ignore_assijoin(xself, self, (rv != -1))
               ? su_EX_OK : su_EX_ERR;
      goto jleave;
   }

   /* With only <context>, show all settings of it */
   if(*argv == NIL){
      rv |= !a_ignore_addcmd_mux(self, argv, TRU1);
      rv |= !a_ignore_addcmd_mux(self, argv, FAL0);
      goto jleave;
   }

   if(su_cs_starts_with_case("retain", *argv))
      retain = TRU1;
   else if(su_cs_starts_with_case("ignore", *argv))
      retain = FAL0;
   else{
      n_err(_("headerpick: invalid type (retain|ignore): %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   /* With only <context> and <type>, show that setting only */
   if(*argv == NIL){
      rv = !a_ignore_addcmd_mux(self, argv, retain);
      goto jleave;
   }

   rv = !a_ignore_addcmd_mux(self, argv, retain);

jleave:
   NYD_OU;
   return rv;
jesyn:
   mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("headerpick"), NIL);
   rv = su_EX_ERR;
   goto jleave;
}

int
c_unheaderpick(void *vp){
   boole retain;
   struct mx_ignore *self;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;
   argv = vp;

   if((self = mx_ignore_by_name(*argv)) == NIL){
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

   rv = !a_ignore_delcmd_mux(self, argv, retain);

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
mx_ignore_new(char const *name, boole auto_cleanup){
   struct mx_ignore *self;
   NYD_IN;

   self = a_ignore_new(name,
         (auto_cleanup ? a_IGNORE_CLEANUP : a_IGNORE_NONE));

   NYD_OU;
   return self;
}

void
mx_ignore_del(struct mx_ignore *self){
   NYD_IN;

   a_ignore_del_allof(self, TRU1);
   a_ignore_del_allof(self, FAL0);

   if(self->i_flags & a_IGNORE_BLTIN)
      a_ignore_bltin[self->i_flags & a_IGNORE_BLTIN_MASK] = NIL;
   else if(self == a_ignore_list)
      a_ignore_list = self->i_next;
   else{
      struct mx_ignore *x;

      for(x = a_ignore_list; x->i_next != self; x = x->i_next)
         ;
      x->i_next = self->i_next;
   }

   su_FREE(self);

   NYD_OU;
}

struct mx_ignore *
mx_ignore_by_name(char const *name){
   struct mx_ignore *self;
   struct a_ignore_bltin_map const *ibmp;
   NYD_IN;

   for(ibmp = &a_ignore_bltin_map[0];;)
      if(!su_cs_cmp_case(name, ibmp->ibm_name)){
         self = ibmp->ibm_ip;
         goto jleave;
      }else if(++ibmp == &a_ignore_bltin_map[NELEM(a_ignore_bltin_map)])
         break;

   for(self = a_ignore_list; self != NIL; self = self->i_next)
      if(!su_cs_cmp_case(name, self->i_name))
         break;

jleave:
   NYD2_OU;
   return self;
}

boole
mx_ignore_is_any(struct mx_ignore const *self){
   boole rv;
   NYD_IN;

   self = a_ignore_resolve_self(UNCONST(struct mx_ignore*,self), FAL0);
   rv = (self != NIL &&
         (self->i_retain.it_count != 0 || self->i_retain.it_all ||
          self->i_ignore.it_count != 0 || self->i_ignore.it_all));

   NYD_OU;
   return rv;
}

boole
mx_ignore_insert(struct mx_ignore *self, boole retain, char const *dat){
   struct a_ignore_type *itp;
   uz len;
   boole rv, isre;
   NYD_IN;

   self = a_ignore_resolve_self(self, TRU1);

   retain = !!retain; /* Make true bool, TRUM1 has special _lookup meaning */

   rv = FAL0;
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
   isre = FAL0;
#ifdef mx_HAVE_REGEX
   if(!(isre = n_re_could_be_one_buf(dat, len)))
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
   if(a_ignore_lookup(self, retain, dat, len, isre) == (retain ? TRU1 : TRUM1))
      goto jleave;

   itp = retain ? &self->i_retain : &self->i_ignore;

   if(itp->it_count == U32_MAX){
      n_err(_("Header selection size limit reached, cannot insert: %.*s\n"),
         S(int,MIN(len, S32_MAX)), dat);
      rv = FAL0;
      goto jleave;
   }

   rv = a_ignore_insert(itp, dat, len, isre);

jleave:
   NYD_OU;
   return rv;
}

boole
mx_ignore_lookup(struct mx_ignore const *self, char const *dat){
   boole rv;
   NYD_IN;

   if(self == mx_IGNORE_ALL)
      rv = TRUM1;
   else if(*dat == '\0' ||
         (self = a_ignore_resolve_self(UNCONST(struct mx_ignore*,self), FAL0)
            ) == NIL)
      rv = FAL0;
   else if(self->i_retain.it_all)
      rv = TRU1;
   else if(self->i_retain.it_count == 0 && self->i_ignore.it_all)
      rv = TRUM1;
   else{
      uz l;

      l = su_cs_len(dat);

      rv = a_ignore_lookup(self, TRUM1, dat, l, TRUM1);
   }

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_IGNORE
/* s-it-mode */
