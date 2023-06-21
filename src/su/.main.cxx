// Very primitive C++ compile and run instantiator for SU C++ wrappers.

// Sometimes we need a loop limit, e.g., when putting elements in containers
// Be warned it becomes exponential, 42024 at max, maybe
#define a_LOOP_NO 4224

// Call funs which produce statistical output
#define a_STATS(X) //X

// Memory trace on program exit?
#define a_TRACE

#include <su/code.h>

su_USECASE_MX_DISABLED

#include <su/atomic.h>
#include <su/avopt.h>
#include <su/boswap.h>
#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/md.h>
# include <su/md-siphash.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>
#include <su/prime.h>
#include <su/random.h>
#include <su/re.h>
#include <su/sort.h>
#include <su/time.h>
#include <su/utf.h>

//#define NYDPROF_ENABLE
//#define NYD_ENABLE
//#define NYD2_ENABLE
#include <su/code-in.h>
NSPC_USE(su)

#define a_ERR() do {log::write(log::alert, "%u\n", __LINE__); ++a_errors;} while(0)

static uz a_errors;

static void a_abc(void);
static void a_atomic(void);
static void a_avopt(void);
static void a_boswap(void);
//static void a_cs(void); FIXME
static void a_cs_dict(void);
static void a_icodec(void);
static void a_md(void);
static void a_mem_bag(void);
static void a_path_cs(void);
static void a_prime(void);
static void a_random(void);
static void a_re(void);
static void a_sort(void);
static void a_time(void);
static void a_utf(void);

int
main(void){ // {{{
	state::create(state::create_all, "SU@C++", (S(u32,state::debug) | S(u32,log::debug)), state::err_nopass);

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

	log::write(log::info, "Redemption songs");

#if 1
	a_abc();

	/// MT

	a_atomic();

	/// Basics (isolated)

	a_avopt();
	a_boswap();
	a_md();
	a_prime();
	a_time();
	a_utf();

	/// Basics (building upon other basics)

	a_icodec();
	a_mem_bag();
	a_path_cs();
	a_random();
	a_re();
	a_sort();

	/// Extended

	a_cs_dict();

	///
#endif // tests

	a_STATS( mem::trace(); )

	log::write(log::info, (a_errors == 0 ? "These songs of freedom" : "Not to be heard"));

	state::gut(
		(a_errors == 0 ? state::gut_act_norm : state::gut_act_quick)
#ifdef a_TRACE
		| state::gut_mem_trace
#endif
	);

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
	if(a.i1 != 0 || a.i2 != 0x00B1C2D3 || a.i3 != 0x00B1C2D3 || a.i4 != 0x00B1C2D3 || a.i5 != 0)
		a_ERR();
	FIELD_RANGE_ZERO(bla, &a, i2, i3);
	if(a.i1 != 0 || a.i2 != 0 || a.i3 != 0 || a.i4 != 0x00B1C2D3 || a.i5 != 0)
		a_ERR();

	mem::copy(FIELD_RANGE_COPY(bla, &a, &b, i1, i5));
	if(a.i1 != 0x00B1C2D3 || a.i2 != 0x00B1C2D3 || a.i3 != 0x00B1C2D3 || a.i4 != 0x00B1C2D3 || a.i5 != 0x00B1C2D3)
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
	a_X(z, su_6432(U64_C(0xA8B7C6D5E4F3FEFD),0xA8B7C6D5), su_6432(U64_C(0x9AA1B2D3AFFEDEAD), 0x9AA1B2D3))
	a_X(p, su_6432(U64_C(0xA8B7C6D5E4F3FEFD),0xA8B7C6D5), su_6432(U64_C(0x9AA1B2D3AFFEDEAD), 0x9AA1B2D3))

#undef a_X
}
// }}}

// avopt {{{
static void
a_avopt(void){
	static char const a_sopts[] = "A:#";
	static char const * const a_lopts[] = {
		"resource-files:;-3;a",
		"account:;A;b",
		"batch-mode;#;c",
		"long-help;-10;d",
		NIL
	}, * const a_argv[] = {
		"--resource-files=a-a",
		"--resource-files",
			"a-a",
		"--account=a-b",
		"--batch-mode 	",
		"--batch-mode",
		"--long-help",
		"--bummer",
		"--bummer=au",
		"-#A ",
		"--",
		"-#A",
		"Hello, world",
		NIL
	}, * const a_lines[] = {
		"resource-files=a-a",
		"resource-files a-a",
		"account=a-b",
		"account",
		"batch-mode		  ",
		"batch-mode",
		"long-help",
		"bummer",
		"bummer=au",
		NIL
	};

	avopt avox, avo(0, NIL, NIL, a_lopts);
	avox.setup(NELEM(a_argv), a_argv, a_sopts, a_lopts);

	//
	if(avox.argc() != 14)
		a_ERR();
	for(s32 i = 0; (i = avox.parse()) != avox.state_done;){
		switch(i){
		default:
			a_ERR();
			break;

		case -3:
			if(avox.argc() != 13 && avox.argc() != 11)
				a_ERR();
			if(avox.current_opt() != -3)
				a_ERR();
			if(cs::cmp(avox.current_arg(), "a-a"))
				a_ERR();
			break;

		case 'A':
			if(avox.argc() != 10 && avox.argc() != 4)
				a_ERR();
			if(avox.current_opt() != 'A')
				a_ERR();
			if((avox.argc() == 9 && cs::cmp(avox.current_arg(), "a-b")) &&
					(avox.argc() == 3 && cs::cmp(avox.current_arg(), " ")))
				a_ERR();
			break;

		case '#':
			if(avox.argc() != 8 && avox.argc() != 4)
				a_ERR();
			if(avox.current_opt() != '#')
				a_ERR();
			if(avox.current_arg() != NIL)
				a_ERR();
			break;

		case -10:
			if(avox.argc() != 7)
				a_ERR();
			if(avox.current_opt() != -10)
				a_ERR();
			if(avox.current_arg() != NIL)
				a_ERR();
			break;

		case avopt::state_err_opt:
			{char const *emsg = avopt::fmt_err_opt; UNUSED(emsg);}
			if(avox.argc() != 9 && avox.argc() != 6 && avox.argc() != 5)
				a_ERR();
			if((avox.argc() == 9 && avox.current_err_opt() != a_argv[4]) &&
					(avox.argc() == 6 && avox.current_err_opt() != a_argv[7]) &&
					(avox.argc() == 5 && avox.current_err_opt() != a_argv[8]))
				a_ERR();
			break;
		}
	}
	if(avox.argc() != 3)
		a_ERR();
	if(avox.argv()[0] != a_argv[11])
		a_ERR();
	if(avox.argv()[1] != a_argv[12])
		a_ERR();
	if(avox.argv()[2] != a_argv[13] || avox.argv()[2] != NIL)
		a_ERR();
	if(avox.current_opt() != avox.state_stop)
		a_ERR();

	//
	for(char const * const *la = a_lines; *la != NIL; ++la){
		switch(avo.parse_line(*la)){
		default:
			a_ERR();
			break;

		case -3:
			if(la != &a_lines[0] && la != &a_lines[1])
				a_ERR();
			if(avo.current_opt() != -3)
				a_ERR();
			if(cs::cmp(avo.current_arg(), "a-a"))
				a_ERR();
			break;

		case 'A':
			if(la != &a_lines[2])
				a_ERR();
			if(avo.current_opt() != 'A')
				a_ERR();
			if(cs::cmp(avo.current_arg(), "a-b"))
				a_ERR();
			break;

		case '#':
			if(la != &a_lines[5])
				a_ERR();
			if(avo.current_opt() != '#')
				a_ERR();
			if(avo.current_arg() != NIL)
				a_ERR();
			break;

		case -10:
			if(la != &a_lines[6])
				a_ERR();
			if(avo.current_opt() != -10)
				a_ERR();
			if(avo.current_arg() != NIL)
				a_ERR();
			break;

		case avopt::state_err_arg:
			{char const *emsg = avopt::fmt_err_arg; UNUSED(emsg);}
			if(la != &a_lines[3])
				a_ERR();
			if(avo.current_err_opt() != *la)
				a_ERR();
			break;

		case avopt::state_err_opt:
			{char const *emsg = avopt::fmt_err_opt; UNUSED(emsg);}
			if(la != &a_lines[4] && la != &a_lines[7] && la != &a_lines[8])
				a_ERR();
			if(avo.current_err_opt() != *la)
				a_ERR();
			break;
		}
	}

#if 0
	static char const aerr_sopts[] = "A\1-NO";
	static char const * const aerr_lopts[] = {
		"",
		";",
		"no-map;", "no-map",
		"inv-map;\1",
		"mul-map;-1;-2",
		"bad-map;-0", "bad-map;2147483647", "bad-map;-2147483648",
		NIL
	};
	avox.setup(0, NIL, aerr_sopts, aerr_lopts);

	static char const aerr_sopts2[] = "AB:";
	static char const * const aerr_lopts2[] = {"A:;A", "B;B", NIL};
	avox.setup(0, NIL, aerr_sopts2, aerr_lopts2);
#endif
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
	a_X(z, su_6432(0xA0C19876B2D34321,0xA0C19876), su_6432(0x2143D3B27698C1A0,0x7698C1A0))

#undef a_X
}
// }}}

// cs_dict {{{
static void a_cs_dict_(u16 addflags);
static void a_cs_dict__case(cs_dict<char const*> *cdp, char const *k[3]);
template<class CSD> static void a_cs_dict__nilisvalo(CSD *cdp);
static void a_cs_dict_dspc(void);

static void
a_cs_dict(void){
	a_cs_dict_(cs_dict<char const*>::f_prime_spaced);
	a_cs_dict_(cs_dict<char const*>::f_none);

	a_cs_dict_(cs_dict<char const*>::f_strong |
		cs_dict<char const*>::f_prime_spaced);
	a_cs_dict_(cs_dict<char const*>::f_strong);

	{
		static type_toolbox<char const*> const xtb = su_TYPE_TOOLBOX_I9R(
					R(type_toolbox<char const*>::clone_fun,0x1),
					R(type_toolbox<char const*>::del_fun,0x2),
					R(type_toolbox<char const*>::assign_fun,0x3),
					NIL, NIL);

		cs_dict<char const*, TRU1> cd1(&xtb, cs_dict<char const*, TRU1>::f_nil_is_valid_object);
		a_cs_dict__nilisvalo(&cd1);

		cs_dict<char const*> cd2(&xtb);
		a_cs_dict__nilisvalo(&cd2);
	}

	a_cs_dict_dspc();
}

static void
a_cs_dict_(u16 addflags){ // {{{
	{
		cs_dict<char const*> cd(NIL, addflags);
		char const *k[3];

		k[0] = "k1";
		k[1] = "k2";
		k[2] = "k3";

		a_cs_dict__case(&cd, k);

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
		cs_dict<char const*,TRU1> cd(auto_type_toolbox<char const*>::get_instance(), addflags);

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
			for(cs_dict<char const*,TRU1>::view cdv2(cd2); cdv2; ++cdv2)
				if(!cd.has_key(cdv2.key()))
					a_ERR();
				else if(cs::cmp(cdv2.data(), cd.lookup(cdv2.key())))
					a_ERR();
				else if(cdv2.data() == cd.lookup(cdv2.key()))
					a_ERR();

			cd2 = cd;
			if(cd2.count() != cd.count())
				a_ERR();
			for(cs_dict<char const*,TRU1>::view cdv2(cd2); cdv2; ++cdv2)
				if(!cd.has_key(cdv2.key()))
					a_ERR();
				else if(cs::cmp(cdv2.data(), cd.lookup(cdv2.key())))
					a_ERR();
				else if(cdv2.data() == cd.lookup(cdv2.key()))
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

		a_cs_dict__case(&cd, k);
	}

	/// Let's do some flag stuff and "big data"
	u32 u32;

	char buf[ienc::buffer_size], *cp;

	cs_dict<NSPC(su)up,FAL0> cdu(NIL, addflags);
	cs_dict<char*,TRU1> cdo(auto_type_toolbox<char*>::get_instance());

	cdo.set_threshold(4).add_flags(cdo.f_head_resort | addflags);

	for(u32 = 0; u32++ < a_LOOP_NO;){
		if((cp = ienc::convert_u32(buf, u32)) == NIL){
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
		else if((cp = ienc::convert_up(buf, R(NSPC(su)up,cdu.lookup(cdov.key())))) == NIL)
			a_ERR();
		else if(cs::cmp(cdov.key(), cp))
			a_ERR();
	}
	if(u32 != a_LOOP_NO)
		a_ERR();

	a_STATS( cdo.statistics(); )

	cdo.clear().add_flags(cdu.f_frozen);
	for(u32 = 0; u32 < a_LOOP_NO; ++u32){
		if((cp = ienc::convert(buf, u32)) == NIL){
			a_ERR();
			break;
		}
		if(cdo.insert(cp, cp) != 0)
			a_ERR();
		char *xp = cdo.lookup(cp);
		if(xp == NIL)
			a_ERR();
		else if(cs::cmp(xp, cp))
			a_ERR();
	}
	if(cdo.count() != a_LOOP_NO)
		a_ERR();

	u32 = 0;
	for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++u32, ++cdov)
		if(cs::cmp(cdov.key(), cdov.data()))
			a_ERR();
		else if(cdov.key() == cdov.data())
			a_ERR();
	if(u32 != a_LOOP_NO)
		a_ERR();

	a_STATS( cdo.statistics(); )

	if(cdo.set_threshold(2).balance().count() != a_LOOP_NO)
		a_ERR();

	a_STATS( cdo.statistics(); )

	u32 = 0;
	for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++u32, ++cdov)
		if(cs::cmp(cdov.key(), cdov.data()))
			a_ERR();
		else if(cdov.key() == cdov.data())
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
				R(type_toolbox<char*>::clone_fun,0x1),
				R(type_toolbox<char*>::del_fun,0x2),
				R(type_toolbox<char*>::assign_fun,0x3),
				NIL, NIL);
		typedef cs_dict<char*,TRU1> csd;

		csd *cdo2 = su_NEW(csd)(&xtb);
		if(cdo2->assign(cdo) != 0) // (replaces tbox)
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
	{
		cs_dict<char*,TRU1> cdo2(auto_type_toolbox<char*>::get_instance()),
			cdo3(auto_type_toolbox<char*>::get_instance());

		for(cs_dict<char*,TRU1>::view cdov(cdo); cdov; ++cdov){
			cs_dict<char*,TRU1>::view cdov2(cdo2), cdov3(cdo3);

			if(cdov2.reset_insert(cdov.key(), cdov.data()))
				a_ERR();
			else if(!cdov2.is_valid())
				a_ERR();
			else if(cs::cmp(cdov2.key(), cdov.key()))
				a_ERR();
			else if(cs::cmp(cdov2.data(), cdov.data()))
				a_ERR();
			else if(cdov2.reset_insert(cdov.key(), UNCONST(char*,su_empty)) != -1)
				a_ERR();
			else if(!cdov2.is_valid())
				a_ERR();
			else if(cs::cmp(cdov2.key(), cdov.key()))
				a_ERR();
			else if(cs::cmp(cdov2.data(), cdov.data()))
				a_ERR();

			if(cdov3.reset_replace(cdov.key(), cdov.data()))
				a_ERR();
			else if(!cdov3.is_valid())
				a_ERR();
			else if(cs::cmp(cdov3.key(), cdov.key()))
				a_ERR();
			else if(cs::cmp(cdov3.data(), cdov.data()))
				a_ERR();
			else if(cdov3.reset_replace(cdov.key(),UNCONST(char*,su_empty)) != -1)
				a_ERR();
			else if(!cdov3.is_valid())
				a_ERR();
			else if(cs::cmp(cdov3.key(), cdov.key()))
				a_ERR();
			else if(!cs::cmp(cdov3.data(), cdov.data()))
				a_ERR();
			else if(cs::cmp(cdov3.data(), su_empty))
				a_ERR();
		}

		if(cdo2.count() != cdo.count())
			a_ERR();
		if(cdo3.count() != cdo.count())
			a_ERR();
	}
} // }}}

static void
a_cs_dict__case(cs_dict<char const*> *cdp, char const *k[3]){ // {{{
	// basics
	if(!cdp->is_empty())
		a_ERR();
	if(cdp->toolbox() != NIL)
		a_ERR();

	s32 s32 = cdp->insert(k[0], "v1");
	if(s32 != 0)
		a_ERR();
	{
		s32 = cdp->insert(k[0], "v1-no");
		if(s32 != -1)
			a_ERR();
		s32 = cdp->replace("k1", "v1-yes");
		if(s32 != -1)
			a_ERR();
	}
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
	{
		char const *ccp = cdp->lookup(k[0]);
		if(ccp == NIL)
			a_ERR();
		else if(cs::cmp(ccp, "v1-yes"))
			a_ERR();
	}
	if(cdp->has_key(k[1]))
		a_ERR();
	{
		char const *ccp = cdp->lookup("k2");
		if(ccp != NIL)
			a_ERR();
	}
	if(!cdp->has_key(k[2]))
		a_ERR();
	{
		char const *ccp = cdp->lookup("k3");
		if(ccp == NIL)
			a_ERR();
		else if(cs::cmp(ccp, "v3"))
			a_ERR();
	}

	{
		cs_dict<char const*> cd2(*cdp);

		if(cd2.count() != 2)
			a_ERR();
		if(!cdp->clear_elems().is_empty())
			a_ERR();

		if(!cd2.has_key(k[0]))
			a_ERR();
		{
			char const *ccp = cd2.lookup("k1");
			if(ccp == NIL)
				a_ERR();
			else if(cs::cmp(ccp, "v1-yes"))
				a_ERR();
		}

		if(!cd2.has_key(k[2]))
			a_ERR();
		{
			char const *ccp = cd2.lookup("k3");
			if(ccp == NIL)
				a_ERR();
			else if(cs::cmp(ccp, "v3"))
				a_ERR();
		}

		if(cdp->assign_elems(cd2) != 0)
			a_ERR();
	}

	s32 = cdp->insert(k[1], "v2");
	if(s32 != 0)
		a_ERR();
	{
		char const *ccp = cdp->lookup("k2");
		if(ccp == NIL)
			a_ERR();
		else if(cs::cmp(ccp, "v2"))
			a_ERR();
	}

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
} // }}}

template<class CSD>
static void a_cs_dict__nilisvalo(CSD *cdp){ // {{{
	if(cdp->insert("one", NIL) != 0)
		a_ERR();
	else if(cdp->insert("two", NIL) != 0)
		a_ERR();
	else{
		if(!cdp->has_key("one"))
			a_ERR();
		else if(cdp->lookup("one") != NIL)
			a_ERR();
		else if(!cdp->has_key("two"))
			a_ERR();
		else if(cdp->lookup("two") != NIL)
			a_ERR();
	}

	if(cdp->replace("one", NIL) != -1)
		a_ERR();
	else if(cdp->replace("two", NIL) != -1)
		a_ERR();
	else{
		if(!cdp->has_key("one"))
			a_ERR();
		else if((*cdp)["one"] != NIL)
			a_ERR();
		else if(!cdp->has_key("two"))
			a_ERR();
		else if((*cdp)["two"] != NIL)
			a_ERR();
	}

	if(!cdp->remove("one"))
		a_ERR();
	else if(!cdp->remove("two"))
		a_ERR();
	else if(!cdp->is_empty())
		a_ERR();
	else if(cdp->count() != 0)
		a_ERR();

	//
	typename CSD::view cdv(*cdp);

	if(cdv.reset_insert("one", NIL) != 0)
		a_ERR();
	else if(cs::cmp(cdv.key(), "one"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.reset_insert("two", NIL) != 0)
		a_ERR();
	else if(cs::cmp(cdv.key(), "two"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else{
		if(!cdv.find("one"))
			a_ERR();
		else if(cs::cmp(cdv.key(), "one"))
			a_ERR();
		else if(cdv.data() != NIL)
			a_ERR();
		else if(!cdv.find("two"))
			a_ERR();
		else if(cs::cmp(cdv.key(), "two"))
			a_ERR();
		else if(cdv.data() != NIL)
			a_ERR();
	}

	if(cdv.reset_replace("one", NIL) != -1)
		a_ERR();
	else if(cs::cmp(cdv.key(), "one"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.reset_replace("two", NIL) != -1)
		a_ERR();
	else if(cs::cmp(cdv.key(), "two"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else{
		if(!cdv.find("one"))
			a_ERR();
		else if(cs::cmp(cdv.key(), "one"))
			a_ERR();
		else if(cdv.data() != NIL)
			a_ERR();
		else if(!cdv.remove().find("two"))
			a_ERR();
		else if(cs::cmp(cdv.key(), "two"))
			a_ERR();
		else if(cdv.data() != NIL)
			a_ERR();
		else if(cdv.remove().is_valid())
			a_ERR();
	}
} // }}}

static void
a_cs_dict_dspc(void){ // {{{
	static type_toolbox<char const*> const xtb = su_TYPE_TOOLBOX_I9R(
				R(type_toolbox<char const*>::clone_fun,0x1),
				R(type_toolbox<char const*>::del_fun,0x2),
				R(type_toolbox<char const*>::assign_fun,0x3),
				NIL, NIL);

	char buf[64];
	char const *vp;
	cs_dict<char const*> cd(&xtb);

	//
	cd.set_data_space(5);

	if((cs::pcopy(buf, "1234"), cd.insert("1", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "2341"), cd.insert("2", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "3412"), cd.insert("three-three-three-three-three-three--", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "4123"), cd.insert("four-four-four-four----", buf)) != err::none)
		a_ERR();

	if((vp = cd.lookup("1")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "1234"))
		a_ERR();
	else if((vp = cd.lookup("2")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "2341"))
		a_ERR();
	else if((vp = cd.lookup("three-three-three-three-three-three--")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "3412"))
		a_ERR();
	else if((vp = cd.lookup("four-four-four-four----")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "4123"))
		a_ERR();

	//
	cd.clear();
	cd.set_data_space(36 +1);

	if((cs::pcopy(buf, "0123456789abcdefghijklmnopqrstuvwxyz"), cd.insert("1", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "123456789abcdefghijklmnopqrstuvwxyz0"), cd.insert("2", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "23456789abcdefghijklmnopqrstuvwxyz01"),
			cd.insert("three-three-three-three-three-three--", buf)) != err::none)
		a_ERR();
	else if((cs::pcopy(buf, "3456789abcdefghijklmnopqrstuvwxyz012"),
			cd.insert("four-four-four-four----", buf)) != err::none)
		a_ERR();

	if((vp = cd.lookup("1")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "0123456789abcdefghijklmnopqrstuvwxyz"))
		a_ERR();
	else if((vp = cd.lookup("2")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "123456789abcdefghijklmnopqrstuvwxyz0"))
		a_ERR();
	else if((vp = cd.lookup("three-three-three-three-three-three--")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "23456789abcdefghijklmnopqrstuvwxyz01"))
		a_ERR();
	else if((vp = cd.lookup("four-four-four-four----")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "3456789abcdefghijklmnopqrstuvwxyz012"))
		a_ERR();

	if(cd.count() != 4)
		a_ERR();

	//
	if(cd.replace("1", "456789abcdefghijklmnopqrstuvwxyz0123") != -1)
		a_ERR();
	else if(cd.replace("2", "56789abcdefghijklmnopqrstuvwxyz01234") != -1)
		a_ERR();
	else if(cd.replace("three-three-three-three-three-three--", "6789abcdefghijklmnopqrstuvwxyz012345") != -1)
		a_ERR();
	else if(cd.replace("four-four-four-four----", "789abcdefghijklmnopqrstuvwxyz0123456") != -1)
		a_ERR();

	if((vp = cd.lookup("1")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "456789abcdefghijklmnopqrstuvwxyz0123"))
		a_ERR();
	else if((vp = cd.lookup("2")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "56789abcdefghijklmnopqrstuvwxyz01234"))
		a_ERR();
	else if((vp = cd.lookup("three-three-three-three-three-three--")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "6789abcdefghijklmnopqrstuvwxyz012345"))
		a_ERR();
	else if((vp = cd.lookup("four-four-four-four----")) == NIL)
		a_ERR();
	else if(cs::cmp(vp, "789abcdefghijklmnopqrstuvwxyz0123456"))
		a_ERR();

	if(cd.count() != 4)
		a_ERR();

	//
	cs_dict<char const*>::view cdv(*&cd);

	if(!cdv.find("1"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "456789abcdefghijklmnopqrstuvwxyz0123"))
		a_ERR();
	else if((cs::pcopy(buf, "-56789abcdefghijklmnopqrstuvwxyz012-"), cdv.set_data(buf)) != err::none)
		a_ERR();
	else if(!cdv.find("2"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "56789abcdefghijklmnopqrstuvwxyz01234"))
		a_ERR();
	else if((cs::pcopy(buf, "-6789abcdefghijklmnopqrstuvwxyz0123-"), cdv.set_data(buf)) != err::none)
		a_ERR();
	else if(!cdv.find("three-three-three-three-three-three--"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "6789abcdefghijklmnopqrstuvwxyz012345"))
		a_ERR();
	else if((cs::pcopy(buf, "-789abcdefghijklmnopqrstuvwxyz01234-"), cdv.set_data(buf)) != err::none)
		a_ERR();
	else if(!cdv.find("four-four-four-four----"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "789abcdefghijklmnopqrstuvwxyz0123456"))
		a_ERR();
	else if((cs::pcopy(buf, "-89abcdefghijklmnopqrstuvwxyz012345-"), cdv.set_data(buf)) != err::none)
		a_ERR();
	//
	else if(!cdv.find("1"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "-56789abcdefghijklmnopqrstuvwxyz012-"))
		a_ERR();
	else if(cdv.set_data("--6789abcdefghijklmnopqrstuvwxyz01--") != err::none)
		a_ERR();
	else if(!cdv.find("2"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "-6789abcdefghijklmnopqrstuvwxyz0123-"))
		a_ERR();
	else if(cdv.set_data("--789abcdefghijklmnopqrstuvwxyz012--") != err::none)
		a_ERR();
	else if(!cdv.find("three-three-three-three-three-three--"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "-789abcdefghijklmnopqrstuvwxyz01234-"))
		a_ERR();
	else if(cdv.set_data("--89abcdefghijklmnopqrstuvwxyz0123--") != err::none)
		a_ERR();
	else if(!cdv.find("four-four-four-four----"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "-89abcdefghijklmnopqrstuvwxyz012345-"))
		a_ERR();
	else if(cdv.set_data("--9abcdefghijklmnopqrstuvwxyz01234--") != err::none)
		a_ERR();
	//
	else if(!cdv.find("1"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "--6789abcdefghijklmnopqrstuvwxyz01--"))
		a_ERR();
	else if(cdv.set_data(NIL) != err::none)
		a_ERR();
	else if(!cdv.find("2"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "--789abcdefghijklmnopqrstuvwxyz012--"))
		a_ERR();
	else if(cdv.set_data(NIL) != err::none)
		a_ERR();
	else if(!cdv.find("three-three-three-three-three-three--"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "--89abcdefghijklmnopqrstuvwxyz0123--"))
		a_ERR();
	else if(cdv.set_data(NIL) != err::none)
		a_ERR();
	else if(!cdv.find("four-four-four-four----"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "--9abcdefghijklmnopqrstuvwxyz01234--"))
		a_ERR();
	else if(cdv.set_data(NIL) != err::none)
		a_ERR();
	//
	else if(!cdv.find("1"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.set_data("---789abcdefghijklmnopqrstuvwxyz0---") != err::none)
		a_ERR();
	else if(!cdv.find("2"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.set_data("---89abcdefghijklmnopqrstuvwxyz01---") != err::none)
		a_ERR();
	else if(!cdv.find("three-three-three-three-three-three--"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.set_data("---9abcdefghijklmnopqrstuvwxyz012---") != err::none)
		a_ERR();
	else if(!cdv.find("four-four-four-four----"))
		a_ERR();
	else if(cdv.data() != NIL)
		a_ERR();
	else if(cdv.set_data("---abcdefghijklmnopqrstuvwxyz0123---") != err::none)
		a_ERR();
	//
	else if(!cdv.find("1"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "---789abcdefghijklmnopqrstuvwxyz0---"))
		a_ERR();
	else if(!cdv.find("2"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "---89abcdefghijklmnopqrstuvwxyz01---"))
		a_ERR();
	else if(!cdv.find("three-three-three-three-three-three--"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "---9abcdefghijklmnopqrstuvwxyz012---"))
		a_ERR();
	else if(!cdv.find("four-four-four-four----"))
		a_ERR();
	else if(cs::cmp(cdv.data(), "---abcdefghijklmnopqrstuvwxyz0123---"))
		a_ERR();

	if(cd.count() != 4)
		a_ERR();

	//
	cd.clear().set_data_space(10).add_flags(cd.f_data_space_raw);

	if(cd.insert("1", "9876543210") != err::none)
		a_ERR();
	else if(cd.insert("2", "jihgfedcba") != err::none)
		a_ERR();
	else if((vp = cd.lookup("1")) == NIL)
		a_ERR();
	// these are hacks
	else if(!mem::cmp(vp, "9876543210", 10))
		a_ERR();
	else if((mem::copy(C(char*,vp), "9876543210", 10), (vp = cd.lookup("1"))) == NIL)
		a_ERR();
	else if(mem::cmp(vp, "9876543210", 10))
		a_ERR();
	else if((vp = cd.lookup("2")) == NIL)
		a_ERR();
	else if(!mem::cmp(vp, "jihgfedcba", 10))
		a_ERR();
	else if((mem::copy(C(char*,vp), "jihgfedcba", 10), (vp = cd.lookup("2"))) == NIL)
		a_ERR();
	else if(mem::cmp(vp, "jihgfedcba", 10))
		a_ERR();
	//
	else if((vp = cd.lookup("1")) == NIL)
		a_ERR();
	else if(mem::cmp(vp, "9876543210", 10))
		a_ERR();
	else if((vp = cd.lookup("2")) == NIL)
		a_ERR();
	else if(mem::cmp(vp, "jihgfedcba", 10))
		a_ERR();
} // }}}
// }}}

// icodec {{{
static void
a_icodec(void){
	char buf[ienc::buffer_size];

	u32 u32 = 0xAFFEDEADu;
	char const *ccp;
	if((ccp = ienc::convert_u32(buf, u32)) == NIL)
		a_ERR();
	if(cs::cmp(ccp, "2952715949"))
		a_ERR();
	if((idec::convert_u32(&u32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != idec::state_consumed)
		a_ERR();
	if(*ccp != '\0')
		a_ERR();
	if(u32 != 0xAFFEDEADu)
		a_ERR();
	if((ccp = ienc::convert_u32(buf, u32, 0x10)) == NIL)
		a_ERR();
	else if(cs::cmp(ccp, "0xAFFEDEAD"))
		a_ERR();
	else if((idec::convert_u32(&u32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != idec::state_consumed)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	if(u32 != 0xAFFEDEADu)
		a_ERR();

	u64 u64 = (S(NSPC(su)u64,u32) << 32) | 0xABBABEEF;
	if((ccp = ienc::convert_u64(buf, u64)) == NIL)
		a_ERR();
	else if(cs::cmp(ccp, "12681818438213746415"))
		a_ERR();
	else if((idec::convert_u64(&u64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != idec::state_consumed)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	if(u64 != U64_C(0xAFFEDEADABBABEEF))
		a_ERR();
	if((ccp = ienc::convert_u64(buf, u64, 0x10)) == NIL)
		a_ERR();
	else if(cs::cmp(ccp, "0xAFFEDEADABBABEEF"))
		a_ERR();
	else if((idec::convert_u64(&u64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != idec::state_consumed)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	if(u64 != U64_C(0xAFFEDEADABBABEEF))
		a_ERR();

	// MAXLIM

	ccp = "999999999999999999999999999999999999999999999";
	if((idec::convert_u64(&u64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(u64 != max::u64)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	ccp = "-999999999999999999999999999999999999999999999";
	if((idec::convert_u64(&u64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(u64 != max::u64)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();

	s64 s64;
	ccp = "999999999999999999999999999999999999999999999";
	if((idec::convert_s64(&s64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(s64 != max::s64)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	ccp = "-999999999999999999999999999999999999999999999";
	if((idec::convert_s64(&s64, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(s64 != min::s64)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();

	ccp = "999999999999999999999999999999999999999999999";
	if((idec::convert_u32(&u32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(u32 != max::u32)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	ccp = "-999999999999999999999999999999999999999999999";
	if((idec::convert_u32(&u32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(u32 != max::u32)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();

	s32 s32;
	ccp = "999999999999999999999999999999999999999999999";
	if((idec::convert_s32(&s32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(s32 != max::s32)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();
	ccp = "-999999999999999999999999999999999999999999999";
	if((idec::convert_s32(&s32, ccp, max::uz, 0, &ccp) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow | idec::state_consumed))
		a_ERR();
	else if(s32 != min::s32)
		a_ERR();
	else if(*ccp != '\0')
		a_ERR();

	//

	if(idec::convert_u64(&u64, "0x", max::uz, 0, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != 'x')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();
	if(idec::convert_u64(&u64, "0X", max::uz, 16, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != 'X')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();

	if(idec::convert_u64(&u64, "0b", max::uz, 0, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != 'b')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();
	if(idec::convert_u64(&u64, "0B", max::uz, 2, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != 'B')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();

	if(idec::convert_u64(&u64, "09", max::uz, 0, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != '9')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();

	if(idec::convert_u64(&u64, "09", max::uz, 8, &ccp) != idec::state_ebase)
		a_ERR();
	else if(*ccp != '9')
		a_ERR();
	if(u64 != U64_C(0))
		a_ERR();

	//

	struct{
		char const *in;
		NSPC(su)u64 out;
		u8 base;
	} const conv[] = {
		{"64#hello", 288970072, 64},
		{"64#world", 543274317, 64},
		{"42#hello", 53974014, 42},
		{"42#world", 101400907, 42},
		{"37#hello", 32599429, 37},
		{"37#world", 61226577, 37},
		{"36#hello", 29234652, 36},
		{"36#world", 54903217, 36},
		{"32#hello", 18306744, 32},
		{"32#vorld", 33320621, 32},
		{"64#ZA_@RD_@US", U64_C(0xF64FFED67FFEE36), 64},
		{NIL, 0, 0}
	};

	for(uz i = 0; conv[i].in != NIL; ++i){
		if(idec::convert_u64(&u64, &conv[i].in[3], max::uz, conv[i].base, &ccp) != idec::state_consumed)
			a_ERR();
		else if(*ccp != '\0')
			a_ERR();
		if(u64 != conv[i].out)
			a_ERR();

		if(idec::convert_u64(&u64, conv[i].in, max::uz, 0, &ccp) != idec::state_consumed)
			a_ERR();
		else if(*ccp != '\0')
			a_ERR();
		if(u64 != conv[i].out)
			a_ERR();

		if((ccp = ienc::convert(buf, u64, conv[i].base, (ienc::mode_no_prefix | ienc::mode_lowercase))) == NIL)
			a_ERR();
		else if(cs::cmp(ccp, &conv[i].in[3]))
			a_ERR();

		if((ccp = ienc::convert(buf, u64, conv[i].base, ienc::mode_lowercase)) == NIL)
			a_ERR();
		else if(cs::cmp(ccp, conv[i].in))
			a_ERR();
	}

	//// Limit / signed / flags

	BITENUM_IS(NSPC(su)u32,idec::mode) m;

	//
	ccp = "0x7FFFFFFFFFFFFFFF ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (idec::mode_signed_type), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_none))
		a_ERR();
	else if(s64 != max::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x7FFFFFFF ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (idec::mode_signed_type | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_none))
		a_ERR();
	else if(s32 != max::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	ccp = "0x8000000000000000 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (idec::mode_signed_type), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s64 != max::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x80000000 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (idec::mode_signed_type | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s32 != max::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	ccp = "0x8000000000000000 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (0), &ccp)) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_none))
		a_ERR();
	else if(s64 != min::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x80000000 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (0 | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_none))
		a_ERR();
	else if(s32 != min::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	//
	ccp = "0x7FFFFFFFFFFFFFFF1 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (idec::mode_signed_type), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s64 != max::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x7FFFFFFF1 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (idec::mode_signed_type | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s32 != max::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	ccp = "0x80000000000000000 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (idec::mode_signed_type), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s64 != max::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x8000000000 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (idec::mode_signed_type | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s32 != max::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	ccp = "0x80000000000000000 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (0), &ccp)) & (idec::state_emask | idec::state_consumed)
			) != (idec::state_eoverflow))
		a_ERR();
	else if(s64 != S(NSPC(su)s64,max::u64))
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "0x8000000000 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (0 | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s32 != S(NSPC(su)s32,max::u32))
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();

	ccp = "-0x80000000000000000 ";
	if(((m = idec::convert(&s64, ccp, max::uz, 0, (idec::mode_signed_type), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s64 != min::s64)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
	ccp = "-0x8000000000 ";
	if(((m = idec::convert(&s32, ccp, max::uz, 0, (idec::mode_signed_type | idec::mode_limit_32bit), &ccp)
			) & (idec::state_emask | idec::state_consumed)) != (idec::state_eoverflow))
		a_ERR();
	else if(s32 != min::s32)
		a_ERR();
	else if(ccp[0] != ' ' || ccp[1] != '\0')
		a_ERR();
}
// }}}

// md {{{
#ifdef su_HAVE_MD
struct a_siphash_t64{
	u8 rdat[8];
};
struct a_siphash_t128{
	u8 rdat[16];
};

// (From OpenSSL test, which claims it is from reference implementation
static struct a_siphash_t64 const a_siphash_t64[] = { // {{{
	{/*0,*/{0x31,0x0e,0x0e,0xdd,0x47,0xdb,0x6f,0x72}},
	{/*1,*/{0xfd,0x67,0xdc,0x93,0xc5,0x39,0xf8,0x74}},
	{/*2,*/{0x5a,0x4f,0xa9,0xd9,0x09,0x80,0x6c,0x0d}},
	{/*3,*/{0x2d,0x7e,0xfb,0xd7,0x96,0x66,0x67,0x85}},
	{/*4,*/{0xb7,0x87,0x71,0x27,0xe0,0x94,0x27,0xcf}},
	{/*5,*/{0x8d,0xa6,0x99,0xcd,0x64,0x55,0x76,0x18}},
	{/*6,*/{0xce,0xe3,0xfe,0x58,0x6e,0x46,0xc9,0xcb}},
	{/*7,*/{0x37,0xd1,0x01,0x8b,0xf5,0x00,0x02,0xab}},
	{/*8,*/{0x62,0x24,0x93,0x9a,0x79,0xf5,0xf5,0x93}},
	{/*9,*/{0xb0,0xe4,0xa9,0x0b,0xdf,0x82,0x00,0x9e}},
	{/*10,*/{0xf3,0xb9,0xdd,0x94,0xc5,0xbb,0x5d,0x7a}},
	{/*11,*/{0xa7,0xad,0x6b,0x22,0x46,0x2f,0xb3,0xf4}},
	{/*12,*/{0xfb,0xe5,0x0e,0x86,0xbc,0x8f,0x1e,0x75}},
	{/*13,*/{0x90,0x3d,0x84,0xc0,0x27,0x56,0xea,0x14}},
	{/*14,*/{0xee,0xf2,0x7a,0x8e,0x90,0xca,0x23,0xf7}},
	{/*15,*/{0xe5,0x45,0xbe,0x49,0x61,0xca,0x29,0xa1}},
	{/*16,*/{0xdb,0x9b,0xc2,0x57,0x7f,0xcc,0x2a,0x3f}},
	{/*17,*/{0x94,0x47,0xbe,0x2c,0xf5,0xe9,0x9a,0x69}},
	{/*18,*/{0x9c,0xd3,0x8d,0x96,0xf0,0xb3,0xc1,0x4b}},
	{/*19,*/{0xbd,0x61,0x79,0xa7,0x1d,0xc9,0x6d,0xbb}},
	{/*20,*/{0x98,0xee,0xa2,0x1a,0xf2,0x5c,0xd6,0xbe}},
	{/*21,*/{0xc7,0x67,0x3b,0x2e,0xb0,0xcb,0xf2,0xd0}},
	{/*22,*/{0x88,0x3e,0xa3,0xe3,0x95,0x67,0x53,0x93}},
	{/*23,*/{0xc8,0xce,0x5c,0xcd,0x8c,0x03,0x0c,0xa8}},
	{/*24,*/{0x94,0xaf,0x49,0xf6,0xc6,0x50,0xad,0xb8}},
	{/*25,*/{0xea,0xb8,0x85,0x8a,0xde,0x92,0xe1,0xbc}},
	{/*26,*/{0xf3,0x15,0xbb,0x5b,0xb8,0x35,0xd8,0x17}},
	{/*27,*/{0xad,0xcf,0x6b,0x07,0x63,0x61,0x2e,0x2f}},
	{/*28,*/{0xa5,0xc9,0x1d,0xa7,0xac,0xaa,0x4d,0xde}},
	{/*29,*/{0x71,0x65,0x95,0x87,0x66,0x50,0xa2,0xa6}},
	{/*30,*/{0x28,0xef,0x49,0x5c,0x53,0xa3,0x87,0xad}},
	{/*31,*/{0x42,0xc3,0x41,0xd8,0xfa,0x92,0xd8,0x32}},
	{/*32,*/{0xce,0x7c,0xf2,0x72,0x2f,0x51,0x27,0x71}},
	{/*33,*/{0xe3,0x78,0x59,0xf9,0x46,0x23,0xf3,0xa7}},
	{/*34,*/{0x38,0x12,0x05,0xbb,0x1a,0xb0,0xe0,0x12}},
	{/*35,*/{0xae,0x97,0xa1,0x0f,0xd4,0x34,0xe0,0x15}},
	{/*36,*/{0xb4,0xa3,0x15,0x08,0xbe,0xff,0x4d,0x31}},
	{/*37,*/{0x81,0x39,0x62,0x29,0xf0,0x90,0x79,0x02}},
	{/*38,*/{0x4d,0x0c,0xf4,0x9e,0xe5,0xd4,0xdc,0xca}},
	{/*39,*/{0x5c,0x73,0x33,0x6a,0x76,0xd8,0xbf,0x9a}},
	{/*40,*/{0xd0,0xa7,0x04,0x53,0x6b,0xa9,0x3e,0x0e}},
	{/*41,*/{0x92,0x59,0x58,0xfc,0xd6,0x42,0x0c,0xad}},
	{/*42,*/{0xa9,0x15,0xc2,0x9b,0xc8,0x06,0x73,0x18}},
	{/*43,*/{0x95,0x2b,0x79,0xf3,0xbc,0x0a,0xa6,0xd4}},
	{/*44,*/{0xf2,0x1d,0xf2,0xe4,0x1d,0x45,0x35,0xf9}},
	{/*45,*/{0x87,0x57,0x75,0x19,0x04,0x8f,0x53,0xa9}},
	{/*46,*/{0x10,0xa5,0x6c,0xf5,0xdf,0xcd,0x9a,0xdb}},
	{/*47,*/{0xeb,0x75,0x09,0x5c,0xcd,0x98,0x6c,0xd0}},
	{/*48,*/{0x51,0xa9,0xcb,0x9e,0xcb,0xa3,0x12,0xe6}},
	{/*49,*/{0x96,0xaf,0xad,0xfc,0x2c,0xe6,0x66,0xc7}},
	{/*50,*/{0x72,0xfe,0x52,0x97,0x5a,0x43,0x64,0xee}},
	{/*51,*/{0x5a,0x16,0x45,0xb2,0x76,0xd5,0x92,0xa1}},
	{/*52,*/{0xb2,0x74,0xcb,0x8e,0xbf,0x87,0x87,0x0a}},
	{/*53,*/{0x6f,0x9b,0xb4,0x20,0x3d,0xe7,0xb3,0x81}},
	{/*54,*/{0xea,0xec,0xb2,0xa3,0x0b,0x22,0xa8,0x7f}},
	{/*55,*/{0x99,0x24,0xa4,0x3c,0xc1,0x31,0x57,0x24}},
	{/*56,*/{0xbd,0x83,0x8d,0x3a,0xaf,0xbf,0x8d,0xb7}},
	{/*57,*/{0x0b,0x1a,0x2a,0x32,0x65,0xd5,0x1a,0xea}},
	{/*58,*/{0x13,0x50,0x79,0xa3,0x23,0x1c,0xe6,0x60}},
	{/*59,*/{0x93,0x2b,0x28,0x46,0xe4,0xd7,0x06,0x66}},
	{/*60,*/{0xe1,0x91,0x5f,0x5c,0xb1,0xec,0xa4,0x6c}},
	{/*61,*/{0xf3,0x25,0x96,0x5c,0xa1,0x6d,0x62,0x9f}},
	{/*62,*/{0x57,0x5f,0xf2,0x8e,0x60,0x38,0x1b,0xe5}},
	{/*63,*/{0x72,0x45,0x06,0xeb,0x4c,0x32,0x8a,0x95}}
}; // }}}

static struct a_siphash_t128 const a_siphash_t128[] = { // {{{
	{/*0,*/{0xa3,0x81,0x7f,0x04,0xba,0x25,0xa8,0xe6,
		 0x6d,0xf6,0x72,0x14,0xc7,0x55,0x02,0x93}},
	{/*1,*/{0xda,0x87,0xc1,0xd8,0x6b,0x99,0xaf,0x44,
		 0x34,0x76,0x59,0x11,0x9b,0x22,0xfc,0x45}},
	{/*2,*/{0x81,0x77,0x22,0x8d,0xa4,0xa4,0x5d,0xc7,
		 0xfc,0xa3,0x8b,0xde,0xf6,0x0a,0xff,0xe4}},
	{/*3,*/{0x9c,0x70,0xb6,0x0c,0x52,0x67,0xa9,0x4e,
		 0x5f,0x33,0xb6,0xb0,0x29,0x85,0xed,0x51}},
	{/*4,*/{0xf8,0x81,0x64,0xc1,0x2d,0x9c,0x8f,0xaf,
		 0x7d,0x0f,0x6e,0x7c,0x7b,0xcd,0x55,0x79}},
	{/*5,*/{0x13,0x68,0x87,0x59,0x80,0x77,0x6f,0x88,
		 0x54,0x52,0x7a,0x07,0x69,0x0e,0x96,0x27}},
	{/*6,*/{0x14,0xee,0xca,0x33,0x8b,0x20,0x86,0x13,
		 0x48,0x5e,0xa0,0x30,0x8f,0xd7,0xa1,0x5e}},
	{/*7,*/{0xa1,0xf1,0xeb,0xbe,0xd8,0xdb,0xc1,0x53,
		 0xc0,0xb8,0x4a,0xa6,0x1f,0xf0,0x82,0x39}},
	{/*8,*/{0x3b,0x62,0xa9,0xba,0x62,0x58,0xf5,0x61,
		 0x0f,0x83,0xe2,0x64,0xf3,0x14,0x97,0xb4}},
	{/*9,*/{0x26,0x44,0x99,0x06,0x0a,0xd9,0xba,0xab,
		 0xc4,0x7f,0x8b,0x02,0xbb,0x6d,0x71,0xed}},
	{/*10,*/{0x00,0x11,0x0d,0xc3,0x78,0x14,0x69,0x56,
		 0xc9,0x54,0x47,0xd3,0xf3,0xd0,0xfb,0xba}},
	{/*11,*/{0x01,0x51,0xc5,0x68,0x38,0x6b,0x66,0x77,
		 0xa2,0xb4,0xdc,0x6f,0x81,0xe5,0xdc,0x18}},
	{/*12,*/{0xd6,0x26,0xb2,0x66,0x90,0x5e,0xf3,0x58,
		 0x82,0x63,0x4d,0xf6,0x85,0x32,0xc1,0x25}},
	{/*13,*/{0x98,0x69,0xe2,0x47,0xe9,0xc0,0x8b,0x10,
		 0xd0,0x29,0x93,0x4f,0xc4,0xb9,0x52,0xf7}},
	{/*14,*/{0x31,0xfc,0xef,0xac,0x66,0xd7,0xde,0x9c,
		 0x7e,0xc7,0x48,0x5f,0xe4,0x49,0x49,0x02}},
	{/*15,*/{0x54,0x93,0xe9,0x99,0x33,0xb0,0xa8,0x11,
		 0x7e,0x08,0xec,0x0f,0x97,0xcf,0xc3,0xd9}},
	{/*16,*/{0x6e,0xe2,0xa4,0xca,0x67,0xb0,0x54,0xbb,
		 0xfd,0x33,0x15,0xbf,0x85,0x23,0x05,0x77}},
	{/*17,*/{0x47,0x3d,0x06,0xe8,0x73,0x8d,0xb8,0x98,
		 0x54,0xc0,0x66,0xc4,0x7a,0xe4,0x77,0x40}},
	{/*18,*/{0xa4,0x26,0xe5,0xe4,0x23,0xbf,0x48,0x85,
		 0x29,0x4d,0xa4,0x81,0xfe,0xae,0xf7,0x23}},
	{/*19,*/{0x78,0x01,0x77,0x31,0xcf,0x65,0xfa,0xb0,
		 0x74,0xd5,0x20,0x89,0x52,0x51,0x2e,0xb1}},
	{/*20,*/{0x9e,0x25,0xfc,0x83,0x3f,0x22,0x90,0x73,
		 0x3e,0x93,0x44,0xa5,0xe8,0x38,0x39,0xeb}},
	{/*21,*/{0x56,0x8e,0x49,0x5a,0xbe,0x52,0x5a,0x21,
		 0x8a,0x22,0x14,0xcd,0x3e,0x07,0x1d,0x12}},
	{/*22,*/{0x4a,0x29,0xb5,0x45,0x52,0xd1,0x6b,0x9a,
		 0x46,0x9c,0x10,0x52,0x8e,0xff,0x0a,0xae}},
	{/*23,*/{0xc9,0xd1,0x84,0xdd,0xd5,0xa9,0xf5,0xe0,
		 0xcf,0x8c,0xe2,0x9a,0x9a,0xbf,0x69,0x1c}},
	{/*24,*/{0x2d,0xb4,0x79,0xae,0x78,0xbd,0x50,0xd8,
		 0x88,0x2a,0x8a,0x17,0x8a,0x61,0x32,0xad}},
	{/*25,*/{0x8e,0xce,0x5f,0x04,0x2d,0x5e,0x44,0x7b,
		 0x50,0x51,0xb9,0xea,0xcb,0x8d,0x8f,0x6f}},
	{/*26,*/{0x9c,0x0b,0x53,0xb4,0xb3,0xc3,0x07,0xe8,
		 0x7e,0xae,0xe0,0x86,0x78,0x14,0x1f,0x66}},
	{/*27,*/{0xab,0xf2,0x48,0xaf,0x69,0xa6,0xea,0xe4,
		 0xbf,0xd3,0xeb,0x2f,0x12,0x9e,0xeb,0x94}},
	{/*28,*/{0x06,0x64,0xda,0x16,0x68,0x57,0x4b,0x88,
		 0xb9,0x35,0xf3,0x02,0x73,0x58,0xae,0xf4}},
	{/*29,*/{0xaa,0x4b,0x9d,0xc4,0xbf,0x33,0x7d,0xe9,
		 0x0c,0xd4,0xfd,0x3c,0x46,0x7c,0x6a,0xb7}},
	{/*30,*/{0xea,0x5c,0x7f,0x47,0x1f,0xaf,0x6b,0xde,
		 0x2b,0x1a,0xd7,0xd4,0x68,0x6d,0x22,0x87}},
	{/*31,*/{0x29,0x39,0xb0,0x18,0x32,0x23,0xfa,0xfc,
		 0x17,0x23,0xde,0x4f,0x52,0xc4,0x3d,0x35}},
	{/*32,*/{0x7c,0x39,0x56,0xca,0x5e,0xea,0xfc,0x3e,
		 0x36,0x3e,0x9d,0x55,0x65,0x46,0xeb,0x68}},
	{/*33,*/{0x77,0xc6,0x07,0x71,0x46,0xf0,0x1c,0x32,
		 0xb6,0xb6,0x9d,0x5f,0x4e,0xa9,0xff,0xcf}},
	{/*34,*/{0x37,0xa6,0x98,0x6c,0xb8,0x84,0x7e,0xdf,
		 0x09,0x25,0xf0,0xf1,0x30,0x9b,0x54,0xde}},
	{/*35,*/{0xa7,0x05,0xf0,0xe6,0x9d,0xa9,0xa8,0xf9,
		 0x07,0x24,0x1a,0x2e,0x92,0x3c,0x8c,0xc8}},
	{/*36,*/{0x3d,0xc4,0x7d,0x1f,0x29,0xc4,0x48,0x46,
		 0x1e,0x9e,0x76,0xed,0x90,0x4f,0x67,0x11}},
	{/*37,*/{0x0d,0x62,0xbf,0x01,0xe6,0xfc,0x0e,0x1a,
		 0x0d,0x3c,0x47,0x51,0xc5,0xd3,0x69,0x2b}},
	{/*38,*/{0x8c,0x03,0x46,0x8b,0xca,0x7c,0x66,0x9e,
		 0xe4,0xfd,0x5e,0x08,0x4b,0xbe,0xe7,0xb5}},
	{/*39,*/{0x52,0x8a,0x5b,0xb9,0x3b,0xaf,0x2c,0x9c,
		 0x44,0x73,0xcc,0xe5,0xd0,0xd2,0x2b,0xd9}},
	{/*40,*/{0xdf,0x6a,0x30,0x1e,0x95,0xc9,0x5d,0xad,
		 0x97,0xae,0x0c,0xc8,0xc6,0x91,0x3b,0xd8}},
	{/*41,*/{0x80,0x11,0x89,0x90,0x2c,0x85,0x7f,0x39,
		 0xe7,0x35,0x91,0x28,0x5e,0x70,0xb6,0xdb}},
	{/*42,*/{0xe6,0x17,0x34,0x6a,0xc9,0xc2,0x31,0xbb,
		 0x36,0x50,0xae,0x34,0xcc,0xca,0x0c,0x5b}},
	{/*43,*/{0x27,0xd9,0x34,0x37,0xef,0xb7,0x21,0xaa,
		 0x40,0x18,0x21,0xdc,0xec,0x5a,0xdf,0x89}},
	{/*44,*/{0x89,0x23,0x7d,0x9d,0xed,0x9c,0x5e,0x78,
		 0xd8,0xb1,0xc9,0xb1,0x66,0xcc,0x73,0x42}},
	{/*45,*/{0x4a,0x6d,0x80,0x91,0xbf,0x5e,0x7d,0x65,
		 0x11,0x89,0xfa,0x94,0xa2,0x50,0xb1,0x4c}},
	{/*46,*/{0x0e,0x33,0xf9,0x60,0x55,0xe7,0xae,0x89,
		 0x3f,0xfc,0x0e,0x3d,0xcf,0x49,0x29,0x02}},
	{/*47,*/{0xe6,0x1c,0x43,0x2b,0x72,0x0b,0x19,0xd1,
		 0x8e,0xc8,0xd8,0x4b,0xdc,0x63,0x15,0x1b}},
	{/*48,*/{0xf7,0xe5,0xae,0xf5,0x49,0xf7,0x82,0xcf,
		 0x37,0x90,0x55,0xa6,0x08,0x26,0x9b,0x16}},
	{/*49,*/{0x43,0x8d,0x03,0x0f,0xd0,0xb7,0xa5,0x4f,
		 0xa8,0x37,0xf2,0xad,0x20,0x1a,0x64,0x03}},
	{/*50,*/{0xa5,0x90,0xd3,0xee,0x4f,0xbf,0x04,0xe3,
		 0x24,0x7e,0x0d,0x27,0xf2,0x86,0x42,0x3f}},
	{/*51,*/{0x5f,0xe2,0xc1,0xa1,0x72,0xfe,0x93,0xc4,
		 0xb1,0x5c,0xd3,0x7c,0xae,0xf9,0xf5,0x38}},
	{/*52,*/{0x2c,0x97,0x32,0x5c,0xbd,0x06,0xb3,0x6e,
		 0xb2,0x13,0x3d,0xd0,0x8b,0x3a,0x01,0x7c}},
	{/*53,*/{0x92,0xc8,0x14,0x22,0x7a,0x6b,0xca,0x94,
		 0x9f,0xf0,0x65,0x9f,0x00,0x2a,0xd3,0x9e}},
	{/*54,*/{0xdc,0xe8,0x50,0x11,0x0b,0xd8,0x32,0x8c,
		 0xfb,0xd5,0x08,0x41,0xd6,0x91,0x1d,0x87}},
	{/*55,*/{0x67,0xf1,0x49,0x84,0xc7,0xda,0x79,0x12,
		 0x48,0xe3,0x2b,0xb5,0x92,0x25,0x83,0xda}},
	{/*56,*/{0x19,0x38,0xf2,0xcf,0x72,0xd5,0x4e,0xe9,
		 0x7e,0x94,0x16,0x6f,0xa9,0x1d,0x2a,0x36}},
	{/*57,*/{0x74,0x48,0x1e,0x96,0x46,0xed,0x49,0xfe,
		 0x0f,0x62,0x24,0x30,0x16,0x04,0x69,0x8e}},
	{/*58,*/{0x57,0xfc,0xa5,0xde,0x98,0xa9,0xd6,0xd8,
		 0x00,0x64,0x38,0xd0,0x58,0x3d,0x8a,0x1d}},
	{/*59,*/{0x9f,0xec,0xde,0x1c,0xef,0xdc,0x1c,0xbe,
		 0xd4,0x76,0x36,0x74,0xd9,0x57,0x53,0x59}},
	{/*60,*/{0xe3,0x04,0x0c,0x00,0xeb,0x28,0xf1,0x53,
		 0x66,0xca,0x73,0xcb,0xd8,0x72,0xe7,0x40}},
	{/*61,*/{0x76,0x97,0x00,0x9a,0x6a,0x83,0x1d,0xfe,
		 0xcc,0xa9,0x1c,0x59,0x93,0x67,0x0f,0x7a}},
	{/*62,*/{0x58,0x53,0x54,0x23,0x21,0xf5,0x67,0xa0,
		 0x05,0xd5,0x47,0xa4,0xf0,0x47,0x59,0xbd}},
	{/*63,*/{0x51,0x50,0xd1,0x77,0x2f,0x50,0x83,0x4a,
		 0x50,0x3e,0x06,0x9a,0x97,0x3f,0xbd,0x7c}}
}; // }}}

static void a_md__siphash(void);

template<uz KSZ, uz DGSTSZ, uz INSZ_MAX, class TDAT>
static void a_md__test(char const *name, md *mdp, TDAT const *tdat);

// test vtbl, class {{{
class a_md__sade;

static void *a_md__new(u32 estate);
static void a_md__del(void *self);
static up a_md__prop(void const *self, enum su_md_prop prop);
static s32 a_md__setup(void *self, void const *k, uz kl, uz ds);

static struct su_md_vtbl const a_md__sade = {
	FIN(mdvtbl_mdvtbl_new) &a_md__new,
	FIN(mdvtbl_mdvtbl_del) &a_md__del,
	FIN(mdvtbl_property) &a_md__prop,
	FIN(mdvtbl_setup) &a_md__setup,
	FIN(mdvtbl_update) R(void(*)(void*,void const*,uz),&su_siphash_update),
	FIN(mdvtbl_end) R(void(*)(void*,void*),&su_siphash_end)
};

static void *
a_md__new(u32 estate){
	return su_TALLOCF(struct su_siphash, 1, estate);
}

static void
a_md__del(void *self){
	su_FREE(self);
}

static up
a_md__prop(void const *self, enum su_md_prop prop){
	up rv;
	UNUSED(self);
	switch(S(uz,prop)){
	case su_MD_PROP_ALGO: rv = su_MD_ALGO_EXTRA; break;
	case su_MD_PROP_NAME: rv = R(up,"sade"); break;
	case su_MD_PROP_DISPLAY_NAME: rv = R(up,"Sade"); break;
	case su_MD_PROP_KEY_SIZE_MIN: rv = su_SIPHASH_KEY_SIZE; break;
	case su_MD_PROP_KEY_SIZE_MAX: rv = su_SIPHASH_KEY_SIZE; break;
	case su_MD_PROP_DIGEST_SIZE_MIN: rv = su_SIPHASH_DIGEST_SIZE_64; break;
	case su_MD_PROP_DIGEST_SIZE_MAX: rv = su_SIPHASH_DIGEST_SIZE_64; break;
	case su_MD_PROP_BLOCK_SIZE: rv = su_SIPHASH_BLOCK_SIZE; break;
	default: rv = S(up,-1); break;
	}
	return rv;
}

static s32
a_md__setup(void *self, void const *k, uz kl, uz ds){
	s32 rv;
	if(kl != su_SIPHASH_KEY_SIZE || ds != su_SIPHASH_DIGEST_SIZE_64)
		rv = err::inval;
	else
		rv = su_siphash_setup(S(struct su_siphash*,self), k);
	return rv;
}

class a_md__sade : public md{
	su_siphash m_sh;
public:
	a_md__sade(void) : m_sh() {}

	OVRX(~a_md__sade(void)) {}

	OVRX(up property(prop prop) const){
		return a_md__prop(NIL, S(su_md_prop,prop));
	}

	OVRX(s32 setup(void const *key, uz key_len, uz digest_size)){
		return a_md__setup(&m_sh, key, key_len, digest_size);
	}

	OVRX(void update(void const *dat, uz dat_len)){
		su_siphash_update(&m_sh, dat, dat_len);
	}

	OVRX(void end(void *store)) {su_siphash_end(&m_sh, store);}

	static md *create(u32 estate){
		UNUSED(estate);
		return su_NEW(a_md__sade);
	}
};
// }}}
#endif // su_HAVE_MD

static void
a_md(void){
#ifdef su_HAVE_MD
	a_md__siphash();

	md *mdp;

	a_md__test<siphash::key_size, siphash::digest_size_64, NELEM(a_siphash_t64), struct a_siphash_t64>
		("siphash", md::new_by_algo(md::algo_siphash), &a_siphash_t64[0]);

	a_md__test<siphash::key_size, siphash::digest_size_128, NELEM(a_siphash_t128), struct a_siphash_t128>
		("siphash", md::new_by_algo(md::algo_siphash), &a_siphash_t128[0]);

	a_md__test<siphash::key_size, siphash::digest_size_64, NELEM(a_siphash_t64), struct a_siphash_t64>
		("siphash", md::new_by_name("siphash"), &a_siphash_t64[0]);

	a_md__test<siphash::key_size, siphash::digest_size_128, NELEM(a_siphash_t128), struct a_siphash_t128>
		("siphash", md::new_by_name("siphash"), &a_siphash_t128[0]);

	//

	if((mdp = md::new_by_name("sade")) != NIL)
		a_ERR();

	if(su_md_install("sade", &a_md__sade, su_STATE_NONE) != su_ERR_NONE)
		a_ERR();
	{
		a_md__test<siphash::key_size, siphash::digest_size_64, NELEM(a_siphash_t64), struct a_siphash_t64>
			("sade", md::new_by_name("sade"), &a_siphash_t64[0]);
	}
	if(!su_md_uninstall("sade", &a_md__sade))
		a_ERR();

	if((mdp = md::new_by_name("sade")) != NIL)
		a_ERR();

	if(md::install("sade", &a_md__sade::create, state::none) != err::none)
		a_ERR();
	{
		a_md__test<siphash::key_size, siphash::digest_size_64, NELEM(a_siphash_t64), struct a_siphash_t64>
			("sade", md::new_by_name("sade"), &a_siphash_t64[0]);
	}
	if(!md::uninstall("sade", &a_md__sade::create))
		a_ERR();

	if((mdp = md::new_by_name("sade")) != NIL)
		a_ERR();
#endif // su_HAVE_MD
}

#ifdef su_HAVE_MD
static void
a_md__siphash(void){ // {{{
	for(uz idx = 0; idx < NELEM(a_siphash_t64); ++idx){
		u8 key[siphash::key_size], digest[siphash::digest_size_max], in[NELEM(a_siphash_t64)];
		siphash sh;

		for(uz i = 0; i < sizeof(key); ++i)
			key[i] = S(u8,i);

		LCTAV(NELEM(a_siphash_t64) <= max::u8);
		for(u8 i = 0; i < idx; ++i)
			in[i] = i;

		sh.setup(key);
		sh.update(in, idx);
		mem::set(digest, '@', sizeof digest);
		sh.end(digest);
		if(sh.digest_size() != siphash::digest_size_64)
			a_ERR();
		if(mem::cmp(digest, a_siphash_t64[idx].rdat, sh.digest_size()))
			a_ERR();

		mem::set(digest, '@', sizeof digest);
		siphash::once(digest, key, in, idx);
		if(mem::cmp(digest, a_siphash_t64[idx].rdat, siphash::digest_size_64))
			a_ERR();

		sh.setup(key, siphash::digest_128);
		sh.update(in, idx);
		mem::set(digest, '@', sizeof digest);
		sh.end(digest);
		if(sh.digest_size() != siphash::digest_size_128)
			a_ERR();
		if(mem::cmp(digest, a_siphash_t128[idx].rdat, sh.digest_size()))
			a_ERR();

		mem::set(digest, '@', sizeof digest);
		siphash::once(digest, key, in, idx, siphash::digest_128);
		if(mem::cmp(digest, a_siphash_t128[idx].rdat, siphash::digest_size_128))
			a_ERR();
		// more complicated stuff by generic md test!
	}
} // }}}

template<uz KSZ, uz DGSTSZ, uz INSZ_MAX, class TDAT>
static void
a_md__test(char const *name, md *mdp, TDAT const *tdat){ // {{{
	if(mdp == NIL){
		a_ERR();
		goto j_leave;
	}

	if(cs::cmp(name, mdp->name())){
		a_ERR();
		goto jleave;
	}

	if(mdp->property(md::prop_key_size_min) > KSZ || mdp->property(md::prop_key_size_max) < KSZ){
		a_ERR();
		goto jleave;
	}

	if(mdp->property(md::prop_digest_size_min) > DGSTSZ || mdp->property(md::prop_digest_size_max) < DGSTSZ){
		a_ERR();
		goto jleave;
	}

	for(TDAT const *tp = &tdat[0]; tp < &tdat[INSZ_MAX]; ++tp){
		u8 key[KSZ], digest[DGSTSZ], in[INSZ_MAX];
		uz i;

		for(i = 0; i < KSZ; ++i)
			key[i] = S(u8,i);

		LCTAV(INSZ_MAX <= max::u8);
		for(u8 j = 0; j < P2UZ(tp - &tdat[0]); ++j)
			in[j] = j;

		// once
		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		mdp->update(in, P2UZ(tp - &tdat[0]));
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();

		// bytewise
		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		for(i = 0; i < P2UZ(tp - &tdat[0]); ++i)
			mdp->update(&in[i], 1);
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();

		// 2-bytewise
		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		for(i = 0; i + 2 < P2UZ(tp - &tdat[0]); i += 2)
			mdp->update(&in[i], 2);
		for(; i < P2UZ(tp - &tdat[0]); ++i)
			mdp->update(&in[i], 1);
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();

		// 3-bytewise
		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		for(i = 0; i + 3 < P2UZ(tp - &tdat[0]); i += 3)
			mdp->update(&in[i], 3);
		for(; i < P2UZ(tp - &tdat[0]); ++i)
			mdp->update(&in[i], 1);
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();

		// blockwise
		uz blk = mdp->property(mdp->prop_block_size);

		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		for(i = 0; i + blk < P2UZ(tp - &tdat[0]); i += blk)
			mdp->update(&in[i], blk);
		for(; i < P2UZ(tp - &tdat[0]); ++i)
			mdp->update(&in[i], 1);
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();

		// ... blockwise + 1
		++blk;
		if(mdp->setup(key, KSZ, DGSTSZ) != err::none){
			a_ERR();
			goto jleave;
		}
		for(i = 0; i + blk < P2UZ(tp - &tdat[0]); i += blk)
			mdp->update(&in[i], blk);
		for(; i < P2UZ(tp - &tdat[0]); ++i)
			mdp->update(&in[i], 1);
		mem::set(digest, '@', sizeof digest);
		mdp->end(digest);
		if(mem::cmp(digest, tp->rdat, DGSTSZ))
			a_ERR();
	}

jleave:
	su_DEL(mdp);
j_leave:;
} // }}}
#endif // su_HAVE_MD
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

// path_cs {{{
static void
a_path_cs(void){
	// Tests from POSIX manual
	char buf[80];
	char const *bp;

	// basename
#undef X
#define X(S,B) mem::copy(buf, S, sizeof(S)); bp = path::basename(buf); if(cs::cmp(bp, B)) a_ERR()
	X("usr", "usr");
	X("usr/", "usr");
	X("usr///////", "usr");
	X("", ".");
	X("/", "/");
#if su_OS_POSIX
	X("//", "//");
#endif
	X("///", "/");
	X("/usr/", "usr");
	X("/usr/lib", "lib");
	X("//usr//lib//", "lib");
	X("/home//dwc//test", "test");

	// dirname
#undef X
#define X(S,B) mem::copy(buf, S, sizeof(S)); bp = path::dirname(buf); if(cs::cmp(bp, B)) a_ERR()
	X("usr", ".");
	X("usr/", ".");
	X("usr///////", ".");
	X("", ".");
	X("/", "/");
#if su_OS_POSIX
	X("//", "//");
#endif
	X("///", "/");
	X("/usr/", "/");
	X("/usr/lib", "/usr");
	X("//usr//lib//", "//usr");
	X("/home//dwc//test", "/home//dwc");
#undef X
}
// }}}

// prime {{{
static void
a_prime(void){
	u32 u32 = prime::lookup_next(0);
	if(u32 != prime::lookup_min)
		a_ERR();
	u64 u64_2, u64 = prime::get_next(u32);
	if(u32 == u64 || (u32 == 2 && u64 != 3))
		a_ERR();
	u64_2 = prime::get_next(u64, FAL0);
	if(u64 == u64_2)
		a_ERR();
	u64 = prime::get_former(u64_2 + 1, FAL0);
	if(u64 != u64_2)
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
			"albert", "berta", "david", "emil", "friedrich", "gustav", "heinrich", "isidor"
	}, *arr_mixed[] = {
			"gustav", "david", "isidor", "friedrich", "berta", "albert", "heinrich", "emil"
	};

	sort::shell(arr_mixed, NELEM(arr_mixed), &cs::cmp);

	for(uz i = NELEM(arr_sorted); i-- != 0;)
		if(cs::cmp(arr_sorted[i], arr_mixed[i]))
			a_ERR();
}
// }}}

// random {{{
static uz a_random_i;

static boole a_random__gfun(void **cookie, void *buf, uz len);

static void
a_random(void){ // xxx too late, already initialized...
	char buf[64];

	{
		char buf1[NELEM(buf)], buf2[NELEM(buf)], buf3[NELEM(buf)];
		random r0;
		if(r0.create(random::type_r) != state::none)
			a_ERR();
		if(r0.type() != random::type_r)
			a_ERR();
		if(r0.reseed_after() != 0)
			a_ERR();
		if(!r0.seed())
			a_ERR();
		if(!r0.generate(buf, sizeof buf))
			a_ERR();
		if(!r0.generate(buf1, sizeof buf1))
			a_ERR();

		if(!r0.seed())
			a_ERR();
		if(!r0(buf2, sizeof buf2))
			a_ERR();
		if(!r0(buf3, sizeof buf3))
			a_ERR();

		if(mem::cmp(buf, buf2, sizeof buf))
			a_ERR();
		if(mem::cmp(buf1, buf3, sizeof buf1))
			a_ERR();
	}

	{
		char buf1[NELEM(buf)];
		random r1;
		if(r1.create(random::type_p) != state::none)
			a_ERR();
		if(r1.type() != random::type_p)
			a_ERR();
		if(r1.reseed_after() != 0)
			a_ERR();
		if(!r1.seed())
			a_ERR();
		if(!r1.generate(buf, sizeof buf))
			a_ERR();

		if(!r1.seed())
			a_ERR();
		if(!r1(buf1, sizeof buf1))
			a_ERR();
		if(!mem::cmp(buf, buf1, sizeof buf))
			a_ERR();
	}

	{
		random r2;
		if(r2.create() != state::none)
			a_ERR();
		if(r2.type() != random::type_sp)
			a_ERR();
		if(r2.reseed_after() != 0)
			a_ERR();
		if(r2.set_reseed_after(1024).reseed_after() != 1024)
			a_ERR();
		if(!r2.seed())
			a_ERR();
		if(!r2.generate(buf, sizeof buf))
			a_ERR();
		if(!r2(buf, sizeof buf))
			a_ERR();

		random r3;
		if(r3.create(random::type_vsp) != state::none)
			a_ERR();
		if(r3.type() != random::type_vsp)
			a_ERR();
		if(r3.reseed_after() == 0)
			a_ERR();
		if(r3.set_reseed_after(0).reseed_after() != 0)
			a_ERR();
		if(!r3.seed(r2))
			a_ERR();
		if(!r3.generate(buf, sizeof buf))
			a_ERR();
		if(!r3(buf, sizeof buf))
			a_ERR();
	}

	{
		if(random::vsp_install(&a_random__gfun) != state::none)
			a_ERR();

		a_random_i = 0;
		{
			char buf2[8];
			random r1;
			if(r1.create(random::type_vsp) != state::none)
				a_ERR();
			if(r1.type() != random::type_vsp)
				a_ERR();
			if(r1.reseed_after() == 0)
				a_ERR();
			if(a_random_i != 0)
				a_ERR();
			if(!r1.generate(buf2, 0)) // no-op if len==0
				a_ERR();
			if(a_random_i != 0)
				a_ERR();
			if(!r1.seed())
				a_ERR();
			if(a_random_i != 1)
				a_ERR();
			if(r1.generate(buf2, sizeof buf2))
				a_ERR();
			if(!r1.generate(buf2, sizeof buf2))
				a_ERR();
			if(a_random_i != (1 | 2))
				a_ERR();
			if(mem::cmp(buf2, "abrababe", sizeof buf2))
				a_ERR();
			if(!r1(buf2, sizeof buf2))
				a_ERR();
			if(mem::cmp(buf2, "abrababe", sizeof buf2))
				a_ERR();
		}
		if(a_random_i != (1 | 2 | 4))
			a_ERR();

		if(random::vsp_install(NIL, state::err_pass) != state::none)
			a_ERR();
	}

	if(random::builtin_generate(buf, sizeof buf, state::err_pass) != state::none)
		a_ERR();

	if(!random::builtin_seed(TRU1))
		a_ERR();
	if(!random::builtin_seed(FAL0))
		a_ERR();

	if(!random::builtin_set_reseed_after(TRU1, 0)) // xxx useless, only link-test
		a_ERR();
	if(!random::builtin_set_reseed_after(FAL0, 0))
		a_ERR();
}

static boole
a_random__gfun(void **cookie, void *buf, uz len){
	boole rv;

	rv = TRU1;

	if(*cookie == NIL){
		a_random_i |= 1;
		if(buf != NIL || len != 0)
			a_ERR();
		*cookie = R(random*,0x4221);
	}else if(buf == NIL){
		a_random_i |= 4;
		if(*cookie != R(random*,0x4221) || len != 0)
			a_ERR();
	}else{
		if(len != sizeof("abrababe") -1)
			a_ERR();
		if(a_random_i & 2)
			mem::copy(buf, "abrababe", len);
		else
			rv = FAL0;
		a_random_i |= 2;
	}

	return rv;
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
static void a_time__utils(void);
static void a_time__spec(void);

static void
a_time(void){
	a_time__utils();
	a_time__spec();
}

static void
a_time__utils(void){
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
a_time__spec(void){
	if(time::spec::sec_millis != 1000l) a_ERR();
	if(time::spec::sec_micros != 1000l * 1000) a_ERR();
	if(time::spec::sec_nanos != 1000l * 1000 * 1000) a_ERR();

	time::spec ts;
	// add/sub tested at the end

	if(!ts.current().is_valid())
		a_ERR();

	time::spec ts2(ts);
	if(!ts2.is_valid()) a_ERR();
	if(ts2.sec() != ts.sec()) a_ERR();
	if(ts2.nano() != ts.nano()) a_ERR();
	if(ts.cmp(ts2) != 0) a_ERR();
	if(ts2.cmp(ts) != 0) a_ERR();
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
	if(ts.cmp(ts2) <= 0) a_ERR();
	if(ts2.cmp(ts) >= 0) a_ERR();
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
	if(ts.cmp(ts2) >= 0) a_ERR();
	if(ts2.cmp(ts) <= 0) a_ERR();
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
	if(ts.cmp(ts2) != 0) a_ERR();
	if(ts2.cmp(ts) != 0) a_ERR();
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
	if(ts.cmp(ts2) <= 0) a_ERR();
	if(ts2.cmp(ts) >= 0) a_ERR();
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
	if(ts.cmp(ts2) >= 0) a_ERR();
	if(ts2.cmp(ts) <= 0) a_ERR();
	if(ts2 == ts) a_ERR();
	if(!(ts2 != ts)) a_ERR();
	if(ts2 < ts) a_ERR();
	if(ts2 <= ts) a_ERR();
	if(!(ts2 >= ts)) a_ERR();
	if(!(ts2 > ts)) a_ERR();

	/* add/sub */
	ts.sec(30).nano(ts.sec_nanos - 10);
	if(!ts.is_valid()) a_ERR();
	if(ts.sec() != 30) a_ERR();
	if(ts.nano() != ts.sec_nanos - 10) a_ERR();

	if(!(ts2 = ts).is_valid()) a_ERR();
	if(ts2.sec() != 30) a_ERR();
	if(ts2.nano() != ts2.sec_nanos - 10) a_ERR();

	if(!ts2.add(ts2).is_valid()) a_ERR();
	if(ts2.sec() != 61) a_ERR();
	if(ts2.nano() != ts2.sec_nanos - 20) a_ERR();

	if(!(ts2 = ts).sub(ts2).is_valid()) a_ERR();
	if(ts2.sec() != 0) a_ERR();
	if(ts2.nano() != 0) a_ERR();

	ts2 = ts;
	ts.sec(ts.sec() + 1);
	ts2.nano(ts2.nano() + 11);
	if(!ts.sub(ts2).is_valid()) a_ERR();
	if(ts.sec() != 0) a_ERR();
	if(ts.nano() != ts.sec_nanos - 11) a_ERR();
}
// }}}

// utf {{{
static void
a_utf(void){
	if(cs::cmp(utf8::name, su_utf8_name) || cs::cmp(utf8::name, su_UTF8_NAME))
		a_ERR();
	if(cs::cmp(utf32::name, su_utf32_name) || cs::cmp(utf32::name, su_UTF32_NAME))
		a_ERR();

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
// s-itt-mode
