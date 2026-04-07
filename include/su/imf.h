/*@ Internet Message Format (RFC 733 / 822 -> 2822 -> 5322+6854) parser.
 *
 * Copyright (c) 2024 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
struct su_imf_msgid;
struct su_imf_shtok;

#ifndef su_HAVE_MEM_BAG
# error IMF only with su_HAVE_MEM_BAG
#endif

/* su_imf* {{{ */
/* doc {{{ */
/*!
 * \defgroup IMF Internet Message Format
 * \ingroup NET
 * \brief Internet Message Format parser (\r{su/imf.h})
 *
 * A parser for
 * RFC 5322 (Internet Message Format) plus
 * RFC 6854 (Allow Group Syntax in the "From:" and "Sender:"),
 * available only if \r{su_HAVE_IMF} is defined.
 * Some remarks:
 *
 * \list{\li{
 * "Postel's parser".
 * It is assumed that there is a desire to parse the message, if at all possible.
 * The parser(s) cannot be used to formally verify whether something is valid,
 * even more so with \r{su_IMF_MODE_RELAX}!
 * For example, (long) obsolete syntax is supported,
 * \c{CFWS} is accepted almost everywhere,
 * and where the ABNF syntax says "a OR b OR c" more often than not "a", "b" and "c" can occur intermixed,
 * to accommodate certain misuses seen in the wild.
 * (Low level parsers like su_imf_c_WSP() are available,
 * which can be used to build strict parsers.
 * }\li{
 * The parser is meant to be used "as a second stage" that is fed in superficially preparsed content.
 * For example it does not handle follow-up header lines: whereas it removes the \c{CRLF},
 * it does neither verify a following \c{WSP}, nor does it simply discard that
 * (a problem in \c{quoted-string}s and more).
 * }\li{
 * It uses the rules of RFC 5322 (and, practically, incorporates the updates of RFC 6854).
 * It also includes some erratas, like 3135, in order to not allow effectively empty \c{local-part}s,
 * as well as empty (DNS) sublabels in local-parts.
 * (In parts affected by \r{su_IMF_MODE_RELAX}.)
 * }\li{
 * Deviating from the standard \c{FWS} includes single \c{LF} and \c{CR} bytes in addition to \c{CRLF}.
 * (This for example eases working with \c{sendmail(1)}-style milters which only use \c{LF}.)
 * }\li{
 * Further verifications may be necessary.
 * A successful parse guarantees that only correct(able) tokens have been seen,
 * not whether, for example, a \c{domain-literal} is a useful address in a useful network,
 * or whether a domain name would succeed verification according to the rules of RFCs 1035 and 1123, section 2.1, etc.
 * }\li{
 * Route addresses are foiled to the last hop, the real address.
 * (As via RFC 5322, "4.4. Obsolete Addressing".)
 * }\li{
 * Any whitespace (that is semantically visible) in result strings is normalized to a single \c{SP},
 * except for whitespace within \c{quoted-string}s (according to the RFC).
 * }\li{
 * Quotations in results strings are normalized.
 * This means that presence of (or, with \r{su_IMF_MODE_DISPLAY_NAME_DOT}, necessity for) quotation marks will
 * result in the result to be embraced as such by a single pair of quotation marks (it is requoted).
 * \remarks{Needless and empty quotations are not "normalized away" unless noted otherwise.}
 * }\li{
 * No further normalization occurs.
 * (In particular, domain names may have any case, or be IDNA labels.)
 * }\li{
 * Comments in display names of address groups are discarded (after being parsed correctly),
 * just like comments "in the void" (like \c{..., (comment), ...}).
 * }\li{
 * ASCII \NUL aka bytes with value 0 are never supported in bodies.
 * }}
 *
 * \head1{ABNF}
 *
 * \head2{RFC 5234 (Augmented BNF for Syntax Specifications: ABNF), B.1. Core Rules}
 *
 * \cb{
 * ALPHA  = %x41-5A / %x61-7A; A-Z / a-z
 * BIT    = "0" / "1"
 * CHAR   = %x01-7F; any 7-bit US-ASCII character, excluding NUL
 * CR     = %x0D ; carriage return
 * CRLF   = CR LF; Internet standard newline
 * CTL    = %x00-1F / %x7F; controls
 * DIGIT  = %x30-39; 0-9
 * DQUOTE = %x22; " (Double Quote)
 * HEXDIG = DIGIT / "A" / "B" / "C" / "D" / "E" / "F"
 * HT     = %x09; horizontal tab
 * LF     = %x0A; linefeed
 * [LWSP  = *(WSP / CRLF WSP); Use of this linear-white-space rule
 *              ; permits lines containing only white space that are no longer legal in mail headers
 *              ; and have caused interoperability problems in other contexts.
 *              ; Do not use when defining mail headers and use with caution in other contexts.]
 * OCTET  = %x00-FF; 8 bits of data
 * SP     = %x20
 * VCHAR  = %x21-7E; visible (printing) characters
 * WSP    = SP / HT; white space
 * }
 *
 * \head2{RFC 5322 (Internet Message Format) plus RFC 6854 (Allow Group Syntax in the "From:" and "Sender:")}
 *
 * \cb{
 * CFWS           = (1*([FWS] comment) [FWS]) / FWS
 * FWS            = ([*WSP CRLF] 1*WSP) / obs-FWS; Folding white space; NOTE lone LF, CR are FWS, too
 * addr-spec      = local-part "@" domain
 * address        = mailbox / group
 * address-list   = (address *("," address)) / obs-addr-list
 * angle-addr     = [CFWS] "<" addr-spec ">" [CFWS] / obs-angle-addr
 * atext          = ALPHA / DIGIT / "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "/" / "=" / "?" /
 *                      "^" / "_" / "`" / "{" / "|" / "}" / "~"; Printable US-ASCII less specials; used for "atom"s
 * atom           = [CFWS] 1*atext [CFWS]
 * comment        = "(" *([FWS] ccontent) [FWS] ")"
 * ccontent       = ctext / quoted-pair / comment
 * ctext          = %d33-39 / %d42-91 / %d93-126 / obs-ctext; Printable US-ASCII not including "(", ")", or "\"
 * display-name   = phrase
 * domain         = dot-atom / domain-literal / obs-domain
 * domain-literal = [CFWS] "[" *([FWS] dtext) [FWS] "]" [CFWS]; to be used bypassing normal name-resolution
 * dot-atom       = [CFWS] dot-atom-text [CFWS]
 * dot-atom-text  = 1*atext *("." 1*atext)
 * dtext          = %d33-90 / d94-126 / obs-dtext; Printable US-ASCII not including "[", "]", or "\"
 * group          = display-name ":" [group-list] ";" [CFWS]
 * group-list     = mailbox-list / CFWS / obs-group-list
 * local-part     = dot-atom / quoted-string / obs-local-part
 * mailbox        = name-addr / addr-spec
 * mailbox-list   = (mailbox *("," mailbox)) / obs-mbox-list
 * name-addr      = [display-name] angle-addr
 * phrase         = 1*word / obs-phrase
 * qcontent       = qtext / quoted-pair
 * quoted-pair    = ("\" (VCHAR / WSP)) / obs-qp
 * quoted-string  = [CFWS] DQUOTE *([FWS] qcontent) [FWS] DQUOTE [CFWS]
 * qtext          = %d33 / d35-91 / d93-126 / obs-qtext; Printable US-ASCII excluding "\" and the quote character
 * specials       = "(" / ")" / "<" / ">" / "[" / "]" / ":" / ";" / "@" / "\" / "," / "." / DQUOTE
 *                      ; Special characters that do not appear in "atext"
 * unstructured   = (*([FWS] VCHAR) *WSP) / obs-unstruct
 * word           = atom / quoted-string
 *
 * obs-FWS         = 1*WSP *(CRLF 1*WSP)
 * obs-NO-WS-CTL   = %d1-8 / %d11 / %d12 / %d14-31 / %d127; US-ASCII controls except CR, LF, and whitespace
 * obs-addr-list   = *([CFWS] ",") address *("," [address / CFWS])
 * obs-angle-addr  = [CFWS] "<" obs-route addr-spec ">" [CFWS]
 * obs-ctext       = obs-NO-WS-CTL
 * obs-domain      = atom *("." atom)
 * obs-domain-list = *(CFWS / ",") "@" domain  *("," [CFWS] ["@" domain])
 * obs-dtext       = obs-NO-WS-CTL / quoted-pair
 * obs-group-list  = 1*([CFWS] ",") [CFWS]
 * obs-qtext       = obs-NO-WS-CTL
 * obs-local-part  = word *("." word)
 * obs-mbox-list   = *([CFWS] ",") mailbox *("," [mailbox / CFWS])
 * obs-phrase      = word *(word / "." / CFWS)
 * obs-phrase-list = [phrase / CFWS] *("," [phrase / CFWS])
 * obs-qp          = "\" (%d0 / obs-NO-WS-CTL / LF / CR); NOTE this implementation does not support NUL
 * obs-route       = obs-domain-list ":"
 * obs-unstruct    = *((*LF *CR *(obs-utext *LF *CR)) / FWS)
 * obs-utext       = %d0 / obs-NO-WS-CTL / VCHAR
 * }
 *
 * Additional notes:
 *
 * \list{\li{
 * The parser adheres to RFC 5322 4.4., Obsolete Addressing:
 * "When interpreting addresses, the route portion SHOULD be ignored".
 * }\li{
 * In order to accommodate that "3.2.2. Folding White Space and Comments" allows \c{CFWS} rather freely,
 * Postel'ize that and allow it practically everywhere.
 * (To note standards like DMARC etc allow it in all portions of K=V, different to MIME RFC 2025.
 * }\li{
 * \c{obs-qp} and \c{obs-utext} are supported only WITHOUT NUL.
 * }}
 *
 * \head2{RFC 5322 Message-Id, RFC 2045 Content-ID}
 *
 * \cb{
 * msg-id = [CFWS] "<" id-left "@" id-right ">" [CFWS]
 * id-left = dot-atom-text / obs-id-left
 * id-right = dot-atom-text / no-fold-literal / obs-id-right
 * no-fold-literal = "[" *dtext "]"
 *
 * obs-id-left = local-part
 * obs-id-right = domain
 * }
 * @{
 */
/* }}} */

/* Whether we shall use a table lookup for char classification.
 * If value is S8_MAX we save array space with increased code size, with U8_MAX we simply lookup bytes */
#define su__IMF_TABLE_SIZE su_U8_MAX

/*! Shares bit range with \r{su_imf_state} and \r{su_imf_err}. */
enum su_imf_mode{
	su_IMF_MODE_NONE, /*!< Nothing (this is 0). */

	/*! Ok \c{.} in \c{display-name}, support \c{Dr. X &lt;y&#40;z&gt;} user input (result correctly quoted).
	 * For \r{su_imf_parse_addr_header()}. */
	su_IMF_MODE_DISPLAY_NAME_DOT = 1u<<0,
	/*! Ok plain user name in \c{angle-addr}ess, that is \c{&lt;USERNAME&gt;}, without domain name.
	 * The validity of \c{USERNAME} and its expansion is up to the caller.
	 * Sets \r{su_IMF_STATE_ADDR_SPEC_NO_DOMAIN} when encountered.
	 * For \r{su_imf_parse_addr_header()}. */
	su_IMF_MODE_ADDR_SPEC_NO_DOMAIN = 1u<<1,
	/*! Heavily loosen syntax checks when parsing a non-literal \c{domain}.
	 * To support locale language in domain names this can be set:
	 * the validity of the domain, likely its IDNA conversion, is up to the caller.
	 * Sets \r{su_IMF_STATE_DOMAIN_XLABEL} when encountered.
	 * For \r{su_imf_parse_addr_header()}. */
	su_IMF_MODE_DOMAIN_XLABEL = 1u<<2,

	/*! Treat it as an error if none or more than one \c{msg-id} exists.
	 * Overwrites (\ASSERT_EXEC{logs}) \r{su_IMF_MODE_STOP_EARLY}!
	 * For \r{su_imf_parse_msgid_header()}. */
	su_IMF_MODE_ID_SINGLE = 1u<<3,

	/*! Parse \c{dot-atom-text} (unquoted period) instead of \c{atext} for \c{phrase}.
	 * For \r{su_imf_parse_struct_header()}. */
	su_IMF_MODE_DOT_ATEXT = 1u<<5,
	/*! Generate result tokens for comments.
	 * For \r{su_imf_parse_struct_header()}. */
	su_IMF_MODE_TOK_COMMENT = 1u<<6,
	/*! Treat (unquoted) semicolon as a terminator of the current token, instead of an error.
	 * Multiple tokens may still be generated (in between semicolons).
	 * For \r{su_imf_parse_struct_header()}. */
	su_IMF_MODE_TOK_SEMICOLON = 1u<<7,
	/*! Allow empty tokens (mostly in conjunction with \r{su_IMF_MODE_TOK_SEMICOLON}).
	 * As whitespace is skipped over, and empty (or ignored) comments and quoted-strings do not generate output,
	 * a semantically meaningful semicolon (for example in the \c{Authentication-Results} header) will not generate
	 * a token until this flag is set.
	 * If set, an empty quoted-string does generate an (unquoted) output token.
	 * For \r{su_imf_parse_struct_header()}. */
	su_IMF_MODE_TOK_EMPTY = 1u<<8,

	/*! Ignore certain \r{su_imf_err}ors which would otherwise cause hard failures.
	 * Such a condition is then reported via \r{su_IMF_STATE_RELAX} (and an error) via \r{su_imf_addr::imfa_mse}.
	 * Some notes:
	 * \list{\li{
	 * For \r{su_imf_parse_addr_header()}:
	 * lonely colon \c{:} becomes a "valid" empty address group,
	 * a single trailing dot \c{.} is allowed in an otherwise valid \c{domain} (\r{su_IMF_ERR_CONTENT}),
	 * and more (see \r{su_imf_err}).
	 * }\li{
	 * For \r{su_imf_parse_msgid_header()}: (try to) ignore
	 * missing surrounding angle brackets,
	 * missing or multiple &#40; at-signs,
	 * superfluous spaces, and trailing dots; and much more.
	 * \remarks{Due to bogus \c{msg-id}s seen in the wild, very forgiving;
	 * even more so with \r{su_IMF_MODE_ID_SINGLE}!}
	 * }} */
	su_IMF_MODE_RELAX = 1u<<9,
	/*! Stop parsing whenever the first address or token has been successfully parsed.
	 * \remarks{Empty groups (\c{Undisclosed recipients:;}) do not count as addresses, therefore the result list
	 * can consist of multiple nodes despite that flag being set.} */
	su_IMF_MODE_STOP_EARLY = 1u<<10,

	su__IMF_MODE_ADDR_MASK = su_IMF_MODE_DISPLAY_NAME_DOT | su_IMF_MODE_ADDR_SPEC_NO_DOMAIN |
			su_IMF_MODE_DOMAIN_XLABEL | su_IMF_MODE_RELAX | su_IMF_MODE_STOP_EARLY,
	su__IMF_MODE_STRUCT_MASK = su_IMF_MODE_DOT_ATEXT | su_IMF_MODE_TOK_COMMENT |
			su_IMF_MODE_TOK_SEMICOLON | su_IMF_MODE_TOK_EMPTY |
			su_IMF_MODE_RELAX | su_IMF_MODE_STOP_EARLY,
	su__IMF_MODE_MSGID_MASK = su_IMF_MODE_ID_SINGLE | su_IMF_MODE_RELAX | su_IMF_MODE_STOP_EARLY
};

/*! Shares bit range with \r{su_imf_mode} and \r{su_imf_err}. */
enum su_imf_state{
	/*!< \r{su_IMF_MODE_DISPLAY_NAME_DOT}, and an unquoted \c{.} was seen (result is correctly quoted). */
	su_IMF_STATE_DISPLAY_NAME_DOT = 1u<<11,
	/*! \r{su_IMF_MODE_ADDR_SPEC_NO_DOMAIN}, \c{&lt;USERNAME&gt;} was seen. */
	su_IMF_STATE_ADDR_SPEC_NO_DOMAIN = 1u<<12,
	/*! \r{su_IMF_MODE_DOMAIN_XLABEL}, and strict content check failed. */
	su_IMF_STATE_DOMAIN_XLABEL = 1u<<13,
	/*! (Possibly empty) Domain literal seen; surrounding brackets are not stripped.
	 * Related to \r{su_imf_addr::imfa_domain} and \r{su_imf_msgid::imfmi_id_right}. */
	su_IMF_STATE_DOMAIN_LITERAL = 1u<<14,
	su_IMF_STATE_GROUP = 1u<<15, /*!< Belongs to an address group (that maybe starts and/or ends). */
	su_IMF_STATE_GROUP_START = 1u<<16, /*!< Group start. */
	su_IMF_STATE_GROUP_END = 1u<<17, /*!< Group end. */
	su_IMF_STATE_GROUP_EMPTY = 1u<<18, /*!< Group without address, for example \c{Undisclosed recipients:;}. */

	su_IMF_STATE_SEMICOLON = 1u<<21, /*!< With \r{su_IMF_MODE_TOK_SEMICOLON}, a semicolon was seen. */
	su_IMF_STATE_COMMENT = 1u<<22, /*!< With \r{su_IMF_MODE_TOK_COMMENT}, result represents a parsed comment. */

	/*! Errors were ignored due to \r{su_IMF_MODE_RELAX}.
	 * \r{su_imf_err} bits (but \r{su_IMF_ERR_RELAX}) are set. */
	su_IMF_STATE_RELAX = 1u<<23,

	/*! A mask of all states except \r{su_IMF_STATE_RELAX}. */
	su_IMF_STATE_MASK = su_IMF_STATE_DISPLAY_NAME_DOT | su_IMF_STATE_ADDR_SPEC_NO_DOMAIN |
				su_IMF_STATE_DOMAIN_XLABEL | su_IMF_STATE_DOMAIN_LITERAL |
				su_IMF_STATE_GROUP | su_IMF_STATE_GROUP_START |
				su_IMF_STATE_GROUP_END | su_IMF_STATE_GROUP_EMPTY |
			su_IMF_STATE_SEMICOLON | su_IMF_STATE_COMMENT
};

/*! Shares bit range with \r{su_imf_mode} and \r{su_imf_state}. */
enum su_imf_err{
	su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY = 1u<<24, /*!< Address group display-name must not be empty, but is. */
	su_IMF_ERR_DISPLAY_NAME_DOT = 1u<<25, /*!< \c{.} in display-name, no \r{su_IMF_MODE_DISPLAY_NAME_DOT}. */
	su_IMF_ERR_DQUOTE = 1u<<26, /*!< Quoted-string \c{".."} content invalid, or quote not closed. */
	su_IMF_ERR_GROUP_OPEN = 1u<<27, /*!< A group was not closed. */

	su_IMF_ERR_COMMENT = 1u<<28, /*!< Comment content invalid, or comment not closed. */

	su_IMF_ERR_CONTENT = 1u<<29, /*!< Invalid or (unexpectedly) empty content; also: invalid route, fallback. */

	su_IMF_ERR_RELAX = 1u<<30, /*!< Errors could have been or were ignored with or due \r{su_IMF_MODE_RELAX}. */

	/*! A mask of all errors except \r{su_IMF_ERR_RELAX}. */
	su_IMF_ERR_MASK = su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY | su_IMF_ERR_DISPLAY_NAME_DOT |
				su_IMF_ERR_DQUOTE | su_IMF_ERR_GROUP_OPEN |
			su_IMF_ERR_COMMENT |
			su_IMF_ERR_CONTENT
};

/* Classification bits for su_imf_c_*() series */
enum su_imf_c_class{
	su_IMF_C_ALPHA = 1u<<0,
	su_IMF_C_DIGIT = 1u<<1,
	su_IMF_C_VCHAR = 1u<<2,
	su_IMF_C_ATEXT = 1u<<3,
	su_IMF_C_CTEXT = 1u<<4,
	su_IMF_C_DTEXT = 1u<<5,
	su_IMF_C_QTEXT = 1u<<6,
	su_IMF_C_SPECIAL = 1u<<7,
	su_IMF_C_NO_WS_CTL = 1u<<8,

	su_IMF_C_CR = 1u<<9,
	su_IMF_C_DQUOTE = 1u<<10,
	su_IMF_C_HT = 1u<<11,
	su_IMF_C_LF = 1u<<12,
	su_IMF_C_SP = 1u<<13
};

/*! Parsed \c{address-list} structure; all buffers are accessible and \NUL terminated. */
struct su_imf_addr{
	struct su_imf_addr *imfa_next; /*!< In case of \c{address-list}, \c{group}s with \c{mailbox-list}, etc. */
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

/*! Parsed \c{msg-id} structure; all buffers are accessible and \NUL terminated. */
struct su_imf_msgid{
	struct su_imf_msgid *imfmi_next; /*!< Next \c{msg-id}, if any. */
	char *imfmi_id_left; /*!< \c{id-left}. */
	char *imfmi_id_right; /*!< \c{id-right} (literal with \r{su_IMF_STATE_DOMAIN_LITERAL}). */
	/*! Any comment content within a \c{msg-id}, joined together.
	 * Note that surrounding comments are discarded. */
	char *imfmi_comm;
	u32 imfmi_id_left_len; /*!< \_ */
	u32 imfmi_id_right_len; /*!< \_ */
	u32 imfmi_comm_len; /*!< \_ */
	u32 imfmi_mse; /*!< Bitmix of \r{su_imf_mode}, \r{su_imf_state} and \r{su_imf_err}. */
	char imfmi_dat[VFIELD_SIZE(0)]; /* Storage for any text (single chunk struct) */
};

/*! Parsed structured header field token; the buffer is accessible and \NUL terminated. */
struct su_imf_shtok{
	struct su_imf_shtok *imfsht_next; /*!< Next token, if any. */
	u32 imfsht_mse; /*!< Bitmix of \r{su_imf_mode}, \r{su_imf_state} and \r{su_imf_err}. */
	u32 imfsht_len; /*!< Length of \c{imfsht_dat}. */
	char imfsht_dat[VFIELD_SIZE(0)]; /*!< \_ */
};

EXPORT_DATA u16 const su__imf_c_tbl[su__IMF_TABLE_SIZE + 1];

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

/*! Parse an (possibly multiline) \c{address-list} header field body.
 * (Since RFC 6854 updated RFC 5322 this covers all address-related fields of IMF.)
 *
 * Stores a result list in \a{*app}, or \NIL if nothing can be parsed.
 * A result without any data is not produced.
 * Results may contain \c{IMF_ERR_} entries with \r{su_IMF_MODE_RELAX}, or according to \c{IMF_MODE_*}.
 * If \a{endptr_or_nil} is set it will point to where parsing stopped (points to \NUL but in error cases).
 *
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

/*! Parse a (possibly multiline) \c{msg-id} header field body.
 * This covers \c{Message-ID}, \c{In-Reply-To} and \c{References} from RFC 5322,
 * as well as \c{Content-ID} from RFC 2045,
 * the first and last of which via \r{su_IMF_MODE_ID_SINGLE}.
 *
 * If \a{mode} has \r{su_IMF_MODE_RELAX} set a very permissive parser is used
 *FIXME missing <> only with SINGLE??
FIXME


 */
EXPORT s32 su_imf_parse_msgid_header(struct su_imf_msgid **mipp, char const *header, BITENUM(u32,su_imf_mode) mode,
		struct su_mem_bag *membp, char const **endptr_or_nil);

/*! Parse a (possibly multiline) structured header field body (a mix of \c{CFWS} and \c{phrase} tokens).
 * If (a non-empty) \c{quoted-string} is parsed within \c{phrase}, the entire result token will be (re-)quoted as such.
 *
 * With \r{su_IMF_MODE_DOT_ATEXT} RFC 5322 \c{phrase} is assumed to contain \c{dot-atom-text}, not \c{atext}.
 * With \r{su_IMF_MODE_TOK_COMMENT} comments create result tokens instead of being discarded:
 * adjacent comments are joined, and results do not contain the parenthesis, for example \c{(a (b)) (c)}
 * creates the result \c{a b c}.
 *
 * With \r{su_IMF_MODE_TOK_SEMICOLON} a(n unquoted) semicolon does not result in error but terminates the current
 * token: it is not stored, but announced via \r{su_IMF_STATE_SEMICOLON} in \r{su_imf_shtok::imfsht_mse};
 * then \r{su_IMF_MODE_TOK_EMPTY} can also be used: an input \¢{"";();;} would then create three empty tokens
 * with only \r{su_IMF_STATE_SEMICOLON} set.
 *
 * Stores a result list in \a{*shtpp}, or \NIL if nothing can be parsed.
 * \r{su_IMF_MODE_RELAX} may be used, and \r{su_IMF_MODE_STOP_EARLY}, too.
 * If \a{endptr_or_nil} is set it will point to where parsing stopped (points to \NUL but in error cases).
 *
 * Returns \ERR{NONE} on success, -\ERR{NODATA} on empty (or only whitespace) input, -\ERR{OVERFLOW} if input is too
 * long, or -\ERR{NOMEM} if an allocation failed.
 * A positive return represents a \r{su_imf_state} and \r{su_imf_err} (logical subset) bitmix of the parse error.
 * The same result allocation scheme as for \r{su_imf_parse_addr_header()} is used. */
EXPORT s32 su_imf_parse_struct_header(struct su_imf_shtok **shtpp, char const *header, BITENUM(u32,su_imf_mode) mode,
		struct su_mem_bag *membp, char const **endptr_or_nil);

#undef a_X
#if su__IMF_TABLE_SIZE == U8_MAX
# define a_X(X,Y) ((su__imf_c_tbl[S(u8,X)] & (Y)) != 0)
#elif su__IMF_TABLE_SIZE == S8_MAX
# define a_X(X,Y) (S(u8,X) <= S8_MAX && (su__imf_c_tbl[S(u8,X)] & (Y)) != 0)
#else
# error su__IMF_TABLE_SIZE must be U8_MAX or S8_MAX
#endif

/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_ALPHA(char c) {return a_X(c, su_IMF_C_ALPHA);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_DIGIT(char c) {return a_X(c, su_IMF_C_DIGIT);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_VCHAR(char c) {return a_X(c, su_IMF_C_VCHAR);}
/*! \_ */
SINLINE boole su_imf_c_atext(char c) {return a_X(c, su_IMF_C_ATEXT);}
/*! \_ */
SINLINE boole su_imf_c_ctext(char c) {return a_X(c, su_IMF_C_CTEXT);}
/*! \_ */
SINLINE boole su_imf_c_dtext(char c) {return a_X(c, su_IMF_C_DTEXT);}
/*! \_ */
SINLINE boole su_imf_c_qtext(char c) {return a_X(c, su_IMF_C_QTEXT);}
/*! \_ */
SINLINE boole su_imf_c_special(char c) {return a_X(c, su_IMF_C_SPECIAL);}
/*! \_ */
SINLINE boole su_imf_c_obs_NO_WS_CTL(char c) {return a_X(c, su_IMF_C_NO_WS_CTL);}

/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_CR(char c) {return a_X(c, su_IMF_C_CR);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_DQUOTE(char c) {return a_X(c, su_IMF_C_DQUOTE);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_HT(char c) {return a_X(c, su_IMF_C_HT);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_LF(char c) {return a_X(c, su_IMF_C_LF);}
/*! (RFC 5234, B.1. Core Rules.) */
SINLINE boole su_imf_c_SP(char c) {return a_X(c, su_IMF_C_SP);}

/*! (RFC 5234, B.1. Core Rules; \r{su_imf_c_SP()} or \r{su_imf_c_HT()}.) */
SINLINE boole su_imf_c_WSP(char c) {return (a_X(c, su_IMF_C_SP | su_IMF_C_HT));}
/*! Any of \r{su_imf_c_SP()}, \r{su_imf_c_HT()}, \r{su_imf_c_LF()} or \r{su_imf_c_CR()}. */
SINLINE boole su_imf_c_ANY_WSP(char c) {return (a_X(c, su_IMF_C_SP | su_IMF_C_HT | su_IMF_C_LF | su_IMF_C_CR));}

/*! Is \a{c2} a valid second byte of a RFC 5322 quoted-pair? */
SINLINE boole su_imf_c_quoted_pair_c2(char c2){ /* NIL unsupported (manual) */
	return (a_X(c2, su_IMF_C_VCHAR | su_IMF_C_SP | su_IMF_C_HT | su_IMF_C_NO_WS_CTL | su_IMF_C_LF | su_IMF_C_CR));
}

#undef a_X
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class imf;
//class imf::addr;
//class imf::msgid;
//class imf::shtok;

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
	class msgid;
	class shtok;

	/* addr {{{ */
	/*! \cd{su_imf_addr} */
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

		/*! \r{su_imf_addr::imfa_group_display_name_len} */
		u32 group_display_name_len(void) const {return imfa_group_display_name_len;}

		/*! \r{su_imf_addr::imfa_group_display_name} */
		char const *group_display_name(void) const {return imfa_group_display_name;}

		/*! \r{su_imf_addr::imfa_display_name_len} */
		u32 display_name_len(void) const {return imfa_display_name_len;}

		/*! \r{su_imf_addr::imfa_display_name} */
		char const *display_name(void) const {return imfa_display_name;}

		/*! \r{su_imf_addr::imfa_locpar_len} */
		u32 locpar_len(void) const {return imfa_locpar_len;}

		/*! \r{su_imf_addr::imfa_locpar} */
		char const *locpar(void) const {return imfa_locpar;}

		/*! \r{su_imf_addr::imfa_domain_len} */
		u32 domain_len(void) const {return imfa_domain_len;}

		/*! \r{su_imf_addr::imfa_domain} */
		char const *domain(void) const {return imfa_domain;}

		/*! \r{su_imf_addr::imfa_comm_len} */
		u32 comm_len(void) const {return imfa_comm_len;}

		/*! \r{su_imf_addr::imfa_comm} */
		char const *comm(void) const {return imfa_comm;}
	};
	/* }}} */

	/* msgid {{{ */
	/*! \cd{su_imf_msgid} */
	class msgid : private su_imf_msgid{
		friend class imf;
	protected:
		msgid(void) {}
	public:
		~msgid(void) {}

		/*! \r{su_imf_msgid::imfmi_next} */
		msgid *next(void) const {return S(msgid*,imfmi_next);}

		/*! \r{su_imf_msgid::imfmi_mse} */
		u32 mse(void) const {return imfmi_mse;}

		/*! \r{su_imf_msgid::imfmi_id_left_len} */
		u32 id_left_len(void) const {return imfmi_id_left_len;}

		/*! \r{su_imf_msgid::imfmi_id_left} */
		char const *id_left(void) const {return imfmi_id_left;}

		/*! \r{su_imf_msgid::imfmi_id_right_len} */
		u32 id_right_len(void) const {return imfmi_id_right_len;}

		/*! \r{su_imf_msgid::imfmi_id_right} */
		char const *id_right(void) const {return imfmi_id_right;}

		/*! \r{su_imf_msgid::imfmi_comm_len} */
		u32 comm_len(void) const {return imfmi_comm_len;}

		/*! \r{su_imf_msgid::imfmi_comm} */
		char const *comm(void) const {return imfmi_comm;}
	};
	/* }}} */

	/* shtok {{{ */
	/*! \cd{su_imf_shtok} */
	class shtok : private su_imf_shtok{
		friend class imf;
	protected:
		shtok(void) {}
	public:
		~shtok(void) {}

		/*! \r{su_imf_shtok::imfsht_next} */
		shtok *next(void) const {return S(shtok*,imfsht_next);}

		/*! \r{su_imf_shtokr::imfsht_mse} */
		u32 mse(void) const {return imfsht_mse;}

		/*! \r{su_imf_shtok::imfsht_len} */
		u32 len(void) const {return imfsht_len;}

		/*! \r{su_imf_shtok::imfsht_dat} */
		char const *dat(void) const {return imfsht_dat;}
	};
	/* }}} */

private:
#ifndef DOXYGEN
	su_CXXCAST(addr, struct su_imf_addr);
	su_CXXCAST(msgid, struct su_imf_msgid);
	su_CXXCAST(shtok, struct su_imf_shtok);
#endif
public:

	/*! \cd{su_imf_mode} */
	enum mode{
		mode_none = su_IMF_MODE_NONE, /*!< \cd{su_IMF_MODE_NONE} */

		mode_display_name_dot = su_IMF_MODE_DISPLAY_NAME_DOT, /*!< \cd{su_IMF_MODE_DISPLAY_NAME_DOT} */
		mode_addr_spec_no_domain = su_IMF_MODE_ADDR_SPEC_NO_DOMAIN, /*!< \cd{su_IMF_MODE_ADDR_SPEC_NO_DOMAIN} */
		mode_domain_xlabel = su_IMF_MODE_DOMAIN_XLABEL, /*!< \cd{su_IMF_MODE_DOMAIN_XLABEL} */

		mode_id_single = su_IMF_MODE_ID_SINGLE, /*!< \cd{su_IMF_MODE_ID_SINGLE} */

		mode_dot_atext = su_IMF_MODE_DOT_ATEXT, /*!< \cd{su_IMF_MODE_DOT_ATEXT} */
		mode_tok_comment = su_IMF_MODE_TOK_COMMENT, /*! \cd{su_IMF_MODE_TOK_COMMENT} */
		mode_tok_semicolon = su_IMF_MODE_TOK_SEMICOLON, /*!< \cd{su_IMF_MODE_TOK_SEMICOLON} */
		mode_tok_empty = su_IMF_MODE_TOK_EMPTY, /*! \cd{su_IMF_MODE_TOK_EMPTY} */

		mode_relax = su_IMF_MODE_RELAX, /*!< \cd{su_IMF_MODE_RELAX} */
		mode_stop_early = su_IMF_MODE_STOP_EARLY /*!< \cd{su_IMF_MODE_STOP_EARLY} */
	};

	/*! \cd{su_imf_state} */
	enum state{
		state_display_name_dot = su_IMF_STATE_DISPLAY_NAME_DOT, /*!< \cd{su_IMF_STATE_DISPLAY_NAME_DOT} */
		/*! \cd{su_IMF_STATE_ADDR_SPEC_NO_DOMAIN} */
		state_addr_spec_no_domain = su_IMF_STATE_ADDR_SPEC_NO_DOMAIN,
		state_domain_xlabel = su_IMF_STATE_DOMAIN_XLABEL, /*!< \cd{su_IMF_STATE_DOMAIN_XLABEL} */
		state_domain_literal = su_IMF_STATE_DOMAIN_LITERAL, /*!< \cd{su_IMF_STATE_DOMAIN_LITERAL} */
		state_group = su_IMF_STATE_GROUP, /*!< \cd{su_IMF_STATE_GROUP} */
		state_group_start = su_IMF_STATE_GROUP_START, /*!< \cd{su_IMF_STATE_GROUP_START} */
		state_group_end = su_IMF_STATE_GROUP_END, /*!< \cd{su_IMF_STATE_GROUP_END} */
		state_group_empty = su_IMF_STATE_GROUP_EMPTY, /*!< \cd{su_IMF_STATE_GROUP_EMPTY} */

		state_semicolon = su_IMF_STATE_SEMICOLON, /*!< \cd{su_IMF_STATE_SEMICOLON} */
		state_comment = su_IMF_STATE_COMMENT, /*!< \cd{su_IMF_STATE_COMMENT} */

		state_relax = su_IMF_STATE_RELAX, /*!< \cd{su_IMF_STATE_RELAX} */

		state_mask = su_IMF_STATE_MASK /*!< \cd{su_IMF_STATE_MASK} */
	};

	/*! \cd{su_imf_err} */
	enum err{
		/*! \cd{su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY} */
		err_group_display_name_empty = su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY,
		err_display_name_dot = su_IMF_ERR_DISPLAY_NAME_DOT, /*!< \cd{su_IMF_ERR_DISPLAY_NAME_DOT} */
		err_dquote = su_IMF_ERR_DQUOTE, /*!< \cd{su_IMF_ERR_DQUOTE} */
		err_group_open = su_IMF_ERR_GROUP_OPEN, /*!< \cd{su_IMF_ERR_GROUP_OPEN} */

		err_comment = su_IMF_ERR_COMMENT, /*!< \cd{su_IMF_ERR_COMMENT} */

		err_content = su_IMF_ERR_CONTENT, /*!< \cd{su_IMF_ERR_CONTENT} */

		err_relax = su_IMF_ERR_RELAX, /*!< \cd{su_IMF_ERR_RELAX} */

		err_mask = su_IMF_ERR_MASK /*!< \cd{su_IMF_ERR_MASK} */
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

	/*! \r{su_imf_parse_msgid_header()} */
	static s32 parse_msgid_header(msgid *&mipp, char const *header, BITENUM(u32,mode) mode, mem_bag &membp,
			char const **endptr_or_nil){
		return su_imf_parse_msgid_header(R(su_imf_msgid**,&mipp), header, mode, S(struct su_mem_bag*,&membp),
			endptr_or_nil);
	}

	/*! \r{su_imf_parse_struct_header()} */
	static s32 parse_struct_header(shtok *&shtpp, char const *header, BITENUM(u32,mode) mode, mem_bag &membp,
			char const **endptr_or_nil){
		return su_imf_parse_struct_header(R(su_imf_shtok**,&shtpp), header, mode, S(struct su_mem_bag*,&membp),
			endptr_or_nil);
	}

	/*! \r{su_imf_c_ALPHA()} */
	static boole c_ALPHA(char c) {return su_imf_c_ALPHA(c);}
	/*! \r{su_imf_c_DIGIT()} */
	static boole c_DIGIT(char c) {return su_imf_c_DIGIT(c);}
	/*! \r{su_imf_c_VCHAR()} */
	static boole c_VCHAR(char c) {return su_imf_c_VCHAR(c);}
	/*! \r{su_imf_c_atext()} */
	static boole c_atext(char c) {return su_imf_c_atext(c);}
	/*! \r{su_imf_c_ctext()} */
	static boole c_ctext(char c) {return su_imf_c_ctext(c);}
	/*! \r{su_imf_c_dtext()} */
	static boole c_dtext(char c) {return su_imf_c_dtext(c);}
	/*! \r{su_imf_c_qtext()} */
	static boole c_qtext(char c) {return su_imf_c_qtext(c);}
	/*! \r{su_imf_c_special()} */
	static boole c_special(char c) {return su_imf_c_special(c);}
	/*! \r{su_imf_c_obs_NO_WS_CTL()} */
	static boole c_obs_NO_WS_CTL(char c) {return su_imf_c_obs_NO_WS_CTL(c);}

	/*! \r{su_imf_c_CR()} */
	static boole c_CR(char c) {return su_imf_c_CR(c);}
	/*! \r{su_imf_c_DQUOTE()} */
	static boole c_DQUOTE(char c) {return su_imf_c_DQUOTE(c);}
	/*! \r{su_imf_c_HT()} */
	static boole c_HT(char c) {return su_imf_c_HT(c);}
	/*! \r{su_imf_c_LF()} */
	static boole c_LF(char c) {return su_imf_c_LF(c);}
	/*! \r{su_imf_c_SP()} */
	static boole c_SP(char c) {return su_imf_c_SP(c);}

	/*! \r{su_imf_c_WSP()} */
	static boole c_WSP(char c) {return su_imf_c_WSP(c);}
	/*! \r{su_imf_c_ANY_WSP()} */
	static boole c_ANY_WSP(char c) {return su_imf_c_ANY_WSP(c);}

	/*! \r{su_imf_c_quoted_pair_c2()} */
	static boole c_quoted_pair_c2(char c) {return su_imf_c_quoted_pair_c2(c);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_HAVE_IMF */
#endif /* su_IMF_H */
/* s-itt-mode */
