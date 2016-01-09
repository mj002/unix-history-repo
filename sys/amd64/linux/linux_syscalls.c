/*
 * System call names.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * $FreeBSD$
 * created from FreeBSD: stable/10/sys/amd64/linux/syscalls.master 293567 2016-01-09 17:13:43Z dchagin 
 */

const char *linux_syscallnames[] = {
#define	nosys	linux_nosys
	"read",			/* 0 = read */
	"write",			/* 1 = write */
	"linux_open",			/* 2 = linux_open */
	"close",			/* 3 = close */
	"linux_newstat",			/* 4 = linux_newstat */
	"linux_newfstat",			/* 5 = linux_newfstat */
	"linux_newlstat",			/* 6 = linux_newlstat */
	"poll",			/* 7 = poll */
	"linux_lseek",			/* 8 = linux_lseek */
	"linux_mmap2",			/* 9 = linux_mmap2 */
	"linux_mprotect",			/* 10 = linux_mprotect */
	"munmap",			/* 11 = munmap */
	"linux_brk",			/* 12 = linux_brk */
	"linux_rt_sigaction",			/* 13 = linux_rt_sigaction */
	"linux_rt_sigprocmask",			/* 14 = linux_rt_sigprocmask */
	"linux_rt_sigreturn",			/* 15 = linux_rt_sigreturn */
	"linux_ioctl",			/* 16 = linux_ioctl */
	"linux_pread",			/* 17 = linux_pread */
	"linux_pwrite",			/* 18 = linux_pwrite */
	"readv",			/* 19 = readv */
	"writev",			/* 20 = writev */
	"linux_access",			/* 21 = linux_access */
	"linux_pipe",			/* 22 = linux_pipe */
	"linux_select",			/* 23 = linux_select */
	"sched_yield",			/* 24 = sched_yield */
	"linux_mremap",			/* 25 = linux_mremap */
	"linux_msync",			/* 26 = linux_msync */
	"linux_mincore",			/* 27 = linux_mincore */
	"madvise",			/* 28 = madvise */
	"linux_shmget",			/* 29 = linux_shmget */
	"linux_shmat",			/* 30 = linux_shmat */
	"linux_shmctl",			/* 31 = linux_shmctl */
	"dup",			/* 32 = dup */
	"dup2",			/* 33 = dup2 */
	"linux_pause",			/* 34 = linux_pause */
	"linux_nanosleep",			/* 35 = linux_nanosleep */
	"linux_getitimer",			/* 36 = linux_getitimer */
	"linux_alarm",			/* 37 = linux_alarm */
	"linux_setitimer",			/* 38 = linux_setitimer */
	"linux_getpid",			/* 39 = linux_getpid */
	"linux_sendfile",			/* 40 = linux_sendfile */
	"linux_socket",			/* 41 = linux_socket */
	"linux_connect",			/* 42 = linux_connect */
	"linux_accept",			/* 43 = linux_accept */
	"linux_sendto",			/* 44 = linux_sendto */
	"linux_recvfrom",			/* 45 = linux_recvfrom */
	"linux_sendmsg",			/* 46 = linux_sendmsg */
	"linux_recvmsg",			/* 47 = linux_recvmsg */
	"linux_shutdown",			/* 48 = linux_shutdown */
	"linux_bind",			/* 49 = linux_bind */
	"linux_listen",			/* 50 = linux_listen */
	"linux_getsockname",			/* 51 = linux_getsockname */
	"linux_getpeername",			/* 52 = linux_getpeername */
	"linux_socketpair",			/* 53 = linux_socketpair */
	"linux_setsockopt",			/* 54 = linux_setsockopt */
	"linux_getsockopt",			/* 55 = linux_getsockopt */
	"linux_clone",			/* 56 = linux_clone */
	"linux_fork",			/* 57 = linux_fork */
	"linux_vfork",			/* 58 = linux_vfork */
	"linux_execve",			/* 59 = linux_execve */
	"linux_exit",			/* 60 = linux_exit */
	"linux_wait4",			/* 61 = linux_wait4 */
	"linux_kill",			/* 62 = linux_kill */
	"linux_newuname",			/* 63 = linux_newuname */
	"linux_semget",			/* 64 = linux_semget */
	"linux_semop",			/* 65 = linux_semop */
	"linux_semctl",			/* 66 = linux_semctl */
	"linux_shmdt",			/* 67 = linux_shmdt */
	"linux_msgget",			/* 68 = linux_msgget */
	"linux_msgsnd",			/* 69 = linux_msgsnd */
	"linux_msgrcv",			/* 70 = linux_msgrcv */
	"linux_msgctl",			/* 71 = linux_msgctl */
	"linux_fcntl",			/* 72 = linux_fcntl */
	"flock",			/* 73 = flock */
	"fsync",			/* 74 = fsync */
	"linux_fdatasync",			/* 75 = linux_fdatasync */
	"linux_truncate",			/* 76 = linux_truncate */
	"linux_ftruncate",			/* 77 = linux_ftruncate */
	"linux_getdents",			/* 78 = linux_getdents */
	"linux_getcwd",			/* 79 = linux_getcwd */
	"linux_chdir",			/* 80 = linux_chdir */
	"fchdir",			/* 81 = fchdir */
	"linux_rename",			/* 82 = linux_rename */
	"linux_mkdir",			/* 83 = linux_mkdir */
	"linux_rmdir",			/* 84 = linux_rmdir */
	"linux_creat",			/* 85 = linux_creat */
	"linux_link",			/* 86 = linux_link */
	"linux_unlink",			/* 87 = linux_unlink */
	"linux_symlink",			/* 88 = linux_symlink */
	"linux_readlink",			/* 89 = linux_readlink */
	"linux_chmod",			/* 90 = linux_chmod */
	"fchmod",			/* 91 = fchmod */
	"linux_chown",			/* 92 = linux_chown */
	"fchown",			/* 93 = fchown */
	"linux_lchown",			/* 94 = linux_lchown */
	"umask",			/* 95 = umask */
	"gettimeofday",			/* 96 = gettimeofday */
	"linux_getrlimit",			/* 97 = linux_getrlimit */
	"getrusage",			/* 98 = getrusage */
	"linux_sysinfo",			/* 99 = linux_sysinfo */
	"linux_times",			/* 100 = linux_times */
	"linux_ptrace",			/* 101 = linux_ptrace */
	"linux_getuid",			/* 102 = linux_getuid */
	"linux_syslog",			/* 103 = linux_syslog */
	"linux_getgid",			/* 104 = linux_getgid */
	"setuid",			/* 105 = setuid */
	"setgid",			/* 106 = setgid */
	"geteuid",			/* 107 = geteuid */
	"getegid",			/* 108 = getegid */
	"setpgid",			/* 109 = setpgid */
	"linux_getppid",			/* 110 = linux_getppid */
	"getpgrp",			/* 111 = getpgrp */
	"setsid",			/* 112 = setsid */
	"setreuid",			/* 113 = setreuid */
	"setregid",			/* 114 = setregid */
	"linux_getgroups",			/* 115 = linux_getgroups */
	"linux_setgroups",			/* 116 = linux_setgroups */
	"setresuid",			/* 117 = setresuid */
	"getresuid",			/* 118 = getresuid */
	"setresgid",			/* 119 = setresgid */
	"getresgid",			/* 120 = getresgid */
	"getpgid",			/* 121 = getpgid */
	"linux_setfsuid",			/* 122 = linux_setfsuid */
	"linux_setfsgid",			/* 123 = linux_setfsgid */
	"linux_getsid",			/* 124 = linux_getsid */
	"linux_capget",			/* 125 = linux_capget */
	"linux_capset",			/* 126 = linux_capset */
	"linux_rt_sigpending",			/* 127 = linux_rt_sigpending */
	"linux_rt_sigtimedwait",			/* 128 = linux_rt_sigtimedwait */
	"linux_rt_sigqueueinfo",			/* 129 = linux_rt_sigqueueinfo */
	"linux_rt_sigsuspend",			/* 130 = linux_rt_sigsuspend */
	"linux_sigaltstack",			/* 131 = linux_sigaltstack */
	"linux_utime",			/* 132 = linux_utime */
	"linux_mknod",			/* 133 = linux_mknod */
	"#134",			/* 134 = uselib */
	"linux_personality",			/* 135 = linux_personality */
	"linux_ustat",			/* 136 = linux_ustat */
	"linux_statfs",			/* 137 = linux_statfs */
	"linux_fstatfs",			/* 138 = linux_fstatfs */
	"linux_sysfs",			/* 139 = linux_sysfs */
	"linux_getpriority",			/* 140 = linux_getpriority */
	"setpriority",			/* 141 = setpriority */
	"linux_sched_setparam",			/* 142 = linux_sched_setparam */
	"linux_sched_getparam",			/* 143 = linux_sched_getparam */
	"linux_sched_setscheduler",			/* 144 = linux_sched_setscheduler */
	"linux_sched_getscheduler",			/* 145 = linux_sched_getscheduler */
	"linux_sched_get_priority_max",			/* 146 = linux_sched_get_priority_max */
	"linux_sched_get_priority_min",			/* 147 = linux_sched_get_priority_min */
	"linux_sched_rr_get_interval",			/* 148 = linux_sched_rr_get_interval */
	"mlock",			/* 149 = mlock */
	"munlock",			/* 150 = munlock */
	"mlockall",			/* 151 = mlockall */
	"munlockall",			/* 152 = munlockall */
	"linux_vhangup",			/* 153 = linux_vhangup */
	"#154",			/* 154 = modify_ldt */
	"linux_pivot_root",			/* 155 = linux_pivot_root */
	"linux_sysctl",			/* 156 = linux_sysctl */
	"linux_prctl",			/* 157 = linux_prctl */
	"linux_arch_prctl",			/* 158 = linux_arch_prctl */
	"linux_adjtimex",			/* 159 = linux_adjtimex */
	"linux_setrlimit",			/* 160 = linux_setrlimit */
	"chroot",			/* 161 = chroot */
	"sync",			/* 162 = sync */
	"acct",			/* 163 = acct */
	"settimeofday",			/* 164 = settimeofday */
	"linux_mount",			/* 165 = linux_mount */
	"linux_umount",			/* 166 = linux_umount */
	"swapon",			/* 167 = swapon */
	"linux_swapoff",			/* 168 = linux_swapoff */
	"linux_reboot",			/* 169 = linux_reboot */
	"linux_sethostname",			/* 170 = linux_sethostname */
	"linux_setdomainname",			/* 171 = linux_setdomainname */
	"linux_iopl",			/* 172 = linux_iopl */
	"#173",			/* 173 = ioperm */
	"linux_create_module",			/* 174 = linux_create_module */
	"linux_init_module",			/* 175 = linux_init_module */
	"linux_delete_module",			/* 176 = linux_delete_module */
	"linux_get_kernel_syms",			/* 177 = linux_get_kernel_syms */
	"linux_query_module",			/* 178 = linux_query_module */
	"linux_quotactl",			/* 179 = linux_quotactl */
	"linux_nfsservctl",			/* 180 = linux_nfsservctl */
	"linux_getpmsg",			/* 181 = linux_getpmsg */
	"linux_putpmsg",			/* 182 = linux_putpmsg */
	"linux_afs_syscall",			/* 183 = linux_afs_syscall */
	"linux_tuxcall",			/* 184 = linux_tuxcall */
	"linux_security",			/* 185 = linux_security */
	"linux_gettid",			/* 186 = linux_gettid */
	"#187",			/* 187 = linux_readahead */
	"linux_setxattr",			/* 188 = linux_setxattr */
	"linux_lsetxattr",			/* 189 = linux_lsetxattr */
	"linux_fsetxattr",			/* 190 = linux_fsetxattr */
	"linux_getxattr",			/* 191 = linux_getxattr */
	"linux_lgetxattr",			/* 192 = linux_lgetxattr */
	"linux_fgetxattr",			/* 193 = linux_fgetxattr */
	"linux_listxattr",			/* 194 = linux_listxattr */
	"linux_llistxattr",			/* 195 = linux_llistxattr */
	"linux_flistxattr",			/* 196 = linux_flistxattr */
	"linux_removexattr",			/* 197 = linux_removexattr */
	"linux_lremovexattr",			/* 198 = linux_lremovexattr */
	"linux_fremovexattr",			/* 199 = linux_fremovexattr */
	"linux_tkill",			/* 200 = linux_tkill */
	"linux_time",			/* 201 = linux_time */
	"linux_sys_futex",			/* 202 = linux_sys_futex */
	"linux_sched_setaffinity",			/* 203 = linux_sched_setaffinity */
	"linux_sched_getaffinity",			/* 204 = linux_sched_getaffinity */
	"linux_set_thread_area",			/* 205 = linux_set_thread_area */
	"#206",			/* 206 = linux_io_setup */
	"#207",			/* 207 = linux_io_destroy */
	"#208",			/* 208 = linux_io_getevents */
	"#209",			/* 209 = inux_io_submit */
	"#210",			/* 210 = linux_io_cancel */
	"#211",			/* 211 = linux_get_thread_area */
	"linux_lookup_dcookie",			/* 212 = linux_lookup_dcookie */
	"linux_epoll_create",			/* 213 = linux_epoll_create */
	"linux_epoll_ctl_old",			/* 214 = linux_epoll_ctl_old */
	"linux_epoll_wait_old",			/* 215 = linux_epoll_wait_old */
	"linux_remap_file_pages",			/* 216 = linux_remap_file_pages */
	"linux_getdents64",			/* 217 = linux_getdents64 */
	"linux_set_tid_address",			/* 218 = linux_set_tid_address */
	"#219",			/* 219 = restart_syscall */
	"linux_semtimedop",			/* 220 = linux_semtimedop */
	"linux_fadvise64",			/* 221 = linux_fadvise64 */
	"linux_timer_create",			/* 222 = linux_timer_create */
	"linux_timer_settime",			/* 223 = linux_timer_settime */
	"linux_timer_gettime",			/* 224 = linux_timer_gettime */
	"linux_timer_getoverrun",			/* 225 = linux_timer_getoverrun */
	"linux_timer_delete",			/* 226 = linux_timer_delete */
	"linux_clock_settime",			/* 227 = linux_clock_settime */
	"linux_clock_gettime",			/* 228 = linux_clock_gettime */
	"linux_clock_getres",			/* 229 = linux_clock_getres */
	"linux_clock_nanosleep",			/* 230 = linux_clock_nanosleep */
	"linux_exit_group",			/* 231 = linux_exit_group */
	"linux_epoll_wait",			/* 232 = linux_epoll_wait */
	"linux_epoll_ctl",			/* 233 = linux_epoll_ctl */
	"linux_tgkill",			/* 234 = linux_tgkill */
	"linux_utimes",			/* 235 = linux_utimes */
	"#236",			/* 236 = vserver */
	"linux_mbind",			/* 237 = linux_mbind */
	"linux_set_mempolicy",			/* 238 = linux_set_mempolicy */
	"linux_get_mempolicy",			/* 239 = linux_get_mempolicy */
	"linux_mq_open",			/* 240 = linux_mq_open */
	"linux_mq_unlink",			/* 241 = linux_mq_unlink */
	"linux_mq_timedsend",			/* 242 = linux_mq_timedsend */
	"linux_mq_timedreceive",			/* 243 = linux_mq_timedreceive */
	"linux_mq_notify",			/* 244 = linux_mq_notify */
	"linux_mq_getsetattr",			/* 245 = linux_mq_getsetattr */
	"linux_kexec_load",			/* 246 = linux_kexec_load */
	"linux_waitid",			/* 247 = linux_waitid */
	"linux_add_key",			/* 248 = linux_add_key */
	"linux_request_key",			/* 249 = linux_request_key */
	"linux_keyctl",			/* 250 = linux_keyctl */
	"linux_ioprio_set",			/* 251 = linux_ioprio_set */
	"linux_ioprio_get",			/* 252 = linux_ioprio_get */
	"linux_inotify_init",			/* 253 = linux_inotify_init */
	"linux_inotify_add_watch",			/* 254 = linux_inotify_add_watch */
	"linux_inotify_rm_watch",			/* 255 = linux_inotify_rm_watch */
	"linux_migrate_pages",			/* 256 = linux_migrate_pages */
	"linux_openat",			/* 257 = linux_openat */
	"linux_mkdirat",			/* 258 = linux_mkdirat */
	"linux_mknodat",			/* 259 = linux_mknodat */
	"linux_fchownat",			/* 260 = linux_fchownat */
	"linux_futimesat",			/* 261 = linux_futimesat */
	"linux_newfstatat",			/* 262 = linux_newfstatat */
	"linux_unlinkat",			/* 263 = linux_unlinkat */
	"linux_renameat",			/* 264 = linux_renameat */
	"linux_linkat",			/* 265 = linux_linkat */
	"linux_symlinkat",			/* 266 = linux_symlinkat */
	"linux_readlinkat",			/* 267 = linux_readlinkat */
	"linux_fchmodat",			/* 268 = linux_fchmodat */
	"linux_faccessat",			/* 269 = linux_faccessat */
	"linux_pselect6",			/* 270 = linux_pselect6 */
	"linux_ppoll",			/* 271 = linux_ppoll */
	"linux_unshare",			/* 272 = linux_unshare */
	"linux_set_robust_list",			/* 273 = linux_set_robust_list */
	"linux_get_robust_list",			/* 274 = linux_get_robust_list */
	"linux_splice",			/* 275 = linux_splice */
	"linux_tee",			/* 276 = linux_tee */
	"linux_sync_file_range",			/* 277 = linux_sync_file_range */
	"linux_vmsplice",			/* 278 = linux_vmsplice */
	"linux_move_pages",			/* 279 = linux_move_pages */
	"linux_utimensat",			/* 280 = linux_utimensat */
	"linux_epoll_pwait",			/* 281 = linux_epoll_pwait */
	"linux_signalfd",			/* 282 = linux_signalfd */
	"linux_timerfd",			/* 283 = linux_timerfd */
	"linux_eventfd",			/* 284 = linux_eventfd */
	"linux_fallocate",			/* 285 = linux_fallocate */
	"linux_timerfd_settime",			/* 286 = linux_timerfd_settime */
	"linux_timerfd_gettime",			/* 287 = linux_timerfd_gettime */
	"linux_accept4",			/* 288 = linux_accept4 */
	"linux_signalfd4",			/* 289 = linux_signalfd4 */
	"linux_eventfd2",			/* 290 = linux_eventfd2 */
	"linux_epoll_create1",			/* 291 = linux_epoll_create1 */
	"linux_dup3",			/* 292 = linux_dup3 */
	"linux_pipe2",			/* 293 = linux_pipe2 */
	"linux_inotify_init1",			/* 294 = linux_inotify_init1 */
	"linux_preadv",			/* 295 = linux_preadv */
	"linux_pwritev",			/* 296 = linux_pwritev */
	"linux_rt_tsigqueueinfo",			/* 297 = linux_rt_tsigqueueinfo */
	"linux_perf_event_open",			/* 298 = linux_perf_event_open */
	"linux_recvmmsg",			/* 299 = linux_recvmmsg */
	"linux_fanotify_init",			/* 300 = linux_fanotify_init */
	"linux_fanotify_mark",			/* 301 = linux_fanotify_mark */
	"linux_prlimit64",			/* 302 = linux_prlimit64 */
	"linux_name_to_handle_at",			/* 303 = linux_name_to_handle_at */
	"linux_open_by_handle_at",			/* 304 = linux_open_by_handle_at */
	"linux_clock_adjtime",			/* 305 = linux_clock_adjtime */
	"linux_syncfs",			/* 306 = linux_syncfs */
	"linux_sendmmsg",			/* 307 = linux_sendmmsg */
	"linux_setns",			/* 308 = linux_setns */
	"linux_process_vm_readv",			/* 309 = linux_process_vm_readv */
	"linux_process_vm_writev",			/* 310 = linux_process_vm_writev */
	"linux_kcmp",			/* 311 = linux_kcmp */
	"linux_finit_module",			/* 312 = linux_finit_module */
};
