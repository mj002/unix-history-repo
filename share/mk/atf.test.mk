# $FreeBSD$
#
# You must include bsd.test.mk instead of this file from your Makefile.
#
# Logic to build and install ATF test programs; i.e. test programs linked
# against the ATF libraries.

.if !target(__<bsd.test.mk>__)
.error atf.test.mk cannot be included directly.
.endif

# List of C, C++ and shell test programs to build.
#
# Programs listed here are built using PROGS, PROGS_CXX and SCRIPTS,
# respectively, from bsd.prog.mk.  However, the build rules are tweaked to
# require the ATF libraries.
#
# Test programs registered in this manner are set to be installed into TESTSDIR
# (which should be overriden by the Makefile) and are not required to provide a
# manpage.
ATF_TESTS_C?=
ATF_TESTS_CXX?=
ATF_TESTS_SH?=

# Whether to allow using the deprecated ATF tools or not.
#
# If 'yes', this file will generate Atffiles when requested and will also
# support using the deprecated atf-run tool to execute the tests.
ALLOW_DEPRECATED_ATF_TOOLS?= no

# Knob to control the handling of the Atffile for this Makefile.
#
# If 'yes', an Atffile exists in the source tree and is installed into
# TESTSDIR.
#
# If 'auto', an Atffile is automatically generated based on the list of test
# programs built by the Makefile and is installed into TESTSDIR.  This is the
# default and is sufficient in the majority of the cases.
#
# If 'no', no Atffile is installed.
ATFFILE?= auto

# Path to the prefix of the installed ATF tools, if any.
#
# If atf-run and atf-report are installed from ports, we automatically define a
# realtest target below to run the tests using these tools.  The tools are
# searched for in the hierarchy specified by this variable.
ATF_PREFIX?= /usr/local

# C compiler passed to ATF tests that need to build code.
ATF_BUILD_CC?= ${DESTDIR}/usr/bin/cc
TESTS_ENV+= ATF_BUILD_CC=${ATF_BUILD_CC}

# C preprocessor passed to ATF tests that need to build code.
ATF_BUILD_CPP?= ${DESTDIR}/usr/bin/cpp
TESTS_ENV+= ATF_BUILD_CPP=${ATF_BUILD_CPP}

# C++ compiler passed to ATF tests that need to build code.
ATF_BUILD_CXX?= ${DESTDIR}/usr/bin/c++
TESTS_ENV+= ATF_BUILD_CXX=${ATF_BUILD_CXX}

# Shell interpreter used to run atf-sh(1) based tests.
ATF_SHELL?= ${DESTDIR}/bin/sh
TESTS_ENV+= ATF_SHELL=${ATF_SHELL}

.if !empty(ATF_TESTS_C)
PROGS+= ${ATF_TESTS_C}
_TESTS+= ${ATF_TESTS_C}
.for _T in ${ATF_TESTS_C}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}.c
DPADD.${_T}+= ${LIBATF_C}
LDADD.${_T}+= -latf-c
USEPRIVATELIB+= atf-c
TEST_INTERFACE.${_T}= atf
.endfor
.endif

.if !empty(ATF_TESTS_CXX)
PROGS_CXX+= ${ATF_TESTS_CXX}
_TESTS+= ${ATF_TESTS_CXX}
.for _T in ${ATF_TESTS_CXX}
BINDIR.${_T}= ${TESTSDIR}
MAN.${_T}?= # empty
SRCS.${_T}?= ${_T}${CXX_SUFFIX:U.cc}
DPADD.${_T}+= ${LIBATF_CXX} ${LIBATF_C}
LDADD.${_T}+= -latf-c++ -latf-c
USEPRIVATELIB+= atf-c++
TEST_INTERFACE.${_T}= atf
.endfor
.endif

.if !empty(ATF_TESTS_SH)
SCRIPTS+= ${ATF_TESTS_SH}
_TESTS+= ${ATF_TESTS_SH}
.for _T in ${ATF_TESTS_SH}
SCRIPTSDIR_${_T}= ${TESTSDIR}
TEST_INTERFACE.${_T}= atf
CLEANFILES+= ${_T} ${_T}.tmp
ATF_TESTS_SH_SRC_${_T}?= ${_T}.sh
${_T}: ${ATF_TESTS_SH_SRC_${_T}}
	echo '#! /usr/bin/atf-sh' > ${.TARGET}.tmp
	cat ${.ALLSRC} >> ${.TARGET}.tmp
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
.endif

.if ${ALLOW_DEPRECATED_ATF_TOOLS} != "no"

.if ${ATFFILE:tl} != "no"
FILES+=	Atffile
FILESDIR_Atffile= ${TESTSDIR}

.if ${ATFFILE:tl} == "auto"
CLEANFILES+= Atffile Atffile.tmp

Atffile: Makefile
	@{ echo 'Content-Type: application/X-atf-atffile; version="1"'; \
	echo; \
	echo '# Automatically generated by atf-test.mk.'; \
	echo; \
	echo 'prop: test-suite = "'${TESTSUITE}'"'; \
	echo; \
	for tp in ${ATF_TESTS_C} ${ATF_TESTS_CXX} ${ATF_TESTS_SH} \
	    ${TESTS_SUBDIRS}; \
	do \
	    echo "tp: $${tp}"; \
	done; } >Atffile.tmp
	@mv Atffile.tmp Atffile
.endif
.endif

ATF_REPORT?= ${ATF_PREFIX}/bin/atf-report
ATF_RUN?= ${ATF_PREFIX}/bin/atf-run
.if exists(${ATF_RUN}) && exists(${ATF_REPORT})
# Definition of the "make test" target and supporting variables.
#
# This target, by necessity, can only work for native builds (i.e. a freeBSD
# host building a release for the same system).  The target runs ATF, which is
# not in the toolchain, and the tests execute code built for the target host.
#
# Due to the dependencies of the binaries built by the source tree and how they
# are used by tests, it is highly possible for a execution of "make test" to
# report bogus results unless the new binaries are put in place.
_TESTS_FIFO= ${.OBJDIR}/atf-run.fifo
_TESTS_LOG= ${.OBJDIR}/atf-run.log
CLEANFILES+= ${_TESTS_FIFO} ${_TESTS_LOG}
realtest: .PHONY
	@set -e; \
	if [ -z "${TESTSDIR}" ]; then \
	    echo "*** No TESTSDIR defined; nothing to do."; \
	    exit 0; \
	fi; \
	cd ${DESTDIR}${TESTSDIR}; \
	rm -f ${_TESTS_FIFO}; \
	mkfifo ${_TESTS_FIFO}; \
	tee ${_TESTS_LOG} < ${_TESTS_FIFO} | ${TESTS_ENV} ${ATF_REPORT} & \
	set +e; \
	${TESTS_ENV} ${ATF_RUN} >> ${_TESTS_FIFO}; \
	result=$${?}; \
	wait; \
	rm -f ${_TESTS_FIFO}; \
	echo; \
	echo "*** The verbatim output of atf-run has been saved to ${_TESTS_LOG}"; \
	echo "***"; \
	echo "*** WARNING: atf-run is deprecated; please install kyua instead"; \
	exit $${result}
.endif

.endif
