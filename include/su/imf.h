/*@ Internet Message Format (RFC 822 -> 2822 -> 5322) parser.
 *
 * Copyright (c) 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_IMF_H
#define su_IMF_H

/*!
 * \file
 * \ingroup IMF
 * \brief \r{IMF}
 */

#include <su/code.h>

#if defined su_HAVE_IMF || defined DOXYGEN /*XXX DOXYGEN bug; ifdef su_HAVE_IMF*/
#include <su/mem-bag.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_imf_addr;

/* su_imf_addr {{{ */
/*!
 * \defgroup IMF Internet Message Format
 * \ingroup NET
 * \brief Internet Message Format parser (\r{su/imf.h})
 *
 * \list{\li{
 * The parser is meant to be used "as a second stage" that is fed in superficially preparsed content.
 * For example it does not verify that follow-up header lines start with white space.
 * }\li{
 * Deviating from the standard \c{FWS} includes plain \c{LF} and \c{CR} bytes in addition to \c{CRLF}.
 * }\li{
 * "Postel's parser" as for example \c{CFWS} is accepted almost everywhere.
 * It is assumed that there is a desire to parse the message, if at all possible.
 * }\li{
 * Route addresses are foiled to the last hop, the real address.
 * (As via RFC 5322, "4.4. Obsolete Addressing".)
 * }\li{
 * Any whitespace in result strings is normalized.
 * }\li{
 * Quotations in results strings are normalized.
 * This means that presence of (or, with \r{su_IMF_MODE_OK_DISPLAY_NAME_DOT}, necessity for) quotation marks will
 * result in the result to be embraced as such by a single pair of quotation marks.
 * }\li{
 * Comments in display names of address groups are discarded (after being parsed correctly),
 * just like comments "in the void" (like \c{..., (comment), ...}).
 * }\li{
 * ASCII NUL aka bytes with value 0 are never supported in bodies.
 * }}
 *
 * \remarks{Only available if \r{su_HAVE_IMF} is defined.}
 * @{
 */

/*! Shares bit range with \r{su_imf_state} and \r{su_imf_err}. */
enum su_imf_mode{
	su_IMF_MODE_NONE, /*!< Nothing (this is 0). */
	su_IMF_MODE_RELAX = 1u<<0, /*!< Ignore some errors which would otherwise cause hard failure. */
	/*! Ok \c{.} in display-name, support \c{Dr. X &lt;y&#40;z&gt;} user input. */
	su_IMF_MODE_OK_DISPLAY_NAME_DOT = 1u<<1,
	/*! Ok plain user name in angle-bracket address, that is \c{&lt;USERNAME&gt;}, without domain name.
	 * The validity of \c{USERNAME} and its expansion is up to the caller. */
	su_IMF_MODE_OK_ADDR_SPEC_NO_DOMAIN = 1u<<2,
	/*! Stop parsing whenever the first address has been successfully parsed.
	 * \remarks{Empty groups (\c{Undisclosed recipients:;}) do not count as addresses.} */
	su_IMF_MODE_STOP_EARLY = 1u<<7,
	su__IMF_MODE_MASK = 0xFF
};

/*! Shares bit range with \r{su_imf_mode} and \r{su_imf_err}. */
enum su_imf_state{
	/*! Errors were ignored due to \r{su_IMF_MODE_RELAX}.
	 * \r{su_imf_err} bits (but \r{su_IMF_ERR_RELAX}) are set.
	 * Be aware that with this the plain empty string \c{:} is a "valid" empty group. */
	su_IMF_STATE_RELAX = 1u<<8,
	/*!< \r{su_IMF_MODE_OK_DISPLAY_NAME_DOT}, and an unquoted . was seen; result is correctly quoted. */
	su_IMF_STATE_DISPLAY_NAME_DOT = 1u<<9,
	/*! \r{su_IMF_MODE_OK_ADDR_SPEC_NO_DOMAIN}, \c{&lt;USERNAME&gt;} was seen. */
	su_IMF_STATE_ADDR_SPEC_NO_DOMAIN = 1u<<10,
	/*! \r{su_imf_addr::imfa_domain} is a (possibly empty) literal; surrounding brackets are not stripped. */
	su_IMF_STATE_DOMAIN_LITERAL = 1u<<11,
	su_IMF_STATE_GROUP = 1u<<12, /*!< Belongs to a group (that maybe starts and/or ends). */
	su_IMF_STATE_GROUP_START = 1u<<13, /*!< Group start. */
	su_IMF_STATE_GROUP_END = 1u<<14, /*!< Group end. */
	su_IMF_STATE_GROUP_EMPTY = 1u<<15, /*!< Group without address, for example \c{Undisclosed recipients:;}. */
	/*! A mask of all states except \r{su_IMF_STATE_RELAX}. */
	su_IMF_STATE_MASK = su_IMF_STATE_DISPLAY_NAME_DOT | su_IMF_STATE_ADDR_SPEC_NO_DOMAIN |
			su_IMF_STATE_DOMAIN_LITERAL | su_IMF_STATE_GROUP | su_IMF_STATE_GROUP_START |
			su_IMF_STATE_GROUP_END | su_IMF_STATE_GROUP_EMPTY
};

/*! Shares bit range with \r{su_imf_mode} and \r{su_imf_state}. */
enum su_imf_err{
	su_IMF_ERR_RELAX = 1u<<20, /*!< Errors could have been or were ignored with or due \r{su_IMF_MODE_RELAX}. */
	su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY = 1u<<21, /*!< Group display-name must not be empty, but is. */
	su_IMF_ERR_DISPLAY_NAME_DOT = 1u<<22, /*!< \c{.} in display-name, no \r{su_IMF_MODE_OK_DISPLAY_NAME_DOT}. */
	su_IMF_ERR_ADDR_SPEC = 1u<<23, /*!< Invalid or empty address content; also: invalid route; also: fallback. */
	su_IMF_ERR_COMMENT = 1u<<24, /*!< Comment content invalid, or comment not closed. */
	su_IMF_ERR_DQUOTE = 1u<<25, /*!< Quoted-string \c{".."} content invalid, or quote not closed. */
	su_IMF_ERR_GROUP_OPEN = 1u<<26, /*!< A group was not closed. */
	/*! A mask of all errors except \r{su_IMF_ERR_RELAX}. */
	su_IMF_ERR_MASK = su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY | su_IMF_ERR_DISPLAY_NAME_DOT | su_IMF_ERR_ADDR_SPEC |
			su_IMF_ERR_COMMENT | su_IMF_ERR_DQUOTE | su_IMF_ERR_GROUP_OPEN
};

/*! \_ */
struct su_imf_addr{
	struct su_imf_addr *imfa_next; /*!< In case of address lists and/or groups. */
	char *imfa_group_display_name; /*!< Only with \r{su_IMF_STATE_GROUP_START}. */
	char *imfa_display_name; /*!< Any display-name content, joined together. */
	char *imfa_locpar; /*!< Local part of address. */
	char *imfa_domain; /*!< Domain or domain literal (with \r{su_IMF_STATE_DOMAIN_LITERAL}). */
	char *imfa_comm; /*!< Any comment content, joined together. */
	u32 imfa_group_display_name_len; /*!< \_ */
	u32 imfa_display_name_len; /*!< \_ */
	u32 imfa_locpar_len; /*!< \_ */
	u32 imfa_domain_len; /*!< \_ */
	u32 imfa_comm_len; /*!< \_ */
	u32 imfa_mse; /*!< Bitmix of \r{su_imf_mode}, \r{su_imf_state} and \r{su_imf_err}. */
	char imfa_dat[VFIELD_SIZE(0)]; /* Storage for any text (single chunk struct) */
};

/*! Create a snap for the bag type \r{IMF} uses; see \r{su_imf_parse_addr_header()}. */
INLINE void *su_imf_snap_create(struct su_mem_bag *membp){
	ASSERT_RET(membp != NIL, NIL);
#ifdef su_HAVE_MEM_BAG_LOFI
	return su_mem_bag_lofi_snap_create(membp);
#else
	su_mem_bag_auto_snap_create(membp);
	return NIL;
#endif
}

/*! Gut the snap created by \r{su_imf_snap_create()}. */
INLINE void su_imf_snap_gut(struct su_mem_bag *membp, void *snap){
	ASSERT_RET_VOID(membp != NIL);
#ifdef su_HAVE_MEM_BAG_LOFI
	su_mem_bag_lofi_snap_gut(membp, snap);
#else
	UNUSED(snap);
	su_mem_bag_auto_snap_gut(membp);
#endif
}

/*! Parse an (possibly multiline) Internet Message Format address(-list / mailbox(-list)) header field body.
 * Stores a result list in \a{*app}, or \NIL if nothing can be parsed.
 * Results may contain \c{IMF_ERR_} entries with \r{su_IMF_MODE_RELAX}, or according to \c{IMF_MODE_OK_*}.
 * If \a{endptr_or_nil} is set it will point to where parsing stopped (points to \NUL but in error cases).
 * Returns \ERR{NONE} on success, -\ERR{NODATA} on empty (or only whitespace) input, -\ERR{OVERFLOW} if input is too
 * long, or -\ERR{NOMEM} if an allocation failed;
 * A positive return represents a \r{su_imf_state} and \r{su_imf_err} bitmix of the parse error.
 *
 * Any result address, including any text, is stored as a single memory chunk: either via \r{su_HAVE_MEM_BAG_LOFI}
 * if available, via \r{su_HAVE_MEM_BAG_AUTO} otherwise: "normal" heap is not supported.
 * That is to say that creating a snap, copy over results, and then gut() the entire memory is needed or possible;
 * \r{su_imf_snap_create()} and \r{su_imf_snap_gut()} can be used for this. */
EXPORT s32 su_imf_parse_addr_header(struct su_imf_addr **app, char const *header, BITENUM(u32,su_imf_mode) mode,
		struct su_mem_bag *membp, char const **endptr_or_nil);
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class imf;
//class imf::addr;

/* imf {{{ */
/*!
 * \ingroup IMF
 * C++ variant of \r{IMF} (\r{su/imf.h})
 */
class imf{
	// friend of imf::match, mem_bag
	su_CLASS_NO_COPY(imf);
public:
	class addr;

	/* addr {{{ */
	/*! \copydoc{su_imf_addr} */
	class addr : private su_imf_addr{
		friend class imf;
	protected:
		addr(void) {}
	public:
		~addr(void) {}

		/*! \r{su_imf_addr::imfa_next} */
		addr *next(void) const {return S(addr*,imfa_next);}

		/*! \r{su_imf_addr::imfa_mse} */
		u32 mse(void) const {return imfa_mse;}

		/*! \r{su_imf_addr::imfa_group_display_name} */
		char const *group_display_name(void) const {return imfa_group_display_name;}

		/*! \r{su_imf_addr::imfa_group_display_name_len} */
		u32 group_display_name_len(void) const {return imfa_group_display_name_len;}

		/*! \r{su_imf_addr::imfa_display_name} */
		char const *display_name(void) const {return imfa_display_name;}

		/*! \r{su_imf_addr::imfa_display_name_len} */
		u32 display_name_len(void) const {return imfa_display_name_len;}

		/*! \r{su_imf_addr::imfa_locpar} */
		char const *locpar(void) const {return imfa_locpar;}

		/*! \r{su_imf_addr::imfa_locpar_len} */
		u32 locpar_len(void) const {return imfa_locpar_len;}

		/*! \r{su_imf_addr::imfa_domain} */
		char const *domain(void) const {return imfa_domain;}

		/*! \r{su_imf_addr::imfa_domain_len} */
		u32 domain_len(void) const {return imfa_domain_len;}

		/*! \r{su_imf_addr::imfa_comm} */
		char const *comm(void) const {return imfa_comm;}

		/*! \r{su_imf_addr::imfa_comm_len} */
		u32 comm_len(void) const {return imfa_comm_len;}
	};
	/* }}} */

private:
#ifndef DOXYGEN
	su_CXXCAST(addr, struct su_imf_addr);
#endif
public:

	/*! \copydoc{su_imf_mode} */
	enum mode{
		mode_none = su_IMF_MODE_NONE, /*!< \copydoc{su_IMF_MODE_NONE} */
		mode_relax = su_IMF_MODE_RELAX, /*!< \copydoc{su_IMF_MODE_RELAX} */
		/*! \copydoc{su_IMF_MODE_OK_DISPLAY_NAME_DOT} */
		mode_ok_display_name_dot = su_IMF_MODE_OK_DISPLAY_NAME_DOT,
		/*! \copydoc{su_IMF_MODE_OK_ADDR_SPEC_NO_DOMAIN} */
		mode_ok_addr_spec_no_domain = su_IMF_MODE_OK_ADDR_SPEC_NO_DOMAIN,
		mode_stop_early = su_IMF_MODE_STOP_EARLY /*!< \copydoc{su_IMF_MODE_STOP_EARLY} */
	};

	/*! \copydoc{su_imf_state} */
	enum state{
		state_relax = su_IMF_STATE_RELAX, /*!< \copydoc{su_IMF_STATE_RELAX} */
		state_display_name_dot = su_IMF_STATE_DISPLAY_NAME_DOT, /*!< \copydoc{su_IMF_STATE_DISPLAY_NAME_DOT} */
		/*! \copydoc{su_IMF_STATE_ADDR_SPEC_NO_DOMAIN} */
		state_addr_spec_no_domain = su_IMF_STATE_ADDR_SPEC_NO_DOMAIN,
		state_domain_literal = su_IMF_STATE_DOMAIN_LITERAL, /*!< \copydoc{su_IMF_STATE_DOMAIN_LITERAL} */
		state_group = su_IMF_STATE_GROUP, /*!< \copydoc{su_IMF_STATE_GROUP} */
		state_group_start = su_IMF_STATE_GROUP_START, /*!< \copydoc{su_IMF_STATE_GROUP_START} */
		state_group_end = su_IMF_STATE_GROUP_END, /*!< \copydoc{su_IMF_STATE_GROUP_END} */
		state_group_empty = su_IMF_STATE_GROUP_EMPTY, /*!< \copydoc{su_IMF_STATE_GROUP_EMPTY} */
		state_mask = su_IMF_STATE_MASK /*!< \copydoc{su_IMF_STATE_MASK} */
	};

	/*! \copydoc{su_imf_err} */
	enum err{
		err_relax = su_IMF_ERR_RELAX, /*!< \copydoc{su_IMF_ERR_RELAX} */
		/*! \copydoc{su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY} */
		err_group_display_name_empty = su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY,
		err_display_name_dot = su_IMF_ERR_DISPLAY_NAME_DOT, /*!< \copydoc{su_IMF_ERR_DISPLAY_NAME_DOT} */
		err_addr_spec = su_IMF_ERR_ADDR_SPEC, /*!< \copydoc{su_IMF_ERR_ADDR_SPEC} */
		err_comment = su_IMF_ERR_COMMENT, /*!< \copydoc{su_IMF_ERR_COMMENT} */
		err_dquote = su_IMF_ERR_DQUOTE, /*!< \copydoc{su_IMF_ERR_DQUOTE} */
		err_group_open = su_IMF_ERR_GROUP_OPEN, /*!< \copydoc{su_IMF_ERR_GROUP_OPEN} */
		err_mask = su_IMF_ERR_MASK /*!< \copydoc{su_IMF_ERR_MASK} */
	};

	/*! \r{su_imf_snap_create()} */
	static void *snap_create(mem_bag &membp) {return su_imf_snap_create(S(struct su_mem_bag*,&membp));}

	/*! \r{su_imf_snap_gut()} */
	static void snap_gut(mem_bag &membp, void *vp) {su_imf_snap_gut(S(struct su_mem_bag*,&membp), vp);}

	/*! \r{su_imf_parse_addr_header()} */
	static s32 parse_addr_header(addr *&app, char const *header, BITENUM(u32,mode) mode, mem_bag &membp,
			char const **endptr_or_nil){
		return su_imf_parse_addr_header(R(su_imf_addr**,&app), header, mode, S(struct su_mem_bag*,&membp),
			endptr_or_nil);
	}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_HAVE_IMF */
#endif /* su_IMF_H */
/* s-itt-mode */
