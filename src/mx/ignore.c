/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `headerpick', `retain' and `ignore', and `un..' variants.
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE ignore

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

struct a_ignore_type{
   ui32_t it_count;     /* Entries in .it_ht (and .it_re) */
   bool_t it_all;       /* _All_ fields ought to be _type_ (ignore/retain) */
   ui8_t it__dummy[3];
   struct a_ignore_field{
      struct a_ignore_field *if_next;
      char if_field[n_VFIELD_SIZE(0)]; /* Header field */
   } *it_ht[3]; /* TODO make hashmap dynamic */
#ifdef HAVE_REGEX
   struct a_ignore_re{
      struct a_ignore_re *ir_next;
      regex_t ir_regex;
      char ir_input[n_VFIELD_SIZE(0)]; /* Regex input text (for showing it) */
   } *it_re, *it_re_tail;
#endif
};

struct n_ignore{
   struct a_ignore_type i_retain;
   struct a_ignore_type i_ignore;
   bool_t i_auto;       /* In auto-reclaimed, not heap memory */
   bool_t i_bltin;      /* Is a built-in n_IGNORE* type */
   ui8_t i_ibm_idx;     /* If .i_bltin: a_ignore_bltin_map[] idx */
   ui8_t i__dummy[5];
};

struct a_ignore_bltin_map{
   struct n_ignore *ibm_ip;
   char const ibm_name[8];
};

static struct a_ignore_bltin_map const a_ignore_bltin_map[] = {
   {n_IGNORE_TYPE, "type\0"},
   {n_IGNORE_SAVE, "save\0"},
   {n_IGNORE_FWD, "forward\0"},
   {n_IGNORE_TOP, "top\0"},

   {n_IGNORE_TYPE, "print\0"},
   {n_IGNORE_FWD, "fwd\0"}
};
#ifdef HAVE_DEVEL /* Avoid gcc warn cascade since n_ignore is defined locally */
n_CTAV(-n__IGNORE_TYPE - n__IGNORE_ADJUST == 0);
n_CTAV(-n__IGNORE_SAVE - n__IGNORE_ADJUST == 1);
n_CTAV(-n__IGNORE_FWD - n__IGNORE_ADJUST == 2);
n_CTAV(-n__IGNORE_TOP - n__IGNORE_ADJUST == 3);
n_CTAV(n__IGNORE_MAX == 3);
#endif

static struct n_ignore *a_ignore_bltin[n__IGNORE_MAX + 1];
/* Almost everyone uses `ignore'/`retain', put _TYPE in BSS */
static struct n_ignore a_ignore_type;

/* Return real self, which is xself unless that is one of the built-in specials,
 * in which case NULL is returned if nonexistent and docreate is false.
 * The other statics assume self has been resolved (unless noted) */
static struct n_ignore *a_ignore_resolve_self(struct n_ignore *xself,
                           bool_t docreate);

/* Lookup whether a mapping is contained: TRU1=retained, TRUM1=ignored.
 * If retain is _not_ TRUM1 then only the retained/ignored slot is inspected,
 * and regular expressions are not executed but instead their .ir_input is
 * text-compared against len bytes of dat.
 * Note it doesn't handle the .it_all "all fields" condition */
static bool_t a_ignore_lookup(struct n_ignore const *self, bool_t retain,
               char const *dat, size_t len);

/* Delete all retain( else ignor)ed members */
static void a_ignore_del_allof(struct n_ignore *ip, bool_t retain);

/* Try to map a string to one of the built-in types */
static struct a_ignore_bltin_map const *a_ignore_resolve_bltin(char const *cp);

/* Logic behind `headerpick T T' (a.k.a. `retain'+) */
static bool_t a_ignore_addcmd_mux(struct n_ignore *ip, char const **list,
               bool_t retain);

static void a_ignore__show(struct n_ignore const *ip, bool_t retain);
static int a_ignore__cmp(void const *l, void const *r);

/* Logic behind `unheaderpick T T' (a.k.a. `unretain'+) */
static bool_t a_ignore_delcmd_mux(struct n_ignore *ip, char const **list,
               bool_t retain);

static bool_t a_ignore__delone(struct n_ignore *ip, bool_t retain,
               char const *field);

static struct n_ignore *
a_ignore_resolve_self(struct n_ignore *xself, bool_t docreate){
   uintptr_t suip;
   struct n_ignore *self;
   NYD2_IN;

   self = xself;
   suip = -(uintptr_t)self - n__IGNORE_ADJUST;

   if(suip <= n__IGNORE_MAX){
      if((self = a_ignore_bltin[suip]) == NULL && docreate){
         if(xself == n_IGNORE_TYPE){
            self = &a_ignore_type;
            /* LIB: memset(self, 0, sizeof *self);*/
         }else
            self = n_ignore_new(FAL0);
         self->i_bltin = TRU1;
         self->i_ibm_idx = (ui8_t)suip;
         a_ignore_bltin[suip] = self;
      }
   }
   NYD2_OU;
   return self;
}

static bool_t
a_ignore_lookup(struct n_ignore const *self, bool_t retain,
      char const *dat, size_t len){
   bool_t rv;
#ifdef HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   ui32_t hi;
   NYD2_IN;

   if(len == UIZ_MAX)
      len = strlen(dat);
   hi = n_torek_ihashn(dat, len) % n_NELEM(self->i_retain.it_ht);

   /* Again: doesn't handle .it_all conditions! */
   /* (Inner functions would be nice, again) */
   if(retain && self->i_retain.it_count > 0){
      rv = TRU1;
      for(ifp = self->i_retain.it_ht[hi]; ifp != NULL; ifp = ifp->if_next)
         if(!ascncasecmp(ifp->if_field, dat, len))
            goto jleave;
#ifdef HAVE_REGEX
      if(dat[len - 1] != '\0')
         dat = savestrbuf(dat, len);
      for(irp = self->i_retain.it_re; irp != NULL; irp = irp->ir_next)
         if((retain == TRUM1
               ? (regexec(&irp->ir_regex, dat, 0,NULL, 0) != REG_NOMATCH)
               : !strncmp(irp->ir_input, dat, len)))
            goto jleave;
#endif
      rv = (retain == TRUM1) ? TRUM1 : FAL0;
   }else if((retain == TRUM1 || !retain) && self->i_ignore.it_count > 0){
      rv = TRUM1;
      for(ifp = self->i_ignore.it_ht[hi]; ifp != NULL; ifp = ifp->if_next)
         if(!ascncasecmp(ifp->if_field, dat, len))
            goto jleave;
#ifdef HAVE_REGEX
      if(dat[len - 1] != '\0')
         dat = savestrbuf(dat, len);
      for(irp = self->i_ignore.it_re; irp != NULL; irp = irp->ir_next)
         if((retain == TRUM1
               ? (regexec(&irp->ir_regex, dat, 0,NULL, 0) != REG_NOMATCH)
               : !strncmp(irp->ir_input, dat, len)))
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
a_ignore_del_allof(struct n_ignore *ip, bool_t retain){
#ifdef HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   struct a_ignore_type *itp;
   NYD2_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

   if(!ip->i_auto){
      size_t i;

      for(i = 0; i < n_NELEM(itp->it_ht); ++i)
         for(ifp = itp->it_ht[i]; ifp != NULL;){
            struct a_ignore_field *x;

            x = ifp;
            ifp = ifp->if_next;
            n_free(x);
         }
   }

#ifdef HAVE_REGEX
   for(irp = itp->it_re; irp != NULL;){
      struct a_ignore_re *x;

      x = irp;
      irp = irp->ir_next;
      regfree(&x->ir_regex);
      if(!ip->i_auto)
         n_free(x);
   }
#endif

   memset(itp, 0, sizeof *itp);
   NYD2_OU;
}

static struct a_ignore_bltin_map const *
a_ignore_resolve_bltin(char const *cp){
   struct a_ignore_bltin_map const *ibmp;
   NYD2_IN;

   for(ibmp = &a_ignore_bltin_map[0];;)
      if(!asccasecmp(cp, ibmp->ibm_name))
         break;
      else if(++ibmp == &a_ignore_bltin_map[n_NELEM(a_ignore_bltin_map)]){
         ibmp = NULL;
         break;
      }
   NYD2_OU;
   return ibmp;
}

static bool_t
a_ignore_addcmd_mux(struct n_ignore *ip, char const **list, bool_t retain){
   char const **ap;
   bool_t rv;
   NYD2_IN;

   ip = a_ignore_resolve_self(ip, rv = (*list != NULL));

   if(!rv){
      if(ip != NULL && ip->i_bltin)
         a_ignore__show(ip, retain);
      rv = TRU1;
   }else{
      for(ap = list; *ap != 0; ++ap)
         switch(n_ignore_insert_cp(ip, retain, *ap)){
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
a_ignore__show(struct n_ignore const *ip, bool_t retain){
#ifdef HAVE_REGEX
   struct a_ignore_re *irp;
#endif
   struct a_ignore_field *ifp;
   size_t i, sw;
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
   for(ap = ring, i = 0; i < n_NELEM(itp->it_ht); ++i)
      for(ifp = itp->it_ht[i]; ifp != NULL; ifp = ifp->if_next)
         *ap++ = ifp->if_field;
   *ap = NULL;

   qsort(ring, PTR2SIZE(ap - ring), sizeof *ring, &a_ignore__cmp);

   i = fprintf(n_stdout, "headerpick %s %s",
      a_ignore_bltin_map[ip->i_ibm_idx].ibm_name,
      (retain ? "retain" : "ignore"));
   sw = n_scrnwidth;

   for(ap = ring; *ap != NULL; ++ap){
      /* These fields are all ASCII, no visual width needed */
      size_t len;

      len = strlen(*ap) + 1;
      if(UICMP(z, len, >=, sw - i)){
         fputs(" \\\n ", n_stdout);
         i = 1;
      }
      i += len;
      putc(' ', n_stdout);
      fputs(*ap, n_stdout);
   }

   /* Regular expression in FIFO order */
#ifdef HAVE_REGEX
   for(irp = itp->it_re; irp != NULL; irp = irp->ir_next){
      size_t len;
      char const *cp;

      cp = n_shexp_quote_cp(irp->ir_input, FAL0);
      len = strlen(cp) + 1;
      if(UICMP(z, len, >=, sw - i)){
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

static int
a_ignore__cmp(void const *l, void const *r){
   int rv;

   rv = asccasecmp(*(char const * const *)l, *(char const * const *)r);
   return rv;
}

static bool_t
a_ignore_delcmd_mux(struct n_ignore *ip, char const **list, bool_t retain){
   char const *cp;
   struct a_ignore_type *itp;
   bool_t rv;
   NYD2_IN;

   ip = a_ignore_resolve_self(ip, rv = (*list != NULL));
   itp = retain ? &ip->i_retain : &ip->i_ignore;

   if(itp->it_count == 0 && !itp->it_all)
      n_err(_("No fields currently being %s\n"),
         (retain ? _("retained") : _("ignored")));
   else
      while((cp = *list++) != NULL)
         if(cp[0] == '*' && cp[1] == '\0')
            a_ignore_del_allof(ip, retain);
         else if(!a_ignore__delone(ip, retain, cp)){
            n_err(_("Field not %s: %s\n"),
               (retain ? _("retained") : _("ignored")), cp);
            rv = FAL0;
         }
   NYD2_OU;
   return rv;
}

static bool_t
a_ignore__delone(struct n_ignore *ip, bool_t retain, char const *field){
   struct a_ignore_type *itp;
   NYD_IN;

   itp = retain ? &ip->i_retain : &ip->i_ignore;

#ifdef HAVE_REGEX
   if(n_is_maybe_regex(field)){
      struct a_ignore_re **lirp, *irp;

      for(irp = *(lirp = &itp->it_re); irp != NULL;
            lirp = &irp->ir_next, irp = irp->ir_next)
         if(!strcmp(field, irp->ir_input)){
            *lirp = irp->ir_next;
            if(irp == itp->it_re_tail)
               itp->it_re_tail = irp->ir_next;

            regfree(&irp->ir_regex);
            if(!ip->i_auto)
               n_free(irp);
            --itp->it_count;
            goto jleave;
         }
   }else
#endif /* HAVE_REGEX */
   {
      struct a_ignore_field **ifpp, *ifp;
      ui32_t hi;

      hi = n_torek_ihashn(field, UIZ_MAX) % n_NELEM(itp->it_ht);

      for(ifp = *(ifpp = &itp->it_ht[hi]); ifp != NULL;
            ifpp = &ifp->if_next, ifp = ifp->if_next)
         if(!asccasecmp(ifp->if_field, field)){
            *ifpp = ifp->if_next;
            if(!ip->i_auto)
               n_free(ifp);
            --itp->it_count;
           goto jleave;
         }
   }

   ip = NULL;
jleave:
   NYD_OU;
   return (ip != NULL);
}

FL int
c_headerpick(void *vp){
   bool_t retain;
   struct a_ignore_bltin_map const *ibmp;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;
   argv = vp;

   /* Without arguments, show all settings of all contexts */
   if(*argv == NULL){
      rv = 0;
      for(ibmp = &a_ignore_bltin_map[0];
            ibmp <= &a_ignore_bltin_map[n__IGNORE_MAX]; ++ibmp){
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, TRU1);
         rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, FAL0);
      }
      goto jleave;
   }

   if((ibmp = a_ignore_resolve_bltin(*argv)) == NULL){
      n_err(_("`headerpick': invalid context: %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   /* With only <context>, show all settings of it */
   if(*argv == NULL){
      rv = 0;
      rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, TRU1);
      rv |= !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, FAL0);
      goto jleave;
   }

   if(is_asccaseprefix(*argv, "retain"))
      retain = TRU1;
   else if(is_asccaseprefix(*argv, "ignore"))
      retain = FAL0;
   else{
      n_err(_("`headerpick': invalid type (retain, ignore): %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   /* With only <context> and <type>, show its settings */
   if(*argv == NULL){
      rv = !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, retain);
      goto jleave;
   }

   rv = !a_ignore_addcmd_mux(ibmp->ibm_ip, argv, retain);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_unheaderpick(void *vp){
   bool_t retain;
   struct a_ignore_bltin_map const *ibmp;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;
   argv = vp;

   if((ibmp = a_ignore_resolve_bltin(*argv)) == NULL){
      n_err(_("`unheaderpick': invalid context: %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   if(is_asccaseprefix(*argv, "retain"))
      retain = TRU1;
   else if(is_asccaseprefix(*argv, "ignore"))
      retain = FAL0;
   else{
      n_err(_("`unheaderpick': invalid type (retain, ignore): %s\n"), *argv);
      goto jleave;
   }
   ++argv;

   rv = !a_ignore_delcmd_mux(ibmp->ibm_ip, argv, retain);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_retain(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_TYPE, vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_ignore(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_TYPE, vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_unretain(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_TYPE, vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_unignore(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_TYPE, vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_saveretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_SAVE, v, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_saveignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_SAVE, v, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_unsaveretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_SAVE, v, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_unsaveignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_SAVE, v, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_fwdretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_FWD, v, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_fwdignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_addcmd_mux(n_IGNORE_FWD, v, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_unfwdretain(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_FWD, v, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_unfwdignore(void *v){ /* TODO v15 drop */
   int rv;
   NYD_IN;

   rv = !a_ignore_delcmd_mux(n_IGNORE_FWD, v, FAL0);
   NYD_OU;
   return rv;
}

FL struct n_ignore *
n_ignore_new(bool_t isauto){
   struct n_ignore *self;
   NYD_IN;

   self = isauto ? n_autorec_calloc(1, sizeof *self) : n_calloc(1,sizeof *self);
   self->i_auto = isauto;
   NYD_OU;
   return self;
}

FL void
n_ignore_del(struct n_ignore *self){
   NYD_IN;
   a_ignore_del_allof(self, TRU1);
   a_ignore_del_allof(self, FAL0);
   if(!self->i_auto)
      n_free(self);
   NYD_OU;
}

FL bool_t
n_ignore_is_any(struct n_ignore const *self){
   bool_t rv;
   NYD_IN;

   self = a_ignore_resolve_self(n_UNCONST(self), FAL0);
   rv = (self != NULL &&
         (self->i_retain.it_count != 0 || self->i_retain.it_all ||
          self->i_ignore.it_count != 0 || self->i_ignore.it_all));
   NYD_OU;
   return rv;
}

FL bool_t
n_ignore_insert(struct n_ignore *self, bool_t retain,
      char const *dat, size_t len){
#ifdef HAVE_REGEX
   struct a_ignore_re *irp;
   bool_t isre;
#endif
   struct a_ignore_field *ifp;
   struct a_ignore_type *itp;
   bool_t rv;
   NYD_IN;

   retain = !!retain; /* Make it true bool, TRUM1 has special _lookup meaning */
   rv = FAL0;
   self = a_ignore_resolve_self(self, TRU1);

   if(len == UIZ_MAX)
      len = strlen(dat);

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
#ifdef HAVE_REGEX
   if(!(isre = n_is_maybe_regex_buf(dat, len)))
#endif
   {
      char c;
      size_t i;

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

   if(itp->it_count == UI32_MAX){
      n_err(_("Header selection size limit reached, cannot insert: %.*s\n"),
         (int)n_MIN(len, SI32_MAX), dat);
      rv = FAL0;
      goto jleave;
   }

   rv = TRU1;
#ifdef HAVE_REGEX
   if(isre){
      struct a_ignore_re *x;
      int s;
      size_t i;

      i = n_VSTRUCT_SIZEOF(struct a_ignore_re, ir_input) + ++len;
      irp = self->i_auto ? n_autorec_alloc(i) : n_alloc(i);
      memcpy(irp->ir_input, dat, --len);
      irp->ir_input[len] = '\0';

      if((s = regcomp(&irp->ir_regex, irp->ir_input,
            REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
         n_err(_("Invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(irp->ir_input, FAL0),
            n_regex_err_to_doc(NULL, s));
         if(!self->i_auto)
            n_free(irp);
         rv = FAL0;
         goto jleave;
      }

      irp->ir_next = NULL;
      if((x = itp->it_re_tail) != NULL)
         x->ir_next = irp;
      else
         itp->it_re = irp;
      itp->it_re_tail = irp;
   }else
#endif /* HAVE_REGEX */
   {
      ui32_t hi;
      size_t i;

      i = n_VSTRUCT_SIZEOF(struct a_ignore_field, if_field) + len + 1;
      ifp = self->i_auto ? n_autorec_alloc(i) : n_alloc(i);
      memcpy(ifp->if_field, dat, len);
      ifp->if_field[len] = '\0';
      hi = n_torek_ihashn(dat, len) % n_NELEM(itp->it_ht);
      ifp->if_next = itp->it_ht[hi];
      itp->it_ht[hi] = ifp;
   }
   ++itp->it_count;
jleave:
   NYD_OU;
   return rv;
}

FL bool_t
n_ignore_lookup(struct n_ignore const *self, char const *dat, size_t len){
   bool_t rv;
   NYD_IN;

   if(self == n_IGNORE_ALL)
      rv = TRUM1;
   else if(len == 0 ||
         (self = a_ignore_resolve_self(n_UNCONST(self), FAL0)) == NULL)
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

/* s-it-mode */
