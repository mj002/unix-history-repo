/*-
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
 * Copyright (c) 2001, 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by NAI Labs,
 * the Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD$
 */

/*
 * Developed by the TrustedBSD Project.
 * MLS fixed label mandatory confidentiality policy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/mac.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/pipe.h>
#include <sys/sysctl.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>

#include <vm/vm.h>

#include <sys/mac_policy.h>

#include <security/mac_mls/mac_mls.h>

SYSCTL_DECL(_security_mac);

SYSCTL_NODE(_security_mac, OID_AUTO, mls, CTLFLAG_RW, 0,
    "TrustedBSD mac_mls policy controls");

static int	mac_mls_enabled = 0;
SYSCTL_INT(_security_mac_mls, OID_AUTO, enabled, CTLFLAG_RW,
    &mac_mls_enabled, 0, "Enforce MAC/MLS policy");
TUNABLE_INT("security.mac.mls.enabled", &mac_mls_enabled);

static int	destroyed_not_inited;
SYSCTL_INT(_security_mac_mls, OID_AUTO, destroyed_not_inited, CTLFLAG_RD,
    &destroyed_not_inited, 0, "Count of labels destroyed but not inited");

static int	ptys_equal = 0;
SYSCTL_INT(_security_mac_mls, OID_AUTO, ptys_equal, CTLFLAG_RW,
    &ptys_equal, 0, "Label pty devices as mls/equal on create");
TUNABLE_INT("security.mac.mls.ptys_equal", &ptys_equal);

static int	revocation_enabled = 0;
SYSCTL_INT(_security_mac_mls, OID_AUTO, revocation_enabled, CTLFLAG_RW,
    &revocation_enabled, 0, "Revoke access to objects on relabel");
TUNABLE_INT("security.mac.mls.revocation_enabled", &revocation_enabled);

static int	max_compartments = MAC_MLS_MAX_COMPARTMENTS;
SYSCTL_INT(_security_mac_mls, OID_AUTO, max_compartments, CTLFLAG_RD,
    &max_compartments, 0, "Maximum compartments the policy supports");

static int	mac_mls_slot;
#define	SLOT(l)	((struct mac_mls *)LABEL_TO_SLOT((l), mac_mls_slot).l_ptr)

MALLOC_DEFINE(M_MACMLS, "mls label", "MAC/MLS labels");

static __inline int
mls_bit_set_empty(u_char *set) {
	int i;

	for (i = 0; i < MAC_MLS_MAX_COMPARTMENTS >> 3; i++)
		if (set[i] != 0)
			return (0);
	return (1);
}

static struct mac_mls *
mls_alloc(int flag)
{
	struct mac_mls *mac_mls;

	mac_mls = malloc(sizeof(struct mac_mls), M_MACMLS, M_ZERO | flag);

	return (mac_mls);
}

static void
mls_free(struct mac_mls *mac_mls)
{

	if (mac_mls != NULL)
		free(mac_mls, M_MACMLS);
	else
		atomic_add_int(&destroyed_not_inited, 1);
}

static int
mls_atmostflags(struct mac_mls *mac_mls, int flags)
{

	if ((mac_mls->mm_flags & flags) != mac_mls->mm_flags)
		return (EINVAL);
	return (0);
}

static int
mac_mls_dominate_element(struct mac_mls_element *a,
    struct mac_mls_element *b)
{
	int bit;

	switch (a->mme_type) {
	case MAC_MLS_TYPE_EQUAL:
	case MAC_MLS_TYPE_HIGH:
		return (1);

	case MAC_MLS_TYPE_LOW:
		switch (b->mme_type) {
		case MAC_MLS_TYPE_LEVEL:
		case MAC_MLS_TYPE_HIGH:
			return (0);

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_LOW:
			return (1);

		default:
			panic("mac_mls_dominate_element: b->mme_type invalid");
		}

	case MAC_MLS_TYPE_LEVEL:
		switch (b->mme_type) {
		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_LOW:
			return (1);

		case MAC_MLS_TYPE_HIGH:
			return (0);

		case MAC_MLS_TYPE_LEVEL:
			for (bit = 1; bit <= MAC_MLS_MAX_COMPARTMENTS; bit++)
				if (!MAC_MLS_BIT_TEST(bit,
				    a->mme_compartments) &&
				    MAC_MLS_BIT_TEST(bit, b->mme_compartments))
					return (0);
			return (a->mme_level >= b->mme_level);

		default:
			panic("mac_mls_dominate_element: b->mme_type invalid");
		}

	default:
		panic("mac_mls_dominate_element: a->mme_type invalid");
	}

	return (0);
}

static int
mac_mls_range_in_range(struct mac_mls *rangea, struct mac_mls *rangeb)
{

	return (mac_mls_dominate_element(&rangeb->mm_rangehigh,
	    &rangea->mm_rangehigh) &&
	    mac_mls_dominate_element(&rangea->mm_rangelow,
	    &rangeb->mm_rangelow));
}

static int
mac_mls_single_in_range(struct mac_mls *single, struct mac_mls *range)
{

	KASSERT((single->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_single_in_range: a not single"));
	KASSERT((range->mm_flags & MAC_MLS_FLAG_RANGE) != 0,
	    ("mac_mls_single_in_range: b not range"));

	return (mac_mls_dominate_element(&range->mm_rangehigh,
	    &single->mm_single) &&
	    mac_mls_dominate_element(&single->mm_single,
	    &range->mm_rangelow));

	return (1);
}

static int
mac_mls_dominate_single(struct mac_mls *a, struct mac_mls *b)
{
	KASSERT((a->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_dominate_single: a not single"));
	KASSERT((b->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_dominate_single: b not single"));

	return (mac_mls_dominate_element(&a->mm_single, &b->mm_single));
}

static int
mac_mls_equal_element(struct mac_mls_element *a, struct mac_mls_element *b)
{

	if (a->mme_type == MAC_MLS_TYPE_EQUAL ||
	    b->mme_type == MAC_MLS_TYPE_EQUAL)
		return (1);

	return (a->mme_type == b->mme_type && a->mme_level == b->mme_level);
}

static int
mac_mls_equal_single(struct mac_mls *a, struct mac_mls *b)
{

	KASSERT((a->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_equal_single: a not single"));
	KASSERT((b->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_equal_single: b not single"));

	return (mac_mls_equal_element(&a->mm_single, &b->mm_single));
}

static int
mac_mls_contains_equal(struct mac_mls *mac_mls)
{

	if (mac_mls->mm_flags & MAC_MLS_FLAG_SINGLE)
		if (mac_mls->mm_single.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);

	if (mac_mls->mm_flags & MAC_MLS_FLAG_RANGE) {
		if (mac_mls->mm_rangelow.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);
		if (mac_mls->mm_rangehigh.mme_type == MAC_MLS_TYPE_EQUAL)
			return (1);
	}

	return (0);
}

static int
mac_mls_subject_equal_ok(struct mac_mls *mac_mls)
{

	KASSERT((mac_mls->mm_flags & MAC_MLS_FLAGS_BOTH) == MAC_MLS_FLAGS_BOTH,
	    ("mac_mls_subject_equal_ok: subject doesn't have both labels"));

	/* If the single is EQUAL, it's ok. */
	if (mac_mls->mm_single.mme_type == MAC_MLS_TYPE_EQUAL)
		return (0);

	/* If either range endpoint is EQUAL, it's ok. */
	if (mac_mls->mm_rangelow.mme_type == MAC_MLS_TYPE_EQUAL ||
	    mac_mls->mm_rangehigh.mme_type == MAC_MLS_TYPE_EQUAL)
		return (0);

	/* If the range is low-high, it's ok. */
	if (mac_mls->mm_rangelow.mme_type == MAC_MLS_TYPE_LOW &&
	    mac_mls->mm_rangehigh.mme_type == MAC_MLS_TYPE_HIGH)
		return (0);

	/* It's not ok. */
	return (EPERM);
}

static int
mac_mls_valid(struct mac_mls *mac_mls)
{

	if (mac_mls->mm_flags & MAC_MLS_FLAG_SINGLE) {
		switch (mac_mls->mm_single.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mac_mls->mm_single.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mac_mls->mm_single.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (mac_mls->mm_single.mme_type != MAC_MLS_TYPE_UNDEF)
			return (EINVAL);
	}

	if (mac_mls->mm_flags & MAC_MLS_FLAG_RANGE) {
		switch (mac_mls->mm_rangelow.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mac_mls->mm_rangelow.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mac_mls->mm_rangelow.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}

		switch (mac_mls->mm_rangehigh.mme_type) {
		case MAC_MLS_TYPE_LEVEL:
			break;

		case MAC_MLS_TYPE_EQUAL:
		case MAC_MLS_TYPE_HIGH:
		case MAC_MLS_TYPE_LOW:
			if (mac_mls->mm_rangehigh.mme_level != 0 ||
			    !MAC_MLS_BIT_SET_EMPTY(
			    mac_mls->mm_rangehigh.mme_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
		if (!mac_mls_dominate_element(&mac_mls->mm_rangehigh,
		    &mac_mls->mm_rangelow))
			return (EINVAL);
	} else {
		if (mac_mls->mm_rangelow.mme_type != MAC_MLS_TYPE_UNDEF ||
		    mac_mls->mm_rangehigh.mme_type != MAC_MLS_TYPE_UNDEF)
			return (EINVAL);
	}

	return (0);
}

static void
mac_mls_set_range(struct mac_mls *mac_mls, u_short typelow,
    u_short levellow, u_char *compartmentslow, u_short typehigh,
    u_short levelhigh, u_char *compartmentshigh)
{

	mac_mls->mm_rangelow.mme_type = typelow;
	mac_mls->mm_rangelow.mme_level = levellow;
	if (compartmentslow != NULL)
		memcpy(mac_mls->mm_rangelow.mme_compartments,
		    compartmentslow,
		    sizeof(mac_mls->mm_rangelow.mme_compartments));
	mac_mls->mm_rangehigh.mme_type = typehigh;
	mac_mls->mm_rangehigh.mme_level = levelhigh;
	if (compartmentshigh != NULL)
		memcpy(mac_mls->mm_rangehigh.mme_compartments,
		    compartmentshigh,
		    sizeof(mac_mls->mm_rangehigh.mme_compartments));
	mac_mls->mm_flags |= MAC_MLS_FLAG_RANGE;
}

static void
mac_mls_set_single(struct mac_mls *mac_mls, u_short type, u_short level,
    u_char *compartments)
{

	mac_mls->mm_single.mme_type = type;
	mac_mls->mm_single.mme_level = level;
	if (compartments != NULL)
		memcpy(mac_mls->mm_single.mme_compartments, compartments,
		    sizeof(mac_mls->mm_single.mme_compartments));
	mac_mls->mm_flags |= MAC_MLS_FLAG_SINGLE;
}

static void
mac_mls_copy_range(struct mac_mls *labelfrom, struct mac_mls *labelto)
{

	KASSERT((labelfrom->mm_flags & MAC_MLS_FLAG_RANGE) != 0,
	    ("mac_mls_copy_range: labelfrom not range"));

	labelto->mm_rangelow = labelfrom->mm_rangelow;
	labelto->mm_rangehigh = labelfrom->mm_rangehigh;
	labelto->mm_flags |= MAC_MLS_FLAG_RANGE;
}

static void
mac_mls_copy_single(struct mac_mls *labelfrom, struct mac_mls *labelto)
{

	KASSERT((labelfrom->mm_flags & MAC_MLS_FLAG_SINGLE) != 0,
	    ("mac_mls_copy_single: labelfrom not single"));

	labelto->mm_single = labelfrom->mm_single;
	labelto->mm_flags |= MAC_MLS_FLAG_SINGLE;
}

static void
mac_mls_copy(struct mac_mls *source, struct mac_mls *dest)
{

	if (source->mm_flags & MAC_MLS_FLAG_SINGLE)
		mac_mls_copy_single(source, dest);
	if (source->mm_flags & MAC_MLS_FLAG_RANGE)
		mac_mls_copy_range(source, dest);
}

/*
 * Policy module operations.
 */
static void
mac_mls_destroy(struct mac_policy_conf *conf)
{

}

static void
mac_mls_init(struct mac_policy_conf *conf)
{

}

/*
 * Label operations.
 */
static void
mac_mls_init_label(struct label *label)
{

	SLOT(label) = mls_alloc(M_WAITOK);
}

static int
mac_mls_init_label_waitcheck(struct label *label, int flag)
{

	SLOT(label) = mls_alloc(flag);
	if (SLOT(label) == NULL)
		return (ENOMEM);

	return (0);
}

static void
mac_mls_destroy_label(struct label *label)
{

	mls_free(SLOT(label));
	SLOT(label) = NULL;
}

/*
 * mac_mls_element_to_string() is basically an snprintf wrapper with
 * the same properties as snprintf().  It returns the length it would
 * have added to the string in the event the string is too short.
 */
static size_t
mac_mls_element_to_string(char *string, size_t size,
    struct mac_mls_element *element)
{
	int pos, bit = 1;

	switch (element->mme_type) {
	case MAC_MLS_TYPE_HIGH:
		return (snprintf(string, size, "high"));

	case MAC_MLS_TYPE_LOW:
		return (snprintf(string, size, "low"));

	case MAC_MLS_TYPE_EQUAL:
		return (snprintf(string, size, "equal"));

	case MAC_MLS_TYPE_LEVEL:
		pos = snprintf(string, size, "%d:", element->mme_level);
		for (bit = 1; bit <= MAC_MLS_MAX_COMPARTMENTS; bit++) {
			if (MAC_MLS_BIT_TEST(bit, element->mme_compartments))
				pos += snprintf(string + pos, size - pos,
				    "%d+", bit);
		}
		if (string[pos - 1] == '+' || string[pos - 1] == ':')
			string[--pos] = NULL;
		return (pos);

	default:
		panic("mac_mls_element_to_string: invalid type (%d)",
		    element->mme_type);
	}
}

static size_t
mac_mls_to_string(char *string, size_t size, size_t *caller_len,
    struct mac_mls *mac_mls)
{
	size_t left, len;
	char *curptr;

	bzero(string, size);
	curptr = string;
	left = size;

	if (mac_mls->mm_flags & MAC_MLS_FLAG_SINGLE) {
		len = mac_mls_element_to_string(curptr, left,
		    &mac_mls->mm_single);
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;
	}

	if (mac_mls->mm_flags & MAC_MLS_FLAG_RANGE) {
		len = snprintf(curptr, left, "(");
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;

		len = mac_mls_element_to_string(curptr, left,
		    &mac_mls->mm_rangelow);
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;

		len = snprintf(curptr, left, "-");
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;

		len = mac_mls_element_to_string(curptr, left,
		    &mac_mls->mm_rangehigh);
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;

		len = snprintf(curptr, left, ")");
		if (len >= left)
			return (EINVAL);
		left -= len;
		curptr += len;
	}

	*caller_len = strlen(string);
	return (0);
}

static int
mac_mls_externalize_label(struct label *label, char *element_name,
    char *element_data, size_t size, size_t *len, int *claimed)
{
	struct mac_mls *mac_mls;
	int error;

	if (strcmp(MAC_MLS_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	mac_mls = SLOT(label);

	error = mac_mls_to_string(element_data, size, len, mac_mls);
	if (error)
		return (error);

	*len = strlen(element_data);
	return (0);
}

static int
mac_mls_externalize_vnode_oldmac(struct label *label, struct oldmac *extmac)
{
	struct mac_mls *mac_mls;

	mac_mls = SLOT(label);

	if (mac_mls == NULL) {
		printf("mac_mls_externalize: NULL pointer\n");
		return (0);
	}

	extmac->m_mls = *mac_mls;

	return (0);
}

static int
mac_mls_parse_element(struct mac_mls_element *element, char *string)
{

	if (strcmp(string, "high") == 0 ||
	    strcmp(string, "hi") == 0) {
		element->mme_type = MAC_MLS_TYPE_HIGH;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else if (strcmp(string, "low") == 0 ||
	    strcmp(string, "lo") == 0) {
		element->mme_type = MAC_MLS_TYPE_LOW;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else if (strcmp(string, "equal") == 0 ||
	    strcmp(string, "eq") == 0) {
		element->mme_type = MAC_MLS_TYPE_EQUAL;
		element->mme_level = MAC_MLS_TYPE_UNDEF;
	} else {
		char *p0, *p1;
		int d;

		p0 = string;
		d = strtol(p0, &p1, 10);

		if (d < 0 || d > 65535)
			return (EINVAL);
		element->mme_type = MAC_MLS_TYPE_LEVEL;
		element->mme_level = d;

		if (*p1 != ':')  {
			if (p1 == p0 || *p1 != '\0')
				return (EINVAL);
			else
				return (0);
		}
		else
			if (*(p1 + 1) == '\0')
				return (0);

		while ((p0 = ++p1)) {
			d = strtol(p0, &p1, 10);
			if (d < 1 || d > MAC_MLS_MAX_COMPARTMENTS)
				return (EINVAL);

			MAC_MLS_BIT_SET(d, element->mme_compartments);

			if (*p1 == '\0')
				break;
			if (p1 == p0 || *p1 != '+')
				return (EINVAL);
		}
	}

	return (0);
}

/*
 * Note: destructively consumes the string, make a local copy before
 * calling if that's a problem.
 */
static int
mac_mls_parse(struct mac_mls *mac_mls, char *string)
{
	char *range, *rangeend, *rangehigh, *rangelow, *single;
	int error;

	/* Do we have a range? */
	single = string;
	range = index(string, '(');
	if (range == single)
		single = NULL;
	rangelow = rangehigh = NULL;
	if (range != NULL) {
		/* Nul terminate the end of the single string. */
		*range = '\0';
		range++;
		rangelow = range;
		rangehigh = index(rangelow, '-');
		if (rangehigh == NULL)
			return (EINVAL);
		rangehigh++;
		if (*rangelow == '\0' || *rangehigh == '\0')
			return (EINVAL);
		rangeend = index(rangehigh, ')');
		if (rangeend == NULL)
			return (EINVAL);
		if (*(rangeend + 1) != '\0')
			return (EINVAL);
		/* Nul terminate the ends of the ranges. */
		*(rangehigh - 1) = '\0';
		*rangeend = '\0';
	}
	KASSERT((rangelow != NULL && rangehigh != NULL) ||
	    (rangelow == NULL && rangehigh == NULL),
	    ("mac_mls_internalize_label: range mismatch"));

	bzero(mac_mls, sizeof(*mac_mls));
	if (single != NULL) {
		error = mac_mls_parse_element(&mac_mls->mm_single, single);
		if (error)
			return (error);
		mac_mls->mm_flags |= MAC_MLS_FLAG_SINGLE;
	}

	if (rangelow != NULL) {
		error = mac_mls_parse_element(&mac_mls->mm_rangelow,
		    rangelow);
		if (error)
			return (error);
		error = mac_mls_parse_element(&mac_mls->mm_rangehigh,
		    rangehigh);
		if (error)
			return (error);
		mac_mls->mm_flags |= MAC_MLS_FLAG_RANGE;
	}

	error = mac_mls_valid(mac_mls);
	if (error)
		return (error);

	return (0);
}

static int
mac_mls_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{
	struct mac_mls *mac_mls, mac_mls_temp;
	int error;

	if (strcmp(MAC_MLS_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	error = mac_mls_parse(&mac_mls_temp, element_data);
	if (error)
		return (error);

	mac_mls = SLOT(label);
	*mac_mls = mac_mls_temp;

	return (0);
}

static void
mac_mls_copy_label(struct label *src, struct label *dest)
{

	*SLOT(dest) = *SLOT(src);
}

/*
 * Labeling event operations: file system objects, and things that look
 * a lot like file system objects.
 */
static void
mac_mls_create_devfs_device(dev_t dev, struct devfs_dirent *devfs_dirent,
    struct label *label)
{
	struct mac_mls *mac_mls;
	int mls_type;

	mac_mls = SLOT(label);
	if (strcmp(dev->si_name, "null") == 0 ||
	    strcmp(dev->si_name, "zero") == 0 ||
	    strcmp(dev->si_name, "random") == 0 ||
	    strncmp(dev->si_name, "fd/", strlen("fd/")) == 0)
		mls_type = MAC_MLS_TYPE_EQUAL;
	else if (strcmp(dev->si_name, "kmem") == 0 ||
	    strcmp(dev->si_name, "mem") == 0)
		mls_type = MAC_MLS_TYPE_HIGH;
	else if (ptys_equal &&
	    (strncmp(dev->si_name, "ttyp", strlen("ttyp")) == 0 ||
	    strncmp(dev->si_name, "ptyp", strlen("ptyp")) == 0))
		mls_type = MAC_MLS_TYPE_EQUAL;
	else
		mls_type = MAC_MLS_TYPE_LOW;
	mac_mls_set_single(mac_mls, mls_type, 0, NULL);
}

static void
mac_mls_create_devfs_directory(char *dirname, int dirnamelen,
    struct devfs_dirent *devfs_dirent, struct label *label)
{
	struct mac_mls *mac_mls;

	mac_mls = SLOT(label);
	mac_mls_set_single(mac_mls, MAC_MLS_TYPE_LOW, 0, NULL);
}

static void
mac_mls_create_devfs_symlink(struct ucred *cred, struct devfs_dirent *dd,
    struct label *ddlabel, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(delabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_devfs_vnode(struct devfs_dirent *devfs_dirent,
    struct label *direntlabel, struct vnode *vp, struct label *vnodelabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(direntlabel);
	dest = SLOT(vnodelabel);
	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_vnode(struct ucred *cred, struct vnode *parent,
    struct label *parentlabel, struct vnode *child, struct label *childlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(childlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_mount(struct ucred *cred, struct mount *mp,
    struct label *mntlabel, struct label *fslabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(mntlabel);
	mac_mls_copy_single(source, dest);
	dest = SLOT(fslabel);
	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_root_mount(struct ucred *cred, struct mount *mp,
    struct label *mntlabel, struct label *fslabel)
{
	struct mac_mls *mac_mls;

	/* Always mount root as high integrity. */
	mac_mls = SLOT(fslabel);
	mac_mls_set_single(mac_mls, MAC_MLS_TYPE_LOW, 0, NULL);
	mac_mls = SLOT(mntlabel);
	mac_mls_set_single(mac_mls, MAC_MLS_TYPE_LOW, 0, NULL);
}

static void
mac_mls_relabel_vnode(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, struct label *label)
{
	struct mac_mls *source, *dest;

	source = SLOT(label);
	dest = SLOT(vnodelabel);

	mac_mls_copy(source, dest);
}

static void
mac_mls_update_devfsdirent(struct devfs_dirent *devfs_dirent,
    struct label *direntlabel, struct vnode *vp, struct label *vnodelabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(vnodelabel);
	dest = SLOT(direntlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_update_procfsvnode(struct vnode *vp, struct label *vnodelabel,
    struct ucred *cred)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(vnodelabel);

	/*
	 * Only copy the single, not the range, since vnodes only have
	 * a single.
	 */
	mac_mls_copy_single(source, dest);
}

static int
mac_mls_update_vnode_from_externalized(struct vnode *vp,
    struct label *vnodelabel, struct oldmac *extmac)
{
	struct mac_mls *source, *dest;
	int error;

	source = &extmac->m_mls;
	dest = SLOT(vnodelabel);

	error = mac_mls_valid(source);
	if (error)
		return (error);

	if ((source->mm_flags & MAC_MLS_FLAGS_BOTH) != MAC_MLS_FLAG_SINGLE)
		return (EINVAL);

	mac_mls_copy_single(source, dest);

	return (0);
}

static void
mac_mls_update_vnode_from_mount(struct vnode *vp, struct label *vnodelabel,
    struct mount *mp, struct label *fslabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(fslabel);
	dest = SLOT(vnodelabel);

	mac_mls_copy_single(source, dest);
}

/*
 * Labeling event operations: IPC object.
 */
static void
mac_mls_create_mbuf_from_socket(struct socket *so, struct label *socketlabel,
    struct mbuf *m, struct label *mbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(socketlabel);
	dest = SLOT(mbuflabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_socket(struct ucred *cred, struct socket *socket,
    struct label *socketlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(socketlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_pipe(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(pipelabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_socket_from_socket(struct socket *oldsocket,
    struct label *oldsocketlabel, struct socket *newsocket,
    struct label *newsocketlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(oldsocketlabel);
	dest = SLOT(newsocketlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_relabel_socket(struct ucred *cred, struct socket *socket,
    struct label *socketlabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(socketlabel);

	mac_mls_copy(source, dest);
}

static void
mac_mls_relabel_pipe(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(pipelabel);

	mac_mls_copy(source, dest);
}

static void
mac_mls_set_socket_peer_from_mbuf(struct mbuf *mbuf, struct label *mbuflabel,
    struct socket *socket, struct label *socketpeerlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(mbuflabel);
	dest = SLOT(socketpeerlabel);

	mac_mls_copy_single(source, dest);
}

/*
 * Labeling event operations: network objects.
 */
static void
mac_mls_set_socket_peer_from_socket(struct socket *oldsocket,
    struct label *oldsocketlabel, struct socket *newsocket,
    struct label *newsocketpeerlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(oldsocketlabel);
	dest = SLOT(newsocketpeerlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_bpfdesc(struct ucred *cred, struct bpf_d *bpf_d,
    struct label *bpflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred->cr_label);
	dest = SLOT(bpflabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_ifnet(struct ifnet *ifnet, struct label *ifnetlabel)
{
	struct mac_mls *dest;
	int level;

	dest = SLOT(ifnetlabel);

	if (ifnet->if_type == IFT_LOOP)
		level = MAC_MLS_TYPE_EQUAL;
	else
		level = MAC_MLS_TYPE_LOW;

	mac_mls_set_single(dest, level, 0, NULL);
	mac_mls_set_range(dest, level, 0, NULL, level, 0, NULL);
}

static void
mac_mls_create_ipq(struct mbuf *fragment, struct label *fragmentlabel,
    struct ipq *ipq, struct label *ipqlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(fragmentlabel);
	dest = SLOT(ipqlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_datagram_from_ipq(struct ipq *ipq, struct label *ipqlabel,
    struct mbuf *datagram, struct label *datagramlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(ipqlabel);
	dest = SLOT(datagramlabel);

	/* Just use the head, since we require them all to match. */
	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_fragment(struct mbuf *datagram, struct label *datagramlabel,
    struct mbuf *fragment, struct label *fragmentlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(datagramlabel);
	dest = SLOT(fragmentlabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_mbuf_from_mbuf(struct mbuf *oldmbuf,
    struct label *oldmbuflabel, struct mbuf *newmbuf,
    struct label *newmbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(oldmbuflabel);
	dest = SLOT(newmbuflabel);

	/*
	 * Because the source mbuf may not yet have been "created",
	 * just initialized, we do a conditional copy.  Since we don't
	 * allow mbufs to have ranges, do a KASSERT to make sure that
	 * doesn't happen.
	 */
	KASSERT((source->mm_flags & MAC_MLS_FLAG_RANGE) == 0,
	    ("mac_mls_create_mbuf_from_mbuf: source mbuf has range"));
	mac_mls_copy(source, dest);
}

static void
mac_mls_create_mbuf_linklayer(struct ifnet *ifnet, struct label *ifnetlabel,
    struct mbuf *mbuf, struct label *mbuflabel)
{
	struct mac_mls *dest;

	dest = SLOT(mbuflabel);

	mac_mls_set_single(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
}

static void
mac_mls_create_mbuf_from_bpfdesc(struct bpf_d *bpf_d, struct label *bpflabel,
    struct mbuf *mbuf, struct label *mbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(bpflabel);
	dest = SLOT(mbuflabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_mbuf_from_ifnet(struct ifnet *ifnet, struct label *ifnetlabel,
    struct mbuf *m, struct label *mbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(ifnetlabel);
	dest = SLOT(mbuflabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_mbuf_multicast_encap(struct mbuf *oldmbuf,
    struct label *oldmbuflabel, struct ifnet *ifnet, struct label *ifnetlabel,
    struct mbuf *newmbuf, struct label *newmbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(oldmbuflabel);
	dest = SLOT(newmbuflabel);

	mac_mls_copy_single(source, dest);
}

static void
mac_mls_create_mbuf_netlayer(struct mbuf *oldmbuf, struct label *oldmbuflabel,
    struct mbuf *newmbuf, struct label *newmbuflabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(oldmbuflabel);
	dest = SLOT(newmbuflabel);

	mac_mls_copy_single(source, dest);
}

static int
mac_mls_fragment_match(struct mbuf *fragment, struct label *fragmentlabel,
    struct ipq *ipq, struct label *ipqlabel)
{
	struct mac_mls *a, *b;

	a = SLOT(ipqlabel);
	b = SLOT(fragmentlabel);

	return (mac_mls_equal_single(a, b));
}

static void
mac_mls_relabel_ifnet(struct ucred *cred, struct ifnet *ifnet,
    struct label *ifnetlabel, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(ifnetlabel);

	mac_mls_copy(source, dest);
}

static void
mac_mls_update_ipq(struct mbuf *fragment, struct label *fragmentlabel,
    struct ipq *ipq, struct label *ipqlabel)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

/*
 * Labeling event operations: processes.
 */
static void
mac_mls_create_cred(struct ucred *cred_parent, struct ucred *cred_child)
{
	struct mac_mls *source, *dest;

	source = SLOT(&cred_parent->cr_label);
	dest = SLOT(&cred_child->cr_label);

	mac_mls_copy_single(source, dest);
	mac_mls_copy_range(source, dest);
}

static void
mac_mls_execve_transition(struct ucred *old, struct ucred *new,
    struct vnode *vp, struct label *vnodelabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(&old->cr_label);
	dest = SLOT(&new->cr_label);

	mac_mls_copy_single(source, dest);
	mac_mls_copy_range(source, dest);
}

static int
mac_mls_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *vnodelabel)
{

	return (0);
}

static void
mac_mls_create_proc0(struct ucred *cred)
{
	struct mac_mls *dest;

	dest = SLOT(&cred->cr_label);

	mac_mls_set_single(dest, MAC_MLS_TYPE_EQUAL, 0, NULL);
	mac_mls_set_range(dest, MAC_MLS_TYPE_LOW, 0, NULL, MAC_MLS_TYPE_HIGH,
	    0, NULL);
}

static void
mac_mls_create_proc1(struct ucred *cred)
{
	struct mac_mls *dest;

	dest = SLOT(&cred->cr_label);

	mac_mls_set_single(dest, MAC_MLS_TYPE_LOW, 0, NULL);
	mac_mls_set_range(dest, MAC_MLS_TYPE_LOW, 0, NULL, MAC_MLS_TYPE_HIGH,
	    0, NULL);
}

static void
mac_mls_relabel_cred(struct ucred *cred, struct label *newlabel)
{
	struct mac_mls *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(&cred->cr_label);

	mac_mls_copy(source, dest);
}

/*
 * Access control checks.
 */
static int
mac_mls_check_bpfdesc_receive(struct bpf_d *bpf_d, struct label *bpflabel,
     struct ifnet *ifnet, struct label *ifnetlabel)
{
	struct mac_mls *a, *b;

	if (!mac_mls_enabled)
		return (0);

	a = SLOT(bpflabel);
	b = SLOT(ifnetlabel);

	if (mac_mls_equal_single(a, b))
		return (0);
	return (EACCES);
}

static int
mac_mls_check_cred_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_mls *subj, *new;
	int error;

	subj = SLOT(&cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is an MLS label update for the credential, it may be
	 * an update of single, range, or both.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAGS_BOTH) {
		/*
		 * To change the MLS single label on a credential, the
		 * new single label must be in the current range.
		 */
		if (new->mm_flags & MAC_MLS_FLAG_SINGLE &&
		    !mac_mls_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS range label on a credential, the
		 * new range label must be in the current range.
		 */
		if (new->mm_flags & MAC_MLS_FLAG_RANGE &&
		    !mac_mls_range_in_range(new, subj))
			return (EPERM);

		/*
		 * To have EQUAL in any component of the new credential
		 * MLS label, the subject must already have EQUAL in
		 * their label.
		 */
		if (mac_mls_contains_equal(new)) {
			error = mac_mls_subject_equal_ok(subj);
			if (error)
				return (error);
		}

		/*
		 * XXXMAC: Additional consistency tests regarding the single
		 * and range of the new label might be performed here.
		 */
	}

	return (0);
}

static int
mac_mls_check_cred_visible(struct ucred *u1, struct ucred *u2)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&u1->cr_label);
	obj = SLOT(&u2->cr_label);

	/* XXX: range */
	if (!mac_mls_dominate_single(subj, obj))
		return (ESRCH);

	return (0);
}

static int
mac_mls_check_ifnet_relabel(struct ucred *cred, struct ifnet *ifnet,
    struct label *ifnetlabel, struct label *newlabel)
{
	struct mac_mls *subj, *new;
	int error;

	subj = SLOT(&cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is an MLS label update for the interface, it may
	 * be an update of single, range, or both.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAGS_BOTH) {
		/*
		 * Rely on traditional superuser status for the MLS
		 * interface relabel requirements.  XXX: This will go
		 * away.
		 */
		error = suser_cred(cred, 0);
		if (error)
			return (EPERM);

		/*
		 * XXXMAC: Additional consistency tests regarding the single
		 * and the range of the new label might be performed here.
		 */
	}

	return (0);
}

static int
mac_mls_check_ifnet_transmit(struct ifnet *ifnet, struct label *ifnetlabel,
    struct mbuf *m, struct label *mbuflabel)
{
	struct mac_mls *p, *i;

	if (!mac_mls_enabled)
		return (0);

	p = SLOT(mbuflabel);
	i = SLOT(ifnetlabel);

	return (mac_mls_single_in_range(p, i) ? 0 : EACCES);
}

static int
mac_mls_check_mount_stat(struct ucred *cred, struct mount *mp,
    struct label *mntlabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(mntlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_pipe_ioctl(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel, unsigned long cmd, void /* caddr_t */ *data)
{

	if(!mac_mls_enabled)
		return (0);

	/* XXX: This will be implemented soon... */

	return (0);
}

static int
mac_mls_check_pipe_poll(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT((pipelabel));

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_pipe_read(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT((pipelabel));

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_pipe_relabel(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel, struct label *newlabel)
{
	struct mac_mls *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(&cred->cr_label);
	obj = SLOT(pipelabel);

	/*
	 * If there is an MLS label update for a pipe, it must be a
	 * single update.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_SINGLE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of a pipe (MLS label or not), MLS must
	 * authorize the relabel.
	 */
	if (!mac_mls_single_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_SINGLE) {
		/*
		 * To change the MLS label on a pipe, the new pipe label
		 * must be in the subject range.
		 */
		if (!mac_mls_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on a pipe to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (mac_mls_contains_equal(new)) {
			error = mac_mls_subject_equal_ok(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_mls_check_pipe_stat(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT((pipelabel));

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_pipe_write(struct ucred *cred, struct pipe *pipe,
    struct label *pipelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT((pipelabel));

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_proc_debug(struct ucred *cred, struct proc *proc)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(&proc->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_mls_dominate_single(subj, obj))
		return (ESRCH);
	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_proc_sched(struct ucred *cred, struct proc *proc)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(&proc->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_mls_dominate_single(subj, obj))
		return (ESRCH);
	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_proc_signal(struct ucred *cred, struct proc *proc, int signum)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(&proc->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_mls_dominate_single(subj, obj))
		return (ESRCH);
	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_socket_deliver(struct socket *so, struct label *socketlabel,
    struct mbuf *m, struct label *mbuflabel)
{
	struct mac_mls *p, *s;

	if (!mac_mls_enabled)
		return (0);

	p = SLOT(mbuflabel);
	s = SLOT(socketlabel);

	return (mac_mls_equal_single(p, s) ? 0 : EACCES);
}

static int
mac_mls_check_socket_relabel(struct ucred *cred, struct socket *socket,
    struct label *socketlabel, struct label *newlabel)
{
	struct mac_mls *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(&cred->cr_label);
	obj = SLOT(socketlabel);

	/*
	 * If there is an MLS label update for the socket, it may be
	 * an update of single.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_SINGLE);
	if (error)
		return (error);

	/*
	 * To relabel a socket, the old socket single must be in the subject
	 * range.
	 */
	if (!mac_mls_single_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_SINGLE) {
		/*
		 * To relabel a socket, the new socket single must be in
		 * the subject range.
		 */
		if (!mac_mls_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on the socket to contain EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (mac_mls_contains_equal(new)) {
			error = mac_mls_subject_equal_ok(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_mls_check_socket_visible(struct ucred *cred, struct socket *socket,
    struct label *socketlabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(socketlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (ENOENT);

	return (0);
}

static int
mac_mls_check_vnode_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_create(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct componentname *cnp, struct vattr *vap)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_delete(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct vnode *vp, struct label *label,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *label, acl_type_t type)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_exec(struct ucred *cred, struct vnode *vp,
    struct label *label)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_getacl(struct ucred *cred, struct vnode *vp,
    struct label *label, acl_type_t type)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *label, int attrnamespace, const char *name, struct uio *uio)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_link(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct vnode *vp, struct label *label,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	obj = SLOT(dlabel);
	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_mmap(struct ucred *cred, struct vnode *vp,
    struct label *label, int prot)
{
	struct mac_mls *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!mac_mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!mac_mls_dominate_single(subj, obj))
			return (EACCES);
	}
	if (prot & VM_PROT_WRITE) {
		if (!mac_mls_dominate_single(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mac_mls_check_vnode_open(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, mode_t acc_mode)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	/* XXX privilege override for admin? */
	if (acc_mode & (VREAD | VEXEC | VSTAT)) {
		if (!mac_mls_dominate_single(subj, obj))
			return (EACCES);
	}
	if (acc_mode & (VWRITE | VAPPEND | VADMIN)) {
		if (!mac_mls_dominate_single(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mac_mls_check_vnode_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *label)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(&active_cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *label)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(&active_cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, struct label *newlabel)
{
	struct mac_mls *old, *new, *subj;
	int error;

	old = SLOT(vnodelabel);
	new = SLOT(newlabel);
	subj = SLOT(&cred->cr_label);

	/*
	 * If there is an MLS label update for the vnode, it must be a
	 * single label.
	 */
	error = mls_atmostflags(new, MAC_MLS_FLAG_SINGLE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of the vnode (MLS label or not), MLS must
	 * authorize the relabel.
	 */
	if (!mac_mls_single_in_range(old, subj))
		return (EPERM);

	/*
	 * If the MLS label is to be changed, authorize as appropriate.
	 */
	if (new->mm_flags & MAC_MLS_FLAG_SINGLE) {
		/*
		 * To change the MLS label on a vnode, the new vnode label
		 * must be in the subject range.
		 */
		if (!mac_mls_single_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the MLS label on the vnode to be EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (mac_mls_contains_equal(new)) {
			error = mac_mls_subject_equal_ok(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}


static int
mac_mls_check_vnode_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct vnode *vp, struct label *label,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dlabel, struct vnode *vp, struct label *label, int samedir,
    struct componentname *cnp)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(dlabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	if (vp != NULL) {
		obj = SLOT(label);

		if (!mac_mls_dominate_single(obj, subj))
			return (EACCES);
	}

	return (0);
}

static int
mac_mls_check_vnode_revoke(struct ucred *cred, struct vnode *vp,
    struct label *label)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_setacl(struct ucred *cred, struct vnode *vp,
    struct label *label, acl_type_t type, struct acl *acl)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, int attrnamespace, const char *name,
    struct uio *uio)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	/* XXX: protect the MAC EA in a special way? */

	return (0);
}

static int
mac_mls_check_vnode_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, u_long flags)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, mode_t mode)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, uid_t uid, gid_t gid)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vnodelabel, struct timespec atime, struct timespec mtime)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vnodelabel)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled)
		return (0);

	subj = SLOT(&active_cred->cr_label);
	obj = SLOT(vnodelabel);

	if (!mac_mls_dominate_single(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_mls_check_vnode_write(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *label)
{
	struct mac_mls *subj, *obj;

	if (!mac_mls_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(&active_cred->cr_label);
	obj = SLOT(label);

	if (!mac_mls_dominate_single(obj, subj))
		return (EACCES);

	return (0);
}

static struct mac_policy_op_entry mac_mls_ops[] =
{
	{ MAC_DESTROY,
	    (macop_t)mac_mls_destroy },
	{ MAC_INIT,
	    (macop_t)mac_mls_init },
	{ MAC_INIT_BPFDESC_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_CRED_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_DEVFSDIRENT_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_IFNET_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_IPQ_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_MBUF_LABEL,
	    (macop_t)mac_mls_init_label_waitcheck },
	{ MAC_INIT_MOUNT_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_MOUNT_FS_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_PIPE_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_INIT_SOCKET_LABEL,
	    (macop_t)mac_mls_init_label_waitcheck },
	{ MAC_INIT_SOCKET_PEER_LABEL,
	    (macop_t)mac_mls_init_label_waitcheck },
	{ MAC_INIT_VNODE_LABEL,
	    (macop_t)mac_mls_init_label },
	{ MAC_DESTROY_BPFDESC_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_CRED_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_DEVFSDIRENT_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_IFNET_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_IPQ_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_MBUF_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_MOUNT_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_MOUNT_FS_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_PIPE_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_SOCKET_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_SOCKET_PEER_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_DESTROY_VNODE_LABEL,
	    (macop_t)mac_mls_destroy_label },
	{ MAC_COPY_PIPE_LABEL,
	    (macop_t)mac_mls_copy_label },
	{ MAC_COPY_VNODE_LABEL,
	    (macop_t)mac_mls_copy_label },
	{ MAC_EXTERNALIZE_CRED_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_IFNET_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_PIPE_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_SOCKET_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_SOCKET_PEER_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_VNODE_LABEL,
	    (macop_t)mac_mls_externalize_label },
	{ MAC_EXTERNALIZE_VNODE_OLDMAC,
	    (macop_t)mac_mls_externalize_vnode_oldmac },
	{ MAC_INTERNALIZE_CRED_LABEL,
	    (macop_t)mac_mls_internalize_label },
	{ MAC_INTERNALIZE_IFNET_LABEL,
	    (macop_t)mac_mls_internalize_label },
	{ MAC_INTERNALIZE_PIPE_LABEL,
	    (macop_t)mac_mls_internalize_label },
	{ MAC_INTERNALIZE_SOCKET_LABEL,
	    (macop_t)mac_mls_internalize_label },
	{ MAC_INTERNALIZE_VNODE_LABEL,
	    (macop_t)mac_mls_internalize_label },
	{ MAC_CREATE_DEVFS_DEVICE,
	    (macop_t)mac_mls_create_devfs_device },
	{ MAC_CREATE_DEVFS_DIRECTORY,
	    (macop_t)mac_mls_create_devfs_directory },
	{ MAC_CREATE_DEVFS_SYMLINK,
	    (macop_t)mac_mls_create_devfs_symlink },
	{ MAC_CREATE_DEVFS_VNODE,
	    (macop_t)mac_mls_create_devfs_vnode },
	{ MAC_CREATE_VNODE,
	    (macop_t)mac_mls_create_vnode },
	{ MAC_CREATE_MOUNT,
	    (macop_t)mac_mls_create_mount },
	{ MAC_CREATE_ROOT_MOUNT,
	    (macop_t)mac_mls_create_root_mount },
	{ MAC_RELABEL_VNODE,
	    (macop_t)mac_mls_relabel_vnode },
	{ MAC_UPDATE_DEVFSDIRENT,
	    (macop_t)mac_mls_update_devfsdirent },
	{ MAC_UPDATE_PROCFSVNODE,
	    (macop_t)mac_mls_update_procfsvnode },
	{ MAC_UPDATE_VNODE_FROM_EXTERNALIZED,
	    (macop_t)mac_mls_update_vnode_from_externalized },
	{ MAC_UPDATE_VNODE_FROM_MOUNT,
	    (macop_t)mac_mls_update_vnode_from_mount },
	{ MAC_CREATE_MBUF_FROM_SOCKET,
	    (macop_t)mac_mls_create_mbuf_from_socket },
	{ MAC_CREATE_PIPE,
	    (macop_t)mac_mls_create_pipe },
	{ MAC_CREATE_SOCKET,
	    (macop_t)mac_mls_create_socket },
	{ MAC_CREATE_SOCKET_FROM_SOCKET,
	    (macop_t)mac_mls_create_socket_from_socket },
	{ MAC_RELABEL_PIPE,
	    (macop_t)mac_mls_relabel_pipe },
	{ MAC_RELABEL_SOCKET,
	    (macop_t)mac_mls_relabel_socket },
	{ MAC_SET_SOCKET_PEER_FROM_MBUF,
	    (macop_t)mac_mls_set_socket_peer_from_mbuf },
	{ MAC_SET_SOCKET_PEER_FROM_SOCKET,
	    (macop_t)mac_mls_set_socket_peer_from_socket },
	{ MAC_CREATE_BPFDESC,
	    (macop_t)mac_mls_create_bpfdesc },
	{ MAC_CREATE_DATAGRAM_FROM_IPQ,
	    (macop_t)mac_mls_create_datagram_from_ipq },
	{ MAC_CREATE_FRAGMENT,
	    (macop_t)mac_mls_create_fragment },
	{ MAC_CREATE_IFNET,
	    (macop_t)mac_mls_create_ifnet },
	{ MAC_CREATE_IPQ,
	    (macop_t)mac_mls_create_ipq },
	{ MAC_CREATE_MBUF_FROM_MBUF,
	    (macop_t)mac_mls_create_mbuf_from_mbuf },
	{ MAC_CREATE_MBUF_LINKLAYER,
	    (macop_t)mac_mls_create_mbuf_linklayer },
	{ MAC_CREATE_MBUF_FROM_BPFDESC,
	    (macop_t)mac_mls_create_mbuf_from_bpfdesc },
	{ MAC_CREATE_MBUF_FROM_IFNET,
	    (macop_t)mac_mls_create_mbuf_from_ifnet },
	{ MAC_CREATE_MBUF_MULTICAST_ENCAP,
	    (macop_t)mac_mls_create_mbuf_multicast_encap },
	{ MAC_CREATE_MBUF_NETLAYER,
	    (macop_t)mac_mls_create_mbuf_netlayer },
	{ MAC_FRAGMENT_MATCH,
	    (macop_t)mac_mls_fragment_match },
	{ MAC_RELABEL_IFNET,
	    (macop_t)mac_mls_relabel_ifnet },
	{ MAC_UPDATE_IPQ,
	    (macop_t)mac_mls_update_ipq },
	{ MAC_CREATE_CRED,
	    (macop_t)mac_mls_create_cred },
	{ MAC_EXECVE_TRANSITION,
	    (macop_t)mac_mls_execve_transition },
	{ MAC_EXECVE_WILL_TRANSITION,
	    (macop_t)mac_mls_execve_will_transition },
	{ MAC_CREATE_PROC0,
	    (macop_t)mac_mls_create_proc0 },
	{ MAC_CREATE_PROC1,
	    (macop_t)mac_mls_create_proc1 },
	{ MAC_RELABEL_CRED,
	    (macop_t)mac_mls_relabel_cred },
	{ MAC_CHECK_BPFDESC_RECEIVE,
	    (macop_t)mac_mls_check_bpfdesc_receive },
	{ MAC_CHECK_CRED_RELABEL,
	    (macop_t)mac_mls_check_cred_relabel },
	{ MAC_CHECK_CRED_VISIBLE,
	    (macop_t)mac_mls_check_cred_visible },
	{ MAC_CHECK_IFNET_RELABEL,
	    (macop_t)mac_mls_check_ifnet_relabel },
	{ MAC_CHECK_IFNET_TRANSMIT,
	    (macop_t)mac_mls_check_ifnet_transmit },
	{ MAC_CHECK_MOUNT_STAT,
	    (macop_t)mac_mls_check_mount_stat },
	{ MAC_CHECK_PIPE_IOCTL,
	    (macop_t)mac_mls_check_pipe_ioctl },
	{ MAC_CHECK_PIPE_POLL,
	    (macop_t)mac_mls_check_pipe_poll },
	{ MAC_CHECK_PIPE_READ,
	    (macop_t)mac_mls_check_pipe_read },
	{ MAC_CHECK_PIPE_RELABEL,
	    (macop_t)mac_mls_check_pipe_relabel },
	{ MAC_CHECK_PIPE_STAT,
	    (macop_t)mac_mls_check_pipe_stat },
	{ MAC_CHECK_PIPE_WRITE,
	    (macop_t)mac_mls_check_pipe_write },
	{ MAC_CHECK_PROC_DEBUG,
	    (macop_t)mac_mls_check_proc_debug },
	{ MAC_CHECK_PROC_SCHED,
	    (macop_t)mac_mls_check_proc_sched },
	{ MAC_CHECK_PROC_SIGNAL,
	    (macop_t)mac_mls_check_proc_signal },
	{ MAC_CHECK_SOCKET_DELIVER,
	    (macop_t)mac_mls_check_socket_deliver },
	{ MAC_CHECK_SOCKET_RELABEL,
	    (macop_t)mac_mls_check_socket_relabel },
	{ MAC_CHECK_SOCKET_VISIBLE,
	    (macop_t)mac_mls_check_socket_visible },
	{ MAC_CHECK_VNODE_ACCESS,
	    (macop_t)mac_mls_check_vnode_open },
	{ MAC_CHECK_VNODE_CHDIR,
	    (macop_t)mac_mls_check_vnode_chdir },
	{ MAC_CHECK_VNODE_CHROOT,
	    (macop_t)mac_mls_check_vnode_chroot },
	{ MAC_CHECK_VNODE_CREATE,
	    (macop_t)mac_mls_check_vnode_create },
	{ MAC_CHECK_VNODE_DELETE,
	    (macop_t)mac_mls_check_vnode_delete },
	{ MAC_CHECK_VNODE_DELETEACL,
	    (macop_t)mac_mls_check_vnode_deleteacl },
	{ MAC_CHECK_VNODE_EXEC,
	    (macop_t)mac_mls_check_vnode_exec },
	{ MAC_CHECK_VNODE_GETACL,
	    (macop_t)mac_mls_check_vnode_getacl },
	{ MAC_CHECK_VNODE_GETEXTATTR,
	    (macop_t)mac_mls_check_vnode_getextattr },
	{ MAC_CHECK_VNODE_LINK,
	    (macop_t)mac_mls_check_vnode_link },
	{ MAC_CHECK_VNODE_LOOKUP,
	    (macop_t)mac_mls_check_vnode_lookup },
	{ MAC_CHECK_VNODE_MMAP,
	    (macop_t)mac_mls_check_vnode_mmap },
	{ MAC_CHECK_VNODE_MPROTECT,
	    (macop_t)mac_mls_check_vnode_mmap },
	{ MAC_CHECK_VNODE_OPEN,
	    (macop_t)mac_mls_check_vnode_open },
	{ MAC_CHECK_VNODE_POLL,
	    (macop_t)mac_mls_check_vnode_poll },
	{ MAC_CHECK_VNODE_READ,
	    (macop_t)mac_mls_check_vnode_read },
	{ MAC_CHECK_VNODE_READDIR,
	    (macop_t)mac_mls_check_vnode_readdir },
	{ MAC_CHECK_VNODE_READLINK,
	    (macop_t)mac_mls_check_vnode_readlink },
	{ MAC_CHECK_VNODE_RELABEL,
	    (macop_t)mac_mls_check_vnode_relabel },
	{ MAC_CHECK_VNODE_RENAME_FROM,
	    (macop_t)mac_mls_check_vnode_rename_from },
	{ MAC_CHECK_VNODE_RENAME_TO,
	    (macop_t)mac_mls_check_vnode_rename_to },
	{ MAC_CHECK_VNODE_REVOKE,
	    (macop_t)mac_mls_check_vnode_revoke },
	{ MAC_CHECK_VNODE_SETACL,
	    (macop_t)mac_mls_check_vnode_setacl },
	{ MAC_CHECK_VNODE_SETEXTATTR,
	    (macop_t)mac_mls_check_vnode_setextattr },
	{ MAC_CHECK_VNODE_SETFLAGS,
	    (macop_t)mac_mls_check_vnode_setflags },
	{ MAC_CHECK_VNODE_SETMODE,
	    (macop_t)mac_mls_check_vnode_setmode },
	{ MAC_CHECK_VNODE_SETOWNER,
	    (macop_t)mac_mls_check_vnode_setowner },
	{ MAC_CHECK_VNODE_SETUTIMES,
	    (macop_t)mac_mls_check_vnode_setutimes },
	{ MAC_CHECK_VNODE_STAT,
	    (macop_t)mac_mls_check_vnode_stat },
	{ MAC_CHECK_VNODE_WRITE,
	    (macop_t)mac_mls_check_vnode_write },
	{ MAC_OP_LAST, NULL }
};

MAC_POLICY_SET(mac_mls_ops, trustedbsd_mac_mls, "TrustedBSD MAC/MLS",
    MPC_LOADTIME_FLAG_NOTLATE, &mac_mls_slot);
