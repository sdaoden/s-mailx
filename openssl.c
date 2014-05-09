/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ OpenSSL functions. TODO this needs an overhaul -- there _are_ stack leaks!?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE(openssl)
#ifdef HAVE_OPENSSL
#include <sys/socket.h>

#include <dirent.h>
#include <netdb.h>

#include <netinet/in.h>

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

/*
 * OpenSSL client implementation according to: John Viega, Matt Messier,
 * Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 */

#ifdef HAVE_STACK_OF
# define _STACKOF(X)    STACK_OF(X)
#else
# define _STACKOF(X)    /*X*/STACK
#endif

struct ssl_method {
   char const           sm_name[8];
   SSL_METHOD const *   (*sm_fun)(void);
};

struct smime_cipher {
   char const           sc_name[8];
   EVP_CIPHER const *   (*sc_fun)(void);
};

/* Supported SSL/TLS methods: update manual on change! */
static struct ssl_method const   _ssl_methods[] = {
   {"auto", &SSLv23_client_method},
#define _SSL_DEFAULT_METHOD      SSLv23_client_method
#ifndef OPENSSL_NO_TLS1
# ifdef TLS1_2_VERSION
   {"tls1.2", &TLSv1_2_client_method},
# endif
# ifdef TLS1_1_VERSION
   {"tls1.1", &TLSv1_1_client_method},
# endif
   {"tls1", &TLSv1_client_method},
#endif
#ifndef OPENSSL_NO_SSL3
   {"ssl3", &SSLv3_client_method},
#endif
#ifndef OPENSSL_NO_SSL2
   {"ssl2", &SSLv2_client_method}
#endif
};

/* Supported S/MIME cipher algorithms: update manual on change! */
static struct smime_cipher const _smime_ciphers[] = {
#ifndef OPENSSL_NO_AES
# define _SMIME_DEFAULT_CIPHER   EVP_aes_128_cbc   /* According to RFC 5751 */
   {"aes-128", &EVP_aes_128_cbc},
   {"aes-256", &EVP_aes_256_cbc},
   {"aes-192", &EVP_aes_192_cbc},
#endif
#ifndef OPENSSL_NO_DES
# ifndef _SMIME_DEFAULT_CIPHER
#  define _SMIME_DEFAULT_CIPHER   EVP_des_ede3_cbc
# endif
   {"des3", &EVP_des_ede3_cbc},
   {"des", &EVP_des_cbc},
#endif
#ifndef OPENSSL_NO_RC2
   {"rc2-40", &EVP_rc2_40_cbc},
   {"rc2-64", &EVP_rc2_64_cbc},
#endif
};
#ifndef _SMIME_DEFAULT_CIPHER
# error Your OpenSSL library does not include the necessary
# error cipher algorithms that are required to support S/MIME
#endif

static int        initialized;
static int        rand_init;
static int        message_number;
static int        verify_error_found;

static int        ssl_rand_init(void);
static void       ssl_init(void);
static int        ssl_verify_cb(int success, X509_STORE_CTX *store);
static const SSL_METHOD *ssl_select_method(char const *uhp);
static void       ssl_load_verifications(struct sock *sp);
static void       ssl_certificate(struct sock *sp, char const *uhp);
static enum okay  ssl_check_host(char const *server, struct sock *sp);
static int        smime_verify(struct message *m, int n, _STACKOF(X509) *chain,
                        X509_STORE *store);
static EVP_CIPHER const * _smime_cipher(char const *name);
static int        ssl_password_cb(char *buf, int size, int rwflag,
                     void *userdata);
static FILE *     smime_sign_cert(char const *xname, char const *xname2,
                     bool_t dowarn);
static char *     _smime_sign_include_certs(char const *name);
static bool_t     _smime_sign_include_chain_creat(_STACKOF(X509) **chain,
                     char const *cfiles);
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay  load_crl1(X509_STORE *store, char const *name);
#endif
static enum okay  load_crls(X509_STORE *store, enum okeys fok, enum okeys dok);

static int
ssl_rand_init(void)
{
   char *cp, *x;
   int state = 0;
   NYD_ENTER;

   if ((cp = ok_vlook(ssl_rand_egd)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_egd(cp = x) == -1)
         fprintf(stderr, tr(245, "entropy daemon at \"%s\" not available\n"),
            cp);
      else
         state = 1;
   } else if ((cp = ok_vlook(ssl_rand_file)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_load_file(cp = x, 1024) == -1)
         fprintf(stderr, tr(246, "entropy file at \"%s\" not available\n"), cp);
      else {
         struct stat st;

         if (!stat(cp, &st) && S_ISREG(st.st_mode) && !access(cp, W_OK)) {
            if (RAND_write_file(cp) == -1) {
               fprintf(stderr, tr(247,
                  "writing entropy data to \"%s\" failed\n"), cp);
            }
         }
         state = 1;
      }
   }
   NYD_LEAVE;
   return state;
}

static void
ssl_init(void)
{
   NYD_ENTER;
   if (initialized == 0) {
      SSL_library_init();
      initialized = 1;
   }
   if (rand_init == 0)
      rand_init = ssl_rand_init();
   NYD_LEAVE;
}

static int
ssl_verify_cb(int success, X509_STORE_CTX *store)
{
   int rv = TRU1;
   NYD_ENTER;

   if (success == 0) {
      char data[256];
      X509 *cert = X509_STORE_CTX_get_current_cert(store);
      int depth = X509_STORE_CTX_get_error_depth(store);
      int err = X509_STORE_CTX_get_error(store);

      verify_error_found = 1;
      if (message_number)
         fprintf(stderr, "Message %d: ", message_number);
      fprintf(stderr, tr(229, "Error with certificate at depth: %i\n"), depth);
      X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof data);
      fprintf(stderr, tr(230, " issuer = %s\n"), data);
      X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof data);
      fprintf(stderr, tr(231, " subject = %s\n"), data);
      fprintf(stderr, tr(232, " err %i: %s\n"),
         err, X509_verify_cert_error_string(err));
      if (ssl_verify_decide() != OKAY)
         rv = FAL0;
   }
   NYD_LEAVE;
   return rv;
}

static SSL_METHOD const *
ssl_select_method(char const *uhp)
{
   SSL_METHOD const *method;
   char *cp;
   size_t i;
   NYD_ENTER;

   if ((cp = ssl_method_string(uhp)) != NULL) {
      method = NULL;
      for (i = 0; i < NELEM(_ssl_methods); ++i)
         if (!strcmp(_ssl_methods[i].sm_name, cp)) {
            method = (*_ssl_methods[i].sm_fun)();
            goto jleave;
         }
      fprintf(stderr, tr(244, "Invalid SSL method \"%s\"\n"), cp);
   }
   method = _SSL_DEFAULT_METHOD();
jleave:
   NYD_LEAVE;
   return method;
}

static void
ssl_load_verifications(struct sock *sp)
{
   char *ca_dir, *ca_file;
   X509_STORE *store;
   NYD_ENTER;

   if (ssl_verify_level == SSL_VERIFY_IGNORE)
      goto jleave;

   if ((ca_dir = ok_vlook(ssl_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(ssl_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if (ca_dir != NULL || ca_file != NULL) {
      if (SSL_CTX_load_verify_locations(sp->s_ctx, ca_file, ca_dir) != 1) {
         fprintf(stderr, tr(233, "Error loading "));
         if (ca_dir) {
            fputs(ca_dir, stderr);
            if (ca_file)
               fputs(tr(234, " or "), stderr);
         }
         if (ca_file)
            fputs(ca_file, stderr);
         fputs("\n", stderr);
      }
   }

   if (!ok_blook(ssl_no_default_ca)) {
      if (SSL_CTX_set_default_verify_paths(sp->s_ctx) != 1)
         fprintf(stderr, tr(243, "Error loading default CA locations\n"));
   }

   verify_error_found = 0;
   message_number = 0;
   SSL_CTX_set_verify(sp->s_ctx, SSL_VERIFY_PEER, ssl_verify_cb);
   store = SSL_CTX_get_cert_store(sp->s_ctx);
   load_crls(store, ok_v_ssl_crl_file, ok_v_ssl_crl_dir);
jleave:
   NYD_LEAVE;
}

static void
ssl_certificate(struct sock *sp, char const *uhp)
{
   size_t i;
   char *certvar, *keyvar, *cert, *key, *x;
   NYD_ENTER;

   i = strlen(uhp);
   certvar = ac_alloc(i + 9 +1);
   memcpy(certvar, "ssl-cert-", 9);
   memcpy(certvar + 9, uhp, i +1);

   if ((cert = vok_vlook(certvar)) != NULL ||
         (cert = ok_vlook(ssl_cert)) != NULL) {
      x = cert;
      if ((cert = file_expand(cert)) == NULL) {
         cert = x;
         goto jbcert;
      } else if (SSL_CTX_use_certificate_chain_file(sp->s_ctx, cert) == 1) {
         keyvar = ac_alloc(strlen(uhp) + 8 +1);
         memcpy(keyvar, "ssl-key-", 8);
         memcpy(keyvar + 8, uhp, i +1);
         if ((key = vok_vlook(keyvar)) == NULL &&
               (key = ok_vlook(ssl_key)) == NULL)
            key = cert;
         else if ((x = key, key = file_expand(key)) == NULL) {
            key = x;
            goto jbkey;
         }
         if (SSL_CTX_use_PrivateKey_file(sp->s_ctx, key, SSL_FILETYPE_PEM) != 1)
jbkey:
            fprintf(stderr, tr(238, "cannot load private key from file %s\n"),
               key);
         ac_free(keyvar);
      } else
jbcert:
         fprintf(stderr, tr(239, "cannot load certificate from file %s\n"),
            cert);
   }
   ac_free(certvar);
   NYD_LEAVE;
}

static enum okay
ssl_check_host(char const *server, struct sock *sp)
{
   char data[256];
   X509 *cert;
   X509_NAME *subj;
   _STACKOF(GENERAL_NAME) *gens;
   GENERAL_NAME *gen;
   int i;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cert = SSL_get_peer_certificate(sp->s_ssl)) == NULL) {
      fprintf(stderr, tr(248, "no certificate from \"%s\"\n"), server);
      goto jleave;
   }

   gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
   if (gens != NULL) {
      for (i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
         gen = sk_GENERAL_NAME_value(gens, i);
         if (gen->type == GEN_DNS) {
            if (options & OPT_VERBOSE)
               fprintf(stderr, "Comparing DNS: <%s>; should <%s>\n",
                  server, (char*)gen->d.ia5->data);
            rv = rfc2595_hostname_match(server, (char*)gen->d.ia5->data);
            if (rv == OKAY)
               goto jdone;
         }
      }
   }

   if ((subj = X509_get_subject_name(cert)) != NULL &&
         X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof data)
            > 0) {
      data[sizeof data - 1] = '\0';
      if (options & OPT_VERBOSE)
         fprintf(stderr, "Comparing commonName: <%s>; should <%s>\n",
            server, data);
      rv = rfc2595_hostname_match(server, data);
   }
jdone:
   X509_free(cert);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
smime_verify(struct message *m, int n, _STACKOF(X509) *chain, X509_STORE *store)
{
   char data[LINESIZE], *sender, *to, *cc, *cnttype;
   int rv, c, i, j;
   struct message *x;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs;
   _STACKOF(GENERAL_NAME) *gens;
   X509 *cert;
   X509_NAME *subj;
   GENERAL_NAME *gen;
   NYD_ENTER;

   rv = 1;
   fp = NULL;
   fb = NULL;
   verify_error_found = 0;
   message_number = n;

   for (;;) {
      sender = getsender(m);
      to = hfield1("to", m);
      cc = hfield1("cc", m);
      cnttype = hfield1("content-type", m);
      if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
         goto jleave;
      if (cnttype && !strncmp(cnttype, "application/x-pkcs7-mime", 24)) {
         if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
            goto jleave;
         if (x != (struct message*)-1) {
            m = x;
            continue;
         }
      }
      size = m->m_size;
      break;
   }

   if ((fp = Ftmp(NULL, "smimever", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto jleave;
   }
   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush_rewind(fp);

   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(tr(537,
         "Error creating BIO verification object for message %d"), n);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(tr(538, "Error reading PKCS#7 object for message %d"), n);
      goto jleave;
   }
   if (PKCS7_verify(pkcs7, chain, store, pb, NULL, 0) != 1) {
      ssl_gen_err(tr(539, "Error verifying message %d"), n);
      goto jleave;
   }

   if (sender == NULL) {
      fprintf(stderr, tr(540, "Warning: Message %d has no sender.\n"), n);
      rv = 0;
      goto jleave;
   }

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      fprintf(stderr, tr(541, "No certificates found in message %d.\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
      if (gens != NULL) {
         for (j = 0; j < sk_GENERAL_NAME_num(gens); ++j) {
            gen = sk_GENERAL_NAME_value(gens, j);
            if (gen->type == GEN_EMAIL) {
               if (options & OPT_VERBOSE)
                  fprintf(stderr,
                     "Comparing subject_alt_name: <%s>; should <%s>\n",
                     sender, (char*)gen->d.ia5->data);
               if (!asccasecmp((char*)gen->d.ia5->data, sender))
                  goto jfound;
            }
         }
      }

      if ((subj = X509_get_subject_name(cert)) != NULL &&
            X509_NAME_get_text_by_NID(subj, NID_pkcs9_emailAddress,
               data, sizeof data) > 0) {
         data[sizeof data -1] = '\0';
         if (options & OPT_VERBOSE)
            fprintf(stderr, "Comparing emailAddress: <%s>; should <%s>\n",
               sender, data);
         if (!asccasecmp(data, sender))
            goto jfound;
      }
   }
   fprintf(stderr, tr(542, "Message %d: certificate does not match <%s>\n"),
      n, sender);
   goto jleave;
jfound:
   if (verify_error_found == 0)
      printf(tr(543, "Message %d was verified successfully.\n"), n);
   rv = verify_error_found;
jleave:
   if (fb != NULL)
      BIO_free(fb);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

static EVP_CIPHER const *
_smime_cipher(char const *name)
{
   EVP_CIPHER const *cipher;
   char *vn, *cp;
   size_t i;
   NYD_ENTER;

   vn = ac_alloc(i = strlen(name) + 13 +1);
   snprintf(vn, (int)i, "smime-cipher-%s", name);
   cp = vok_vlook(vn);
   ac_free(vn);

   if (cp != NULL) {
      cipher = NULL;
      for (i = 0; i < NELEM(_smime_ciphers); ++i)
         if (!strcmp(_smime_ciphers[i].sc_name, cp)) {
            cipher = (*_smime_ciphers[i].sc_fun)();
            goto jleave;
         }
      fprintf(stderr, tr(240, "Invalid cipher(s): %s\n"), cp);
   } else
      cipher = _SMIME_DEFAULT_CIPHER();
jleave:
   NYD_LEAVE;
   return cipher;
}

static int
ssl_password_cb(char *buf, int size, int rwflag, void *userdata)
{
   char *pass;
   size_t len;
   NYD_ENTER;
   UNUSED(rwflag);
   UNUSED(userdata);

   if ((pass = getpassword("PEM pass phrase:")) != NULL) {
      len = strlen(pass);
      if (UICMP(z, len, >=, size))
         len = size -1;
      memcpy(buf, pass, len);
      buf[len] = '\0';
   } else
      len = 0;
   NYD_LEAVE;
   return (int)len;
}

static FILE *
smime_sign_cert(char const *xname, char const *xname2, bool_t dowarn)
{
   char *vn, *cp;
   int vs;
   struct name *np;
   char const *name = xname, *name2 = xname2;
   FILE *fp = NULL;
   NYD_ENTER;

jloop:
   if (name) {
      np = lextract(name, GTO | GSKIN);
      while (np != NULL) {
         /* This needs to be more intelligent since it will currently take the
          * first name for which a private key is available regardless of
          * whether it is the right one for the message */
         vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-cert-%s", np->n_name);
         cp = vok_vlook(vn);
         ac_free(vn);
         if (cp != NULL)
            goto jopen;
         np = np->n_flink;
      }
      if (name2 != NULL) {
         name = name2;
         name2 = NULL;
         goto jloop;
      }
   }

   if ((cp = ok_vlook(smime_sign_cert)) == NULL)
      goto jerr;
jopen:
   if ((cp = file_expand(cp)) == NULL)
      goto jleave;
   if ((fp = Fopen(cp, "r")) == NULL)
      perror(cp);
jleave:
   NYD_LEAVE;
   return fp;
jerr:
   if (dowarn) {
      fprintf(stderr, tr(558, "Could not find a certificate for %s"), xname);
      if (xname2)
         fprintf(stderr, tr(559, "or %s"), xname2);
      fputc('\n', stderr);
   }
   goto jleave;
}

static char *
_smime_sign_include_certs(char const *name)
{
   char *rv;
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-include-certs-%s", np->n_name);
         rv = vok_vlook(vn);
         ac_free(vn);
         if (rv != NULL)
            goto jleave;
      }
   }
   rv = ok_vlook(smime_sign_include_certs);
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_smime_sign_include_chain_creat(_STACKOF(X509) **chain, char const *cfiles)
{
   X509 *tmp;
   FILE *fp;
   char *nfield, *cfield, *x;
   NYD_ENTER;

   *chain = sk_X509_new_null();

   for (nfield = savestr(cfiles);
         (cfield = n_strsep(&nfield, ',', TRU1)) != NULL;) {
      if ((x = file_expand(cfield)) == NULL ||
            (fp = Fopen(cfield = x, "r")) == NULL) {
         perror(cfiles);
         goto jerr;
      }
      if ((tmp = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(tr(560, "Error reading certificate from \"%s\""), cfield);
         Fclose(fp);
         goto jerr;
      }
      sk_X509_push(*chain, tmp);
      Fclose(fp);
   }

   if (sk_X509_num(*chain) == 0) {
      fprintf(stderr, tr(561, "smime-sign-include-certs defined but empty\n"));
      goto jerr;
   }
jleave:
   NYD_LEAVE;
   return (*chain != NULL);
jerr:
   sk_X509_pop_free(*chain, X509_free);
   *chain = NULL;
   goto jleave;
}

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay
load_crl1(X509_STORE *store, char const *name)
{
   X509_LOOKUP *lookup;
   enum okay rv = STOP;
   NYD_ENTER;

   if (options & OPT_VERBOSE)
      printf("Loading CRL from \"%s\".\n", name);
   if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) {
      ssl_gen_err(tr(565, "Error creating X509 lookup object"));
      goto jleave;
   }
   if (X509_load_crl_file(lookup, name, X509_FILETYPE_PEM) != 1) {
      ssl_gen_err(tr(566, "Error loading CRL from \"%s\""), name);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* new OpenSSL */

static enum okay
load_crls(X509_STORE *store, enum okeys fok, enum okeys dok)
{
   char *crl_file, *crl_dir;
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   DIR *dirp;
   struct dirent *dp;
   char *fn = NULL;
   int fs = 0, ds, es;
#endif
   enum okay rv = STOP;
   NYD_ENTER;

   if ((crl_file = _var_oklook(fok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      if ((crl_file = file_expand(crl_file)) == NULL ||
            load_crl1(store, crl_file) != OKAY)
         goto jleave;
#else
      fprintf(stderr, tr(567,
         "This OpenSSL version is too old to use CRLs.\n"));
      goto jleave;
#endif
   }

   if ((crl_dir = _var_oklook(dok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      char *x;
      if ((x = file_expand(crl_dir)) == NULL ||
            (dirp = opendir(crl_dir = x)) == NULL) {
         perror(crl_dir);
         goto jleave;
      }

      ds = strlen(crl_dir);
      fn = smalloc(fs = ds + 20);
      memcpy(fn, crl_dir, ds);
      fn[ds] = '/';
      while ((dp = readdir(dirp)) != NULL) {
         if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
               (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;
         if (dp->d_name[0] == '.')
            continue;
         if (ds + (es = strlen(dp->d_name)) + 2 < fs)
            fn = srealloc(fn, fs = ds + es + 20);
         memcpy(fn + ds + 1, dp->d_name, es + 1);
         if (load_crl1(store, fn) != OKAY) {
            closedir(dirp);
            free(fn);
            goto jleave;
         }
      }
      closedir(dirp);
      free(fn);
#else /* old OpenSSL */
      fprintf(stderr, tr(567,
         "This OpenSSL version is too old to use CRLs.\n"));
      goto jleave;
#endif
   }
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   if (crl_file || crl_dir)
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
         X509_V_FLAG_CRL_CHECK_ALL);
#endif
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
ssl_open(char const *server, struct sock *sp, char const *uhp)
{
   char *cp;
   long opts;
   enum okay rv = STOP;
   NYD_ENTER;

   ssl_init();
   ssl_set_verify_level(uhp);
   if ((sp->s_ctx = SSL_CTX_new(UNCONST(ssl_select_method(uhp)))) == NULL) {
      ssl_gen_err(tr(261, "SSL_CTX_new() failed"));
      goto jleave;
   }

#ifdef SSL_MODE_AUTO_RETRY
   /* available with OpenSSL 0.9.6 or later */
   SSL_CTX_set_mode(sp->s_ctx, SSL_MODE_AUTO_RETRY);
#endif /* SSL_MODE_AUTO_RETRY */
   opts = SSL_OP_ALL;
   if (!ok_blook(ssl_v2_allow))
      opts |= SSL_OP_NO_SSLv2;
   SSL_CTX_set_options(sp->s_ctx, opts);
   ssl_load_verifications(sp);
   ssl_certificate(sp, uhp);
   if ((cp = ok_vlook(ssl_cipher_list)) != NULL) {
      if (SSL_CTX_set_cipher_list(sp->s_ctx, cp) != 1)
         fprintf(stderr, tr(240, "Invalid cipher(s): %s\n"), cp);
   }

   if ((sp->s_ssl = SSL_new(sp->s_ctx)) == NULL) {
      ssl_gen_err(tr(262, "SSL_new() failed"));
      goto jleave;
   }
   SSL_set_fd(sp->s_ssl, sp->s_fd);
   if (SSL_connect(sp->s_ssl) < 0) {
      ssl_gen_err(tr(263, "could not initiate SSL/TLS connection"));
      goto jleave;
   }
   if (ssl_verify_level != SSL_VERIFY_IGNORE) {
      if (ssl_check_host(server, sp) != OKAY) {
         fprintf(stderr, tr(249, "host certificate does not match \"%s\"\n"),
            server);
         if (ssl_verify_decide() != OKAY)
            goto jleave;
      }
   }
   sp->s_use_ssl = 1;
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
ssl_gen_err(char const *fmt, ...)
{
   va_list ap;
   NYD_ENTER;

   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
   SSL_load_error_strings();
   fprintf(stderr, ": %s\n", ERR_error_string(ERR_get_error(), NULL));
   NYD_LEAVE;
}

FL int
c_verify(void *vp)
{
   int *msgvec = vp, *ip, ec = 0, rv = 1;
   _STACKOF(X509) *chain = NULL;
   X509_STORE *store;
   char *ca_dir, *ca_file;
   NYD_ENTER;

   ssl_init();

   ssl_verify_level = SSL_VERIFY_STRICT;
   if ((store = X509_STORE_new()) == NULL) {
      ssl_gen_err(tr(544, "Error creating X509 store"));
      goto jleave;
   }
   X509_STORE_set_verify_cb_func(store, ssl_verify_cb);

   if ((ca_dir = ok_vlook(smime_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(smime_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if (ca_dir != NULL || ca_file != NULL) {
      if (X509_STORE_load_locations(store, ca_file, ca_dir) != 1) {
         ssl_gen_err(tr(545, "Error loading %s"),
            (ca_file != NULL) ? ca_file : ca_dir);
         goto jleave;
      }
   }
   if (!ok_blook(smime_no_default_ca)) {
      if (X509_STORE_set_default_paths(store) != 1) {
         ssl_gen_err(tr(546, "Error loading default CA locations"));
         goto jleave;
      }
   }

   if (load_crls(store, ok_v_smime_crl_file, ok_v_smime_crl_dir) != OKAY)
      goto jleave;
   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      ec |= smime_verify(mp, *ip, chain, store);
   }
   if ((rv = ec) != 0)
      exit_status |= EXIT_ERR;
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_sign(FILE *ip, char const *addr)
{
   FILE *rv = NULL, *sp = NULL, *fp = NULL, *bp, *hp;
   X509 *cert = NULL;
   _STACKOF(X509) *chain = NULL;
   PKCS7 *pkcs7;
   EVP_PKEY *pkey = NULL;
   BIO *bb, *sb;
   bool_t bail = FAL0;
   NYD_ENTER;

   ssl_init();

   if (addr == NULL) {
      fprintf(stderr, tr(531, "No \"from\" address for signing specified\n"));
      goto jleave;
   }
   if ((fp = smime_sign_cert(addr, NULL, 1)) == NULL)
      goto jleave;

   if ((pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(tr(532, "Error reading private key from"));
      goto jleave;
   }

   rewind(fp);
   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(tr(533, "Error reading signer certificate from"));
      goto jleave;
   }
   Fclose(fp);
   fp = NULL;

   if ((addr = _smime_sign_include_certs(addr)) != NULL &&
         !_smime_sign_include_chain_creat(&chain, addr))
      goto jleave;

   if ((sp = Ftmp(NULL, "smimesign", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      perror("tempfile");
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP) {
      bail = TRU1;
      goto jerr1;
   }

   sb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (sb = BIO_new_fp(sp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(tr(534, "Error creating BIO signing objects"));
      bail = TRU1;
      goto jerr;
   }

   if ((pkcs7 = PKCS7_sign(cert, pkey, chain, bb, PKCS7_DETACHED)) == NULL) {
      ssl_gen_err(tr(535, "Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
   if (PEM_write_bio_PKCS7(sb, pkcs7) == 0) {
      ssl_gen_err(tr(536, "Error writing signed S/MIME data"));
      bail = TRU1;
      /*goto jerr*/
   }
jerr:
   if (sb != NULL)
      BIO_free(sb);
   if (bb != NULL)
      BIO_free(bb);
   if (!bail) {
      rewind(bp);
      fflush_rewind(sp);
      rv = smime_sign_assemble(hp, bp, sp);
   } else
jerr1:
      Fclose(sp);

jleave:
   if (chain != NULL)
      sk_X509_pop_free(chain, X509_free);
   if (cert != NULL)
      X509_free(cert);
   if (pkey != NULL)
      EVP_PKEY_free(pkey);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_encrypt(FILE *ip, char const *xcertfile, char const *to)
{
   char *certfile = UNCONST(xcertfile);
   FILE *rv = NULL, *yp, *fp, *bp, *hp;
   X509 *cert;
   PKCS7 *pkcs7;
   BIO *bb, *yb;
   _STACKOF(X509) *certs;
   EVP_CIPHER const *cipher;
   bool_t bail = FAL0;
   NYD_ENTER;

   if ((certfile = file_expand(certfile)) == NULL)
      goto jleave;

   ssl_init();
   if ((cipher = _smime_cipher(to)) == NULL)
      goto jleave;
   if ((fp = Fopen(certfile, "r")) == NULL) {
      perror(certfile);
      goto jleave;
   }

   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(tr(547, "Error reading encryption certificate from \"%s\""),
         certfile);
      bail = TRU1;
   }
   Fclose(fp);
   if (bail)
      goto jleave;
   bail = FAL0;

   certs = sk_X509_new_null();
   sk_X509_push(certs, cert);

   if ((yp = Ftmp(NULL, "smimeenc", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP) {
      Fclose(yp);
      goto jleave;
   }

   yb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (yb = BIO_new_fp(yp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(tr(548, "Error creating BIO encryption objects"));
      bail = TRU1;
      goto jerr;
   }
   if ((pkcs7 = PKCS7_encrypt(certs, bb, cipher, 0)) == NULL) {
      ssl_gen_err(tr(549, "Error creating the PKCS#7 encryption object"));
      bail = TRU1;
      goto jerr;
   }
   if (PEM_write_bio_PKCS7(yb, pkcs7) == 0) {
      ssl_gen_err(tr(550, "Error writing encrypted S/MIME data"));
      bail = TRU1;
      /* goto jerr */
   }
jerr:
   if (bb != NULL)
      BIO_free(bb);
   if (yb != NULL)
      BIO_free(yb);
   Fclose(bp);
   if (bail)
      Fclose(yp);
   else {
      fflush_rewind(yp);
      rv = smime_encrypt_assemble(hp, yp);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
smime_decrypt(struct message *m, char const *to, char const *cc, int signcall)
{
   struct message *rv;
   FILE *fp, *bp, *hp, *op;
   X509 *cert;
   PKCS7 *pkcs7;
   EVP_PKEY *pkey;
   BIO *bb, *pb, *ob;
   long size;
   FILE *yp;
   NYD_ENTER;

   rv = NULL;
   cert = NULL;
   pkey = NULL;
   size = m->m_size;

   if ((yp = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

   ssl_init();
   if ((fp = smime_sign_cert(to, cc, 0)) != NULL) {
      pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL);
      if (pkey == NULL) {
         ssl_gen_err(tr(551, "Error reading private key"));
         Fclose(fp);
         goto jleave;
      }
      rewind(fp);

      if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(tr(552, "Error reading decryption certificate"));
         Fclose(fp);
         EVP_PKEY_free(pkey);
         goto jleave;
      }
      Fclose(fp);
   }

   if ((op = Ftmp(NULL, "smimedec", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tempfile");
      goto j_ferr;
   }

   if (smime_split(yp, &hp, &bp, size, 1) == STOP)
      goto jferr;

   if ((ob = BIO_new_fp(op, BIO_NOCLOSE)) == NULL ||
         (bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(tr(553, "Error creating BIO decryption objects"));
      goto jferr;
   }
   if ((pkcs7 = SMIME_read_PKCS7(bb, &pb)) == NULL) {
      ssl_gen_err(tr(554, "Error reading PKCS#7 object"));
jferr:
      Fclose(op);
j_ferr:
      if (cert)
         X509_free(cert);
      if (pkey)
         EVP_PKEY_free(pkey);
      goto jleave;
   }

   if (PKCS7_type_is_signed(pkcs7)) {
      if (signcall) {
         setinput(&mb, m, NEED_BODY);
         rv = (struct message*)-1;
         goto jerr2;
      }
      if (PKCS7_verify(pkcs7, NULL, NULL, NULL, ob,
            PKCS7_NOVERIFY | PKCS7_NOSIGS) != 1)
         goto jerr;
      fseek(hp, 0L, SEEK_END);
      fprintf(hp, "X-Encryption-Cipher: none\n");
      fflush(hp);
      rewind(hp);
   } else if (pkey == NULL) {
      fprintf(stderr, tr(555, "No appropriate private key found.\n"));
      goto jerr2;
   } else if (cert == NULL) {
      fprintf(stderr, tr(556, "No appropriate certificate found.\n"));
      goto jerr2;
   } else if (PKCS7_decrypt(pkcs7, pkey, cert, ob, 0) != 1) {
jerr:
      ssl_gen_err(tr(557, "Error decrypting PKCS#7 object"));
jerr2:
      BIO_free(bb);
      BIO_free(ob);
      Fclose(op);
      Fclose(bp);
      Fclose(hp);
      if (cert != NULL)
         X509_free(cert);
      if (pkey != NULL)
         EVP_PKEY_free(pkey);
      goto jleave;
   }
   BIO_free(bb);
   BIO_free(ob);
   if (cert)
      X509_free(cert);
   if (pkey)
      EVP_PKEY_free(pkey);
   fflush_rewind(op);
   Fclose(bp);

   rv = smime_decrypt_assemble(m, hp, op);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
smime_certsave(struct message *m, int n, FILE *op)
{
   struct message *x;
   char *to, *cc, *cnttype;
   int c, i;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs, *chain = NULL;
   X509 *cert;
   enum okay rv = STOP;
   NYD_ENTER;

   message_number = n;
jloop:
   to = hfield1("to", m);
   cc = hfield1("cc", m);
   cnttype = hfield1("content-type", m);
   if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;
   if (cnttype && !strncmp(cnttype, "application/x-pkcs7-mime", 24)) {
      if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
         goto jleave;
      if (x != (struct message*)-1) {
         m = x;
         goto jloop;
      }
   }
   size = m->m_size;

   if ((fp = Ftmp(NULL, "smimecert", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      perror("tempfile");
      goto jleave;
   }

   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush(fp);

   rewind(fp);
   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err("Error creating BIO object for message %d", n);
      Fclose(fp);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(tr(562, "Error reading PKCS#7 object for message %d"), n);
      BIO_free(fb);
      Fclose(fp);
      goto jleave;
   }
   BIO_free(fb);
   Fclose(fp);

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      fprintf(stderr, tr(563, "No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      if (X509_print_fp(op, cert) == 0 || PEM_write_X509(op, cert) == 0) {
         ssl_gen_err(tr(564, "Error writing certificate %d from message %d"),
            i, n);
         goto jleave;
      }
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_OPENSSL */

/* vim:set fenc=utf-8:s-it-mode */
