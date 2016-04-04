/*
 * System call argument to DTrace register array converstion.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * $FreeBSD$
 * This file is part of the DTrace syscall provider.
 */

static void
systrace_args(int sysnum, void *params, uint64_t *uarg, int *n_args)
{
	int64_t *iarg  = (int64_t *) uarg;
	switch (sysnum) {
	/* cloudabi_sys_clock_res_get */
	case 0: {
		struct cloudabi_sys_clock_res_get_args *p = params;
		iarg[0] = p->clock_id; /* cloudabi_clockid_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_clock_time_get */
	case 1: {
		struct cloudabi_sys_clock_time_get_args *p = params;
		iarg[0] = p->clock_id; /* cloudabi_clockid_t */
		iarg[1] = p->precision; /* cloudabi_timestamp_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_condvar_signal */
	case 2: {
		struct cloudabi_sys_condvar_signal_args *p = params;
		uarg[0] = (intptr_t) p->condvar; /* cloudabi_condvar_t * */
		iarg[1] = p->scope; /* cloudabi_scope_t */
		iarg[2] = p->nwaiters; /* cloudabi_nthreads_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_fd_close */
	case 3: {
		struct cloudabi_sys_fd_close_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_fd_create1 */
	case 4: {
		struct cloudabi_sys_fd_create1_args *p = params;
		iarg[0] = p->type; /* cloudabi_filetype_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_fd_create2 */
	case 5: {
		struct cloudabi_sys_fd_create2_args *p = params;
		iarg[0] = p->type; /* cloudabi_filetype_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_fd_datasync */
	case 6: {
		struct cloudabi_sys_fd_datasync_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_fd_dup */
	case 7: {
		struct cloudabi_sys_fd_dup_args *p = params;
		iarg[0] = p->from; /* cloudabi_fd_t */
		*n_args = 1;
		break;
	}
	/* cloudabi64_sys_fd_pread */
	case 8: {
		struct cloudabi64_sys_fd_pread_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->iov; /* const cloudabi64_iovec_t * */
		uarg[2] = p->iovcnt; /* size_t */
		iarg[3] = p->offset; /* cloudabi_filesize_t */
		*n_args = 4;
		break;
	}
	/* cloudabi64_sys_fd_pwrite */
	case 9: {
		struct cloudabi64_sys_fd_pwrite_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->iov; /* const cloudabi64_ciovec_t * */
		uarg[2] = p->iovcnt; /* size_t */
		iarg[3] = p->offset; /* cloudabi_filesize_t */
		*n_args = 4;
		break;
	}
	/* cloudabi64_sys_fd_read */
	case 10: {
		struct cloudabi64_sys_fd_read_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->iov; /* const cloudabi64_iovec_t * */
		uarg[2] = p->iovcnt; /* size_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_fd_replace */
	case 11: {
		struct cloudabi_sys_fd_replace_args *p = params;
		iarg[0] = p->from; /* cloudabi_fd_t */
		iarg[1] = p->to; /* cloudabi_fd_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_fd_seek */
	case 12: {
		struct cloudabi_sys_fd_seek_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		iarg[1] = p->offset; /* cloudabi_filedelta_t */
		iarg[2] = p->whence; /* cloudabi_whence_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_fd_stat_get */
	case 13: {
		struct cloudabi_sys_fd_stat_get_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* cloudabi_fdstat_t * */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_fd_stat_put */
	case 14: {
		struct cloudabi_sys_fd_stat_put_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* const cloudabi_fdstat_t * */
		iarg[2] = p->flags; /* cloudabi_fdsflags_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_fd_sync */
	case 15: {
		struct cloudabi_sys_fd_sync_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		*n_args = 1;
		break;
	}
	/* cloudabi64_sys_fd_write */
	case 16: {
		struct cloudabi64_sys_fd_write_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->iov; /* const cloudabi64_ciovec_t * */
		uarg[2] = p->iovcnt; /* size_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_file_advise */
	case 17: {
		struct cloudabi_sys_file_advise_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		iarg[1] = p->offset; /* cloudabi_filesize_t */
		iarg[2] = p->len; /* cloudabi_filesize_t */
		iarg[3] = p->advice; /* cloudabi_advice_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_file_allocate */
	case 18: {
		struct cloudabi_sys_file_allocate_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		iarg[1] = p->offset; /* cloudabi_filesize_t */
		iarg[2] = p->len; /* cloudabi_filesize_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_file_create */
	case 19: {
		struct cloudabi_sys_file_create_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		iarg[3] = p->type; /* cloudabi_filetype_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_file_link */
	case 20: {
		struct cloudabi_sys_file_link_args *p = params;
		iarg[0] = p->fd1; /* cloudabi_lookup_t */
		uarg[1] = (intptr_t) p->path1; /* const char * */
		uarg[2] = p->path1len; /* size_t */
		iarg[3] = p->fd2; /* cloudabi_fd_t */
		uarg[4] = (intptr_t) p->path2; /* const char * */
		uarg[5] = p->path2len; /* size_t */
		*n_args = 6;
		break;
	}
	/* cloudabi_sys_file_open */
	case 21: {
		struct cloudabi_sys_file_open_args *p = params;
		iarg[0] = p->dirfd; /* cloudabi_lookup_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		iarg[3] = p->oflags; /* cloudabi_oflags_t */
		uarg[4] = (intptr_t) p->fds; /* const cloudabi_fdstat_t * */
		*n_args = 5;
		break;
	}
	/* cloudabi_sys_file_readdir */
	case 22: {
		struct cloudabi_sys_file_readdir_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* void * */
		uarg[2] = p->nbyte; /* size_t */
		iarg[3] = p->cookie; /* cloudabi_dircookie_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_file_readlink */
	case 23: {
		struct cloudabi_sys_file_readlink_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		uarg[3] = (intptr_t) p->buf; /* char * */
		uarg[4] = p->bufsize; /* size_t */
		*n_args = 5;
		break;
	}
	/* cloudabi_sys_file_rename */
	case 24: {
		struct cloudabi_sys_file_rename_args *p = params;
		iarg[0] = p->oldfd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->old; /* const char * */
		uarg[2] = p->oldlen; /* size_t */
		iarg[3] = p->newfd; /* cloudabi_fd_t */
		uarg[4] = (intptr_t) p->new; /* const char * */
		uarg[5] = p->newlen; /* size_t */
		*n_args = 6;
		break;
	}
	/* cloudabi_sys_file_stat_fget */
	case 25: {
		struct cloudabi_sys_file_stat_fget_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* cloudabi_filestat_t * */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_file_stat_fput */
	case 26: {
		struct cloudabi_sys_file_stat_fput_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* const cloudabi_filestat_t * */
		iarg[2] = p->flags; /* cloudabi_fsflags_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_file_stat_get */
	case 27: {
		struct cloudabi_sys_file_stat_get_args *p = params;
		iarg[0] = p->fd; /* cloudabi_lookup_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		uarg[3] = (intptr_t) p->buf; /* cloudabi_filestat_t * */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_file_stat_put */
	case 28: {
		struct cloudabi_sys_file_stat_put_args *p = params;
		iarg[0] = p->fd; /* cloudabi_lookup_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		uarg[3] = (intptr_t) p->buf; /* const cloudabi_filestat_t * */
		iarg[4] = p->flags; /* cloudabi_fsflags_t */
		*n_args = 5;
		break;
	}
	/* cloudabi_sys_file_symlink */
	case 29: {
		struct cloudabi_sys_file_symlink_args *p = params;
		uarg[0] = (intptr_t) p->path1; /* const char * */
		uarg[1] = p->path1len; /* size_t */
		iarg[2] = p->fd; /* cloudabi_fd_t */
		uarg[3] = (intptr_t) p->path2; /* const char * */
		uarg[4] = p->path2len; /* size_t */
		*n_args = 5;
		break;
	}
	/* cloudabi_sys_file_unlink */
	case 30: {
		struct cloudabi_sys_file_unlink_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->path; /* const char * */
		uarg[2] = p->pathlen; /* size_t */
		iarg[3] = p->flags; /* cloudabi_ulflags_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_lock_unlock */
	case 31: {
		struct cloudabi_sys_lock_unlock_args *p = params;
		uarg[0] = (intptr_t) p->lock; /* cloudabi_lock_t * */
		iarg[1] = p->scope; /* cloudabi_scope_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_mem_advise */
	case 32: {
		struct cloudabi_sys_mem_advise_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* void * */
		uarg[1] = p->len; /* size_t */
		iarg[2] = p->advice; /* cloudabi_advice_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_mem_lock */
	case 33: {
		struct cloudabi_sys_mem_lock_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* const void * */
		uarg[1] = p->len; /* size_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_mem_map */
	case 34: {
		struct cloudabi_sys_mem_map_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* void * */
		uarg[1] = p->len; /* size_t */
		iarg[2] = p->prot; /* cloudabi_mprot_t */
		iarg[3] = p->flags; /* cloudabi_mflags_t */
		iarg[4] = p->fd; /* cloudabi_fd_t */
		iarg[5] = p->off; /* cloudabi_filesize_t */
		*n_args = 6;
		break;
	}
	/* cloudabi_sys_mem_protect */
	case 35: {
		struct cloudabi_sys_mem_protect_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* void * */
		uarg[1] = p->len; /* size_t */
		iarg[2] = p->prot; /* cloudabi_mprot_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_mem_sync */
	case 36: {
		struct cloudabi_sys_mem_sync_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* void * */
		uarg[1] = p->len; /* size_t */
		iarg[2] = p->flags; /* cloudabi_msflags_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_mem_unlock */
	case 37: {
		struct cloudabi_sys_mem_unlock_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* const void * */
		uarg[1] = p->len; /* size_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_mem_unmap */
	case 38: {
		struct cloudabi_sys_mem_unmap_args *p = params;
		uarg[0] = (intptr_t) p->addr; /* void * */
		uarg[1] = p->len; /* size_t */
		*n_args = 2;
		break;
	}
	/* cloudabi64_sys_poll */
	case 39: {
		struct cloudabi64_sys_poll_args *p = params;
		uarg[0] = (intptr_t) p->in; /* const cloudabi64_subscription_t * */
		uarg[1] = (intptr_t) p->out; /* cloudabi64_event_t * */
		uarg[2] = p->nsubscriptions; /* size_t */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_proc_exec */
	case 40: {
		struct cloudabi_sys_proc_exec_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->data; /* const void * */
		uarg[2] = p->datalen; /* size_t */
		uarg[3] = (intptr_t) p->fds; /* const cloudabi_fd_t * */
		uarg[4] = p->fdslen; /* size_t */
		*n_args = 5;
		break;
	}
	/* cloudabi_sys_proc_exit */
	case 41: {
		struct cloudabi_sys_proc_exit_args *p = params;
		iarg[0] = p->rval; /* cloudabi_exitcode_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_proc_fork */
	case 42: {
		*n_args = 0;
		break;
	}
	/* cloudabi_sys_proc_raise */
	case 43: {
		struct cloudabi_sys_proc_raise_args *p = params;
		iarg[0] = p->sig; /* cloudabi_signal_t */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_random_get */
	case 44: {
		struct cloudabi_sys_random_get_args *p = params;
		uarg[0] = (intptr_t) p->buf; /* void * */
		uarg[1] = p->nbyte; /* size_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_sock_accept */
	case 45: {
		struct cloudabi_sys_sock_accept_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* cloudabi_sockstat_t * */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_sock_bind */
	case 46: {
		struct cloudabi_sys_sock_bind_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		iarg[1] = p->fd; /* cloudabi_fd_t */
		uarg[2] = (intptr_t) p->path; /* const char * */
		uarg[3] = p->pathlen; /* size_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_sock_connect */
	case 47: {
		struct cloudabi_sys_sock_connect_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		iarg[1] = p->fd; /* cloudabi_fd_t */
		uarg[2] = (intptr_t) p->path; /* const char * */
		uarg[3] = p->pathlen; /* size_t */
		*n_args = 4;
		break;
	}
	/* cloudabi_sys_sock_listen */
	case 48: {
		struct cloudabi_sys_sock_listen_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		iarg[1] = p->backlog; /* cloudabi_backlog_t */
		*n_args = 2;
		break;
	}
	/* cloudabi64_sys_sock_recv */
	case 49: {
		struct cloudabi64_sys_sock_recv_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->in; /* const cloudabi64_recv_in_t * */
		uarg[2] = (intptr_t) p->out; /* cloudabi64_recv_out_t * */
		*n_args = 3;
		break;
	}
	/* cloudabi64_sys_sock_send */
	case 50: {
		struct cloudabi64_sys_sock_send_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->in; /* const cloudabi64_send_in_t * */
		uarg[2] = (intptr_t) p->out; /* cloudabi64_send_out_t * */
		*n_args = 3;
		break;
	}
	/* cloudabi_sys_sock_shutdown */
	case 51: {
		struct cloudabi_sys_sock_shutdown_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		iarg[1] = p->how; /* cloudabi_sdflags_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_sock_stat_get */
	case 52: {
		struct cloudabi_sys_sock_stat_get_args *p = params;
		iarg[0] = p->sock; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->buf; /* cloudabi_sockstat_t * */
		iarg[2] = p->flags; /* cloudabi_ssflags_t */
		*n_args = 3;
		break;
	}
	/* cloudabi64_sys_thread_create */
	case 53: {
		struct cloudabi64_sys_thread_create_args *p = params;
		uarg[0] = (intptr_t) p->attr; /* cloudabi64_threadattr_t * */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_thread_exit */
	case 54: {
		struct cloudabi_sys_thread_exit_args *p = params;
		uarg[0] = (intptr_t) p->lock; /* cloudabi_lock_t * */
		iarg[1] = p->scope; /* cloudabi_scope_t */
		*n_args = 2;
		break;
	}
	/* cloudabi_sys_thread_tcb_set */
	case 55: {
		struct cloudabi_sys_thread_tcb_set_args *p = params;
		uarg[0] = (intptr_t) p->tcb; /* void * */
		*n_args = 1;
		break;
	}
	/* cloudabi_sys_thread_yield */
	case 56: {
		*n_args = 0;
		break;
	}
	/* cloudabi64_sys_poll_fd */
	case 57: {
		struct cloudabi64_sys_poll_fd_args *p = params;
		iarg[0] = p->fd; /* cloudabi_fd_t */
		uarg[1] = (intptr_t) p->in; /* const cloudabi64_subscription_t * */
		uarg[2] = p->nin; /* size_t */
		uarg[3] = (intptr_t) p->out; /* cloudabi64_event_t * */
		uarg[4] = p->nout; /* size_t */
		uarg[5] = (intptr_t) p->timeout; /* const cloudabi64_subscription_t * */
		*n_args = 6;
		break;
	}
	default:
		*n_args = 0;
		break;
	};
}
static void
systrace_entry_setargdesc(int sysnum, int ndx, char *desc, size_t descsz)
{
	const char *p = NULL;
	switch (sysnum) {
	/* cloudabi_sys_clock_res_get */
	case 0:
		switch(ndx) {
		case 0:
			p = "cloudabi_clockid_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_clock_time_get */
	case 1:
		switch(ndx) {
		case 0:
			p = "cloudabi_clockid_t";
			break;
		case 1:
			p = "cloudabi_timestamp_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_condvar_signal */
	case 2:
		switch(ndx) {
		case 0:
			p = "cloudabi_condvar_t *";
			break;
		case 1:
			p = "cloudabi_scope_t";
			break;
		case 2:
			p = "cloudabi_nthreads_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_close */
	case 3:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_create1 */
	case 4:
		switch(ndx) {
		case 0:
			p = "cloudabi_filetype_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_create2 */
	case 5:
		switch(ndx) {
		case 0:
			p = "cloudabi_filetype_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_datasync */
	case 6:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_dup */
	case 7:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_fd_pread */
	case 8:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_iovec_t *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_filesize_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_fd_pwrite */
	case 9:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_ciovec_t *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_filesize_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_fd_read */
	case 10:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_iovec_t *";
			break;
		case 2:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_replace */
	case 11:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_fd_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_seek */
	case 12:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_filedelta_t";
			break;
		case 2:
			p = "cloudabi_whence_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_stat_get */
	case 13:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_fdstat_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_stat_put */
	case 14:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi_fdstat_t *";
			break;
		case 2:
			p = "cloudabi_fdsflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_fd_sync */
	case 15:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_fd_write */
	case 16:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_ciovec_t *";
			break;
		case 2:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_advise */
	case 17:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_filesize_t";
			break;
		case 2:
			p = "cloudabi_filesize_t";
			break;
		case 3:
			p = "cloudabi_advice_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_allocate */
	case 18:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_filesize_t";
			break;
		case 2:
			p = "cloudabi_filesize_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_create */
	case 19:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_filetype_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_link */
	case 20:
		switch(ndx) {
		case 0:
			p = "cloudabi_lookup_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_fd_t";
			break;
		case 4:
			p = "const char *";
			break;
		case 5:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_open */
	case 21:
		switch(ndx) {
		case 0:
			p = "cloudabi_lookup_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_oflags_t";
			break;
		case 4:
			p = "const cloudabi_fdstat_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_readdir */
	case 22:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "void *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_dircookie_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_readlink */
	case 23:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "char *";
			break;
		case 4:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_rename */
	case 24:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_fd_t";
			break;
		case 4:
			p = "const char *";
			break;
		case 5:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_stat_fget */
	case 25:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_filestat_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_stat_fput */
	case 26:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi_filestat_t *";
			break;
		case 2:
			p = "cloudabi_fsflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_stat_get */
	case 27:
		switch(ndx) {
		case 0:
			p = "cloudabi_lookup_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_filestat_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_stat_put */
	case 28:
		switch(ndx) {
		case 0:
			p = "cloudabi_lookup_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "const cloudabi_filestat_t *";
			break;
		case 4:
			p = "cloudabi_fsflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_symlink */
	case 29:
		switch(ndx) {
		case 0:
			p = "const char *";
			break;
		case 1:
			p = "size_t";
			break;
		case 2:
			p = "cloudabi_fd_t";
			break;
		case 3:
			p = "const char *";
			break;
		case 4:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_file_unlink */
	case 30:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const char *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi_ulflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_lock_unlock */
	case 31:
		switch(ndx) {
		case 0:
			p = "cloudabi_lock_t *";
			break;
		case 1:
			p = "cloudabi_scope_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_advise */
	case 32:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		case 2:
			p = "cloudabi_advice_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_lock */
	case 33:
		switch(ndx) {
		case 0:
			p = "const void *";
			break;
		case 1:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_map */
	case 34:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		case 2:
			p = "cloudabi_mprot_t";
			break;
		case 3:
			p = "cloudabi_mflags_t";
			break;
		case 4:
			p = "cloudabi_fd_t";
			break;
		case 5:
			p = "cloudabi_filesize_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_protect */
	case 35:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		case 2:
			p = "cloudabi_mprot_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_sync */
	case 36:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		case 2:
			p = "cloudabi_msflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_unlock */
	case 37:
		switch(ndx) {
		case 0:
			p = "const void *";
			break;
		case 1:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_mem_unmap */
	case 38:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_poll */
	case 39:
		switch(ndx) {
		case 0:
			p = "const cloudabi64_subscription_t *";
			break;
		case 1:
			p = "cloudabi64_event_t *";
			break;
		case 2:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_proc_exec */
	case 40:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const void *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "const cloudabi_fd_t *";
			break;
		case 4:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_proc_exit */
	case 41:
		switch(ndx) {
		case 0:
			p = "cloudabi_exitcode_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_proc_fork */
	case 42:
		break;
	/* cloudabi_sys_proc_raise */
	case 43:
		switch(ndx) {
		case 0:
			p = "cloudabi_signal_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_random_get */
	case 44:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		case 1:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_accept */
	case 45:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_sockstat_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_bind */
	case 46:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_fd_t";
			break;
		case 2:
			p = "const char *";
			break;
		case 3:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_connect */
	case 47:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_fd_t";
			break;
		case 2:
			p = "const char *";
			break;
		case 3:
			p = "size_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_listen */
	case 48:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_backlog_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_sock_recv */
	case 49:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_recv_in_t *";
			break;
		case 2:
			p = "cloudabi64_recv_out_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_sock_send */
	case 50:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_send_in_t *";
			break;
		case 2:
			p = "cloudabi64_send_out_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_shutdown */
	case 51:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_sdflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_sock_stat_get */
	case 52:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "cloudabi_sockstat_t *";
			break;
		case 2:
			p = "cloudabi_ssflags_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi64_sys_thread_create */
	case 53:
		switch(ndx) {
		case 0:
			p = "cloudabi64_threadattr_t *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_thread_exit */
	case 54:
		switch(ndx) {
		case 0:
			p = "cloudabi_lock_t *";
			break;
		case 1:
			p = "cloudabi_scope_t";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_thread_tcb_set */
	case 55:
		switch(ndx) {
		case 0:
			p = "void *";
			break;
		default:
			break;
		};
		break;
	/* cloudabi_sys_thread_yield */
	case 56:
		break;
	/* cloudabi64_sys_poll_fd */
	case 57:
		switch(ndx) {
		case 0:
			p = "cloudabi_fd_t";
			break;
		case 1:
			p = "const cloudabi64_subscription_t *";
			break;
		case 2:
			p = "size_t";
			break;
		case 3:
			p = "cloudabi64_event_t *";
			break;
		case 4:
			p = "size_t";
			break;
		case 5:
			p = "const cloudabi64_subscription_t *";
			break;
		default:
			break;
		};
		break;
	default:
		break;
	};
	if (p != NULL)
		strlcpy(desc, p, descsz);
}
static void
systrace_return_setargdesc(int sysnum, int ndx, char *desc, size_t descsz)
{
	const char *p = NULL;
	switch (sysnum) {
	/* cloudabi_sys_clock_res_get */
	case 0:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_timestamp_t";
		break;
	/* cloudabi_sys_clock_time_get */
	case 1:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_timestamp_t";
		break;
	/* cloudabi_sys_condvar_signal */
	case 2:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_close */
	case 3:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_create1 */
	case 4:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_fd_t";
		break;
	/* cloudabi_sys_fd_create2 */
	case 5:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_datasync */
	case 6:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_dup */
	case 7:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_fd_t";
		break;
	/* cloudabi64_sys_fd_pread */
	case 8:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi64_sys_fd_pwrite */
	case 9:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi64_sys_fd_read */
	case 10:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi_sys_fd_replace */
	case 11:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_seek */
	case 12:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_filesize_t";
		break;
	/* cloudabi_sys_fd_stat_get */
	case 13:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_stat_put */
	case 14:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_fd_sync */
	case 15:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi64_sys_fd_write */
	case 16:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi_sys_file_advise */
	case 17:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_allocate */
	case 18:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_create */
	case 19:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_link */
	case 20:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_open */
	case 21:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_fd_t";
		break;
	/* cloudabi_sys_file_readdir */
	case 22:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi_sys_file_readlink */
	case 23:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi_sys_file_rename */
	case 24:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_stat_fget */
	case 25:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_stat_fput */
	case 26:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_stat_get */
	case 27:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_stat_put */
	case 28:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_symlink */
	case 29:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_file_unlink */
	case 30:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_lock_unlock */
	case 31:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_advise */
	case 32:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_lock */
	case 33:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_map */
	case 34:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_protect */
	case 35:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_sync */
	case 36:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_unlock */
	case 37:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_mem_unmap */
	case 38:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi64_sys_poll */
	case 39:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	/* cloudabi_sys_proc_exec */
	case 40:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_proc_exit */
	case 41:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_proc_fork */
	case 42:
	/* cloudabi_sys_proc_raise */
	case 43:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_random_get */
	case 44:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_sock_accept */
	case 45:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_fd_t";
		break;
	/* cloudabi_sys_sock_bind */
	case 46:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_sock_connect */
	case 47:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_sock_listen */
	case 48:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi64_sys_sock_recv */
	case 49:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi64_sys_sock_send */
	case 50:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_sock_shutdown */
	case 51:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_sock_stat_get */
	case 52:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi64_sys_thread_create */
	case 53:
		if (ndx == 0 || ndx == 1)
			p = "cloudabi_tid_t";
		break;
	/* cloudabi_sys_thread_exit */
	case 54:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_thread_tcb_set */
	case 55:
		if (ndx == 0 || ndx == 1)
			p = "void";
		break;
	/* cloudabi_sys_thread_yield */
	case 56:
	/* cloudabi64_sys_poll_fd */
	case 57:
		if (ndx == 0 || ndx == 1)
			p = "size_t";
		break;
	default:
		break;
	};
	if (p != NULL)
		strlcpy(desc, p, descsz);
}
