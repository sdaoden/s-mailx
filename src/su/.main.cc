// Very primitive C++ compile and run instantiator for SU C++ wrappers.

// Sometimes we need a loop limit, e.g., when putting elements in containers
#define a_LOOP_NO 1000

// Call funs which produce statistical output
#define a_STATS(X) //X

// Memory trace on program exit?
//#define a_TRACE

#include <su/code.h>

su_USECASE_MX_DISABLED

#include <su/atomic.h>
#include <su/boswap.h>
#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/prime.h>
#include <su/re.h>
#include <su/sort.h>
#include <su/time.h>
#include <su/utf.h>

//#define NYDPROF_ENABLE
//#define NYD_ENABLE
//#define NYD2_ENABLE
#include <su/code-in.h>
NSPC_USE(su)

#define a_ERR() \
      do {log::write(log::alert, "%u\n", __LINE__); ++a_errors;} while(0)

static uz a_errors;

static void a_abc(void);
static void a_atomic(void);
static void a_boswap(void);
//static void a_cs(void); FIXME
static void a_cs_dict(void);
   static void a__cs_dict(u32 addflags);
   static void a__cs_dict_case(cs_dict<char const*> *cdp, char const *k[3]);
static void a_icodec(void);
static void a_mem_bag(void);
static void a_prime(void);
static void a_re(void);
static void a_sort(void);
static void a_time(void);
static void a_utf(void);

int main(void){ // {{{
   state::create("SU@C++", state::debug | log::debug, state::err_nopass);

   if(log::get_show_level())
      a_ERR();
   log::set_show_level(TRU1);
   if(!log::get_show_level())
      a_ERR();

   if(log::get_show_pid())
      a_ERR();
   log::set_show_pid(TRU1);
   if(!log::get_show_pid())
      a_ERR();

   log::write(log::info, "Redemption songs\n");

   a_abc();

   /// MT

   a_atomic();

   /// Basics (isolated)

   a_boswap();
   a_prime();
   a_time();
   a_utf();

   /// Basics (building upon other basics)

   a_icodec();
   a_mem_bag();
   a_re();
   a_sort();

   /// Extended

   a_cs_dict();

#ifdef a_TRACE
   mem::trace();
#endif
   log::write(log::info, (a_errors == 0 ? "These songs of freedom\n"
      : "Not to be heard\n"));
   return (a_errors != 0);
} // }}}

// abc {{{
static void
a_abc(void){
   // su_CC_MEM_ZERO, su_FIELD_RANGE_COPY, su_FIELD_RANGE_ZERO
   struct bla{u32 i1; u32 i2; u64 i3; u32 i4; u32 i5;} a, b;

   a.i1 = a.i2 = a.i3 = a.i4 = a.i5 = 0x00B1C2D3;
   b = a;
   STRUCT_ZERO(bla, &a);
   if(a.i1 != 0 || a.i2 != 0 || a.i3 != 0 || a.i4 != 0 || a.i5 != 0)
      a_ERR();

   mem::copy(FIELD_RANGE_COPY(bla, &a, &b, i2, i4));
   if(a.i1 != 0 || a.i2 != 0x00B1C2D3 || a.i3 != 0x00B1C2D3 ||
         a.i4 != 0x00B1C2D3 || a.i5 != 0)
      a_ERR();
   FIELD_RANGE_ZERO(bla, &a, i2, i3);
   if(a.i1 != 0 || a.i2 != 0 || a.i3 != 0 || a.i4 != 0x00B1C2D3 || a.i5 != 0)
      a_ERR();

   mem::copy(FIELD_RANGE_COPY(bla, &a, &b, i1, i5));
   if(a.i1 != 0x00B1C2D3 || a.i2 != 0x00B1C2D3 || a.i3 != 0x00B1C2D3 ||
         a.i4 != 0x00B1C2D3 || a.i5 != 0x00B1C2D3)
      a_ERR();
   FIELD_RANGE_ZERO(bla, &a, i1, i5);
   if(a.i1 != 0 || a.i2 != 0 || a.i3 != 0 || a.i4 != 0 || a.i5 != 0)
      a_ERR();
}
// }}}

// atomic {{{
static void
a_atomic(void){
#define a_X(X,Y,Z) do{\
   CONCAT(u,X) ov, nv;\
   CONCAT(u,X) ATOMIC store;\
\
   store = ov = Y;\
   nv = Z;\
   if(CONCAT(atomic::xchg_,X)(&store, nv) != ov)\
      a_ERR();\
   if(store != nv)\
      a_ERR();\
\
   if(CONCAT(atomic::busy_xchg_,X)(&store, ov) != nv)\
      a_ERR();\
   if(store != ov)\
      a_ERR();\
\
   if(CONCAT(atomic::cas_,X)(&store, nv, ov))\
      a_ERR();\
   if(!CONCAT(atomic::cas_,X)(&store, ov, nv))\
      a_ERR();\
   if(store != nv)\
      a_ERR();\
\
   CONCAT(atomic::busy_cas_,X)(&store, nv, ov);\
   if(store != ov)\
      a_ERR();\
}while(0);

   a_X(8, 0xA8, 0x9A)
   a_X(16, 0xA8B7, 0x9AA1)
   a_X(32, 0xA8B7C6D5, 0x9AA1B2D3)
   a_X(64, U64_C(0xA8B7C6D5E4F3FEFD), U64_C(0x9AA1B2D3AFFEDEAD))
   a_X(z, su_6432(U64_C(0xA8B7C6D5E4F3FEFD),0xA8B7C6D5),
      su_6432(U64_C(0x9AA1B2D3AFFEDEAD), 0x9AA1B2D3));
   a_X(p, su_6432(U64_C(0xA8B7C6D5E4F3FEFD),0xA8B7C6D5),
      su_6432(U64_C(0x9AA1B2D3AFFEDEAD), 0x9AA1B2D3));

#undef a_X
}
// }}}

// boswap {{{
static void
a_boswap(void){
#define a_X(X,V1,V2) do{\
   CONCAT(u,X) v1, v2;\
\
   v1 = V1;\
   v2 = V2;\
\
   if(CONCAT(boswap::swap_,X)(v1) != v2)\
      a_ERR();\
   if(CONCAT(boswap::swap_,X)(v2) != v1)\
      a_ERR();\
   if(su_BOM_IS_LITTLE()){\
      if(CONCAT(boswap::big_,X)(v1) != v2)\
         a_ERR();\
      if(CONCAT(boswap::big_,X)(v2) != v1)\
         a_ERR();\
      if(CONCAT(boswap::little_,X)(v1) != v1)\
         a_ERR();\
      if(CONCAT(boswap::little_,X)(v2) != v2)\
         a_ERR();\
\
      if(CONCAT(boswap::net_,X)(v1) != v2)\
         a_ERR();\
      if(CONCAT(boswap::net_,X)(v2) != v1)\
         a_ERR();\
   }else{\
      if(CONCAT(boswap::big_,X)(v1) != v1)\
         a_ERR();\
      if(CONCAT(boswap::big_,X)(v2) != v2)\
         a_ERR();\
      if(CONCAT(boswap::little_,X)(v1) != v2)\
         a_ERR();\
      if(CONCAT(boswap::little_,X)(v2) != v1)\
         a_ERR();\
\
      if(CONCAT(boswap::net_,X)(v1) != v1)\
         a_ERR();\
      if(CONCAT(boswap::net_,X)(v2) != v2)\
         a_ERR();\
   }\
}while(0);

   a_X(16, 0xA054, 0x54A0)
   a_X(32, 0xA0C19876, 0x7698C1A0)
   a_X(64, U64_C(0xA0C19876B2D34321), U64_C(0x2143D3B27698C1A0))
   a_X(z, su_6432(0xA0C19876B2D34321,0xA0C19876),
      su_6432(0x2143D3B27698C1A0,0x7698C1A0))

#undef a_X
}
// }}}

// cs_dict {{{
static void
a_cs_dict(void){
   a__cs_dict(cs_dict<char const*>::f_none);
   a__cs_dict(cs_dict<char const*>::f_pow2_spaced);
}

static void
a__cs_dict(u32 addflags){
   {
      cs_dict<char const*> cd(NIL, addflags);
      char const *k[3];

      k[0] = "k1";
      k[1] = "k2";
      k[2] = "k3";

      a__cs_dict_case(&cd, k);

      if(cd.is_empty())
         a_ERR();

      cs_dict<char const*> cd2(cd);
      if(cd2.count() != cd.count())
         a_ERR();
      for(cs_dict<char const*>::view cdv(cd2); cdv; ++cdv)
         if(!cd.has_key(cdv.key()))
            a_ERR();
         else if(cdv.data() != cd.lookup(cdv.key()))
            a_ERR();

      cd2 = cd;
      if(cd2.count() != cd.count())
         a_ERR();
      for(cs_dict<char const*>::view cdv(cd2); cdv; ++cdv)
         if(!cd.has_key(cdv.key()))
            a_ERR();
         else if(cdv.data() != cd.lookup(cdv.key()))
            a_ERR();
   }
   {
      cs_dict<char const*,TRU1> cd(auto_type_toolbox<char const*
         >::get_instance(), addflags);

      cs_dict<char const*,TRU1>::view cdv(cd);
      if(cdv.reset_insert("K1", "V1"))
         a_ERR();
      else if(cdv.reset_insert("K2", "V2"))
         a_ERR();
      else if(cdv.reset_insert("K3", "V3"))
         a_ERR();
      else{
         if(cd.is_empty())
            a_ERR();

         cs_dict<char const*,TRU1> cd2(cd);
         if(cd2.count() != cd.count())
            a_ERR();
         for(cs_dict<char const*,TRU1>::view cdv(cd2); cdv; ++cdv)
            if(!cd.has_key(cdv.key()))
               a_ERR();
            else if(cs::cmp(cdv.data(), cd.lookup(cdv.key())))
               a_ERR();
            else if(cdv.data() == cd.lookup(cdv.key()))
               a_ERR();

         cd2 = cd;
         if(cd2.count() != cd.count())
            a_ERR();
         for(cs_dict<char const*,TRU1>::view cdv(cd2); cdv; ++cdv)
            if(!cd.has_key(cdv.key()))
               a_ERR();
            else if(cs::cmp(cdv.data(), cd.lookup(cdv.key())))
               a_ERR();
            else if(cdv.data() == cd.lookup(cdv.key()))
               a_ERR();
      }
   }

   // Now for the real thing

   {
      cs_dict<char const*> cd(NIL, cd.f_case | addflags);
      char const *k[3];

      k[0] = "K1";
      k[1] = "K2";
      k[2] = "K3";

      a__cs_dict_case(&cd, k);
   }

   /// Let's do some flag stuff and "big data"
   u32 u32;

   char buf[ienc::buffer_size], *cp;

   cs_dict<NSPC(su)up,FAL0> cdu(NIL, addflags);
   cs_dict<char*,TRU1> cdo(auto_type_toolbox<char*>::get_instance());

   cdo.set_treshold_shift(4).add_flags(cdo.f_head_resort | addflags);

   for(u32 = 0; u32++ < a_LOOP_NO;){
      if((cp = ienc::convert(buf, u32)) == NIL){
         a_ERR();
         break;
      }
      if(cdu.insert(cp, R(NSPC(su)up*,u32)) != 0)
         a_ERR();
      if(cdo.insert(cp, cp) != 0)
         a_ERR();
   }
   if(cdu.count() != a_LOOP_NO)
      a_ERR();
   if(cdo.count() != a_LOOP_NO)
      a_ERR();

   // (value really duped?)
   u32 = 0;
   for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++u32, ++cdov){
      if(cs::cmp(cdov.key(), cdov.data()))
         a_ERR();
      if(!cdu.has_key(cdov.key()))
         a_ERR();
      else if((cp = ienc::convert(buf, R(NSPC(su)up,cdu.lookup(cdov.key())))
            ) == NIL)
         a_ERR();
      else if(cs::cmp(cdov.key(), cp))
         a_ERR();
   }
   if(u32 != a_LOOP_NO)
      a_ERR();

   a_STATS( cdo.statistics(); )

   cdo.clear().add_flags(cdu.f_frozen);
   for(u32 = 0; u32++ < a_LOOP_NO;){
      if((cp = ienc::convert(buf, u32)) == NIL){
         a_ERR();
         break;
      }
      if(cdo.insert(cp, cp) != 0)
         a_ERR();
   }
   if(cdo.count() != a_LOOP_NO)
      a_ERR();

   u32 = 0;
   for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++u32, ++cdov)
      if(cs::cmp(cdov.key(), cdov.data()))
         a_ERR();
   if(u32 != a_LOOP_NO)
      a_ERR();

   a_STATS( cdo.statistics(); )

   if(cdo.set_treshold_shift(2).balance().count() != a_LOOP_NO)
      a_ERR();

   a_STATS( cdo.statistics(); )

   u32 = 0;
   for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++u32, ++cdov)
      if(cs::cmp(cdov.key(), cdov.data()))
         a_ERR();
   if(u32 != a_LOOP_NO)
      a_ERR();

   {
      cs_dict<char*,TRU1> cdo2(cdo);
      if(cdo2.count() != cdo.count())
         a_ERR();
   }
   {
      static type_toolbox<char*> const xtb = su_TYPE_TOOLBOX_I9R(
            (type_toolbox<char*>::clone_fun)0x1,
            (type_toolbox<char*>::delete_fun)0x2,
            (type_toolbox<char*>::assign_fun)0x3,
            NIL, NIL);
      typedef cs_dict<char*,TRU1> csd;

      csd *cdo2 = su_NEW(csd)(&xtb);
      if(cdo2->assign(cdo) != 0)
         a_ERR();
      if(cdo2->count() != cdo.count())
         a_ERR();
      su_DEL(cdo2);
   }
   {
      cs_dict<char*,TRU1> cdo2(auto_type_toolbox<char*>::get_instance());
      if(cdo2.assign_elems(cdo) != 0)
         a_ERR();
      if(cdo2.count() != cdo.count())
         a_ERR();
   }
}

static void
a__cs_dict_case(cs_dict<char const*> *cdp, char const *k[3]){
   // basics
   if(!cdp->is_empty())
      a_ERR();
   if(cdp->toolbox() != NIL)
      a_ERR();

   s32 s32 = cdp->insert(k[0], "v1");
   if(s32 != 0)
      a_ERR();
      s32 = cdp->insert(k[0], "v1-no");
      if(s32 != -1)
         a_ERR();
      s32 = cdp->replace("k1", "v1-yes");
      if(s32 != -1)
         a_ERR();
   s32 = cdp->insert(k[1], "v2");
   if(s32 != 0)
      a_ERR();
   s32 = cdp->insert(k[2], "v3");
   if(s32 != 0)
      a_ERR();

   if(cdp->count() != 3)
      a_ERR();
   if(cdp->is_empty())
      a_ERR();

   if(!cdp->remove(k[1]))
      a_ERR();

   if(!cdp->has_key(k[0]))
      a_ERR();
      char const *ccp = cdp->lookup(k[0]);
      if(ccp == NIL)
         a_ERR();
      else if(cs::cmp(ccp, "v1-yes"))
         a_ERR();
   if(cdp->has_key(k[1]))
      a_ERR();
      ccp = cdp->lookup("k2");
      if(ccp != NIL)
         a_ERR();
   if(!cdp->has_key(k[2]))
      a_ERR();
      ccp = cdp->lookup("k3");
      if(ccp == NIL)
         a_ERR();
      else if(cs::cmp(ccp, "v3"))
         a_ERR();

   {
      cs_dict<char const*> cd2(*cdp);

      if(cd2.count() != 2)
         a_ERR();
      if(!cdp->clear_elems().is_empty())
         a_ERR();

      if(!cd2.has_key(k[0]))
         a_ERR();
         ccp = cd2.lookup("k1");
         if(ccp == NIL)
            a_ERR();
         else if(cs::cmp(ccp, "v1-yes"))
            a_ERR();

      if(!cd2.has_key(k[2]))
         a_ERR();
         ccp = cd2.lookup("k3");
         if(ccp == NIL)
            a_ERR();
         else if(cs::cmp(ccp, "v3"))
            a_ERR();

      if(cdp->assign_elems(cd2) != 0)
         a_ERR();
   }

   s32 = cdp->insert(k[1], "v2");
   if(s32 != 0)
      a_ERR();
      ccp = cdp->lookup("k2");
      if(ccp == NIL)
         a_ERR();
      else if(cs::cmp(ccp, "v2"))
         a_ERR();

   // view
   cs_dict<char const*>::view cdv(*cdp), cdv2(cdv);
   u32 u32;
   for(u32 = 0; cdv; ++u32, ++cdv2, ++cdv){
      char const *xk, *v;

      if(!cs::cmp(cdv.key(), xk = "k1"))
         v = "v1-yes";
      else if(!cs::cmp(cdv.key(), xk = "k2"))
         v = "v2";
      else if(!cs::cmp(cdv.key(), xk = "k3"))
         v = "v3";
      else{
         a_ERR();
         continue;
      }
      if(cs::cmp(cdv.key(), cdv2.key()))
         a_ERR();

      if(!cdp->has_key(xk))
         a_ERR();

      if(cs::cmp(v, cdv.data()))
         a_ERR();
      if(cs::cmp(cdv.data(), cdv2.data()))
         a_ERR();
   }
   if(cdv2)
      a_ERR();
   if(u32 != 3)
      a_ERR();

   if(!cdv.find(k[1]))
      a_ERR();
   if(cdv.remove().find("k2"))
      a_ERR();

   for(u32 = 0, cdv.begin(); cdv.is_valid(); ++u32, ++cdv){
      char const *xk, *v;

      if(!cs::cmp(cdv.key(), xk = "k1"))
         v = "v1-yes";
      else if(!cs::cmp(cdv.key(), xk = "k3"))
         v = "v3";
      else{
         a_ERR();
         continue;
      }
      if(!cdp->has_key(xk))
         a_ERR();

      if(cs::cmp(v, cdv.data()))
         a_ERR();

      if(!cdv.has_next() && u32 < 1)
         a_ERR();
   }
   if(u32 != 2)
      a_ERR();

   if(!cdv.find(k[2]))
      a_ERR();
   if(cdv.set_data("v3-newnewnew") != 0)
      a_ERR();
   if(cdp->count() != 2)
      a_ERR();
   if(cs::cmp(cdv.data(), "v3-newnewnew"))
      a_ERR();
   if(cs::cmp(*cdv, "v3-newnewnew"))
      a_ERR();

   /* View insertion */
   if(cdv.reset_insert("vk1", "vv1"))
      a_ERR();
   if(!cdv.is_valid())
      a_ERR();
   else{
      if(cdp->count() != 3)
         a_ERR();
      if(cs::cmp(cdv.key(), "vk1"))
         a_ERR();
      if(cs::cmp(cdv.data(), "vv1"))
         a_ERR();
   }
   if(cdv.reset_insert("vk1", "vv2") != -1)
      a_ERR();
   if(!cdv.is_valid())
      a_ERR();
   else{
      if(cdp->count() != 3)
         a_ERR();
      if(cs::cmp(cdv.key(), "vk1"))
         a_ERR();
      if(cs::cmp(cdv.data(), "vv1"))
         a_ERR();
   }
   if(cdv.reset_replace("vk1", "vv2") != -1)
      a_ERR();
   if(!cdv.is_valid())
      a_ERR();
   else{
      if(cdp->count() != 3)
         a_ERR();
      if(cs::cmp(cdv.key(), "vk1"))
         a_ERR();
      if(cs::cmp(cdv.data(), "vv2"))
         a_ERR();
   }
   if(cdv.reset_replace("vk2", "vv3") != 0)
      a_ERR();
   if(!cdv.is_valid())
      a_ERR();
   else{
      if(cdp->count() != 4)
         a_ERR();
      if(cs::cmp(cdv.key(), "vk2"))
         a_ERR();
      if(cs::cmp(cdv.data(), "vv3"))
         a_ERR();
   }
   if(cdv.reset_replace("vk2", "vv4") != -1)
      a_ERR();
   if(!cdv.is_valid())
      a_ERR();
   else{
      if(cdp->count() != 4)
         a_ERR();
      if(cs::cmp(cdv.key(), "vk2"))
         a_ERR();
      if(cs::cmp(cdv.data(), "vv4"))
         a_ERR();
   }
}
// }}}

// icodec {{{
static void
a_icodec(void){
   char buf[ienc::buffer_size];

   u32 u32 = 0xAFFEDEADu;
   char const *ccp;
   if((ccp = ienc::convert(buf, u32)) == NIL)
      a_ERR();
   if(cs::cmp(ccp, "2952715949"))
      a_ERR();
   if((idec::convert(&u32, ccp, max::uz, 0, idec::mode_limit_32bit, &ccp
            ) & (idec::state_emask | idec::state_consumed)
         ) != idec::state_consumed)
      a_ERR();
   if(*ccp != '\0')
      a_ERR();
   if(u32 != 0xAFFEDEADu)
      a_ERR();
   if((ccp = ienc::convert(buf, u32, 0x10)) == NIL)
      a_ERR();
   else if(cs::cmp(ccp, "0xAFFEDEAD"))
      a_ERR();
   else if((idec::convert(&u32, ccp, max::uz, 0, idec::mode_limit_32bit, &ccp
            ) & (idec::state_emask | idec::state_consumed)
         ) != idec::state_consumed)
      a_ERR();
   else if(*ccp != '\0')
      a_ERR();
   if(u32 != 0xAFFEDEADu)
      a_ERR();

   u64 u64 = (S(NSPC(su)u64,u32) << 32) | 0xABBABEEF;
   if((ccp = ienc::convert(buf, u64)) == NIL)
      a_ERR();
   else if(cs::cmp(ccp, "12681818438213746415"))
      a_ERR();
   else if((idec::convert(&u64, ccp, max::uz, 0, idec::mode_none, &ccp
            ) & (idec::state_emask | idec::state_consumed)
         ) != idec::state_consumed)
      a_ERR();
   else if(*ccp != '\0')
      a_ERR();
   if(u64 != su_U64_C(0xAFFEDEADABBABEEF))
      a_ERR();
   if((ccp = ienc::convert(buf, u64, 0x10)) == NIL)
      a_ERR();
   else if(cs::cmp(ccp, "0xAFFEDEADABBABEEF"))
      a_ERR();
   else if((idec::convert(&u64, ccp, max::uz, 0, idec::mode_none, &ccp
            ) & (idec::state_emask | idec::state_consumed)
         ) != idec::state_consumed)
      a_ERR();
   else if(*ccp != '\0')
      a_ERR();
   if(u64 != su_U64_C(0xAFFEDEADABBABEEF))
      a_ERR();
}
// }}}

// mem_bag {{{
static void
a_mem_bag(void){ // TODO only instantiation test yet
#ifdef su_HAVE_MEM_BAG
   mem_bag *mb;

   mb = su_NEW(mem_bag);

# ifdef su_HAVE_MEM_BAG_AUTO
   mb->auto_allocate(10);
# endif

# ifdef su_HAVE_MEM_BAG_LOFI
   void *lvp = mb->lofi_allocate(10);

   mb->lofi_free(lvp);
# endif

   su_DEL(&mb->reset());
#endif // su_HAVE_MEM_BAG
}
// }}}

// prime {{{
static void
a_prime(void){
   u32 u32 = prime::lookup_next(0);
   if(u32 != prime::lookup_min)
      a_ERR();
   u64 u64 = prime::get_next(u32);
   if(u32 == u64 || (u32 == 2 && u64 != 3))
      a_ERR();

   u32 = prime::lookup_former(max::u32);
   if(u32 != prime::lookup_max)
      a_ERR();
   u64 = prime::get_former(u32 + 1);
   if(u32 != u64)
      a_ERR();
}
// }}}

// sort {{{
static void
a_sort(void){
   char const *arr_sorted[] = {
         "albert", "berta", "david", "emil",
         "friedrich", "gustav", "heinrich", "isidor"
   }, *arr_mixed[] = {
         "gustav", "david", "isidor", "friedrich",
         "berta", "albert", "heinrich", "emil"
   };

   sort::shell(arr_mixed, NELEM(arr_mixed), &cs::cmp);

   for(uz i = NELEM(arr_sorted); i-- != 0;)
      if(cs::cmp(arr_sorted[i], arr_mixed[i]))
         a_ERR();
}
// }}}

// re {{{
static void
a_re(void){
#ifdef su_HAVE_RE
   re re;

   if(re.setup("hello", re::setup_test_only) != re::error_none)
      a_ERR();
   else if(re.error() != re::error_none)
      a_ERR();
   else if(!re.is_setup())
      a_ERR();
   else if(!re.is_test_only())
      a_ERR();
   else if(re.group_count() != 0)
      a_ERR();
   else if(!re.eval("hello world"))
      a_ERR();
   else if(!re.eval_ok())
      a_ERR();
   else if(cs::cmp(re.input(), "hello world"))
      a_ERR();

   if(re.reset().reset().reset().setup("^hello", re::setup_icase))
      a_ERR();
   else if(re.error() != re::error_none)
      a_ERR();
   else if(!re.is_setup())
      a_ERR();
   else if(re.group_count() != 0)
      a_ERR();
   else if(!re.eval("Hello world"))
      a_ERR();
   else if(!re.eval_ok())
      a_ERR();
   else if(cs::cmp(re.input(), "Hello world"))
      a_ERR();

   if(re.setup("^hello[^[:alpha:]]*([[:alpha:]]+)$") != re.error_none)
      a_ERR();
   else if(re.error() != re::error_none)
      a_ERR();
   else if(!re.is_setup())
      a_ERR();
   else if(re.is_test_only())
      a_ERR();
   else if(re.group_count() != 1)
      a_ERR();
   else if(!re.eval("hello, world"))
      a_ERR();
   else if(!re.eval_ok())
      a_ERR();
   else if(re.match_at(1) == NIL)
      a_ERR();
   else if(!re.match_at(1)->is_valid())
      a_ERR();
   else if(re.match_at(1)->start() != 7)
      a_ERR();
   else if(re.match_at(1)->end() != 12)
      a_ERR();
   else if(re.match_at(0) == NIL)
      a_ERR();
   else if(re.match_at(0)->start() != 0)
      a_ERR();
   else if(re.match_at(0)->end() != 12)
      a_ERR();
   else if(cs::cmp(re.input(), "hello, world"))
      a_ERR();
#endif /* su_HAVE_RE */
}
// }}}

// time {{{
static void a__time_utils(void);
static void a__time_spec(void);

static void
a_time(void){
   a__time_utils();
   a__time_spec();
}

static void
a__time_utils(void){
   u32 y, m, d, hour, min, sec;
   uz uz;
   s64 ep;

   if(time::epoch_max != S64_C(0x1D30BE2E1FF))
      a_ERR();
   if(time::min_secs != 60u) a_ERR();
   if(time::hour_mins != 60u) a_ERR();
   if(time::day_hours != 24u) a_ERR();
   if(time::day_secs != 24u * 60u * 60u) a_ERR();
   if(time::year_days != 365u) a_ERR();
   if(time::jdn_epoch != 2440588ul) a_ERR();

   if(time::weekday_sunday != 0 || time::weekday_monday != 1 ||
         time::weekday_tuesday != 2 || time::weekday_wednesday != 3 ||
         time::weekday_thursday != 4 || time::weekday_friday != 5 ||
         time::weekday_saturday != 6)
      a_ERR();
   for(d = 0; d <= 6; ++d){
      if(!time::weekday_is_valid(d))
         a_ERR();
      else if(time::weekday_name_abbrev(d) == NIL)
         a_ERR();
   }
   if(time::weekday_is_valid(d)) a_ERR();

   if(time::month_january != 0 || time::month_february != 1 ||
         time::month_march != 2 || time::month_april != 3 ||
         time::month_may != 4 || time::month_june != 5 ||
         time::month_july != 6 || time::month_august != 7 ||
         time::month_september != 8 || time::month_october != 9 ||
         time::month_november != 10 || time::month_december != 11)
      a_ERR();
   for(m = 0; m <= 11; ++m){
      if(!time::month_is_valid(m))
         a_ERR();
      else if(time::month_name_abbrev(m) == NIL)
         a_ERR();
   }
   if(time::month_is_valid(m)) a_ERR();

   //
   y = m = d = hour = min = sec = max::u32;
      if(time::epoch_to_gregor(-1, &y, &m, &d, &hour, &min, &sec)) a_ERR();
      if(y != 0 || m != 0 || d != 0 || hour != 0 || min != 0 || sec != 0)
         a_ERR();
   y = m = d = hour = min = sec = max::u32;
      if(time::epoch_to_gregor(time::epoch_max + 1, &y, &m, &d,
            &hour, &min, &sec))
         a_ERR();
      if(y != 0 || m != 0 || d != 0 || hour != 0 || min != 0 || sec != 0)
         a_ERR();

   y = m = d = hour = min = sec = max::u32;
      if(!time::epoch_to_gregor(time::epoch_max, &y, &m, &d,
            &hour, &min, &sec))
         a_ERR();
      if(y != 65535 || m != 12 || d != 31 ||
            hour != 23 || min != 59 || sec != 59)
         a_ERR();
   y = m = d = max::u32;
      if(!time::epoch_to_gregor(0, &y, &m, &d)) a_ERR();
      if(y != 1970 || m != 1 || d != 1) a_ERR();
   y = m = d = hour = min = sec = max::u32;
      if(!time::epoch_to_gregor(0, &y, &m, &d, &hour, &min, &sec)) a_ERR();
      if(y != 1970 || m != 1 || d != 1 || hour != 0 || min != 0 || sec != 0)
         a_ERR();
   y = m = d = hour = min = sec = max::u32;
      if(!time::epoch_to_gregor(S64_C(844221007), &y, &m, &d,
            &hour, &min, &sec))
         a_ERR();
      if(y != 1996 || m != 10 || d != 2 || hour != 1 || min != 50 || sec != 7)
         a_ERR();
   y = m = d = hour = min = sec = max::u32;
      if(!time::epoch_to_gregor(S64_C(2147483647), &y, &m, &d,
            &hour, &min, &sec))
         a_ERR();
      if(y != 2038 || m != 1 || d != 19 || hour != 3 || min != 14 || sec != 7)
         a_ERR();

   //
   ep = time::gregor_to_epoch(1969, 12, 31);
   if(ep != -1) a_ERR();
   ep = time::gregor_to_epoch(1969, 12, 31, 23, 59, 59);
   if(ep != -1) a_ERR();
   ep = time::gregor_to_epoch(1970, 1, 1);
   if(ep != 0) a_ERR();
   ep = time::gregor_to_epoch(1970, 1, 1, 0, 0, 0);
   if(ep != 0) a_ERR();
   ep = time::gregor_to_epoch(1970, 1, 1, 0, 1, 1);
   if(ep != 61) a_ERR();
   ep = time::gregor_to_epoch(1996, 10, 2, 1, 50, 7);
   if(ep != 844221007) a_ERR();

   //
   uz = time::gregor_to_jdn(1970, 1, 1);
   if(uz != time::jdn_epoch) a_ERR();
   uz = time::gregor_to_jdn(1970, 1, 2);
   if(uz != time::jdn_epoch + 1) a_ERR();
   uz = time::gregor_to_jdn(1969, 12, 31);
   if(uz != time::jdn_epoch - 1) a_ERR();

   //
   y = m = d = max::u32;
      time::jdn_to_gregor(time::jdn_epoch, &y, &m, &d);
      if(y != 1970 || m != 1 || d != 1) a_ERR();
   y = m = d = max::u32;
      time::jdn_to_gregor(time::jdn_epoch + 1, &y, &m, &d);
      if(y != 1970 || m != 1 || d != 2) a_ERR();
   y = m = d = max::u32;
      time::jdn_to_gregor(time::jdn_epoch - 1, &y, &m, &d);
      if(y != 1969 || m != 12 || d != 31) a_ERR();

   //
   if(!time::year_is_leap(2016)) a_ERR();
      if(time::year_is_leap(2017)) a_ERR();
      if(time::year_is_leap(2018)) a_ERR();
      if(time::year_is_leap(2019)) a_ERR();
      if(!time::year_is_leap(2020)) a_ERR();
   if(!time::year_is_leap(1500)) a_ERR();
      if(!time::year_is_leap(1600)) a_ERR();
      if(!time::year_is_leap(1700)) a_ERR();
      if(time::year_is_leap(1800)) a_ERR();
      if(time::year_is_leap(1900)) a_ERR();
      if(!time::year_is_leap(2000)) a_ERR();
      if(time::year_is_leap(2100)) a_ERR();
}

static void
a__time_spec(void){
   if(time::spec::sec_millis != 1000l) a_ERR();
   if(time::spec::sec_micros != 1000l * 1000) a_ERR();
   if(time::spec::sec_nanos != 1000l * 1000 * 1000) a_ERR();

   time::spec ts;

   if(!ts.current().is_valid())
      a_ERR();

   time::spec ts2(ts);
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec()) a_ERR();
   if(ts2.nano() != ts.nano()) a_ERR();
   if(ts.compare(ts2) != 0) a_ERR();
   if(ts2.compare(ts) != 0) a_ERR();
   if(!(ts2 == ts)) a_ERR();
   if(ts2 != ts) a_ERR();
   if(ts2 < ts) a_ERR();
   if(!(ts2 <= ts)) a_ERR();
   if(!(ts2 >= ts)) a_ERR();
   if(ts2 > ts) a_ERR();

   ts2.sec(ts2.sec() - 1);
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec() - 1) a_ERR();
   if(ts2.nano() != ts.nano()) a_ERR();
   if(ts.compare(ts2) <= 0) a_ERR();
   if(ts2.compare(ts) >= 0) a_ERR();
   if(ts2 == ts) a_ERR();
   if(!(ts2 != ts)) a_ERR();
   if(!(ts2 < ts)) a_ERR();
   if(!(ts2 <= ts)) a_ERR();
   if(ts2 >= ts) a_ERR();
   if(ts2 > ts) a_ERR();

   ts2.sec(ts2.sec() + 2);
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec() + 1) a_ERR();
   if(ts2.nano() != ts.nano()) a_ERR();
   if(ts.compare(ts2) >= 0) a_ERR();
   if(ts2.compare(ts) <= 0) a_ERR();
   if(ts2 == ts) a_ERR();
   if(!(ts2 != ts)) a_ERR();
   if(ts2 < ts) a_ERR();
   if(ts2 <= ts) a_ERR();
   if(!(ts2 >= ts)) a_ERR();
   if(!(ts2 > ts)) a_ERR();

   ts2 = ts;
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec()) a_ERR();
   if(ts2.nano() != ts.nano()) a_ERR();
   if(ts.compare(ts2) != 0) a_ERR();
   if(ts2.compare(ts) != 0) a_ERR();
   if(!(ts2 == ts)) a_ERR();
   if(ts2 != ts) a_ERR();
   if(ts2 < ts) a_ERR();
   if(!(ts2 <= ts)) a_ERR();
   if(!(ts2 >= ts)) a_ERR();
   if(ts2 > ts) a_ERR();

   if(ts.nano() >= ts.sec_nanos - 2)
      ts.nano(ts.nano() - 2);
   else if(ts.nano() <= 1)
      ts.nano(ts.nano() + 2);

   ts2.assign(ts).nano(ts2.nano() - 1);
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec()) a_ERR();
   if(ts2.nano() != ts.nano() - 1) a_ERR();
   if(ts.compare(ts2) <= 0) a_ERR();
   if(ts2.compare(ts) >= 0) a_ERR();
   if(ts2 == ts) a_ERR();
   if(!(ts2 != ts)) a_ERR();
   if(!(ts2 < ts)) a_ERR();
   if(!(ts2 <= ts)) a_ERR();
   if(ts2 >= ts) a_ERR();
   if(ts2 > ts) a_ERR();

   ts2.nano(ts2.nano() + 2);
   if(!ts2.is_valid()) a_ERR();
   if(ts2.sec() != ts.sec()) a_ERR();
   if(ts2.nano() != ts.nano() + 1) a_ERR();
   if(ts.compare(ts2) >= 0) a_ERR();
   if(ts2.compare(ts) <= 0) a_ERR();
   if(ts2 == ts) a_ERR();
   if(!(ts2 != ts)) a_ERR();
   if(ts2 < ts) a_ERR();
   if(ts2 <= ts) a_ERR();
   if(!(ts2 >= ts)) a_ERR();
   if(!(ts2 > ts)) a_ERR();
}
// }}}

// utf {{{
static void
a_utf(void){
   char buf[utf8::buffer_size];

   char const *ccp = utf8::replacer;
   uz i = sizeof(utf8::replacer) -1;
   u32 u32 = utf8::convert_to_32(&ccp, &i);
   if(u32 != utf32::replacer)
      a_ERR();
   if(i != 0 || *ccp != '\0')
      a_ERR();

   i = utf32::convert_to_8(u32, buf);
   if(i != 3 || buf[i] != '\0')
      a_ERR();
   if(cs::cmp(buf, utf8::replacer))
      a_ERR();
}
// }}}

#include <su/code-ou.h>
// s-it-mode
