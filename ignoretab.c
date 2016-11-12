/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `ignore' and `retain' lists of all sort.
 *@ XXX Should these be in nam_a_grp.c?!
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#undef n_FILE
#define n_FILE ignoretab

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Add a list of fields to be ignored or retained */
static bool_t a_ignoretab_insert(char const **list, struct ignoretab *itp,
               bool_t ignorret);

static void a_ignoretab__show(struct ignoretab *itp, bool_t ignorret);
static int a_ignoretab__cmp(void const *l, void const *r);

/* Delete a list of fields from an ignored or retained list) */
static bool_t a_ignoretab_del(char const **list, struct ignoretab *itp,
               bool_t ignorret);

static void a_ignoretab__delall(struct ignoretab *itp);
static bool_t a_ignoretab__del(struct ignoretab *itp, char const *name);

static bool_t
a_ignoretab_insert(char const **list, struct ignoretab *itp, bool_t ignorret){
   char const **ap;
   bool_t rv;
   NYD2_ENTER;

   rv = TRU1;

   if(*list == NULL)
      a_ignoretab__show(itp, ignorret);
   else{
      for(ap = list; *ap != 0; ++ap)
         switch(n_ignoretab_insert_cp(itp, *ap)){
         case FAL0:
            n_err(_("Invalid field name cannot be %s: %s\n"),
               (ignorret ? _("ignored") : _("retained")), *ap);
            rv = FAL0;
            break;
         case TRUM1:
            if(options & OPT_D_V)
               n_err(_("Field already %s: %s\n"),
                  (ignorret ? _("ignored") : _("retained")), *ap);
            /* FALLTHRU */
         case TRU1:
            break;
         }
   }
   NYD2_LEAVE;
   return rv;
}

static void
a_ignoretab__show(struct ignoretab *itp, bool_t ignorret){
   struct n_ignoretab_field *itfp;
   size_t i, sw;
   char const **ap, **ring;
   NYD2_ENTER;

   if(itp->it_count == 0){
      printf(_("No fields currently being %s\n"),
         (ignorret ? _("ignored") : _("retained")));
      goto jleave;
   }

   ring = salloc((itp->it_count +1) * sizeof *ring);
   for(ap = ring, i = 0; i < n_NELEM(itp->it_head); ++i)
      for(itfp = itp->it_head[i]; itfp != NULL; itfp = itfp->itf_next)
         *ap++ = itfp->itf_field;
   *ap = NULL;

   qsort(ring, itp->it_count, sizeof *ring, &a_ignoretab__cmp);

   for(sw = scrnwidth, i = 0, ap = ring; *ap != NULL; ++ap){
      /* These fields are all ASCII */
      size_t len;
      char const *pref;

      pref = "  ";
      len = strlen(*ap) + 2;
      if(UICMP(z, len, >=, sw - i)){
         putchar('\n');
         i = 2;
      }else if(i == 0)
         pref = n_empty;
      i += len;
      printf("%s%s", pref, *ap);
   }
   if(i > 0){
      putchar('\n');
      fflush(stdout);
   }
jleave:
   NYD2_LEAVE;
}

static int
a_ignoretab__cmp(void const *l, void const *r){
   int rv;

   rv = asccasecmp(*(char const * const *)l, *(char const * const *)r);
   return rv;
}

static bool_t
a_ignoretab_del(char const **list, struct ignoretab *itp, bool_t ignorret){
   char const *cp;
   bool_t rv;
   NYD2_ENTER;

   rv = TRU1;

   if(itp->it_count == 0)
      printf(_("No fields currently being %s\n"),
         (ignorret ? _("ignored") : _("retained")));
   else
      while((cp = *list++) != NULL)
         if(cp[0] == '*' && cp[1] == '\0')
            a_ignoretab__delall(itp);
         else if(!a_ignoretab__del(itp, cp)){
            n_err(_("Field not %s: %s\n"),
               (ignorret ? _("ignored") : _("retained")), cp);
            rv = FAL0;
         }
   NYD2_LEAVE;
   return rv;
}

static void
a_ignoretab__delall(struct ignoretab *itp){
   size_t i;
   struct n_ignoretab_field *itfp, *x;
   bool_t isauto;
   NYD2_ENTER;

   if(!(isauto = itp->it_auto))
      for(i = 0; i < n_NELEM(itp->it_head); ++i)
         for(itfp = itp->it_head[i]; itfp != NULL; itfp = x){
            x = itfp->itf_next;
            free(itfp);
         }

   memset(itp, 0, sizeof *itp);
   itp->it_auto = isauto;
   NYD2_LEAVE;
}

static bool_t
a_ignoretab__del(struct ignoretab *itp, char const *name){
   struct n_ignoretab_field **itfpp, *itfp;
   ui32_t h;
   NYD_ENTER;

   h = torek_ihashn(name, UIZ_MAX) % n_NELEM(itp->it_head);

   for(itfp = *(itfpp = &itp->it_head[h]); itfp != NULL;
         itfpp = &itfp->itf_next, itfp = itfp->itf_next)
      if(!asccasecmp(itfp->itf_field, name)){
         --itp->it_count;
         *itfpp = itfp->itf_next;
         if(!itp->it_auto)
            free(itfp);
         break;
      }
   NYD_LEAVE;
   return (itfp != NULL);
}

FL int
c_retfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &ignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_igfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &ignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_saveretfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &saveignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_saveigfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &saveignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_fwdretfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &fwdignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_fwdigfield(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_insert(v, &fwdignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_unignore(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &ignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_unretain(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &ignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_unsaveignore(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &saveignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_unsaveretain(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &saveignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_unfwdignore(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &fwdignore[0], TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_unfwdretain(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_ignoretab_del(v, &fwdignore[1], FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
is_ign(char const *field, size_t fieldlen, struct ignoretab igta[2]){
   bool_t b;
   int rv;
   NYD_ENTER;

   rv = 0;
   if(igta == NULL)
      goto jleave;
   rv = 1;
   if(igta == allignore)
      goto jleave;

   b = (igta[1].it_count > 0);
   rv = n_ignoretab_lookup((b ? &igta[1] : &igta[0]), field, fieldlen);
   if(b)
      rv = !rv;
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct ignoretab *
n_ignoretab_creat(struct ignoretab *self, bool_t isauto){
   NYD_ENTER;
   memset(self, 0, sizeof *self);
   self->it_auto = isauto;
   NYD_LEAVE;
   return self;
}

FL void
n_ignoretab_gut(struct ignoretab *self){
   NYD_ENTER;
   if(!self->it_auto && self->it_count > 0)
      a_ignoretab__delall(self);
   NYD_LEAVE;
}

FL bool_t
n_ignoretab_insert(struct ignoretab *self, char const *dat, size_t len){
   struct n_ignoretab_field *itfp;
   ui32_t h;
   bool_t rv;
   NYD_ENTER;

   /* Detect length as necessary, check for valid fieldname */
   rv = FAL0;
   if(len == UIZ_MAX){
      char c;

      for(len = 0; (c = dat[len]) != '\0'; ++len)
         if(!fieldnamechar(c))
            goto jleave;
   }else if(len == 0)
      goto jleave;
   else{
      char c;
      size_t i;

      for(i = 0; i < len; ++i){
         c = dat[i];
         if(!fieldnamechar(c))
            goto jleave;
      }
   }

   rv = TRUM1;
   if(n_ignoretab_lookup(self, dat, len))
      goto jleave;
   else if(self->it_count == UI32_MAX){
      n_err(_("Hashtable size limit reached.  Cannot insert: %.*s\n"),
         (int)n_MIN(len, SI32_MAX), dat);
      goto jleave;
   }

   ++len;
   /* C99 */{
      size_t i;

      i = sizeof(*itfp) - n_VFIELD_SIZEOF(struct n_ignoretab_field, itf_field
            ) + len;
      itfp = self->it_auto ? salloc(i) : smalloc(i);
   }
   --len;
   memcpy(itfp->itf_field, dat, len);
   itfp->itf_field[len] = '\0';
   h = torek_ihashn(dat, len) % n_NELEM(self->it_head);
   itfp->itf_next = self->it_head[h];
   self->it_head[h] = itfp;
   ++self->it_count;
   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_ignoretab_lookup(struct ignoretab *self, char const *dat, size_t len){
   struct n_ignoretab_field *itfp;
   NYD_ENTER;

   if(len == UIZ_MAX)
      len = strlen(dat);

   for(itfp = self->it_head[torek_ihashn(dat, len) % n_NELEM(self->it_head)];
         itfp != NULL; itfp = itfp->itf_next)
      if(!ascncasecmp(itfp->itf_field, dat, len))
         break;
   NYD_LEAVE;
   return (itfp != NULL);
}

/* s-it-mode */
