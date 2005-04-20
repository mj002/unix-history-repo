/* crypto/pkcs7/pk7_lib.c */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include "cryptlib.h"
#include <openssl/objects.h>
#include <openssl/x509.h>

long PKCS7_ctrl(PKCS7 *p7, int cmd, long larg, char *parg)
	{
	int nid;
	long ret;

	nid=OBJ_obj2nid(p7->type);

	switch (cmd)
		{
	case PKCS7_OP_SET_DETACHED_SIGNATURE:
		if (nid == NID_pkcs7_signed)
			{
			ret=p7->detached=(int)larg;
			if (ret && PKCS7_type_is_data(p7->d.sign->contents))
					{
					ASN1_OCTET_STRING *os;
					os=p7->d.sign->contents->d.data;
					ASN1_OCTET_STRING_free(os);
					p7->d.sign->contents->d.data = NULL;
					}
			}
		else
			{
			PKCS7err(PKCS7_F_PKCS7_CTRL,PKCS7_R_OPERATION_NOT_SUPPORTED_ON_THIS_TYPE);
			ret=0;
			}
		break;
	case PKCS7_OP_GET_DETACHED_SIGNATURE:
		if (nid == NID_pkcs7_signed)
			{
			if(!p7->d.sign  || !p7->d.sign->contents->d.ptr)
				ret = 1;
			else ret = 0;
				
			p7->detached = ret;
			}
		else
			{
			PKCS7err(PKCS7_F_PKCS7_CTRL,PKCS7_R_OPERATION_NOT_SUPPORTED_ON_THIS_TYPE);
			ret=0;
			}
			
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_CTRL,PKCS7_R_UNKNOWN_OPERATION);
		ret=0;
		}
	return(ret);
	}

int PKCS7_content_new(PKCS7 *p7, int type)
	{
	PKCS7 *ret=NULL;

	if ((ret=PKCS7_new()) == NULL) goto err;
	if (!PKCS7_set_type(ret,type)) goto err;
	if (!PKCS7_set_content(p7,ret)) goto err;

	return(1);
err:
	if (ret != NULL) PKCS7_free(ret);
	return(0);
	}

int PKCS7_set_content(PKCS7 *p7, PKCS7 *p7_data)
	{
	int i;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signed:
		if (p7->d.sign->contents != NULL)
			PKCS7_free(p7->d.sign->contents);
		p7->d.sign->contents=p7_data;
		break;
	case NID_pkcs7_digest:
	case NID_pkcs7_data:
	case NID_pkcs7_enveloped:
	case NID_pkcs7_signedAndEnveloped:
	case NID_pkcs7_encrypted:
	default:
		PKCS7err(PKCS7_F_PKCS7_SET_CONTENT,PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
		}
	return(1);
err:
	return(0);
	}

int PKCS7_set_type(PKCS7 *p7, int type)
	{
	ASN1_OBJECT *obj;

	/*PKCS7_content_free(p7);*/
	obj=OBJ_nid2obj(type); /* will not fail */

	switch (type)
		{
	case NID_pkcs7_signed:
		p7->type=obj;
		if ((p7->d.sign=PKCS7_SIGNED_new()) == NULL)
			goto err;
		ASN1_INTEGER_set(p7->d.sign->version,1);
		break;
	case NID_pkcs7_data:
		p7->type=obj;
		if ((p7->d.data=M_ASN1_OCTET_STRING_new()) == NULL)
			goto err;
		break;
	case NID_pkcs7_signedAndEnveloped:
		p7->type=obj;
		if ((p7->d.signed_and_enveloped=PKCS7_SIGN_ENVELOPE_new())
			== NULL) goto err;
		ASN1_INTEGER_set(p7->d.signed_and_enveloped->version,1);
		p7->d.signed_and_enveloped->enc_data->content_type
						= OBJ_nid2obj(NID_pkcs7_data);
		break;
	case NID_pkcs7_enveloped:
		p7->type=obj;
		if ((p7->d.enveloped=PKCS7_ENVELOPE_new())
			== NULL) goto err;
		ASN1_INTEGER_set(p7->d.enveloped->version,0);
		p7->d.enveloped->enc_data->content_type
						= OBJ_nid2obj(NID_pkcs7_data);
		break;
	case NID_pkcs7_encrypted:
		p7->type=obj;
		if ((p7->d.encrypted=PKCS7_ENCRYPT_new())
			== NULL) goto err;
		ASN1_INTEGER_set(p7->d.encrypted->version,0);
		p7->d.encrypted->enc_data->content_type
						= OBJ_nid2obj(NID_pkcs7_data);
		break;

	case NID_pkcs7_digest:
	default:
		PKCS7err(PKCS7_F_PKCS7_SET_TYPE,PKCS7_R_UNSUPPORTED_CONTENT_TYPE);
		goto err;
		}
	return(1);
err:
	return(0);
	}

int PKCS7_add_signer(PKCS7 *p7, PKCS7_SIGNER_INFO *psi)
	{
	int i,j,nid;
	X509_ALGOR *alg;
	STACK_OF(PKCS7_SIGNER_INFO) *signer_sk;
	STACK_OF(X509_ALGOR) *md_sk;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signed:
		signer_sk=	p7->d.sign->signer_info;
		md_sk=		p7->d.sign->md_algs;
		break;
	case NID_pkcs7_signedAndEnveloped:
		signer_sk=	p7->d.signed_and_enveloped->signer_info;
		md_sk=		p7->d.signed_and_enveloped->md_algs;
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_ADD_SIGNER,PKCS7_R_WRONG_CONTENT_TYPE);
		return(0);
		}

	nid=OBJ_obj2nid(psi->digest_alg->algorithm);

	/* If the digest is not currently listed, add it */
	j=0;
	for (i=0; i<sk_X509_ALGOR_num(md_sk); i++)
		{
		alg=sk_X509_ALGOR_value(md_sk,i);
		if (OBJ_obj2nid(alg->algorithm) == nid)
			{
			j=1;
			break;
			}
		}
	if (!j) /* we need to add another algorithm */
		{
		if(!(alg=X509_ALGOR_new())
			|| !(alg->parameter = ASN1_TYPE_new())) {
			PKCS7err(PKCS7_F_PKCS7_ADD_SIGNER,ERR_R_MALLOC_FAILURE);
			return(0);
		}
		alg->algorithm=OBJ_nid2obj(nid);
		alg->parameter->type = V_ASN1_NULL;
		sk_X509_ALGOR_push(md_sk,alg);
		}

	sk_PKCS7_SIGNER_INFO_push(signer_sk,psi);
	return(1);
	}

int PKCS7_add_certificate(PKCS7 *p7, X509 *x509)
	{
	int i;
	STACK_OF(X509) **sk;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signed:
		sk= &(p7->d.sign->cert);
		break;
	case NID_pkcs7_signedAndEnveloped:
		sk= &(p7->d.signed_and_enveloped->cert);
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_ADD_CERTIFICATE,PKCS7_R_WRONG_CONTENT_TYPE);
		return(0);
		}

	if (*sk == NULL)
		*sk=sk_X509_new_null();
	CRYPTO_add(&x509->references,1,CRYPTO_LOCK_X509);
	sk_X509_push(*sk,x509);
	return(1);
	}

int PKCS7_add_crl(PKCS7 *p7, X509_CRL *crl)
	{
	int i;
	STACK_OF(X509_CRL) **sk;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signed:
		sk= &(p7->d.sign->crl);
		break;
	case NID_pkcs7_signedAndEnveloped:
		sk= &(p7->d.signed_and_enveloped->crl);
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_ADD_CRL,PKCS7_R_WRONG_CONTENT_TYPE);
		return(0);
		}

	if (*sk == NULL)
		*sk=sk_X509_CRL_new_null();

	CRYPTO_add(&crl->references,1,CRYPTO_LOCK_X509_CRL);
	sk_X509_CRL_push(*sk,crl);
	return(1);
	}

int PKCS7_SIGNER_INFO_set(PKCS7_SIGNER_INFO *p7i, X509 *x509, EVP_PKEY *pkey,
	     const EVP_MD *dgst)
	{
	char is_dsa;
	if (pkey->type == EVP_PKEY_DSA) is_dsa = 1;
	else is_dsa = 0;
	/* We now need to add another PKCS7_SIGNER_INFO entry */
	ASN1_INTEGER_set(p7i->version,1);
	X509_NAME_set(&p7i->issuer_and_serial->issuer,
		X509_get_issuer_name(x509));

	/* because ASN1_INTEGER_set is used to set a 'long' we will do
	 * things the ugly way. */
	M_ASN1_INTEGER_free(p7i->issuer_and_serial->serial);
	p7i->issuer_and_serial->serial=
		M_ASN1_INTEGER_dup(X509_get_serialNumber(x509));

	/* lets keep the pkey around for a while */
	CRYPTO_add(&pkey->references,1,CRYPTO_LOCK_EVP_PKEY);
	p7i->pkey=pkey;

	/* Set the algorithms */
	if (is_dsa) p7i->digest_alg->algorithm=OBJ_nid2obj(NID_sha1);
	else	
		p7i->digest_alg->algorithm=OBJ_nid2obj(EVP_MD_type(dgst));

	if (p7i->digest_alg->parameter != NULL)
		ASN1_TYPE_free(p7i->digest_alg->parameter);
	if ((p7i->digest_alg->parameter=ASN1_TYPE_new()) == NULL)
		goto err;
	p7i->digest_alg->parameter->type=V_ASN1_NULL;

	p7i->digest_enc_alg->algorithm=OBJ_nid2obj(EVP_PKEY_type(pkey->type));

	if (p7i->digest_enc_alg->parameter != NULL)
		ASN1_TYPE_free(p7i->digest_enc_alg->parameter);
	if(is_dsa) p7i->digest_enc_alg->parameter = NULL;
	else {
		if (!(p7i->digest_enc_alg->parameter=ASN1_TYPE_new()))
			goto err;
		p7i->digest_enc_alg->parameter->type=V_ASN1_NULL;
	}

	return(1);
err:
	return(0);
	}

PKCS7_SIGNER_INFO *PKCS7_add_signature(PKCS7 *p7, X509 *x509, EVP_PKEY *pkey,
	     const EVP_MD *dgst)
	{
	PKCS7_SIGNER_INFO *si;

	if ((si=PKCS7_SIGNER_INFO_new()) == NULL) goto err;
	if (!PKCS7_SIGNER_INFO_set(si,x509,pkey,dgst)) goto err;
	if (!PKCS7_add_signer(p7,si)) goto err;
	return(si);
err:
	return(NULL);
	}

STACK_OF(PKCS7_SIGNER_INFO) *PKCS7_get_signer_info(PKCS7 *p7)
	{
	if (PKCS7_type_is_signed(p7))
		{
		return(p7->d.sign->signer_info);
		}
	else if (PKCS7_type_is_signedAndEnveloped(p7))
		{
		return(p7->d.signed_and_enveloped->signer_info);
		}
	else
		return(NULL);
	}

PKCS7_RECIP_INFO *PKCS7_add_recipient(PKCS7 *p7, X509 *x509)
	{
	PKCS7_RECIP_INFO *ri;

	if ((ri=PKCS7_RECIP_INFO_new()) == NULL) goto err;
	if (!PKCS7_RECIP_INFO_set(ri,x509)) goto err;
	if (!PKCS7_add_recipient_info(p7,ri)) goto err;
	return(ri);
err:
	return(NULL);
	}

int PKCS7_add_recipient_info(PKCS7 *p7, PKCS7_RECIP_INFO *ri)
	{
	int i;
	STACK_OF(PKCS7_RECIP_INFO) *sk;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signedAndEnveloped:
		sk=	p7->d.signed_and_enveloped->recipientinfo;
		break;
	case NID_pkcs7_enveloped:
		sk=	p7->d.enveloped->recipientinfo;
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_ADD_RECIPIENT_INFO,PKCS7_R_WRONG_CONTENT_TYPE);
		return(0);
		}

	sk_PKCS7_RECIP_INFO_push(sk,ri);
	return(1);
	}

int PKCS7_RECIP_INFO_set(PKCS7_RECIP_INFO *p7i, X509 *x509)
	{
	ASN1_INTEGER_set(p7i->version,0);
	X509_NAME_set(&p7i->issuer_and_serial->issuer,
		X509_get_issuer_name(x509));

	M_ASN1_INTEGER_free(p7i->issuer_and_serial->serial);
	p7i->issuer_and_serial->serial=
		M_ASN1_INTEGER_dup(X509_get_serialNumber(x509));

	X509_ALGOR_free(p7i->key_enc_algor);
	p7i->key_enc_algor= X509_ALGOR_dup(x509->cert_info->key->algor);

	CRYPTO_add(&x509->references,1,CRYPTO_LOCK_X509);
	p7i->cert=x509;

	return(1);
	}

X509 *PKCS7_cert_from_signer_info(PKCS7 *p7, PKCS7_SIGNER_INFO *si)
	{
	if (PKCS7_type_is_signed(p7))
		return(X509_find_by_issuer_and_serial(p7->d.sign->cert,
			si->issuer_and_serial->issuer,
			si->issuer_and_serial->serial));
	else
		return(NULL);
	}

int PKCS7_set_cipher(PKCS7 *p7, const EVP_CIPHER *cipher)
	{
	int i;
	ASN1_OBJECT *objtmp;
	PKCS7_ENC_CONTENT *ec;

	i=OBJ_obj2nid(p7->type);
	switch (i)
		{
	case NID_pkcs7_signedAndEnveloped:
		ec=p7->d.signed_and_enveloped->enc_data;
		break;
	case NID_pkcs7_enveloped:
		ec=p7->d.enveloped->enc_data;
		break;
	default:
		PKCS7err(PKCS7_F_PKCS7_SET_CIPHER,PKCS7_R_WRONG_CONTENT_TYPE);
		return(0);
		}

	/* Check cipher OID exists and has data in it*/
	i = EVP_CIPHER_type(cipher);
	if(i == NID_undef) {
		PKCS7err(PKCS7_F_PKCS7_SET_CIPHER,PKCS7_R_CIPHER_HAS_NO_OBJECT_IDENTIFIER);
		return(0);
	}
	objtmp = OBJ_nid2obj(i);

	ec->cipher = cipher;
	return 1;
	}

