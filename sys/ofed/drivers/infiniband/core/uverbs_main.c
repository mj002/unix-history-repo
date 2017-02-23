/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/math64.h>

#include <asm/uaccess.h>

#include "uverbs.h"

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand userspace verbs access");
MODULE_LICENSE("Dual BSD/GPL");

enum {
	IB_UVERBS_MAJOR       = 231,
	IB_UVERBS_BASE_MINOR  = 192,
	IB_UVERBS_MAX_DEVICES = 32
};

#define IB_UVERBS_BASE_DEV	MKDEV(IB_UVERBS_MAJOR, IB_UVERBS_BASE_MINOR)

static int uverbs_copy_from_udata_ex(void *dest, struct ib_udata *udata, size_t len)
{
	return copy_from_user(dest, udata->inbuf, min(udata->inlen, len)) ? -EFAULT : 0;
}

static int uverbs_copy_to_udata_ex(struct ib_udata *udata, void *src, size_t len)
{
	return copy_to_user(udata->outbuf, src, min(udata->outlen, len)) ? -EFAULT : 0;
}

static struct ib_udata_ops uverbs_copy_ex = {
	.copy_from = uverbs_copy_from_udata_ex,
	.copy_to   = uverbs_copy_to_udata_ex
};

#define INIT_UDATA_EX(udata, ibuf, obuf, ilen, olen)		\
	do {							\
		(udata)->ops    = &uverbs_copy_ex;		\
		(udata)->inbuf  = (void __user *)(unsigned long)(ibuf);	\
		(udata)->outbuf = (void __user *)(unsigned long)(obuf);	\
		(udata)->inlen  = (ilen);			\
		(udata)->outlen = (olen);			\
	} while (0)


static struct class *uverbs_class;

DEFINE_SPINLOCK(ib_uverbs_idr_lock);
DEFINE_IDR(ib_uverbs_pd_idr);
DEFINE_IDR(ib_uverbs_mr_idr);
DEFINE_IDR(ib_uverbs_mw_idr);
DEFINE_IDR(ib_uverbs_ah_idr);
DEFINE_IDR(ib_uverbs_cq_idr);
DEFINE_IDR(ib_uverbs_qp_idr);
DEFINE_IDR(ib_uverbs_srq_idr);
DEFINE_IDR(ib_uverbs_xrcd_idr);
DEFINE_IDR(ib_uverbs_rule_idr);
DEFINE_IDR(ib_uverbs_dct_idr);

static DEFINE_SPINLOCK(map_lock);
static DECLARE_BITMAP(dev_map, IB_UVERBS_MAX_DEVICES);

static ssize_t (*uverbs_cmd_table[])(struct ib_uverbs_file *file,
				     const char __user *buf, int in_len,
				     int out_len) = {
	[IB_USER_VERBS_CMD_GET_CONTEXT]   	= ib_uverbs_get_context,
	[IB_USER_VERBS_CMD_QUERY_DEVICE]  	= ib_uverbs_query_device,
	[IB_USER_VERBS_CMD_QUERY_PORT]    	= ib_uverbs_query_port,
	[IB_USER_VERBS_CMD_ALLOC_PD]      	= ib_uverbs_alloc_pd,
	[IB_USER_VERBS_CMD_DEALLOC_PD]    	= ib_uverbs_dealloc_pd,
	[IB_USER_VERBS_CMD_REG_MR]        	= ib_uverbs_reg_mr,
	[IB_USER_VERBS_CMD_DEREG_MR]      	= ib_uverbs_dereg_mr,
	[IB_USER_VERBS_CMD_ALLOC_MW]		= ib_uverbs_alloc_mw,
	[IB_USER_VERBS_CMD_DEALLOC_MW]		= ib_uverbs_dealloc_mw,
	[IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL] = ib_uverbs_create_comp_channel,
	[IB_USER_VERBS_CMD_CREATE_CQ]     	= ib_uverbs_create_cq,
	[IB_USER_VERBS_CMD_RESIZE_CQ]     	= ib_uverbs_resize_cq,
	[IB_USER_VERBS_CMD_POLL_CQ]     	= ib_uverbs_poll_cq,
	[IB_USER_VERBS_CMD_REQ_NOTIFY_CQ]     	= ib_uverbs_req_notify_cq,
	[IB_USER_VERBS_CMD_DESTROY_CQ]    	= ib_uverbs_destroy_cq,
	[IB_USER_VERBS_CMD_CREATE_QP]     	= ib_uverbs_create_qp,
	[IB_USER_VERBS_CMD_QUERY_QP]     	= ib_uverbs_query_qp,
	[IB_USER_VERBS_CMD_MODIFY_QP]     	= ib_uverbs_modify_qp,
	[IB_USER_VERBS_CMD_DESTROY_QP]    	= ib_uverbs_destroy_qp,
	[IB_USER_VERBS_CMD_POST_SEND]    	= ib_uverbs_post_send,
	[IB_USER_VERBS_CMD_POST_RECV]    	= ib_uverbs_post_recv,
	[IB_USER_VERBS_CMD_POST_SRQ_RECV]    	= ib_uverbs_post_srq_recv,
	[IB_USER_VERBS_CMD_CREATE_AH]    	= ib_uverbs_create_ah,
	[IB_USER_VERBS_CMD_DESTROY_AH]    	= ib_uverbs_destroy_ah,
	[IB_USER_VERBS_CMD_ATTACH_MCAST]  	= ib_uverbs_attach_mcast,
	[IB_USER_VERBS_CMD_DETACH_MCAST]  	= ib_uverbs_detach_mcast,
	[IB_USER_VERBS_CMD_CREATE_SRQ]    	= ib_uverbs_create_srq,
	[IB_USER_VERBS_CMD_MODIFY_SRQ]    	= ib_uverbs_modify_srq,
	[IB_USER_VERBS_CMD_QUERY_SRQ]     	= ib_uverbs_query_srq,
	[IB_USER_VERBS_CMD_DESTROY_SRQ]   	= ib_uverbs_destroy_srq,
	[IB_USER_VERBS_CMD_OPEN_XRCD]		= ib_uverbs_open_xrcd,
	[IB_USER_VERBS_CMD_CLOSE_XRCD]		= ib_uverbs_close_xrcd,
	[IB_USER_VERBS_CMD_CREATE_XSRQ]		= ib_uverbs_create_xsrq,
	[IB_USER_VERBS_CMD_OPEN_QP]		= ib_uverbs_open_qp,
};

static int (*uverbs_ex_cmd_table[])(struct ib_uverbs_file *file,
				    struct ib_udata *ucore,
				    struct ib_udata *uhw) = {
	[IB_USER_VERBS_EX_CMD_CREATE_FLOW]	= ib_uverbs_ex_create_flow,
	[IB_USER_VERBS_EX_CMD_DESTROY_FLOW]	= ib_uverbs_ex_destroy_flow,
};

static ssize_t (*uverbs_exp_cmd_table[])(struct ib_uverbs_file *file,
					 struct ib_udata *ucore,
					 struct ib_udata *uhw) = {
	[IB_USER_VERBS_EXP_CMD_CREATE_QP]	= ib_uverbs_exp_create_qp,
	[IB_USER_VERBS_EXP_CMD_MODIFY_CQ]	= ib_uverbs_exp_modify_cq,
	[IB_USER_VERBS_EXP_CMD_MODIFY_QP]	= ib_uverbs_exp_modify_qp,
	[IB_USER_VERBS_EXP_CMD_CREATE_CQ]	= ib_uverbs_exp_create_cq,
	[IB_USER_VERBS_EXP_CMD_QUERY_DEVICE]	= ib_uverbs_exp_query_device,
	[IB_USER_VERBS_EXP_CMD_CREATE_DCT]	= ib_uverbs_exp_create_dct,
	[IB_USER_VERBS_EXP_CMD_DESTROY_DCT]	= ib_uverbs_exp_destroy_dct,
	[IB_USER_VERBS_EXP_CMD_QUERY_DCT]	= ib_uverbs_exp_query_dct,
};

static void ib_uverbs_add_one(struct ib_device *device);
static void ib_uverbs_remove_one(struct ib_device *device);

static void ib_uverbs_release_dev(struct kref *ref)
{
	struct ib_uverbs_device *dev =
		container_of(ref, struct ib_uverbs_device, ref);

	complete(&dev->comp);
}

static void ib_uverbs_release_event_file(struct kref *ref)
{
	struct ib_uverbs_event_file *file =
		container_of(ref, struct ib_uverbs_event_file, ref);

	kfree(file);
}

void ib_uverbs_release_ucq(struct ib_uverbs_file *file,
			  struct ib_uverbs_event_file *ev_file,
			  struct ib_ucq_object *uobj)
{
	struct ib_uverbs_event *evt, *tmp;

	if (ev_file) {
		spin_lock_irq(&ev_file->lock);
		list_for_each_entry_safe(evt, tmp, &uobj->comp_list, obj_list) {
			list_del(&evt->list);
			kfree(evt);
		}
		spin_unlock_irq(&ev_file->lock);

		kref_put(&ev_file->ref, ib_uverbs_release_event_file);
	}

	spin_lock_irq(&file->async_file->lock);
	list_for_each_entry_safe(evt, tmp, &uobj->async_list, obj_list) {
		list_del(&evt->list);
		kfree(evt);
	}
	spin_unlock_irq(&file->async_file->lock);
}

void ib_uverbs_release_uevent(struct ib_uverbs_file *file,
			      struct ib_uevent_object *uobj)
{
	struct ib_uverbs_event *evt, *tmp;

	spin_lock_irq(&file->async_file->lock);
	list_for_each_entry_safe(evt, tmp, &uobj->event_list, obj_list) {
		list_del(&evt->list);
		kfree(evt);
	}
	spin_unlock_irq(&file->async_file->lock);
}

static void ib_uverbs_detach_umcast(struct ib_qp *qp,
				    struct ib_uqp_object *uobj)
{
	struct ib_uverbs_mcast_entry *mcast, *tmp;

	list_for_each_entry_safe(mcast, tmp, &uobj->mcast_list, list) {
		ib_detach_mcast(qp, &mcast->gid, mcast->lid);
		list_del(&mcast->list);
		kfree(mcast);
	}
}

static int ib_uverbs_cleanup_ucontext(struct ib_uverbs_file *file,
				      struct ib_ucontext *context)
{
	struct ib_uobject *uobj, *tmp;
	int err;

	if (!context)
		return 0;

	context->closing = 1;

	list_for_each_entry_safe(uobj, tmp, &context->ah_list, list) {
		struct ib_ah *ah = uobj->object;

		idr_remove_uobj(&ib_uverbs_ah_idr, uobj);
		ib_destroy_ah(ah);
		kfree(uobj);
	}

	/* Remove MWs before QPs, in order to support type 2A MWs. */
	list_for_each_entry_safe(uobj, tmp, &context->mw_list, list) {
		struct ib_mw *mw = uobj->object;

		idr_remove_uobj(&ib_uverbs_mw_idr, uobj);
		err = ib_dealloc_mw(mw);
		if (err) {
			pr_info("user_verbs: couldn't deallocate MW during cleanup.\n");
			pr_info("user_verbs: the system may have become unstable.\n");
		}
		kfree(uobj);
	}
	list_for_each_entry_safe(uobj, tmp, &context->rule_list, list) {
		struct ib_flow *flow_id = uobj->object;

		idr_remove_uobj(&ib_uverbs_rule_idr, uobj);
		ib_destroy_flow(flow_id);
		kfree(uobj);
	}

	list_for_each_entry_safe(uobj, tmp, &context->qp_list, list) {
		struct ib_qp *qp = uobj->object;
		struct ib_uqp_object *uqp =
			container_of(uobj, struct ib_uqp_object, uevent.uobject);

		idr_remove_uobj(&ib_uverbs_qp_idr, uobj);

		ib_uverbs_detach_umcast(qp, uqp);
		err = ib_destroy_qp(qp);
		if (err)
			pr_info("destroying uverbs qp failed: err %d\n", err);

		ib_uverbs_release_uevent(file, &uqp->uevent);
		kfree(uqp);
	}

	list_for_each_entry_safe(uobj, tmp, &context->dct_list, list) {
		struct ib_dct *dct = uobj->object;
		struct ib_udct_object *udct =
			container_of(uobj, struct ib_udct_object, uobject);

		idr_remove_uobj(&ib_uverbs_dct_idr, uobj);

		err = ib_destroy_dct(dct);
		if (err)
			pr_info("destroying uverbs dct failed: err %d\n", err);

		kfree(udct);
	}

	list_for_each_entry_safe(uobj, tmp, &context->srq_list, list) {
		struct ib_srq *srq = uobj->object;
		struct ib_uevent_object *uevent =
			container_of(uobj, struct ib_uevent_object, uobject);

		idr_remove_uobj(&ib_uverbs_srq_idr, uobj);
		err = ib_destroy_srq(srq);
		if (err)
			pr_info("destroying uverbs srq failed: err %d\n", err);
		ib_uverbs_release_uevent(file, uevent);
		kfree(uevent);
	}

	list_for_each_entry_safe(uobj, tmp, &context->cq_list, list) {
		struct ib_cq *cq = uobj->object;
		struct ib_uverbs_event_file *ev_file = cq->cq_context;
		struct ib_ucq_object *ucq =
			container_of(uobj, struct ib_ucq_object, uobject);

		idr_remove_uobj(&ib_uverbs_cq_idr, uobj);
		err = ib_destroy_cq(cq);
		if (err)
			pr_info("destroying uverbs cq failed: err %d\n", err);

		ib_uverbs_release_ucq(file, ev_file, ucq);
		kfree(ucq);
	}

	list_for_each_entry_safe(uobj, tmp, &context->mr_list, list) {
		struct ib_mr *mr = uobj->object;

		idr_remove_uobj(&ib_uverbs_mr_idr, uobj);
		err = ib_dereg_mr(mr);
		if (err) {
			pr_info("user_verbs: couldn't deregister an MR during cleanup.\n");
			pr_info("user_verbs: the system may have become unstable.\n");
		}
		kfree(uobj);
	}

	mutex_lock(&file->device->xrcd_tree_mutex);
	list_for_each_entry_safe(uobj, tmp, &context->xrcd_list, list) {
		struct ib_xrcd *xrcd = uobj->object;
		struct ib_uxrcd_object *uxrcd =
			container_of(uobj, struct ib_uxrcd_object, uobject);

		idr_remove_uobj(&ib_uverbs_xrcd_idr, uobj);
		ib_uverbs_dealloc_xrcd(file->device, xrcd);
		kfree(uxrcd);
	}
	mutex_unlock(&file->device->xrcd_tree_mutex);

	list_for_each_entry_safe(uobj, tmp, &context->pd_list, list) {
		struct ib_pd *pd = uobj->object;

		idr_remove_uobj(&ib_uverbs_pd_idr, uobj);
		ib_dealloc_pd(pd);
		kfree(uobj);
	}

	return context->device->dealloc_ucontext(context);
}

static void ib_uverbs_release_file(struct kref *ref)
{
	struct ib_uverbs_file *file =
		container_of(ref, struct ib_uverbs_file, ref);

	module_put(file->device->ib_dev->owner);
	kref_put(&file->device->ref, ib_uverbs_release_dev);

	kfree(file);
}

static ssize_t ib_uverbs_event_read(struct file *filp, char __user *buf,
				    size_t count, loff_t *pos)
{
	struct ib_uverbs_event_file *file = filp->private_data;
	struct ib_uverbs_event *event;
	int eventsz;
	int ret = 0;

	spin_lock_irq(&file->lock);

	while (list_empty(&file->event_list)) {
		spin_unlock_irq(&file->lock);

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(file->poll_wait,
					     !list_empty(&file->event_list)))
			return -ERESTARTSYS;

		spin_lock_irq(&file->lock);
	}

	event = list_entry(file->event_list.next, struct ib_uverbs_event, list);

	if (file->is_async)
		eventsz = sizeof (struct ib_uverbs_async_event_desc);
	else
		eventsz = sizeof (struct ib_uverbs_comp_event_desc);

	if (eventsz > count) {
		ret   = -EINVAL;
		event = NULL;
	} else {
		list_del(file->event_list.next);
		if (event->counter) {
			++(*event->counter);
			list_del(&event->obj_list);
		}
	}

	spin_unlock_irq(&file->lock);

	if (event) {
		if (copy_to_user(buf, event, eventsz))
			ret = -EFAULT;
		else
			ret = eventsz;
	}

	kfree(event);

	return ret;
}

static unsigned int ib_uverbs_event_poll(struct file *filp,
					 struct poll_table_struct *wait)
{
	unsigned int pollflags = 0;
	struct ib_uverbs_event_file *file = filp->private_data;

	file->filp = filp;
	poll_wait(filp, &file->poll_wait, wait);

	spin_lock_irq(&file->lock);
	if (!list_empty(&file->event_list))
		pollflags = POLLIN | POLLRDNORM;
	spin_unlock_irq(&file->lock);

	return pollflags;
}

static int ib_uverbs_event_fasync(int fd, struct file *filp, int on)
{
	struct ib_uverbs_event_file *file = filp->private_data;

	return fasync_helper(fd, filp, on, &file->async_queue);
}

static int ib_uverbs_event_close(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_event_file *file = filp->private_data;
	struct ib_uverbs_event *entry, *tmp;

	spin_lock_irq(&file->lock);
	file->is_closed = 1;
	list_for_each_entry_safe(entry, tmp, &file->event_list, list) {
		if (entry->counter)
			list_del(&entry->obj_list);
		kfree(entry);
	}
	spin_unlock_irq(&file->lock);

	if (file->is_async) {
		ib_unregister_event_handler(&file->uverbs_file->event_handler);
		kref_put(&file->uverbs_file->ref, ib_uverbs_release_file);
	}
	kref_put(&file->ref, ib_uverbs_release_event_file);

	return 0;
}

static const struct file_operations uverbs_event_fops = {
	.owner	 = THIS_MODULE,
	.read 	 = ib_uverbs_event_read,
	.poll    = ib_uverbs_event_poll,
	.release = ib_uverbs_event_close,
	.fasync  = ib_uverbs_event_fasync,
	.llseek	 = no_llseek,
};

void ib_uverbs_comp_handler(struct ib_cq *cq, void *cq_context)
{
	struct ib_uverbs_event_file    *file = cq_context;
	struct ib_ucq_object	       *uobj;
	struct ib_uverbs_event	       *entry;
	unsigned long			flags;

	if (!file)
		return;

	spin_lock_irqsave(&file->lock, flags);
	if (file->is_closed) {
		spin_unlock_irqrestore(&file->lock, flags);
		return;
	}

	entry = kmalloc(sizeof *entry, GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&file->lock, flags);
		return;
	}

	uobj = container_of(cq->uobject, struct ib_ucq_object, uobject);

	entry->desc.comp.cq_handle = cq->uobject->user_handle;
	entry->counter		   = &uobj->comp_events_reported;

	list_add_tail(&entry->list, &file->event_list);
	list_add_tail(&entry->obj_list, &uobj->comp_list);
	spin_unlock_irqrestore(&file->lock, flags);

	wake_up_interruptible(&file->poll_wait);
	if (file->filp)
		selwakeup(&file->filp->f_selinfo);
	kill_fasync(&file->async_queue, SIGIO, POLL_IN);
}

static void ib_uverbs_async_handler(struct ib_uverbs_file *file,
				    __u64 element, __u64 event,
				    struct list_head *obj_list,
				    u32 *counter)
{
	struct ib_uverbs_event *entry;
	unsigned long flags;

	spin_lock_irqsave(&file->async_file->lock, flags);
	if (file->async_file->is_closed) {
		spin_unlock_irqrestore(&file->async_file->lock, flags);
		return;
	}

	entry = kmalloc(sizeof *entry, GFP_ATOMIC);
	if (!entry) {
		spin_unlock_irqrestore(&file->async_file->lock, flags);
		return;
	}

	entry->desc.async.element    = element;
	entry->desc.async.event_type = event;
	entry->counter               = counter;

	list_add_tail(&entry->list, &file->async_file->event_list);
	if (obj_list)
		list_add_tail(&entry->obj_list, obj_list);
	spin_unlock_irqrestore(&file->async_file->lock, flags);

	wake_up_interruptible(&file->async_file->poll_wait);
	if (file->async_file->filp)
		selwakeup(&file->async_file->filp->f_selinfo);
	kill_fasync(&file->async_file->async_queue, SIGIO, POLL_IN);
}

void ib_uverbs_cq_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_ucq_object *uobj = container_of(event->element.cq->uobject,
						  struct ib_ucq_object, uobject);

	ib_uverbs_async_handler(uobj->uverbs_file, uobj->uobject.user_handle,
				event->event, &uobj->async_list,
				&uobj->async_events_reported);
}

void ib_uverbs_qp_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_uevent_object *uobj;

	uobj = container_of(event->element.qp->uobject,
			    struct ib_uevent_object, uobject);

	ib_uverbs_async_handler(context_ptr, uobj->uobject.user_handle,
				event->event, &uobj->event_list,
				&uobj->events_reported);
}

void ib_uverbs_srq_event_handler(struct ib_event *event, void *context_ptr)
{
	struct ib_uevent_object *uobj;

	uobj = container_of(event->element.srq->uobject,
			    struct ib_uevent_object, uobject);

	ib_uverbs_async_handler(context_ptr, uobj->uobject.user_handle,
				event->event, &uobj->event_list,
				&uobj->events_reported);
}

void ib_uverbs_event_handler(struct ib_event_handler *handler,
			     struct ib_event *event)
{
	struct ib_uverbs_file *file =
		container_of(handler, struct ib_uverbs_file, event_handler);

	ib_uverbs_async_handler(file, event->element.port_num, event->event,
				NULL, NULL);
}

struct file *ib_uverbs_alloc_event_file(struct ib_uverbs_file *uverbs_file,
					int is_async)
{
	struct ib_uverbs_event_file *ev_file;
	struct file *filp;

	ev_file = kzalloc(sizeof *ev_file, GFP_KERNEL);
	if (!ev_file)
		return ERR_PTR(-ENOMEM);

	kref_init(&ev_file->ref);
	spin_lock_init(&ev_file->lock);
	INIT_LIST_HEAD(&ev_file->event_list);
	init_waitqueue_head(&ev_file->poll_wait);
	ev_file->uverbs_file = uverbs_file;
	ev_file->is_async    = is_async;

	/*
	 * fops_get() can't fail here, because we're coming from a
	 * system call on a uverbs file, which will already have a
	 * module reference.
	 */
	filp = alloc_file(FMODE_READ, fops_get(&uverbs_event_fops));

	if (IS_ERR(filp)) {
		kfree(ev_file);
	} else {
	filp->private_data = ev_file;
	}

	return filp;
}

/*
 * Look up a completion event file by FD.  If lookup is successful,
 * takes a ref to the event file struct that it returns; if
 * unsuccessful, returns NULL.
 */
struct ib_uverbs_event_file *ib_uverbs_lookup_comp_file(int fd)
{
	struct ib_uverbs_event_file *ev_file = NULL;
	struct fd f = fdget(fd);

	if (!f.file)
		return NULL;

	if (f.file->f_op != &uverbs_event_fops)
		goto out;

	ev_file = f.file->private_data;
	if (ev_file->is_async) {
		ev_file = NULL;
		goto out;
	}

	kref_get(&ev_file->ref);

out:
	fdput(f);
	return ev_file;
}

static const char *verbs_cmd_str(__u32 cmd)
{
	switch (cmd) {
	case IB_USER_VERBS_CMD_GET_CONTEXT:
		return "GET_CONTEXT";
	case IB_USER_VERBS_CMD_QUERY_DEVICE:
		return "QUERY_DEVICE";
	case IB_USER_VERBS_CMD_QUERY_PORT:
		return "QUERY_PORT";
	case IB_USER_VERBS_CMD_ALLOC_PD:
		return "ALLOC_PD";
	case IB_USER_VERBS_CMD_DEALLOC_PD:
		return "DEALLOC_PD";
	case IB_USER_VERBS_CMD_REG_MR:
		return "REG_MR";
	case IB_USER_VERBS_CMD_DEREG_MR:
		return "DEREG_MR";
	case IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL:
		return "CREATE_COMP_CHANNEL";
	case IB_USER_VERBS_CMD_CREATE_CQ:
		return "CREATE_CQ";
	case IB_USER_VERBS_CMD_RESIZE_CQ:
		return "RESIZE_CQ";
	case IB_USER_VERBS_CMD_POLL_CQ:
		return "POLL_CQ";
	case IB_USER_VERBS_CMD_REQ_NOTIFY_CQ:
		return "REQ_NOTIFY_CQ";
	case IB_USER_VERBS_CMD_DESTROY_CQ:
		return "DESTROY_CQ";
	case IB_USER_VERBS_CMD_CREATE_QP:
		return "CREATE_QP";
	case IB_USER_VERBS_CMD_QUERY_QP:
		return "QUERY_QP";
	case IB_USER_VERBS_CMD_MODIFY_QP:
		return "MODIFY_QP";
	case IB_USER_VERBS_CMD_DESTROY_QP:
		return "DESTROY_QP";
	case IB_USER_VERBS_CMD_POST_SEND:
		return "POST_SEND";
	case IB_USER_VERBS_CMD_POST_RECV:
		return "POST_RECV";
	case IB_USER_VERBS_CMD_POST_SRQ_RECV:
		return "POST_SRQ_RECV";
	case IB_USER_VERBS_CMD_CREATE_AH:
		return "CREATE_AH";
	case IB_USER_VERBS_CMD_DESTROY_AH:
		return "DESTROY_AH";
	case IB_USER_VERBS_CMD_ATTACH_MCAST:
		return "ATTACH_MCAST";
	case IB_USER_VERBS_CMD_DETACH_MCAST:
		return "DETACH_MCAST";
	case IB_USER_VERBS_CMD_CREATE_SRQ:
		return "CREATE_SRQ";
	case IB_USER_VERBS_CMD_MODIFY_SRQ:
		return "MODIFY_SRQ";
	case IB_USER_VERBS_CMD_QUERY_SRQ:
		return "QUERY_SRQ";
	case IB_USER_VERBS_CMD_DESTROY_SRQ:
		return "DESTROY_SRQ";
	case IB_USER_VERBS_CMD_OPEN_XRCD:
		return "OPEN_XRCD";
	case IB_USER_VERBS_CMD_CLOSE_XRCD:
		return "CLOSE_XRCD";
	case IB_USER_VERBS_CMD_CREATE_XSRQ:
		return "CREATE_XSRQ";
	case IB_USER_VERBS_CMD_OPEN_QP:
		return "OPEN_QP";
	}

	return "Unknown command";
}

enum {
	COMMAND_INFO_MASK = 0x1000,
};

static ssize_t ib_uverbs_exp_handle_cmd(struct ib_uverbs_file *file,
					const char __user *buf,
					struct ib_device *dev,
					struct ib_uverbs_cmd_hdr *hdr,
					size_t count,
					int legacy_ex_cmd)
{
	struct ib_udata ucore;
	struct ib_udata uhw;
	struct ib_uverbs_ex_cmd_hdr ex_hdr;
	__u32 command = hdr->command - IB_USER_VERBS_EXP_CMD_FIRST;

	if (hdr->command & ~(__u32)(IB_USER_VERBS_CMD_FLAGS_MASK |
				    IB_USER_VERBS_CMD_COMMAND_MASK))
		return -EINVAL;

	if (command >= ARRAY_SIZE(uverbs_exp_cmd_table) ||
	    !uverbs_exp_cmd_table[command])
		return -EINVAL;

	if (!file->ucontext)
		return -EINVAL;

	if (!(dev->uverbs_exp_cmd_mask & (1ull << command)))
		return -ENOSYS;

	if (legacy_ex_cmd) {
		struct ib_uverbs_ex_cmd_hdr_legacy hxl;
		struct ib_uverbs_ex_cmd_resp1_legacy resp1;
		__u64 response;
		ssize_t ret;

		if (count < sizeof(hxl))
			return -EINVAL;

		if (copy_from_user(&hxl, buf, sizeof(hxl)))
			return -EFAULT;

		if (((hxl.in_words + hxl.provider_in_words) * 4) != count)
			return -EINVAL;

		count -= sizeof(hxl);
		buf += sizeof(hxl);
		if (hxl.out_words || hxl.provider_out_words) {
			if (count < sizeof(resp1))
				return -EINVAL;
			if (copy_from_user(&resp1, buf, sizeof(resp1)))
				return -EFAULT;
			response = resp1.response;
			if (!response)
				return -EINVAL;

			/*
			 * Change user buffer to comply with new extension format.
			 */
			if (sizeof(resp1.comp_mask) != sizeof(resp1.response))
				return -EFAULT;
			buf += sizeof(resp1.comp_mask);
			if (copy_to_user(__DECONST(void __user *, buf), &resp1.comp_mask,
					 sizeof(resp1.response)))
				return -EFAULT;

		} else {
			response = 0;
		}

		INIT_UDATA_EX(&ucore,
			      (hxl.in_words) ? buf : 0,
			      response,
			      hxl.in_words * 4,
			      hxl.out_words * 4);

		INIT_UDATA_EX(&uhw,
			      (hxl.provider_in_words) ? buf + ucore.inlen : 0,
			      (hxl.provider_out_words) ? response + ucore.outlen : 0,
			      hxl.provider_in_words * 4,
			      hxl.provider_out_words * 4);

		ret = uverbs_exp_cmd_table[command](file, &ucore, &uhw);
		/*
		 * UnChange user buffer
		 */
		if (response && copy_to_user(__DECONST(void __user *, buf), &resp1.response, sizeof(resp1.response)))
			return -EFAULT;

		return ret;
	} else {
		if (count < (sizeof(hdr) + sizeof(ex_hdr)))
			return -EINVAL;

		if (copy_from_user(&ex_hdr, buf + sizeof(hdr), sizeof(ex_hdr)))
			return -EFAULT;

		buf += sizeof(hdr) + sizeof(ex_hdr);

		if ((hdr->in_words + ex_hdr.provider_in_words) * 8 != count)
			return -EINVAL;

		if (ex_hdr.response) {
			if (!hdr->out_words && !ex_hdr.provider_out_words)
				return -EINVAL;
		} else {
			if (hdr->out_words || ex_hdr.provider_out_words)
				return -EINVAL;
		}

		INIT_UDATA_EX(&ucore,
			      (hdr->in_words) ? buf : 0,
			      (unsigned long)ex_hdr.response,
			      hdr->in_words * 8,
			      hdr->out_words * 8);

		INIT_UDATA_EX(&uhw,
			      (ex_hdr.provider_in_words) ? buf + ucore.inlen : 0,
			      (ex_hdr.provider_out_words) ? ex_hdr.response + ucore.outlen : 0,
			      ex_hdr.provider_in_words * 8,
			      ex_hdr.provider_out_words * 8);

		return uverbs_exp_cmd_table[command](file, &ucore, &uhw);
	}
}

static ssize_t ib_uverbs_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct ib_uverbs_file *file = filp->private_data;
	struct ib_device *dev = file->device->ib_dev;
	struct ib_uverbs_cmd_hdr hdr;
	struct timespec ts1;
	struct timespec ts2;
	ktime_t t1, t2, delta;
	s64 ds;
	ssize_t ret;
	u64 dividend;
	u32 divisor;
	__u32 flags;
	__u32 command;
	int legacy_ex_cmd = 0;
	size_t written_count = count;

	if (count < sizeof hdr)
		return -EINVAL;

	if (copy_from_user(&hdr, buf, sizeof hdr))
		return -EFAULT;

	/*
	 * For BWD compatibility change old style extension verbs commands
	 * to their equivalent experimental command.
	 */
	if ((hdr.command >= IB_USER_VERBS_LEGACY_CMD_FIRST) &&
	    (hdr.command <= IB_USER_VERBS_LEGACY_EX_CMD_LAST)) {
		hdr.command += IB_USER_VERBS_EXP_CMD_FIRST -
			       IB_USER_VERBS_LEGACY_CMD_FIRST;
		legacy_ex_cmd = 1;
	}

	flags = (hdr.command &
		 IB_USER_VERBS_CMD_FLAGS_MASK) >> IB_USER_VERBS_CMD_FLAGS_SHIFT;
	command = hdr.command & IB_USER_VERBS_CMD_COMMAND_MASK;

	ktime_get_ts(&ts1);
	if (!flags && (command >= IB_USER_VERBS_EXP_CMD_FIRST)) {
		ret = ib_uverbs_exp_handle_cmd(file, buf, dev, &hdr, count, legacy_ex_cmd);
	} else if (!flags) {
		if (command >= ARRAY_SIZE(uverbs_cmd_table) ||
		    !uverbs_cmd_table[command])
			return -EINVAL;

		if (!file->ucontext &&
		    command != IB_USER_VERBS_CMD_GET_CONTEXT)
			return -EINVAL;

		if (!(dev->uverbs_cmd_mask & (1ull << command)))
			return -ENOSYS;

	if (hdr.in_words * 4 != count)
		return -EINVAL;

		ret = uverbs_cmd_table[command](file,
						buf + sizeof(hdr),
						hdr.in_words * 4,
						hdr.out_words * 4);
	} else if (flags == IB_USER_VERBS_CMD_FLAG_EXTENDED) {
		struct ib_udata ucore;
		struct ib_udata uhw;
		struct ib_uverbs_ex_cmd_hdr ex_hdr;

		if (hdr.command & ~(__u32)(IB_USER_VERBS_CMD_FLAGS_MASK |
					   IB_USER_VERBS_CMD_COMMAND_MASK))
		return -EINVAL;

		if (command >= ARRAY_SIZE(uverbs_ex_cmd_table) ||
		    !uverbs_ex_cmd_table[command])
			return -EINVAL;

		if (!file->ucontext)
			return -EINVAL;

		if (!(dev->uverbs_ex_cmd_mask & (1ull << command)))
			return -ENOSYS;

		if (count < (sizeof(hdr) + sizeof(ex_hdr)))
			return -EINVAL;

		if (copy_from_user(&ex_hdr, buf + sizeof(hdr), sizeof(ex_hdr)))
			return -EFAULT;

		count -= sizeof(hdr) + sizeof(ex_hdr);
		buf += sizeof(hdr) + sizeof(ex_hdr);

		if ((hdr.in_words + ex_hdr.provider_in_words) * 8 != count)
			return -EINVAL;

		if (ex_hdr.response) {
			if (!hdr.out_words && !ex_hdr.provider_out_words)
				return -EINVAL;
		} else {
			if (hdr.out_words || ex_hdr.provider_out_words)
		return -EINVAL;
		}

		INIT_UDATA_EX(&ucore,
			      (hdr.in_words) ? buf : 0,
			      (unsigned long)ex_hdr.response,
			      hdr.in_words * 8,
			      hdr.out_words * 8);

		INIT_UDATA_EX(&uhw,
			      (ex_hdr.provider_in_words) ? buf + ucore.inlen : 0,
			      (ex_hdr.provider_out_words) ? ex_hdr.response + ucore.outlen : 0,
			      ex_hdr.provider_in_words * 8,
			      ex_hdr.provider_out_words * 8);

		ret = uverbs_ex_cmd_table[command](file, &ucore, &uhw);

		if (ret)
			return ret;

		return written_count;

	} else {
		return -EFAULT;
	}

	if ((dev->cmd_perf & (COMMAND_INFO_MASK - 1)) == hdr.command) {
		ktime_get_ts(&ts2);
		t1 = timespec_to_ktime(ts1);
		t2 = timespec_to_ktime(ts2);
		delta = ktime_sub(t2, t1);
		ds = ktime_to_ns(delta);
		spin_lock(&dev->cmd_perf_lock);
		dividend = dev->cmd_avg * dev->cmd_n + ds;
		++dev->cmd_n;
		divisor = dev->cmd_n;
		do_div(dividend, divisor);
		dev->cmd_avg = dividend;
		spin_unlock(&dev->cmd_perf_lock);
		if (dev->cmd_perf & COMMAND_INFO_MASK) {
			pr_info("%s: %s execution time = %lld nsec\n",
				file->device->ib_dev->name,
				verbs_cmd_str(hdr.command),
				(long long)ds);
		}
	}
	return ret;
}

static int ib_uverbs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct ib_uverbs_file *file = filp->private_data;

	if (!file->ucontext)
		return -ENODEV;
	else
		return file->device->ib_dev->mmap(file->ucontext, vma);
}
/* XXX Not supported in FreeBSD */
#if 0
static unsigned long ib_uverbs_get_unmapped_area(struct file *filp,
		unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct ib_uverbs_file *file = filp->private_data;

	if (!file->ucontext)
		return -ENODEV;
	else {
		if (!file->device->ib_dev->get_unmapped_area)
			return current->mm->get_unmapped_area(filp, addr, len,
								pgoff, flags);

		return file->device->ib_dev->get_unmapped_area(filp, addr, len,
								pgoff, flags);
	}
}
#endif

static long ib_uverbs_ioctl(struct file *filp,
			   unsigned int cmd, unsigned long arg)
{
	struct ib_uverbs_file *file = filp->private_data;

	if (!file->device->ib_dev->ioctl)
		return -ENOTSUPP;

	if (!file->ucontext)
		return -ENODEV;
	else
		/* provider should provide it's own locking mechanism */
		return file->device->ib_dev->ioctl(file->ucontext, cmd, arg);
}

/*
 * ib_uverbs_open() does not need the BKL:
 *
 *  - the ib_uverbs_device structures are properly reference counted and
 *    everything else is purely local to the file being created, so
 *    races against other open calls are not a problem;
 *  - there is no ioctl method to race against;
 *  - the open method will either immediately run -ENXIO, or all
 *    required initialization will be done.
 */
static int ib_uverbs_open(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_device *dev;
	struct ib_uverbs_file *file;
	int ret;

	dev = container_of(inode->i_cdev->si_drv1, struct ib_uverbs_device, cdev);
	if (dev)
		kref_get(&dev->ref);
	else
		return -ENXIO;

	if (!try_module_get(dev->ib_dev->owner)) {
		ret = -ENODEV;
		goto err;
	}

	file = kmalloc(sizeof *file, GFP_KERNEL);
	if (!file) {
		ret = -ENOMEM;
		goto err_module;
	}

	file->device	 = dev;
	file->ucontext	 = NULL;
	file->async_file = NULL;
	kref_init(&file->ref);
	mutex_init(&file->mutex);

	filp->private_data = file;

	return nonseekable_open(inode, filp);

err_module:
	module_put(dev->ib_dev->owner);

err:
	kref_put(&dev->ref, ib_uverbs_release_dev);
	return ret;
}

static int ib_uverbs_close(struct inode *inode, struct file *filp)
{
	struct ib_uverbs_file *file = filp->private_data;

	ib_uverbs_cleanup_ucontext(file, file->ucontext);

	if (file->async_file)
		kref_put(&file->async_file->ref, ib_uverbs_release_event_file);

	kref_put(&file->ref, ib_uverbs_release_file);

	return 0;
}

static const struct file_operations uverbs_fops = {
	.owner 	 = THIS_MODULE,
	.write 	 = ib_uverbs_write,
	.open 	 = ib_uverbs_open,
	.release = ib_uverbs_close,
	.llseek	 = no_llseek,
	.unlocked_ioctl = ib_uverbs_ioctl,
};

static const struct file_operations uverbs_mmap_fops = {
	.owner 	 = THIS_MODULE,
	.write 	 = ib_uverbs_write,
	.mmap    = ib_uverbs_mmap,
	.open 	 = ib_uverbs_open,
	.release = ib_uverbs_close,
	.llseek	 = no_llseek,
/* XXX Not supported in FreeBSD */
#if 0
	.get_unmapped_area = ib_uverbs_get_unmapped_area,
#endif
	.unlocked_ioctl = ib_uverbs_ioctl,
};

static struct ib_client uverbs_client = {
	.name   = "uverbs",
	.add    = ib_uverbs_add_one,
	.remove = ib_uverbs_remove_one
};

static ssize_t show_ibdev(struct device *device, struct device_attribute *attr,
			  char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;

	return sprintf(buf, "%s\n", dev->ib_dev->name);
}
static DEVICE_ATTR(ibdev, S_IRUGO, show_ibdev, NULL);

static ssize_t show_dev_ref_cnt(struct device *device,
				struct device_attribute *attr, char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;

	return sprintf(buf, "%d\n",  atomic_read(&dev->ref.refcount));
}
static DEVICE_ATTR(ref_cnt, S_IRUGO, show_dev_ref_cnt, NULL);

static ssize_t show_dev_abi_version(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);

	if (!dev)
		return -ENODEV;

	return sprintf(buf, "%d\n", dev->ib_dev->uverbs_abi_ver);
}
static DEVICE_ATTR(abi_version, S_IRUGO, show_dev_abi_version, NULL);

static ssize_t show_abi_version(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", IB_USER_VERBS_ABI_VERSION);
}

static CLASS_ATTR(abi_version, S_IRUGO, show_abi_version, NULL);

static dev_t overflow_maj;
static DECLARE_BITMAP(overflow_map, IB_UVERBS_MAX_DEVICES);

/*
 * If we have more than IB_UVERBS_MAX_DEVICES, dynamically overflow by
 * requesting a new major number and doubling the number of max devices we
 * support. It's stupid, but simple.
 */
static int find_overflow_devnum(void)
{
	int ret;

	if (!overflow_maj) {
		ret = alloc_chrdev_region(&overflow_maj, 0, IB_UVERBS_MAX_DEVICES,
					  "infiniband_verbs");
		if (ret) {
			printk(KERN_ERR "user_verbs: couldn't register dynamic device number\n");
			return ret;
		}
	}

	ret = find_first_zero_bit(overflow_map, IB_UVERBS_MAX_DEVICES);
	if (ret >= IB_UVERBS_MAX_DEVICES)
		return -1;

	return ret;
}
#include <linux/pci.h>

static ssize_t
show_dev_device(struct device *device, struct device_attribute *attr, char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);

	if (!dev || !dev->ib_dev->dma_device)
		return -ENODEV;

	return sprintf(buf, "0x%04x\n",
	    ((struct pci_dev *)dev->ib_dev->dma_device)->device);
}
static DEVICE_ATTR(device, S_IRUGO, show_dev_device, NULL);

static ssize_t
show_dev_vendor(struct device *device, struct device_attribute *attr, char *buf)
{
	struct ib_uverbs_device *dev = dev_get_drvdata(device);

	if (!dev || !dev->ib_dev->dma_device)
		return -ENODEV;

	return sprintf(buf, "0x%04x\n",
	    ((struct pci_dev *)dev->ib_dev->dma_device)->vendor);
}

static DEVICE_ATTR(vendor, S_IRUGO, show_dev_vendor, NULL);

struct attribute *device_attrs[] =
{
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	NULL
};

static struct attribute_group device_group = {
        .name  = "device",
        .attrs  = device_attrs   
};

static void ib_uverbs_add_one(struct ib_device *device)
{
	int devnum;
	dev_t base;
	struct ib_uverbs_device *uverbs_dev;

	if (!device->alloc_ucontext)
		return;

	uverbs_dev = kzalloc(sizeof *uverbs_dev, GFP_KERNEL);
	if (!uverbs_dev)
		return;

	kref_init(&uverbs_dev->ref);
	init_completion(&uverbs_dev->comp);
	uverbs_dev->xrcd_tree = RB_ROOT;
	mutex_init(&uverbs_dev->xrcd_tree_mutex);

	spin_lock(&map_lock);
	devnum = find_first_zero_bit(dev_map, IB_UVERBS_MAX_DEVICES);
	if (devnum >= IB_UVERBS_MAX_DEVICES) {
		spin_unlock(&map_lock);
		devnum = find_overflow_devnum();
		if (devnum < 0)
		goto err;

		spin_lock(&map_lock);
		uverbs_dev->devnum = devnum + IB_UVERBS_MAX_DEVICES;
		base = devnum + overflow_maj;
		set_bit(devnum, overflow_map);
	} else {
		uverbs_dev->devnum = devnum;
		base = devnum + IB_UVERBS_BASE_DEV;
		set_bit(devnum, dev_map);
	}
	spin_unlock(&map_lock);

	uverbs_dev->ib_dev           = device;
	uverbs_dev->num_comp_vectors = device->num_comp_vectors;

	cdev_init(&uverbs_dev->cdev, NULL);
	uverbs_dev->cdev.owner = THIS_MODULE;
	uverbs_dev->cdev.ops = device->mmap ? &uverbs_mmap_fops : &uverbs_fops;
	kobject_set_name(&uverbs_dev->cdev.kobj, "uverbs%d", uverbs_dev->devnum);
	if (cdev_add(&uverbs_dev->cdev, base, 1))
		goto err_cdev;

	uverbs_dev->dev = device_create(uverbs_class, device->dma_device,
					uverbs_dev->cdev.dev, uverbs_dev,
					"uverbs%d", uverbs_dev->devnum);
	if (IS_ERR(uverbs_dev->dev))
		goto err_cdev;

	if (device_create_file(uverbs_dev->dev, &dev_attr_ibdev))
		goto err_class;
	if (device_create_file(uverbs_dev->dev, &dev_attr_ref_cnt))
		goto err_class;
	if (device_create_file(uverbs_dev->dev, &dev_attr_abi_version))
		goto err_class;
	if (sysfs_create_group(&uverbs_dev->dev->kobj, &device_group))
		goto err_class;

	ib_set_client_data(device, &uverbs_client, uverbs_dev);

	return;

err_class:
	device_destroy(uverbs_class, uverbs_dev->cdev.dev);

err_cdev:
	cdev_del(&uverbs_dev->cdev);
	if (uverbs_dev->devnum < IB_UVERBS_MAX_DEVICES)
		clear_bit(devnum, dev_map);
	else
		clear_bit(devnum, overflow_map);

err:
	kref_put(&uverbs_dev->ref, ib_uverbs_release_dev);
	wait_for_completion(&uverbs_dev->comp);
	kfree(uverbs_dev);
	return;
}

static void ib_uverbs_remove_one(struct ib_device *device)
{
	struct ib_uverbs_device *uverbs_dev = ib_get_client_data(device, &uverbs_client);

	if (!uverbs_dev)
		return;

	sysfs_remove_group(&uverbs_dev->dev->kobj, &device_group);
	dev_set_drvdata(uverbs_dev->dev, NULL);
	device_destroy(uverbs_class, uverbs_dev->cdev.dev);
	cdev_del(&uverbs_dev->cdev);

	if (uverbs_dev->devnum < IB_UVERBS_MAX_DEVICES)
	clear_bit(uverbs_dev->devnum, dev_map);
	else
		clear_bit(uverbs_dev->devnum - IB_UVERBS_MAX_DEVICES, overflow_map);

	kref_put(&uverbs_dev->ref, ib_uverbs_release_dev);
	wait_for_completion(&uverbs_dev->comp);
	kfree(uverbs_dev);
}

static char *uverbs_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}

static int __init ib_uverbs_init(void)
{
	int ret;

	ret = register_chrdev_region(IB_UVERBS_BASE_DEV, IB_UVERBS_MAX_DEVICES,
				     "infiniband_verbs");
	if (ret) {
		printk(KERN_ERR "user_verbs: couldn't register device number\n");
		goto out;
	}

	uverbs_class = class_create(THIS_MODULE, "infiniband_verbs");
	if (IS_ERR(uverbs_class)) {
		ret = PTR_ERR(uverbs_class);
		printk(KERN_ERR "user_verbs: couldn't create class infiniband_verbs\n");
		goto out_chrdev;
	}

	uverbs_class->devnode = uverbs_devnode;

	ret = class_create_file(uverbs_class, &class_attr_abi_version);
	if (ret) {
		printk(KERN_ERR "user_verbs: couldn't create abi_version attribute\n");
		goto out_class;
	}

	ret = ib_register_client(&uverbs_client);
	if (ret) {
		printk(KERN_ERR "user_verbs: couldn't register client\n");
		goto out_class;
	}

	return 0;

out_class:
	class_destroy(uverbs_class);

out_chrdev:
	unregister_chrdev_region(IB_UVERBS_BASE_DEV, IB_UVERBS_MAX_DEVICES);

out:
	return ret;
}

static void __exit ib_uverbs_cleanup(void)
{
	ib_unregister_client(&uverbs_client);
	class_destroy(uverbs_class);
	unregister_chrdev_region(IB_UVERBS_BASE_DEV, IB_UVERBS_MAX_DEVICES);
	if (overflow_maj)
		unregister_chrdev_region(overflow_maj, IB_UVERBS_MAX_DEVICES);
	idr_destroy(&ib_uverbs_pd_idr);
	idr_destroy(&ib_uverbs_mr_idr);
	idr_destroy(&ib_uverbs_mw_idr);
	idr_destroy(&ib_uverbs_ah_idr);
	idr_destroy(&ib_uverbs_cq_idr);
	idr_destroy(&ib_uverbs_qp_idr);
	idr_destroy(&ib_uverbs_srq_idr);
}

module_init(ib_uverbs_init);
module_exit(ib_uverbs_cleanup);
