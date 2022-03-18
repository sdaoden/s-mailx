/*@ Shared implementation of associative map-alike containers.
 *@ Include once, define a_TYPE correspondingly, include again.
 *@ XXX "Unroll" more.
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#ifndef a_ASSOC_MAP_H
# define a_ASSOC_MAP_H

# undef a_TYPE_CSDICT
# undef a_TYPE
# undef a_TYPE_IS_DICT
# undef a_TYPE_NAME
# undef a_T
# undef a_T_F
# undef a_T_PRINAME
# undef a_T_PUBNAME
# undef a_T_PRISYM
# undef a_T_PUBSYM
# undef a_FUN
# undef a_TK
# undef a_N
# undef a_N_F
# undef a_N_ALLOC
# undef a_N_FREE
# undef a_V
# undef a_V_F
# undef a_V_PRINAME
# undef a_V_PUBNAME
# undef a_V_PRISYM
# undef a_V_PUBSYM
# undef a_LA
# undef a_LA_F

# define a_TYPE_CSDICT 1
#else
# undef a_ASSOC_MAP_H

# if a_TYPE == a_TYPE_CSDICT
#  define a_TYPE a_TYPE_CSDICT
#  define a_TYPE_IS_DICT 1
#  define a_TYPE_NAME "su_cs_dict"
#  define a_T su_cs_dict
#  define a_T_F(X) su_CONCAT(csd_, X)
#  define a_T_PRINAME(X) su_CONCAT(su__CS_DICT_, X)
#  define a_T_PUBNAME(X) su_CONCAT(su_CS_DICT_, X)
#  define a_T_PRISYM(X) su_CONCAT(su__cs_dict_, X)
#  define a_T_PUBSYM(X) su_CONCAT(su_cs_dict_, X)
#  define a_FUN(X) su_CONCAT(a_csdict_, X)
#  define a_TK char
#  define a_N su__cs_dict_node
#  define a_N_F(X) su_CONCAT(csdn_, X)
#  define a_N_ALLOC(SELF,KLEN,FLAGS) \
   S(struct a_N*,su_ALLOCATE((VSTRUCT_SIZEOF(struct a_N, a_N_F(key)) +\
            (KLEN) +1), 1, ((FLAGS) & su_STATE_ERR_MASK)))
#  define a_N_FREE(SELF,NP) su_FREE(NP)
#  define a_V su_cs_dict_view
#  define a_V_F(X) su_CONCAT(csdv_, X)
#  define a_V_PRINAME(X) su_CONCAT(su__CS_DICT_VIEW_, X)
#  define a_V_PUBNAME(X) su_CONCAT(su_CS_DICT_VIEW_, X)
#  define a_V_PRISYM(X) su_CONCAT(su__cs_dict_view_, X)
#  define a_V_PUBSYM(X) su_CONCAT(su_cs_dict_view_, X)
#  define a_LA a_csdict_lookarg
#  define a_LA_F(X) su_CONCAT(csdla_, X)
# else /* a_TYPE == a_TYPE_CSDICT */
#  error Unknown a_TYPE
# endif

struct a_LA{
   struct a_N **la_slot;
   struct a_N *la_last; /* Only with AUTO_RESORT */
   u32 la_slotidx;
   u32 la_klen; /* Only if a_TYPE_IS_DICT */
   uz la_khash;
};

/* force: ignore FROZEN state.  Return -1 if we did resize, successfully */
SINLINE s32 a_FUN(check_resize)(struct a_T *self, boole force, u32 xcount);
static s32 a_FUN(resize)(struct a_T *self, u32 xcount);

/* */
static s32 a_FUN(assign)(struct a_T *self, struct a_T const *t, boole flags);

/* */
static s32 a_FUN(node_new)(struct a_T *self, struct a_N **res,
      struct a_LA *lap, a_TK const *key, void *value);

/* */
static s32 a_FUN(replace)(struct a_T *self, struct a_N *np, void *value);
static struct a_T *a_FUN(remove)(struct a_T *self, struct a_N *np,
      struct a_LA *lap);

SINLINE s32
a_FUN(check_resize)(struct a_T *self, boole force, u32 xcount){
   /* Simple approach: shift a 64-bit value, do not care for overflow */
   s32 rv;
   u64 s;
   NYD2_IN;

   rv = su_ERR_NONE;

   if(force || !(self->a_T_F(flags) & a_T_PUBNAME(FROZEN))){
      s = self->a_T_F(size);

      if(xcount >= (s << self->a_T_F(tshift)) ||
            (xcount < (s >>= 1) &&
             (self->a_T_F(flags) & a_T_PUBNAME(AUTO_SHRINK))))
         rv = a_FUN(resize)(self, xcount);
   }else
      ASSERT(xcount == 0 || self->a_T_F(size) > 0);

   NYD2_OU;
   return rv;
}

static s32
a_FUN(resize)(struct a_T *self, u32 xcount){
   s32 rv;
   struct a_N **narr, **arr;
   boole pow2;
   u32 size, nsize;
   NYD_IN;

   size = self->a_T_F(size);
   pow2 = ((self->a_T_F(flags) & a_T_PUBNAME(POW2_SPACED)) != 0);

   /* Calculate new size */
   /* C99 */{
      u32 onsize;
      boole grow;

      /* >= to catch initial 0 size */
      grow = ((xcount >> self->a_T_F(tshift)) >= size);
      nsize = size;

      for(;;){
         onsize = nsize;

         if(pow2){
            ASSERT(nsize == 0 || nsize == 1 || IS_POW2(nsize));
            if(grow){
               if(nsize == 0)
                  nsize = 1u << 1;
               else if(UCMP(32, nsize, <, S32_MIN))
                  nsize <<= 1;
            }else if(nsize > (1u << 1))
               nsize >>= 1;
         }else
            nsize = grow ? su_prime_lookup_next(nsize)
                  : su_prime_lookup_former(nsize);

         /* Refuse to excess storage bounds, but do not fail for this: simply
          * keep on going and place more nodes in the slots.
          * Because of the numbers we are talking about this is a theoretical
          * issue (at the time of this writing). */
         if(nsize == onsize)
            break;
         if(grow){
            /* (Again, shift 64-bit to avoid overflow) */
            if((S(u64,nsize) << self->a_T_F(tshift)) >= xcount)
               break;
         }else if((nsize >> 1) <= xcount)
            break;
      }

      if(size == nsize){
         rv = su_ERR_NONE;
         goto jleave;
      }
   }

   /* Try to allocate new array, give up on failure */
   narr = su_TCALLOCF(struct a_N*, nsize,
         (self->a_T_F(flags) & su_STATE_ERR_MASK));
   if(UNLIKELY(narr == NIL)){
      rv = su_err_no();
      goto jleave;
   }

   /* We will succeed: move pointers over */
   self->a_T_F(size) = nsize;

   arr = self->a_T_F(array);
   self->a_T_F(array) = narr;

   if((xcount = self->a_T_F(count)) > 0){
      struct a_N *np, **npos, *xnp;

      do for(np = arr[--size]; np != NIL; --xcount){
         uz i;

         i = np->a_N_F(khash);
         i= pow2 ? i & (nsize - 1) : i % nsize;
         npos = &narr[i];
         xnp = np;
         np = np->a_N_F(next);
         xnp->a_N_F(next) = *npos;
         *npos = xnp;
      }while(xcount > 0 && size > 0);
   }

   if(arr != NIL)
      su_FREE(arr);

   rv = -1;
jleave:
   NYD_OU;
   return rv;
}

static s32
a_FUN(assign)(struct a_T *self, struct a_T const *t, boole flags){
   s32 rv;
   NYD_IN;
   ASSERT(self);

   /* Clear old elements first if there are any, and if necessary */
   rv = su_ERR_NONE;
jerr:
   if(self->a_T_F(count) > 0){
      self = a_T_PUBSYM(clear_elems)(self);
      self->a_T_F(count) = 0;
   }

   ASSERT_NYD_EXEC(t != NIL, rv = su_ERR_FAULT);

   if(flags){
      self->a_T_F(flags) = t->a_T_F(flags);
      self->a_T_F(tbox) = t->a_T_F(tbox);
   }

   if(rv != 0)
      goto jleave;

   if(t->a_T_F(count) == 0)
      goto jleave;

   /* We actually ignore a resize failure if we do have some backing store to
    * put elements into! */
   rv = a_FUN(check_resize)(self, TRU1, t->a_T_F(count));
   if(rv > su_ERR_NONE && self->a_T_F(array) == NIL)
      goto jleave;

   /* C99 */{
      struct a_LA la;
      boole pow2;
      u32 size, tsize, tcnt;
      struct a_N **arr, **tarr, *np, *tnp;

      arr = self->a_T_F(array);
      tarr = t->a_T_F(array);
      size = self->a_T_F(size);
      tsize = t->a_T_F(size);
      tcnt = t->a_T_F(count);
      pow2 = ((self->a_T_F(flags) & a_T_PUBNAME(POW2_SPACED)) != 0);

      while(tcnt != 0){
         for(tnp = tarr[--tsize]; tnp != NIL; --tcnt, tnp = tnp->a_N_F(next)){
            uz i;

            la.la_khash = i = tnp->a_N_F(khash);
            la.la_slotidx = S(u32,i = pow2 ? i & (size - 1) : i % size);
            la.la_slot = &arr[i];
# if a_TYPE_IS_DICT
            la.la_klen = tnp->a_N_F(klen);
# endif
            rv = a_FUN(node_new)(self, &np, &la, tnp->a_N_F(key),
                  tnp->a_N_F(data));
            if(UNLIKELY(rv != 0))
               goto jerr;
         }
      }
   }

   ASSERT(rv == su_ERR_NONE);
jleave:
   NYD_OU;
   return rv;
}

static s32
a_FUN(node_new)(struct a_T *self, struct a_N **res, struct a_LA *lap,
      a_TK const *key, void *value){
   s32 rv;
   u16 flags;
   void *xvalue;
   struct a_N *np;
   NYD_IN;

   np = NIL;
   xvalue = NIL;
   flags = self->a_T_F(flags);

   if(flags & a_T_PUBNAME(OWNS)){
      if(value != NIL){
         xvalue = value = (*self->a_T_F(tbox)->tb_clone)(value,
               (flags & su_STATE_ERR_MASK));
         if(UNLIKELY(xvalue == NIL) && !(flags & a_T_PUBNAME(NILISVALO))){
            rv = su_err_no();
            goto jleave;
         }
      }else
         ASSERT(flags & a_T_PUBNAME(NILISVALO));
   }

   /* ..so create a new node */
   np = a_N_ALLOC(self, lap->la_klen, flags);
   if(UNLIKELY(np == NIL)){
      if(xvalue != NIL)
         (*self->a_T_F(tbox)->tb_delete)(xvalue);
      rv = su_ERR_NOMEM; /* (Cannot be overflow error) */
      goto jleave;
   }
   ++self->a_T_F(count);

   np->a_N_F(next) = *lap->la_slot;
   *lap->la_slot = np;
   np->a_N_F(data) = value;
   np->a_N_F(khash) = lap->la_khash;
   np->a_N_F(klen) = lap->la_klen;

# if a_TYPE == a_TYPE_CSDICT
   su_mem_copy(np->a_N_F(key), key, lap->la_klen +1);
   if(flags & a_T_PUBNAME(CASE)){
      a_TK *nckey;

      for(nckey = np->a_N_F(key); *nckey != '\0'; ++nckey)
         *nckey = su_cs_to_lower(*nckey);
   }
# else
#  error
# endif

   rv = su_ERR_NONE;
jleave:
   *res = np;
   NYD_OU;
   return rv;
}

static s32
a_FUN(replace)(struct a_T *self, struct a_N *np, void *value){
   u16 flags;
   s32 rv;
   NYD_IN;

   rv = su_ERR_NONE;
   flags = self->a_T_F(flags);

   if(flags & a_T_PUBNAME(OWNS)){
      void *ndat;

      ndat = np->a_N_F(data);

      if(LIKELY(value != NIL)){
         if((flags & a_T_PUBNAME(NILISVALO)) && ndat != NIL){
            value = (*self->a_T_F(tbox)->tb_assign)(ndat,
                  value, (flags & su_STATE_ERR_MASK));
            if(LIKELY(value != NIL))
               ndat = NIL;
            else
               rv = su_err_no();
         }else{
            value = (*self->a_T_F(tbox)->tb_clone)(value,
                  (flags & su_STATE_ERR_MASK));
            if(UNLIKELY(value == NIL)){
               rv = su_err_no();
               ndat = NIL;
            }
         }
      }else
         ASSERT(flags & a_T_PUBNAME(NILISVALO));

      if(ndat != NIL)
         (*self->a_T_F(tbox)->tb_delete)(ndat);
   }

   np->a_N_F(data) = value;

   NYD_OU;
   return rv;
}

static struct a_T *
a_FUN(remove)(struct a_T *self, struct a_N *np, struct a_LA *lap){
   NYD_IN;

   --self->a_T_F(count);

   if(np->a_N_F(data) != NIL && (self->a_T_F(flags) & a_T_PUBNAME(OWNS)))
      (*self->a_T_F(tbox)->tb_delete)(np->a_N_F(data));
   if(lap->la_last != NIL)
      lap->la_last->a_N_F(next) = np->a_N_F(next);
   else
      *lap->la_slot = np->a_N_F(next);
   a_N_FREE(self, np);

   NYD_OU;
   return self;
}

struct a_N *
a_T_PRISYM(lookup)(struct a_T const *self, a_TK const *key,
      void *lookarg_or_nil){
# if !a_TYPE_IS_DICT
   boole const key_is = (key != NIL);
# else
   u32 klen;
# endif
   u32 cnt, slotidx;
   uz khash;
   struct a_N *rv, **arr, *last;
   NYD_IN;
   ASSERT(self);

   rv = NIL;

# if a_TYPE == a_TYPE_CSDICT
   khash = su_cs_len(key);
   klen = S(u32,khash);
   if(khash != klen)
      goto jleave;
   khash = ((self->a_T_F(flags) & a_T_PUBNAME(CASE)) ? su_cs_hash_case_cbuf
         : su_cs_hash_cbuf)(key, klen);
# else
#  error
# endif

   if(LIKELY((slotidx = self->a_T_F(size)) > 0))
      slotidx = (self->a_T_F(flags) & a_T_PUBNAME(POW2_SPACED))
            ? khash & (slotidx - 1) : khash % slotidx;
   arr = self->a_T_F(array) + slotidx;

   if(lookarg_or_nil != NIL){
      struct a_LA *lap;

      lap = S(struct a_LA*,lookarg_or_nil);
      lap->la_slot = arr;
      lap->la_last = NIL;
      lap->la_slotidx = slotidx;
# if a_TYPE_IS_DICT
      lap->la_klen = klen;
# endif
      lap->la_khash = khash;
   }

   if(UNLIKELY((cnt = self->a_T_F(count)) == 0))
      goto jleave;

   for(last = rv, rv = *arr; rv != NIL; last = rv, rv = rv->a_N_F(next)){
      if(khash != rv->a_N_F(khash))
         continue;

# if a_TYPE == a_TYPE_CSDICT
      if(klen != rv->a_N_F(klen))
         continue;
      if(((self->a_T_F(flags) & a_T_PUBNAME(CASE))
            ? R(sz(*)(void const*,void const*,uz),&su_cs_cmp_case_n)
            : &su_mem_cmp)(key, rv->a_N_F(key), klen))
         continue;
# else
#  error
# endif

      /* Match! */
      if(last != NIL){
         if(self->a_T_F(flags) & a_T_PUBNAME(HEAD_RESORT)){
            last->a_N_F(next) = rv->a_N_F(next);
            rv->a_N_F(next) = *arr;
            *arr = rv;
         }else if(lookarg_or_nil != NIL)
            S(struct a_LA*,lookarg_or_nil)->la_last = last;
      }
      break;
   }

jleave:
   NYD_OU;
   return rv;
}

s32
a_T_PRISYM(insrep)(struct a_T *self, a_TK const *key, void *value,
      up replace_and_view_or_nil){
   struct a_LA la;
   struct a_N *np;
   s32 rv;
   struct a_V *viewp;
   u16 flags;
   NYD_IN;
   ASSERT(self);

   flags = self->a_T_F(flags);
   viewp = R(struct a_V*,replace_and_view_or_nil & ~TRU1);

   /* Ensure this basic precondition */
   if(value == NIL &&
         (flags & a_T_PUBNAME(OWNS)) && !(flags & a_T_PUBNAME(NILISVALO))){
      rv = su_ERR_INVAL;
      goto jleave;
   }

   /* But on error we will put new node in case we are empty, so create some
    * array space right away */
   if(UNLIKELY(self->a_T_F(size) == 0) &&
         (rv = a_FUN(resize)(self, 1)) > su_ERR_NONE)
      goto jleave;

   /* Try to find a yet existing key */
   np = a_T_PRISYM(lookup)(self, key, &la);

# if a_TYPE == a_TYPE_CSDICT
   /* (Ensure documented maximum key length first) */
   if(UNLIKELY(UCMP(z, la.la_klen, >, S32_MAX))){
      rv = su_state_err(su_STATE_ERR_OVERFLOW, (flags & su_STATE_ERR_MASK),
            _(a_TYPE_NAME ": insertion: key length excess"));
      goto jleave;
   }
# endif

   /* Is it an insertion of something new? */
   if(LIKELY(np == NIL)){
      if(UNLIKELY((rv = a_FUN(node_new)(self, &np, &la, key, value)
            ) != su_ERR_NONE))
         goto jleave;
      /* Never grow array storage if no other node is in this slot.
       * And do not fail if a resize fails at this point, it would only be
       * expensive and of no value, especially as it seems the user is
       * ignoring ENOMEM++ */
      if(UNLIKELY(np->a_N_F(next) != NIL)){
         /* If we did resize and we have to know the node location, it seems
          * easiest and most efficient to simply perform another lookup */
         if(a_FUN(check_resize)(self, FAL0, self->a_T_F(count)
               ) < su_ERR_NONE && viewp != NIL)
            np = a_T_PRISYM(lookup)(self, key, &la);
      }
   }else{
      if(LIKELY(replace_and_view_or_nil & TRU1) &&
            ((rv = a_FUN(replace)(self, np, value)) != su_ERR_NONE))
         goto jleave;
      rv = -1;
   }

jleave:
   if(UNLIKELY(viewp != NIL)){
      if(LIKELY(rv <= su_ERR_NONE)){
         viewp->a_V_F(node) = *la.la_slot;
         viewp->a_V_F(index) = la.la_slotidx;
      }else
         viewp->a_V_F(node) = NIL;
   }

   NYD_OU;
   return rv;
}

#if DVLOR(1, 0)
void
a_T_PRISYM(stats)(struct a_T const *self){
   ul size, empties, multies, maxper, i, j;
   struct a_N **arr, *np;
   NYD_IN;
   ASSERT(self);

   su_log_lock();
   su_log_write(su_LOG_INFO,
      "----------\n>>> " a_TYPE_NAME "(%p): statistics\n",
      self);

   arr = self->a_T_F(array);
   size = self->a_T_F(size);
   empties = multies = maxper = 0;

   /* First pass: overall stats */
   for(i = 0; i < size; ++i){
      j = 0;
      for(np = arr[i]; np != NIL; np = np->a_N_F(next))
         ++j;
      if(j == 0)
         ++empties;
      else{
         if(j > 1)
            ++multies;
         maxper = MAX(maxper, j);
      }
   }
   j = (multies != 0) ? multies : size - empties;
   if(j != 0)
      j = self->a_T_F(count) / j;

   su_log_write(su_LOG_INFO,
      "* Overview\n"
      "  - POW2_SPACED=%d "
# if a_TYPE != a_TYPE_CSDICT
         "OWNS_KEYS=%d "
# endif
# if a_TYPE == a_TYPE_CSDICT
         "CASE=%d "
# endif
         "OWNS=%d "
         "\n"
      "  - HEAD_RESORT=%d AUTO_SHRINK=%d FROZEN=%d ERR_PASS=%d NILISVALO=%d\n"
      "  - Array size : %lu\n"
      "  - Elements   : %lu\n"
      "  - Tresh-Shift: %u\n"
      "* Distribution\n"
      "  - Slots: empty: %lu, multiple: %lu, maximum: %lu, avg/multi: ~%lu\n"
      "* Distribution, visual\n"
      ,
      ((self->a_T_F(flags) & a_T_PUBNAME(POW2_SPACED)) != 0),
# if a_TYPE != a_TYPE_CSDICT
         ((self->a_T_F(flags) & a_T_PUBNAME(OWNS_KEYS)) != 0),
# endif
# if a_TYPE == a_TYPE_CSDICT
         ((self->a_T_F(flags) & a_T_PUBNAME(CASE)) != 0),
# endif
         ((self->a_T_F(flags) & a_T_PUBNAME(OWNS)) != 0),
      ((self->a_T_F(flags) & a_T_PUBNAME(HEAD_RESORT)) != 0),
         ((self->a_T_F(flags) & a_T_PUBNAME(AUTO_SHRINK)) != 0),
         ((self->a_T_F(flags) & a_T_PUBNAME(FROZEN)) != 0),
         ((self->a_T_F(flags) & a_T_PUBNAME(ERR_PASS)) != 0),
         ((self->a_T_F(flags) & a_T_PUBNAME(NILISVALO)) != 0),
      (ul)self->a_T_F(size),
      (ul)self->a_T_F(count),
      (ui)self->a_T_F(tshift),
      (ul)empties, (ul)multies, (ul)maxper, (ul)j
   );

   /* Second pass: visual distribution */
   for(i = 0; i < size; ++i){
      char buf[50], *cp;

      cp = buf;
      for(j = 0, np = arr[i]; np != NIL; np = np->a_N_F(next)){
         if(++j >= sizeof(buf) - 1 -1){
            *cp++ = '@';
            break;
         }
         *cp++ = '*';
      }
      *cp = '\0';

      su_log_write(su_LOG_INFO, "%5lu |%s\n", (ul)i, buf);
   }

   su_log_write(su_LOG_INFO,
      "<<<" a_TYPE_NAME "(%p): statistics\n----------\n",
      self);
   su_log_unlock();
   NYD_OU;
}
#endif /* DVLOR(1, 0) */

struct a_T *
a_T_PUBSYM(create)(struct a_T *self, u16 flags,
      struct su_toolbox const *tbox_or_nil){
   NYD_IN;
   ASSERT(self);

   su_mem_set(self, 0, sizeof *self);

   ASSERT_NYD(!(flags & a_T_PUBNAME(OWNS)) ||
      (tbox_or_nil != NIL && tbox_or_nil->tb_clone != NIL &&
       tbox_or_nil->tb_delete != NIL && tbox_or_nil->tb_assign != NIL));

   self->a_T_F(flags) = (flags &= a_T_PRINAME(CREATE_MASK));
   self->a_T_F(tshift) = 2;
   self->a_T_F(tbox) = tbox_or_nil;
   NYD_OU;
   return self;
}

SHADOW struct a_T *
a_T_PUBSYM(create_copy)(struct a_T *self, struct a_T const *t){
   NYD_IN;
   ASSERT(self);

   su_mem_set(self, 0, sizeof *self);

   ASSERT_NYD(t != NIL);
   (void)a_FUN(assign)(self, t, TRU1);

   NYD_OU;
   return self;
}

void
a_T_PUBSYM(gut)(struct a_T *self){ /* XXX inline */
   NYD_IN;
   ASSERT(self);

   if(self->a_T_F(array) != NIL)
      self = a_T_PUBSYM(clear)(self);
   NYD_OU;
}

s32
a_T_PUBSYM(assign)(struct a_T *self, struct a_T const *t){ /* XXX inline */
   s32 rv;
   NYD_IN;
   ASSERT(self);

   rv = a_FUN(assign)(self, t, TRU1);
   NYD_OU;
   return rv;
}

s32
a_T_PUBSYM(assign_elems)(struct a_T *self, struct a_T const *t){/* XXX inline*/
   s32 rv;
   NYD_IN;
   ASSERT(self);

   rv = a_FUN(assign)(self, t, FAL0);
   NYD_OU;
   return rv;
}

struct a_T *
a_T_PUBSYM(clear)(struct a_T *self){
   NYD_IN;
   ASSERT(self);

   if(self->a_T_F(array) != NIL){
      if(self->a_T_F(count) > 0)
         self = a_T_PUBSYM(clear_elems)(self);

      su_FREE(self->a_T_F(array));
      self->a_T_F(array) = NIL;
      self->a_T_F(size) = 0;
   }

   NYD_OU;
   return self;
}

struct a_T *
a_T_PUBSYM(clear_elems)(struct a_T *self){
   void *vp;
   struct a_N **arr, *np, *tmp;
   su_delete_fun delfun;
   u32 cnt, size;
   NYD_IN;
   ASSERT(self);

   cnt = self->a_T_F(count);
   self->a_T_F(count) = 0;
   size = self->a_T_F(size);
   delfun = (self->a_T_F(flags) & a_T_PUBNAME(OWNS))
         ? self->a_T_F(tbox)->tb_delete : S(su_delete_fun,NIL);
   arr = self->a_T_F(array);

   while(cnt > 0){
      ASSERT(size != 0);
      if((np = arr[--size]) != NIL){
         arr[size] = NIL;

         do{
            tmp = np;
            np = np->a_N_F(next);
            --cnt;

            if(delfun != NIL && (vp = tmp->a_N_F(data)) != NIL)
               (*delfun)(vp);
            a_N_FREE(self, tmp);
         }while(np != NIL);
      }
   }

   NYD_OU;
   return self;
}

struct a_T *
a_T_PUBSYM(swap)(struct a_T *self, struct a_T *t){
   struct a_T tmp;
   NYD_IN;
   ASSERT(self);
   ASSERT(t != NIL);

   tmp = *self;
   *self = *t;
   *t = tmp;

   NYD_OU;
   return self;
}

struct a_T *
a_T_PUBSYM(balance)(struct a_T *self){
   NYD_IN;
   ASSERT(self);

   self->a_T_F(flags) &= ~a_T_PUBNAME(FROZEN);
   (void)a_FUN(check_resize)(self, TRU1, self->a_T_F(count));

   NYD_OU;
   return self;
}

boole
a_T_PUBSYM(remove)(struct a_T *self, a_TK const *key){
   struct a_LA la;
   struct a_N *np;
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD_EXEC(key != NIL, np = NIL);

   np = a_T_PRISYM(lookup)(self, key, &la);
   if(LIKELY(np != NIL))
      self = a_FUN(remove)(self, np, &la);

   NYD_OU;
   return (np != NIL);
}

struct a_V *
a_V_PRISYM(move)(struct a_V *self, u8 type){
   u32 size, idx;
   struct a_N **arr, *np;
   struct a_T *parent;
   NYD_IN;
   ASSERT(self);

   parent = self->a_V_F(parent);
   arr = parent->a_T_F(array);
   size = parent->a_T_F(size);

   if(type == a_V_PRINAME(MOVE_BEGIN)){
      for(np = NIL, idx = 0; idx < size; ++idx)
         if((np = arr[idx]) != NIL)
            break;
      goto jset;
   }else{
      idx = self->a_V_F(index);
      if(UNLIKELY((np = self->a_V_F(node)->a_N_F(next)) == NIL)){
         while(++idx < size)
            if((np = arr[idx]) != NIL)
               break;
      }

      if(LIKELY(type != a_V_PRINAME(MOVE_HAS_NEXT))){
jset:
         if((self->a_V_F(node) = np) != NIL){
            self->a_V_F(index) = idx;
            self->a_V_F(next_node) = NIL;
         }
      }else{
         self->a_V_F(next_index) = idx;
         self->a_V_F(next_node) = np;
      }
   }

   NYD_OU;
   return self;
}

s32
a_V_PUBSYM(set_data)(struct a_V *self, void *value){
   s32 rv;
   struct a_T *parent;
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD_EXEC(a_V_PUBSYM(is_valid)(self), rv = su_ERR_INVAL);

   parent = self->a_V_F(parent);

   if(value == NIL){
      u16 flags;

      flags = parent->a_T_F(flags);

      if((flags & a_T_PUBNAME(OWNS)) && !(flags & a_T_PUBNAME(NILISVALO))){
         rv = su_ERR_INVAL;
         goto jleave;
      }
   }

   rv = a_FUN(replace)(parent, self->a_V_F(node), value);
jleave:
   NYD_OU;
   return rv;
}

boole
a_V_PUBSYM(find)(struct a_V *self, a_TK const *key){
   struct a_LA la;
   struct a_N *np;
   NYD_IN;
   ASSERT(self);

   self->a_V_F(node) =
   np = a_T_PRISYM(lookup)(self->a_V_F(parent), key, &la);
   if(np != NIL){
      self->a_V_F(index) = la.la_slotidx;
      self->a_V_F(next_node) = NIL;
   }

   NYD_OU;
   return (np != NIL);
}

struct a_V *
a_V_PUBSYM(remove)(struct a_V *self){
   struct a_LA la;
   struct a_N *np, *mynp;
   u32 idx;
   struct a_T *parent;
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD(a_V_PUBSYM(is_valid)(self));

   parent = self->a_V_F(parent);

   /* Setup the look arg for our position */
   np = *(la.la_slot = &parent->a_T_F(array)[idx = self->a_V_F(index)]);
   mynp = self->a_V_F(node);
   if(np == mynp)
      la.la_last = NIL;
   else{
      while(np->a_N_F(next) != mynp)
         np = np->a_N_F(next);
      la.la_last = np;
   }

   /* Remove our position */
   np = mynp;
   mynp = mynp->a_N_F(next);
   parent = a_FUN(remove)(parent, np, &la);

   /* Move to the next valid position, if any */
   if(mynp != NIL)
      self->a_V_F(node) = mynp;
   else{
      while(++idx < parent->a_T_F(size))
         if((mynp = parent->a_T_F(array)[idx]) != NIL)
            break;
      self->a_V_F(node) = mynp;
      self->a_V_F(index) = idx;
   }
   self->a_V_F(next_node) = NIL;

   NYD_OU;
   return self;
}

# undef a_TYPE_CSDICT
# undef a_TYPE
# undef a_TYPE_IS_DICT
# undef a_TYPE_NAME
# undef a_T
# undef a_T_F
# undef a_T_PRINAME
# undef a_T_PUBNAME
# undef a_T_PUBSYM
# undef a_T_PRISYM
# undef a_FUN
# undef a_TK
# undef a_N
# undef a_N_F
# undef a_N_ALLOC
# undef a_N_FREE
# undef a_V
# undef a_V_F
# undef a_V_PRINAME
# undef a_V_PUBNAME
# undef a_V_PRISYM
# undef a_V_PUBSYM
# undef a_LA
# undef a_LA_F
#endif /* a_ASSOC_MAP_H */

/* s-it-mode */
