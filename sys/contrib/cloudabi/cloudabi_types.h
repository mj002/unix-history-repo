// Copyright (c) 2016 Nuxi (https://nuxi.nl/) and contributors.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
// This file is automatically generated. Do not edit.
//
// Source: https://github.com/NuxiNL/cloudabi

#ifndef CLOUDABI_TYPES_H
#define CLOUDABI_TYPES_H

#include "cloudabi_types_common.h"

typedef struct {
	_Alignas(4) cloudabi_auxtype_t a_type;
	union {
		size_t a_val;
		void *a_ptr;
	};
} cloudabi_auxv_t;
_Static_assert(offsetof(cloudabi_auxv_t, a_type) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_auxv_t, a_val) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_auxv_t, a_val) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_auxv_t, a_ptr) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_auxv_t, a_ptr) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_auxv_t) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_auxv_t) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_auxv_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_auxv_t) == 8, "Incorrect layout");

typedef struct {
	const void *iov_base;
	size_t iov_len;
} cloudabi_ciovec_t;
_Static_assert(offsetof(cloudabi_ciovec_t, iov_base) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_ciovec_t, iov_len) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_ciovec_t, iov_len) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_ciovec_t) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_ciovec_t) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_ciovec_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_ciovec_t) == 8, "Incorrect layout");

typedef struct {
	_Alignas(8) cloudabi_userdata_t userdata;
	_Alignas(2) cloudabi_errno_t error;
	_Alignas(1) cloudabi_eventtype_t type;
	union {
		struct {
			_Alignas(8) cloudabi_userdata_t identifier;
		} clock;
		struct {
			_Atomic(cloudabi_condvar_t) *condvar;
		} condvar;
		struct {
			_Alignas(8) cloudabi_filesize_t nbytes;
			_Alignas(4) cloudabi_fd_t fd;
			_Alignas(2) cloudabi_eventrwflags_t flags;
		} fd_readwrite;
		struct {
			_Atomic(cloudabi_lock_t) *lock;
		} lock;
		struct {
			_Alignas(4) cloudabi_fd_t fd;
			_Alignas(1) cloudabi_signal_t signal;
			_Alignas(4) cloudabi_exitcode_t exitcode;
		} proc_terminate;
	};
} cloudabi_event_t;
_Static_assert(offsetof(cloudabi_event_t, userdata) == 0, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, error) == 8, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, type) == 10, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, clock.identifier) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, condvar.condvar) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, fd_readwrite.nbytes) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, fd_readwrite.fd) == 24, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, fd_readwrite.flags) == 28, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, lock.lock) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, proc_terminate.fd) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, proc_terminate.signal) == 20, "Incorrect layout");
_Static_assert(offsetof(cloudabi_event_t, proc_terminate.exitcode) == 24, "Incorrect layout");
_Static_assert(sizeof(cloudabi_event_t) == 32, "Incorrect layout");
_Static_assert(_Alignof(cloudabi_event_t) == 8, "Incorrect layout");

typedef struct {
	void *iov_base;
	size_t iov_len;
} cloudabi_iovec_t;
_Static_assert(offsetof(cloudabi_iovec_t, iov_base) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_iovec_t, iov_len) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_iovec_t, iov_len) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_iovec_t) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_iovec_t) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_iovec_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_iovec_t) == 8, "Incorrect layout");

typedef void cloudabi_processentry_t(const cloudabi_auxv_t *auxv);

typedef struct {
	const cloudabi_iovec_t *ri_data;
	size_t ri_datalen;
	cloudabi_fd_t *ri_fds;
	size_t ri_fdslen;
	_Alignas(2) cloudabi_msgflags_t ri_flags;
} cloudabi_recv_in_t;
_Static_assert(offsetof(cloudabi_recv_in_t, ri_data) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_in_t, ri_datalen) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_in_t, ri_datalen) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_in_t, ri_fds) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_in_t, ri_fds) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_in_t, ri_fdslen) == 12, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_in_t, ri_fdslen) == 24, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_in_t, ri_flags) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_in_t, ri_flags) == 32, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_recv_in_t) == 20, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_recv_in_t) == 40, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_recv_in_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_recv_in_t) == 8, "Incorrect layout");

typedef struct {
	const cloudabi_ciovec_t *si_data;
	size_t si_datalen;
	const cloudabi_fd_t *si_fds;
	size_t si_fdslen;
	_Alignas(2) cloudabi_msgflags_t si_flags;
} cloudabi_send_in_t;
_Static_assert(offsetof(cloudabi_send_in_t, si_data) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_send_in_t, si_datalen) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_send_in_t, si_datalen) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_send_in_t, si_fds) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_send_in_t, si_fds) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_send_in_t, si_fdslen) == 12, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_send_in_t, si_fdslen) == 24, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_send_in_t, si_flags) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_send_in_t, si_flags) == 32, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_send_in_t) == 20, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_send_in_t) == 40, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_send_in_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_send_in_t) == 8, "Incorrect layout");

typedef struct {
	size_t so_datalen;
} cloudabi_send_out_t;
_Static_assert(offsetof(cloudabi_send_out_t, so_datalen) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_send_out_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_send_out_t) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_send_out_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_send_out_t) == 8, "Incorrect layout");

typedef struct {
	_Alignas(8) cloudabi_userdata_t userdata;
	_Alignas(2) cloudabi_subflags_t flags;
	_Alignas(1) cloudabi_eventtype_t type;
	union {
		struct {
			_Alignas(8) cloudabi_userdata_t identifier;
			_Alignas(4) cloudabi_clockid_t clock_id;
			_Alignas(8) cloudabi_timestamp_t timeout;
			_Alignas(8) cloudabi_timestamp_t precision;
			_Alignas(2) cloudabi_subclockflags_t flags;
		} clock;
		struct {
			_Atomic(cloudabi_condvar_t) *condvar;
			_Atomic(cloudabi_lock_t) *lock;
			_Alignas(1) cloudabi_scope_t condvar_scope;
			_Alignas(1) cloudabi_scope_t lock_scope;
		} condvar;
		struct {
			_Alignas(4) cloudabi_fd_t fd;
			_Alignas(2) cloudabi_subrwflags_t flags;
		} fd_readwrite;
		struct {
			_Atomic(cloudabi_lock_t) *lock;
			_Alignas(1) cloudabi_scope_t lock_scope;
		} lock;
		struct {
			_Alignas(4) cloudabi_fd_t fd;
		} proc_terminate;
	};
} cloudabi_subscription_t;
_Static_assert(offsetof(cloudabi_subscription_t, userdata) == 0, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, flags) == 8, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, type) == 10, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, clock.identifier) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, clock.clock_id) == 24, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, clock.timeout) == 32, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, clock.precision) == 40, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, clock.flags) == 48, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, condvar.condvar) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_subscription_t, condvar.lock) == 20, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_subscription_t, condvar.lock) == 24, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_subscription_t, condvar.condvar_scope) == 24, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_subscription_t, condvar.condvar_scope) == 32, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_subscription_t, condvar.lock_scope) == 25, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_subscription_t, condvar.lock_scope) == 33, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, fd_readwrite.fd) == 16, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, fd_readwrite.flags) == 20, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, lock.lock) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_subscription_t, lock.lock_scope) == 20, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_subscription_t, lock.lock_scope) == 24, "Incorrect layout");
_Static_assert(offsetof(cloudabi_subscription_t, proc_terminate.fd) == 16, "Incorrect layout");
_Static_assert(sizeof(cloudabi_subscription_t) == 56, "Incorrect layout");
_Static_assert(_Alignof(cloudabi_subscription_t) == 8, "Incorrect layout");

typedef struct {
	void *parent;
} cloudabi_tcb_t;
_Static_assert(offsetof(cloudabi_tcb_t, parent) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_tcb_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_tcb_t) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_tcb_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_tcb_t) == 8, "Incorrect layout");

typedef void cloudabi_threadentry_t(cloudabi_tid_t tid, void *aux);

typedef struct {
	size_t ro_datalen;
	size_t ro_fdslen;
	_Alignas(2) cloudabi_sockaddr_t ro_sockname;
	_Alignas(2) cloudabi_sockaddr_t ro_peername;
	_Alignas(2) cloudabi_msgflags_t ro_flags;
} cloudabi_recv_out_t;
_Static_assert(offsetof(cloudabi_recv_out_t, ro_datalen) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_out_t, ro_fdslen) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_out_t, ro_fdslen) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_out_t, ro_sockname) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_out_t, ro_sockname) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_out_t, ro_peername) == 28, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_out_t, ro_peername) == 36, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_recv_out_t, ro_flags) == 48, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_recv_out_t, ro_flags) == 56, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_recv_out_t) == 52, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_recv_out_t) == 64, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_recv_out_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_recv_out_t) == 8, "Incorrect layout");

typedef struct {
	cloudabi_threadentry_t *entry_point;
	void *stack;
	size_t stack_size;
	void *argument;
} cloudabi_threadattr_t;
_Static_assert(offsetof(cloudabi_threadattr_t, entry_point) == 0, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_threadattr_t, stack) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_threadattr_t, stack) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_threadattr_t, stack_size) == 8, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_threadattr_t, stack_size) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || offsetof(cloudabi_threadattr_t, argument) == 12, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || offsetof(cloudabi_threadattr_t, argument) == 24, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || sizeof(cloudabi_threadattr_t) == 16, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || sizeof(cloudabi_threadattr_t) == 32, "Incorrect layout");
_Static_assert(sizeof(void *) != 4 || _Alignof(cloudabi_threadattr_t) == 4, "Incorrect layout");
_Static_assert(sizeof(void *) != 8 || _Alignof(cloudabi_threadattr_t) == 8, "Incorrect layout");

#endif
