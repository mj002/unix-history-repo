/*-
 * Copyright (c) 1999-2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */
/*
 * Developed by the TrustedBSD Project.
 *
 * ACL support routines specific to POSIX.1e access control lists.  These are
 * utility routines for code common across file systems implementing POSIX.1e
 * ACLs.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/acl.h>

/*
 * Implement a version of vaccess() that understands POSIX.1e ACL semantics;
 * the access ACL has already been prepared for evaluation by the file
 * system and is passed via 'uid', 'gid', and 'acl'.  Return 0 on success,
 * else an errno value.
 */
int
vaccess_acl_posix1e(enum vtype type, uid_t file_uid, gid_t file_gid,
    struct acl *acl, mode_t acc_mode, struct ucred *cred, int *privused)
{
	struct acl_entry *acl_other, *acl_mask;
	mode_t dac_granted;
	mode_t cap_granted;
	mode_t acl_mask_granted;
	int group_matched, i;

	/*
	 * Look for a normal, non-privileged way to access the file/directory
	 * as requested.  If it exists, go with that.  Otherwise, attempt to
	 * use privileges granted via cap_granted.  In some cases, which
	 * privileges to use may be ambiguous due to "best match", in which
	 * case fall back on first match for the time being.
	 */
	if (privused != NULL)
		*privused = 0;

	/*
	 * Determine privileges now, but don't apply until we've found a DAC
	 * entry that matches but has failed to allow access.  POSIX.1e
	 * capabilities are not implemented, but we document how they would
	 * behave here if implemented.
	 */
#ifndef CAPABILITIES
	if (suser_cred(cred, SUSER_ALLOWJAIL) == 0)
		cap_granted = VALLPERM;
	else
		cap_granted = 0;
#else
	cap_granted = 0;

	if (type == VDIR) {
		if ((acc_mode & VEXEC) && !cap_check(cred, NULL,
		     CAP_DAC_READ_SEARCH, SUSER_ALLOWJAIL))
			cap_granted |= VEXEC;
	} else {
		if ((acc_mode & VEXEC) && !cap_check(cred, NULL,
		    CAP_DAC_EXECUTE, SUSER_ALLOWJAIL))
			cap_granted |= VEXEC;
	}

	if ((acc_mode & VREAD) && !cap_check(cred, NULL, CAP_DAC_READ_SEARCH,
	    SUSER_ALLOWJAIL))
		cap_granted |= VREAD;

	if (((acc_mode & VWRITE) || (acc_mode & VAPPEND)) &&
	    !cap_check(cred, NULL, CAP_DAC_WRITE, SUSER_ALLOWJAIL))
		cap_granted |= (VWRITE | VAPPEND);

	if ((acc_mode & VADMIN) && !cap_check(cred, NULL, CAP_FOWNER,
	    SUSER_ALLOWJAIL))
		cap_granted |= VADMIN;
#endif /* CAPABILITIES */

	/*
	 * The owner matches if the effective uid associated with the
	 * credential matches that of the ACL_USER_OBJ entry.  While we're
	 * doing the first scan, also cache the location of the ACL_MASK and
	 * ACL_OTHER entries, preventing some future iterations.
	 */
	acl_mask = acl_other = NULL;
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			if (file_uid != cred->cr_uid)
				break;
			dac_granted = 0;
			dac_granted |= VADMIN;
			if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
				dac_granted |= VEXEC;
			if (acl->acl_entry[i].ae_perm & ACL_READ)
				dac_granted |= VREAD;
			if (acl->acl_entry[i].ae_perm & ACL_WRITE)
				dac_granted |= (VWRITE | VAPPEND);
			if ((acc_mode & dac_granted) == acc_mode)
				return (0);
			if ((acc_mode & (dac_granted | cap_granted)) ==
			    acc_mode) {
				if (privused != NULL)
					*privused = 1;
				return (0);
			}
			goto error;

		case ACL_MASK:
			acl_mask = &acl->acl_entry[i];
			break;

		case ACL_OTHER:
			acl_other = &acl->acl_entry[i];
			break;

		default:
			break;
		}
	}

	/*
	 * An ACL_OTHER entry should always exist in a valid access ACL.  If
	 * it doesn't, then generate a serious failure.  For now, this means
	 * a debugging message and EPERM, but in the future should probably
	 * be a panic.
	 */
	if (acl_other == NULL) {
		/*
		 * XXX This should never happen
		 */
		printf("vaccess_acl_posix1e: ACL_OTHER missing\n");
		return (EPERM);
	}

	/*
	 * Checks against ACL_USER, ACL_GROUP_OBJ, and ACL_GROUP fields are
	 * masked by an ACL_MASK entry, if any.  As such, first identify the
	 * ACL_MASK field, then iterate through identifying potential user
	 * matches, then group matches.  If there is no ACL_MASK, assume that
	 * the mask allows all requests to succeed.
	 */
	if (acl_mask != NULL) {
		acl_mask_granted = 0;
		if (acl_mask->ae_perm & ACL_EXECUTE)
			acl_mask_granted |= VEXEC;
		if (acl_mask->ae_perm & ACL_READ)
			acl_mask_granted |= VREAD;
		if (acl_mask->ae_perm & ACL_WRITE)
			acl_mask_granted |= (VWRITE | VAPPEND);
	} else
		acl_mask_granted = VEXEC | VREAD | VWRITE | VAPPEND;

	/*
	 * Iterate through user ACL entries.  Do checks twice, first without
	 * privilege, and then if a match is found but failed, a second time
	 * with privilege.
	 */

	/*
	 * Check ACL_USER ACL entries.
	 */
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER:
			if (acl->acl_entry[i].ae_id != cred->cr_uid)
				break;
			dac_granted = 0;
			if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
				dac_granted |= VEXEC;
			if (acl->acl_entry[i].ae_perm & ACL_READ)
				dac_granted |= VREAD;
			if (acl->acl_entry[i].ae_perm & ACL_WRITE)
				dac_granted |= (VWRITE | VAPPEND);
			dac_granted &= acl_mask_granted;
			if ((acc_mode & dac_granted) == acc_mode)
				return (0);
			if ((acc_mode & (dac_granted | cap_granted)) !=
			    acc_mode)
				goto error;

			if (privused != NULL)
				*privused = 1;
			return (0);
		}
	}

	/*
	 * Group match is best-match, not first-match, so find a "best"
	 * match.  Iterate across, testing each potential group match.  Make
	 * sure we keep track of whether we found a match or not, so that we
	 * know if we should try again with any available privilege, or if we
	 * should move on to ACL_OTHER.
	 */
	group_matched = 0;
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_GROUP_OBJ:
			if (!groupmember(file_gid, cred))
				break;
			dac_granted = 0;
			if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
				dac_granted |= VEXEC;
			if (acl->acl_entry[i].ae_perm & ACL_READ)
				dac_granted |= VREAD;
			if (acl->acl_entry[i].ae_perm & ACL_WRITE)
				dac_granted |= (VWRITE | VAPPEND);
			dac_granted  &= acl_mask_granted;

			if ((acc_mode & dac_granted) == acc_mode)
				return (0);

			group_matched = 1;
			break;

		case ACL_GROUP:
			if (!groupmember(acl->acl_entry[i].ae_id, cred))
				break;
			dac_granted = 0;
			if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
				dac_granted |= VEXEC;
			if (acl->acl_entry[i].ae_perm & ACL_READ)
				dac_granted |= VREAD;
			if (acl->acl_entry[i].ae_perm & ACL_WRITE)
				dac_granted |= (VWRITE | VAPPEND);
			dac_granted  &= acl_mask_granted;

			if ((acc_mode & dac_granted) == acc_mode)
				return (0);

			group_matched = 1;
			break;

		default:
			break;
		}
	}

	if (group_matched == 1) {
		/*
		 * There was a match, but it did not grant rights via pure
		 * DAC.  Try again, this time with privilege.
		 */
		for (i = 0; i < acl->acl_cnt; i++) {
			switch (acl->acl_entry[i].ae_tag) {
			case ACL_GROUP_OBJ:
				if (!groupmember(file_gid, cred))
					break;
				dac_granted = 0;
				if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
					dac_granted |= VEXEC;
				if (acl->acl_entry[i].ae_perm & ACL_READ)
					dac_granted |= VREAD;
				if (acl->acl_entry[i].ae_perm & ACL_WRITE)
					dac_granted |= (VWRITE | VAPPEND);
				dac_granted &= acl_mask_granted;

				if ((acc_mode & (dac_granted | cap_granted)) !=
				    acc_mode)
					break;

				if (privused != NULL)
					*privused = 1;
				return (0);

			case ACL_GROUP:
				if (!groupmember(acl->acl_entry[i].ae_id,
				    cred))
					break;
				dac_granted = 0;
				if (acl->acl_entry[i].ae_perm & ACL_EXECUTE)
				dac_granted |= VEXEC;
				if (acl->acl_entry[i].ae_perm & ACL_READ)
					dac_granted |= VREAD;
				if (acl->acl_entry[i].ae_perm & ACL_WRITE)
					dac_granted |= (VWRITE | VAPPEND);
				dac_granted &= acl_mask_granted;

				if ((acc_mode & (dac_granted | cap_granted)) !=
				    acc_mode)
					break;

				if (privused != NULL)
					*privused = 1;
				return (0);

			default:
				break;
			}
		}
		/*
		 * Even with privilege, group membership was not sufficient.
		 * Return failure.
		 */
		goto error;
	}
		
	/*
	 * Fall back on ACL_OTHER.  ACL_MASK is not applied to ACL_OTHER.
	 */
	dac_granted = 0;
	if (acl_other->ae_perm & ACL_EXECUTE)
		dac_granted |= VEXEC;
	if (acl_other->ae_perm & ACL_READ)
		dac_granted |= VREAD;
	if (acl_other->ae_perm & ACL_WRITE)
		dac_granted |= (VWRITE | VAPPEND);

	if ((acc_mode & dac_granted) == acc_mode)
		return (0);
	if ((acc_mode & (dac_granted | cap_granted)) == acc_mode) {
		if (privused != NULL)
			*privused = 1;
		return (0);
	}

error:
	return ((acc_mode & VADMIN) ? EPERM : EACCES);
}

/*
 * For the purposes of filesystems maintaining the _OBJ entries in an inode
 * with a mode_t field, this routine converts a mode_t entry to an
 * acl_perm_t.
 */
acl_perm_t
acl_posix1e_mode_to_perm(acl_tag_t tag, mode_t mode)
{
	acl_perm_t	perm = 0;

	switch(tag) {
	case ACL_USER_OBJ:
		if (mode & S_IXUSR)
			perm |= ACL_EXECUTE;
		if (mode & S_IRUSR)
			perm |= ACL_READ;
		if (mode & S_IWUSR)
			perm |= ACL_WRITE;
		return (perm);

	case ACL_GROUP_OBJ:
		if (mode & S_IXGRP)
			perm |= ACL_EXECUTE;
		if (mode & S_IRGRP)
			perm |= ACL_READ;
		if (mode & S_IWGRP)
			perm |= ACL_WRITE;
		return (perm);

	case ACL_OTHER:
		if (mode & S_IXOTH)
			perm |= ACL_EXECUTE;
		if (mode & S_IROTH)
			perm |= ACL_READ;
		if (mode & S_IWOTH)
			perm |= ACL_WRITE;
		return (perm);

	default:
		printf("acl_posix1e_mode_to_perm: invalid tag (%d)\n", tag);
		return (0);
	}
}

/*
 * Given inode information (uid, gid, mode), return an acl entry of the
 * appropriate type.
 */
struct acl_entry
acl_posix1e_mode_to_entry(acl_tag_t tag, uid_t uid, gid_t gid, mode_t mode)
{
	struct acl_entry	acl_entry;

	acl_entry.ae_tag = tag;
	acl_entry.ae_perm = acl_posix1e_mode_to_perm(tag, mode);
	switch(tag) {
	case ACL_USER_OBJ:
		acl_entry.ae_id = uid;
		break;

	case ACL_GROUP_OBJ:
		acl_entry.ae_id = gid;
		break;

	case ACL_OTHER:
		acl_entry.ae_id = ACL_UNDEFINED_ID;
		break;

	default:
		acl_entry.ae_id = ACL_UNDEFINED_ID;
		printf("acl_posix1e_mode_to_entry: invalid tag (%d)\n", tag);
	}

	return (acl_entry);
}

/*
 * Utility function to generate a file mode given appropriate ACL entries.
 */
mode_t
acl_posix1e_perms_to_mode(struct acl_entry *acl_user_obj_entry,
    struct acl_entry *acl_group_obj_entry, struct acl_entry *acl_other_entry)
{
	mode_t	mode;

	mode = 0;
	if (acl_user_obj_entry->ae_perm & ACL_EXECUTE)
		mode |= S_IXUSR;
	if (acl_user_obj_entry->ae_perm & ACL_READ)
		mode |= S_IRUSR;
	if (acl_user_obj_entry->ae_perm & ACL_WRITE)
		mode |= S_IWUSR;
	if (acl_group_obj_entry->ae_perm & ACL_EXECUTE)
		mode |= S_IXGRP;
	if (acl_group_obj_entry->ae_perm & ACL_READ)
		mode |= S_IRGRP;
	if (acl_group_obj_entry->ae_perm & ACL_WRITE)
		mode |= S_IWGRP;
	if (acl_other_entry->ae_perm & ACL_EXECUTE)
		mode |= S_IXOTH;
	if (acl_other_entry->ae_perm & ACL_READ)
		mode |= S_IROTH;
	if (acl_other_entry->ae_perm & ACL_WRITE)
		mode |= S_IWOTH;

	return (mode);
}

/*
 * Utility function to generate a file mode given a complete POSIX.1e access
 * ACL.  Note that if the ACL is improperly formed, this may result in a
 * panic.
 */
mode_t
acl_posix1e_acl_to_mode(struct acl *acl)
{
	struct acl_entry *acl_mask, *acl_user_obj, *acl_group_obj, *acl_other;
	int i;

	/*
	 * Find the ACL entries relevant to a POSIX permission mode.
	 */
	acl_user_obj = acl_group_obj = acl_other = acl_mask = NULL;
	for (i = 0; i < acl->acl_cnt; i++) {
		switch (acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			acl_user_obj = &acl->acl_entry[i];
			break;

		case ACL_GROUP_OBJ:
			acl_group_obj = &acl->acl_entry[i];
			break;

		case ACL_OTHER:
			acl_other = &acl->acl_entry[i];
			break;

		case ACL_MASK:
			acl_mask = &acl->acl_entry[i];
			break;

		case ACL_USER:
		case ACL_GROUP:
			break;

		default:
			panic("acl_posix1e_acl_to_mode: bad ae_tag");
		}
	}

	if (acl_user_obj == NULL || acl_group_obj == NULL || acl_other == NULL)
		panic("acl_posix1e_acl_to_mode: missing base ae_tags");

	/*
	 * POSIX.1e specifies that if there is an ACL_MASK entry, we replace
	 * the mode "group" bits with its permissions.  If there isn't, we
	 * use the ACL_GROUP_OBJ permissions.
	 */
	if (acl_mask != NULL)
		return (acl_posix1e_perms_to_mode(acl_user_obj, acl_mask,
		    acl_other));
	else
		return (acl_posix1e_perms_to_mode(acl_user_obj, acl_group_obj,
		    acl_other));
}

/*
 * Perform a syntactic check of the ACL, sufficient to allow an implementing
 * filesystem to determine if it should accept this and rely on the POSIX.1e
 * ACL properties.
 */
int
acl_posix1e_check(struct acl *acl)
{
	int num_acl_user_obj, num_acl_user, num_acl_group_obj, num_acl_group;
	int num_acl_mask, num_acl_other, i;

	/*
	 * Verify that the number of entries does not exceed the maximum
	 * defined for acl_t.
	 *
	 * Verify that the correct number of various sorts of ae_tags are
	 * present:
	 *   Exactly one ACL_USER_OBJ
	 *   Exactly one ACL_GROUP_OBJ
	 *   Exactly one ACL_OTHER
	 *   If any ACL_USER or ACL_GROUP entries appear, then exactly one
	 *   ACL_MASK entry must also appear.
	 *
	 * Verify that all ae_perm entries are in ACL_PERM_BITS.
	 *
	 * Verify all ae_tag entries are understood by this implementation.
	 *
	 * Note: Does not check for uniqueness of qualifier (ae_id) field.
	 */
	num_acl_user_obj = num_acl_user = num_acl_group_obj = num_acl_group =
	    num_acl_mask = num_acl_other = 0;
	if (acl->acl_cnt > ACL_MAX_ENTRIES || acl->acl_cnt < 0)
		return (EINVAL);
	for (i = 0; i < acl->acl_cnt; i++) {
		/*
		 * Check for a valid tag.
		 */
		switch(acl->acl_entry[i].ae_tag) {
		case ACL_USER_OBJ:
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID; /* XXX */
			if (acl->acl_entry[i].ae_id != ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_user_obj++;
			break;
		case ACL_GROUP_OBJ:
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID; /* XXX */
			if (acl->acl_entry[i].ae_id != ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_group_obj++;
			break;
		case ACL_USER:
			if (acl->acl_entry[i].ae_id == ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_user++;
			break;
		case ACL_GROUP:
			if (acl->acl_entry[i].ae_id == ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_group++;
			break;
		case ACL_OTHER:
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID; /* XXX */
			if (acl->acl_entry[i].ae_id != ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_other++;
			break;
		case ACL_MASK:
			acl->acl_entry[i].ae_id = ACL_UNDEFINED_ID; /* XXX */
			if (acl->acl_entry[i].ae_id != ACL_UNDEFINED_ID)
				return (EINVAL);
			num_acl_mask++;
			break;
		default:
			return (EINVAL);
		}
		/*
		 * Check for valid perm entries.
		 */
		if ((acl->acl_entry[i].ae_perm | ACL_PERM_BITS) !=
		    ACL_PERM_BITS)
			return (EINVAL);
	}
	if ((num_acl_user_obj != 1) || (num_acl_group_obj != 1) ||
	    (num_acl_other != 1) || (num_acl_mask != 0 && num_acl_mask != 1))
		return (EINVAL);
	if (((num_acl_group != 0) || (num_acl_user != 0)) &&
	    (num_acl_mask != 1))
		return (EINVAL);
	return (0);
}

/*
 * Given a requested mode for a new object, and a default ACL, combine the
 * two to produce a new mode.  Be careful not to clear any bits that aren't
 * intended to be affected by the POSIX.1e ACL.  Eventually, this might also
 * take the cmask as an argument, if we push that down into
 * per-filesystem-code.
 */
mode_t
acl_posix1e_newfilemode(mode_t cmode, struct acl *dacl)
{
	mode_t mode;

	mode = cmode;
	/*
	 * The current composition policy is that a permission bit must be
	 * set in *both* the ACL and the requested creation mode for it to
	 * appear in the resulting mode/ACL.  First clear any possibly
	 * effected bits, then reconstruct.
	 */
	mode &= ACL_PRESERVE_MASK;
	mode |= (ACL_OVERRIDE_MASK & cmode & acl_posix1e_acl_to_mode(dacl));

	return (mode);
}
