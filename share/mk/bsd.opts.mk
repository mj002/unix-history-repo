# $FreeBSD$
#
# Option file for src builds.
#
# Users define WITH_FOO and WITHOUT_FOO on the command line or in /etc/src.conf
# and /etc/make.conf files. These translate in the build system to MK_FOO={yes,no}
# with sensible (usually) defaults.
#
# Makefiles must include bsd.opts.mk after defining specific MK_FOO options that
# are applicable for that Makefile (typically there are none, but sometimes there
# are exceptions). Recursive makes usually add MK_FOO=no for options that they wish
# to omit from that make.
#
# Makefiles must include bsd.srcpot.mk before they test the value of any MK_FOO
# variable.
#
# Makefiles may also assume that this file is included by bsd.own.mk should it
# need variables defined there prior to the end of the Makefile where
# bsd.{subdir,lib.bin}.mk is traditionally included.
#
# The old-style YES_FOO and NO_FOO are being phased out. No new instances of them
# should be added. Old instances should be removed since they were just to
# bridge the gap between FreeBSD 4 and FreeBSD 5.
#
# Makefiles should never test WITH_FOO or WITHOUT_FOO directly (although an
# exception is made for _WITHOUT_SRCONF which turns off this mechanism
# completely).
#

.if !target(__<bsd.opts.mk>__)
__<bsd.opts.mk>__:

.if !defined(_WITHOUT_SRCCONF)
#
# Define MK_* variables (which are either "yes" or "no") for users
# to set via WITH_*/WITHOUT_* in /etc/src.conf and override in the
# make(1) environment.
# These should be tested with `== "no"' or `!= "no"' in makefiles.
# The NO_* variables should only be set by makefiles for variables
# that haven't been converted over.
#

# Only these options are used by bsd.*.mk. Most seem legit, except maybe
# OPENSSH.

__DEFAULT_YES_OPTIONS = \
    ASSERT_DEBUG \
    INFO \
    INSTALLLIB \
    KERBEROS \
    MAN \
    MANCOMPRESS \
    NIS \
    NLS \
    OPENSSH \
    PROFILE \
    SSP \
    SYMVER \
    TOOLCHAIN

__DEFAULT_NO_OPTIONS = \
    CTF \
    DEBUG_FILES \
    INSTALL_AS_USER


# meta mode related
__DEFAULT_NO_OPTIONS += \
    AUTO_OBJ \
    META_MODE \
    STAGING \
    STAGING_PROG
    
.include <bsd.mkopt.mk>

#
# Supported NO_* options (if defined, MK_* will be forced to "no",
# regardless of user's setting).
#
# These are transitional and will disappaer in the FreeBSD 12.
#
.for var in \
    CTF \
    DEBUG_FILES \
    INSTALLLIB \
    MAN \
    PROFILE
.if defined(NO_${var})
# This warning may be premature...
#.warning "NO_${var} is defined, but deprecated. Please use MK_${var}=no instead."
MK_${var}:=no
.endif
.endfor

.endif # !_WITHOUT_SRCCONF

.endif
