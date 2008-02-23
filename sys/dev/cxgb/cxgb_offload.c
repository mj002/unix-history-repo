
/**************************************************************************

Copyright (c) 2007, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


***************************************************************************/


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/pciio.h>
#include <sys/conf.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus_dma.h>
#include <sys/rman.h>
#include <sys/ioccom.h>
#include <sys/mbuf.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/proc.h>

#ifdef CONFIG_DEFINED
#include <cxgb_include.h>
#else
#include <dev/cxgb/cxgb_include.h>
#endif

#include <net/if_vlan_var.h>
#include <net/route.h>

#define VALIDATE_TID 0
MALLOC_DEFINE(M_CXGB, "cxgb", "Chelsio 10 Gigabit Ethernet and services");

TAILQ_HEAD(, cxgb_client) client_list;
TAILQ_HEAD(, t3cdev) ofld_dev_list;
TAILQ_HEAD(, adapter) adapter_list;

static struct mtx cxgb_db_lock;
static struct rwlock adapter_list_lock;


static const unsigned int MAX_ATIDS = 64 * 1024;
static const unsigned int ATID_BASE = 0x100000;
static int inited = 0;

static inline int
offload_activated(struct t3cdev *tdev)
{
	struct adapter *adapter = tdev2adap(tdev);
	
	return (isset(&adapter->open_device_map, OFFLOAD_DEVMAP_BIT));
}

/**
 *	cxgb_register_client - register an offload client
 *	@client: the client
 *
 *	Add the client to the client list,
 *	and call backs the client for each activated offload device
 */
void
cxgb_register_client(struct cxgb_client *client)
{
	struct t3cdev *tdev;

	mtx_lock(&cxgb_db_lock);
	TAILQ_INSERT_TAIL(&client_list, client, client_entry);

	if (client->add) {
		TAILQ_FOREACH(tdev, &ofld_dev_list, entry) {
			if (offload_activated(tdev)) {
				client->add(tdev);
			} else
				CTR1(KTR_CXGB,
				    "cxgb_register_client: %p not activated", tdev);
			
		}
	}
	mtx_unlock(&cxgb_db_lock);
}

/**
 *	cxgb_unregister_client - unregister an offload client
 *	@client: the client
 *
 *	Remove the client to the client list,
 *	and call backs the client for each activated offload device.
 */
void
cxgb_unregister_client(struct cxgb_client *client)
{
	struct t3cdev *tdev;

	mtx_lock(&cxgb_db_lock);
	TAILQ_REMOVE(&client_list, client, client_entry);

	if (client->remove) {
		TAILQ_FOREACH(tdev, &ofld_dev_list, entry) {
			if (offload_activated(tdev))
				client->remove(tdev);
		}
	}
	mtx_unlock(&cxgb_db_lock);
}

/**
 *	cxgb_add_clients - activate register clients for an offload device
 *	@tdev: the offload device
 *
 *	Call backs all registered clients once a offload device is activated 
 */
void
cxgb_add_clients(struct t3cdev *tdev)
{
	struct cxgb_client *client;

	mtx_lock(&cxgb_db_lock);
	TAILQ_FOREACH(client, &client_list, client_entry) {
		if (client->add)
			client->add(tdev);
	}
	mtx_unlock(&cxgb_db_lock);
}

/**
 *	cxgb_remove_clients - activate register clients for an offload device
 *	@tdev: the offload device
 *
 *	Call backs all registered clients once a offload device is deactivated 
 */
void
cxgb_remove_clients(struct t3cdev *tdev)
{
	struct cxgb_client *client;

	mtx_lock(&cxgb_db_lock);
	TAILQ_FOREACH(client, &client_list, client_entry) {
		if (client->remove)
			client->remove(tdev);
	}
	mtx_unlock(&cxgb_db_lock);
}

static int
is_offloading(struct ifnet *ifp)
{
	struct adapter *adapter;
	int port;

	rw_rlock(&adapter_list_lock);
	TAILQ_FOREACH(adapter, &adapter_list, adapter_entry) {
		for_each_port(adapter, port) {
			if (ifp == adapter->port[port].ifp) {
				rw_runlock(&adapter_list_lock);
				return 1;
			}
		}
	}
	rw_runlock(&adapter_list_lock);
	return 0;
}

static struct ifnet *
get_iff_from_mac(adapter_t *adapter, const uint8_t *mac, unsigned int vlan)
{
	int i;

	for_each_port(adapter, i) {
#ifdef notyet		
		const struct vlan_group *grp;
#endif		
		const struct port_info *p = &adapter->port[i];
		struct ifnet *ifp = p->ifp;

		if (!memcmp(p->hw_addr, mac, ETHER_ADDR_LEN)) {
#ifdef notyet	
			
			if (vlan && vlan != EVL_VLID_MASK) {
				grp = p->vlan_grp;
				dev = grp ? grp->vlan_devices[vlan] : NULL;
			} else
				while (dev->master)
					dev = dev->master;
#endif			
			return (ifp);
		}
	}
	return (NULL);
}

static inline void
failover_fixup(adapter_t *adapter, int port)
{
	if (adapter->params.rev == 0) {
		struct ifnet *ifp = adapter->port[port].ifp;
		struct cmac *mac = &adapter->port[port].mac;
		if (!(ifp->if_flags & IFF_UP)) {
			/* Failover triggered by the interface ifdown */
			t3_write_reg(adapter, A_XGM_TX_CTRL + mac->offset,
				     F_TXEN);
			t3_read_reg(adapter, A_XGM_TX_CTRL + mac->offset);
		} else {
			/* Failover triggered by the interface link down */
			t3_write_reg(adapter, A_XGM_RX_CTRL + mac->offset, 0);
			t3_read_reg(adapter, A_XGM_RX_CTRL + mac->offset);
			t3_write_reg(adapter, A_XGM_RX_CTRL + mac->offset,
				     F_RXEN);
		}
	}
}

static int
cxgb_ulp_iscsi_ctl(adapter_t *adapter, unsigned int req, void *data)
{
	int ret = 0;
	struct ulp_iscsi_info *uiip = data;

	switch (req) {
	case ULP_ISCSI_GET_PARAMS:
		uiip->llimit = t3_read_reg(adapter, A_ULPRX_ISCSI_LLIMIT);
		uiip->ulimit = t3_read_reg(adapter, A_ULPRX_ISCSI_ULIMIT);
		uiip->tagmask = t3_read_reg(adapter, A_ULPRX_ISCSI_TAGMASK);
		/*
		 * On tx, the iscsi pdu has to be <= tx page size and has to
		 * fit into the Tx PM FIFO.
		 */
		uiip->max_txsz = min(adapter->params.tp.tx_pg_size,
				     t3_read_reg(adapter, A_PM1_TX_CFG) >> 17);
		/* on rx, the iscsi pdu has to be < rx page size and the
		   whole pdu + cpl headers has to fit into one sge buffer */
		/* also check the max rx data length programmed in TP */
		uiip->max_rxsz = min(uiip->max_rxsz,
				     ((t3_read_reg(adapter, A_TP_PARA_REG2))
					>> S_MAXRXDATA) & M_MAXRXDATA);
		break;
	case ULP_ISCSI_SET_PARAMS:
		t3_write_reg(adapter, A_ULPRX_ISCSI_TAGMASK, uiip->tagmask);
		break;
	default:
		ret = (EOPNOTSUPP);
	}
	return ret;
}

/* Response queue used for RDMA events. */
#define ASYNC_NOTIF_RSPQ 0

static int
cxgb_rdma_ctl(adapter_t *adapter, unsigned int req, void *data)
{
	int ret = 0;

	switch (req) {
	case RDMA_GET_PARAMS: {
		struct rdma_info *req = data;

		req->udbell_physbase = rman_get_start(adapter->udbs_res);
		req->udbell_len = rman_get_size(adapter->udbs_res);
		req->tpt_base = t3_read_reg(adapter, A_ULPTX_TPT_LLIMIT);
		req->tpt_top  = t3_read_reg(adapter, A_ULPTX_TPT_ULIMIT);
		req->pbl_base = t3_read_reg(adapter, A_ULPTX_PBL_LLIMIT);
		req->pbl_top  = t3_read_reg(adapter, A_ULPTX_PBL_ULIMIT);
		req->rqt_base = t3_read_reg(adapter, A_ULPRX_RQ_LLIMIT);
		req->rqt_top  = t3_read_reg(adapter, A_ULPRX_RQ_ULIMIT);
		req->kdb_addr =  (void *)((unsigned long)rman_get_virtual(adapter->regs_res) + A_SG_KDOORBELL);		break;
	}
	case RDMA_CQ_OP: {
		struct rdma_cq_op *req = data;

		/* may be called in any context */
		mtx_lock_spin(&adapter->sge.reg_lock);
		ret = t3_sge_cqcntxt_op(adapter, req->id, req->op,
					req->credits);
		mtx_unlock_spin(&adapter->sge.reg_lock);
		break;
	}
	case RDMA_GET_MEM: {
		struct ch_mem_range *t = data;
		struct mc7 *mem;

		if ((t->addr & 7) || (t->len & 7))
			return (EINVAL);
		if (t->mem_id == MEM_CM)
			mem = &adapter->cm;
		else if (t->mem_id == MEM_PMRX)
			mem = &adapter->pmrx;
		else if (t->mem_id == MEM_PMTX)
			mem = &adapter->pmtx;
		else
			return (EINVAL);

		ret = t3_mc7_bd_read(mem, t->addr/8, t->len/8, (u64 *)t->buf);
		if (ret)
			return (ret);
		break;
	}
	case RDMA_CQ_SETUP: {
		struct rdma_cq_setup *req = data;

		mtx_lock_spin(&adapter->sge.reg_lock);
		ret = t3_sge_init_cqcntxt(adapter, req->id, req->base_addr,
					  req->size, ASYNC_NOTIF_RSPQ,
					  req->ovfl_mode, req->credits,
					  req->credit_thres);
		mtx_unlock_spin(&adapter->sge.reg_lock);
		break;
	}
	case RDMA_CQ_DISABLE:
		mtx_lock_spin(&adapter->sge.reg_lock);
		ret = t3_sge_disable_cqcntxt(adapter, *(unsigned int *)data);
		mtx_unlock_spin(&adapter->sge.reg_lock);
		break;
	case RDMA_CTRL_QP_SETUP: {
		struct rdma_ctrlqp_setup *req = data;

		mtx_lock_spin(&adapter->sge.reg_lock);
		ret = t3_sge_init_ecntxt(adapter, FW_RI_SGEEC_START, 0,
					 SGE_CNTXT_RDMA, ASYNC_NOTIF_RSPQ,
					 req->base_addr, req->size,
					 FW_RI_TID_START, 1, 0);
		mtx_unlock_spin(&adapter->sge.reg_lock);
		break;
	}
	default:
		ret = EOPNOTSUPP;
	}
	return (ret);
}

static int
cxgb_offload_ctl(struct t3cdev *tdev, unsigned int req, void *data)
{
	struct adapter *adapter = tdev2adap(tdev);
	struct tid_range *tid;
	struct mtutab *mtup;
	struct iff_mac *iffmacp;
	struct ddp_params *ddpp;
	struct adap_ports *ports;
	struct ofld_page_info *rx_page_info;
	struct tp_params *tp = &adapter->params.tp;
	int port;

	switch (req) {
	case GET_MAX_OUTSTANDING_WR:
		*(unsigned int *)data = FW_WR_NUM;
		break;
	case GET_WR_LEN:
		*(unsigned int *)data = WR_FLITS;
		break;
	case GET_TX_MAX_CHUNK:
		*(unsigned int *)data = 1 << 20;  /* 1MB */
		break;
	case GET_TID_RANGE:
		tid = data;
		tid->num = t3_mc5_size(&adapter->mc5) -
			adapter->params.mc5.nroutes -
			adapter->params.mc5.nfilters -
			adapter->params.mc5.nservers;
		tid->base = 0;
		break;
	case GET_STID_RANGE:
		tid = data;
		tid->num = adapter->params.mc5.nservers;
		tid->base = t3_mc5_size(&adapter->mc5) - tid->num -
			adapter->params.mc5.nfilters -
			adapter->params.mc5.nroutes;
		break;
	case GET_L2T_CAPACITY:
		*(unsigned int *)data = 2048;
		break;
	case GET_MTUS:
		mtup = data;
		mtup->size = NMTUS;
		mtup->mtus = adapter->params.mtus;
		break;
	case GET_IFF_FROM_MAC:
		iffmacp = data;
		iffmacp->dev = get_iff_from_mac(adapter, iffmacp->mac_addr,
					  iffmacp->vlan_tag & EVL_VLID_MASK);
		break;
	case GET_DDP_PARAMS:
		ddpp = data;
		ddpp->llimit = t3_read_reg(adapter, A_ULPRX_TDDP_LLIMIT);
		ddpp->ulimit = t3_read_reg(adapter, A_ULPRX_TDDP_ULIMIT);
		ddpp->tag_mask = t3_read_reg(adapter, A_ULPRX_TDDP_TAGMASK);
		break;
	case GET_PORTS:
		ports = data;
		ports->nports   = adapter->params.nports;
		for_each_port(adapter, port)
			ports->lldevs[port] = adapter->port[port].ifp;
		break;
	case FAILOVER:
		port = *(int *)data;
		t3_port_failover(adapter, port);
		failover_fixup(adapter, port);
		break;
	case FAILOVER_DONE:
		port = *(int *)data;
		t3_failover_done(adapter, port);
		break;
	case FAILOVER_CLEAR:
		t3_failover_clear(adapter);
		break;
	case GET_RX_PAGE_INFO:
		rx_page_info = data;
		rx_page_info->page_size = tp->rx_pg_size;
		rx_page_info->num = tp->rx_num_pgs;
		break;
	case ULP_ISCSI_GET_PARAMS:
	case ULP_ISCSI_SET_PARAMS:
		if (!offload_running(adapter))
			return (EAGAIN);
		return cxgb_ulp_iscsi_ctl(adapter, req, data);
	case RDMA_GET_PARAMS:
	case RDMA_CQ_OP:
	case RDMA_CQ_SETUP:
	case RDMA_CQ_DISABLE:
	case RDMA_CTRL_QP_SETUP:
	case RDMA_GET_MEM:
		if (!offload_running(adapter))
			return (EAGAIN);
		return cxgb_rdma_ctl(adapter, req, data);
	default:
		return (EOPNOTSUPP);
	}
	return 0;
}

/*
 * Dummy handler for Rx offload packets in case we get an offload packet before
 * proper processing is setup.  This complains and drops the packet as it isn't
 * normal to get offload packets at this stage.
 */
static int
rx_offload_blackhole(struct t3cdev *dev, struct mbuf **m, int n)
{
	while (n--)
		m_freem(m[n]);
	return 0;
}

static void
dummy_neigh_update(struct t3cdev *dev, struct rtentry *neigh, uint8_t *enaddr,
    struct sockaddr *sa)
{
}

void
cxgb_set_dummy_ops(struct t3cdev *dev)
{
	dev->recv         = rx_offload_blackhole;
	dev->arp_update = dummy_neigh_update;
}

/*
 * Free an active-open TID.
 */
void *
cxgb_free_atid(struct t3cdev *tdev, int atid)
{
	struct tid_info *t = &(T3C_DATA(tdev))->tid_maps;
	union active_open_entry *p = atid2entry(t, atid);
	void *ctx = p->toe_tid.ctx;

	mtx_lock(&t->atid_lock);
	p->next = t->afree;
	t->afree = p;
	t->atids_in_use--;
	mtx_unlock(&t->atid_lock);

	return ctx;
}

/*
 * Free a server TID and return it to the free pool.
 */
void
cxgb_free_stid(struct t3cdev *tdev, int stid)
{
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;
	union listen_entry *p = stid2entry(t, stid);

	mtx_lock(&t->stid_lock);
	p->next = t->sfree;
	t->sfree = p;
	t->stids_in_use--;
	mtx_unlock(&t->stid_lock);
}

/*
 * Free a server TID and return it to the free pool.
 */
void *
cxgb_get_lctx(struct t3cdev *tdev, int stid)
{
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;
	union listen_entry *p = stid2entry(t, stid);

	return (p->toe_tid.ctx);
}

void
cxgb_insert_tid(struct t3cdev *tdev, struct cxgb_client *client,
	void *ctx, unsigned int tid)
{
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;

	t->tid_tab[tid].client = client;
	t->tid_tab[tid].ctx = ctx;
	atomic_add_int(&t->tids_in_use, 1);
}

/*
 * Populate a TID_RELEASE WR.  The mbuf must be already propely sized.
 */
static inline void
mk_tid_release(struct mbuf *m, unsigned int tid)
{
	struct cpl_tid_release *req;

	m_set_priority(m, CPL_PRIORITY_SETUP);
	req = mtod(m, struct cpl_tid_release *);
	m->m_pkthdr.len = m->m_len = sizeof(*req);
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_TID_RELEASE, tid));
}

static void
t3_process_tid_release_list(void *data, int pending)
{
	struct mbuf *m;
	struct t3cdev *tdev = data;
	struct t3c_data *td = T3C_DATA (tdev);

	mtx_lock(&td->tid_release_lock);
	while (td->tid_release_list) {
		struct toe_tid_entry *p = td->tid_release_list;

		td->tid_release_list = (struct toe_tid_entry *)p->ctx;
		mtx_unlock(&td->tid_release_lock);
		m = m_get(M_WAIT, MT_DATA);
		mk_tid_release(m, p - td->tid_maps.tid_tab);
		cxgb_ofld_send(tdev, m);
		p->ctx = NULL;
		mtx_lock(&td->tid_release_lock);
	}
	mtx_unlock(&td->tid_release_lock);
}

/* use ctx as a next pointer in the tid release list */
void
cxgb_queue_tid_release(struct t3cdev *tdev, unsigned int tid)
{
	struct t3c_data *td = T3C_DATA (tdev);
	struct toe_tid_entry *p = &td->tid_maps.tid_tab[tid];

	printf("queuing tid release\n");
	
	mtx_lock(&td->tid_release_lock);
	p->ctx = td->tid_release_list;
	td->tid_release_list = p;

	if (!p->ctx)
		taskqueue_enqueue(tdev->adapter->tq, &td->tid_release_task);

	mtx_unlock(&td->tid_release_lock);
}

/*
 * Remove a tid from the TID table.  A client may defer processing its last
 * CPL message if it is locked at the time it arrives, and while the message
 * sits in the client's backlog the TID may be reused for another connection.
 * To handle this we atomically switch the TID association if it still points
 * to the original client context.
 */
void
cxgb_remove_tid(struct t3cdev *tdev, void *ctx, unsigned int tid)
{
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;

	if (tid >= t->ntids)
		panic("tid=%d >= t->ntids=%d", tid, t->ntids);
	
	if (tdev->type == T3A)
		atomic_cmpset_ptr((uintptr_t *)&t->tid_tab[tid].ctx, (long)NULL, (long)ctx);
	else {
		struct mbuf *m;

		m = m_get(M_NOWAIT, MT_DATA);
		if (__predict_true(m != NULL)) {
			mk_tid_release(m, tid);
			CTR1(KTR_CXGB, "releasing tid=%u", tid);
			
			cxgb_ofld_send(tdev, m);
			t->tid_tab[tid].ctx = NULL;
		} else
			cxgb_queue_tid_release(tdev, tid);
	}
	atomic_add_int(&t->tids_in_use, -1);
}

int
cxgb_alloc_atid(struct t3cdev *tdev, struct cxgb_client *client,
		     void *ctx)
{
	int atid = -1;
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;

	mtx_lock(&t->atid_lock);
	if (t->afree) {
		union active_open_entry *p = t->afree;

		atid = (p - t->atid_tab) + t->atid_base;
		t->afree = p->next;
		p->toe_tid.ctx = ctx;
		p->toe_tid.client = client;
		t->atids_in_use++;
	}
	mtx_unlock(&t->atid_lock);
	return atid;
}

int
cxgb_alloc_stid(struct t3cdev *tdev, struct cxgb_client *client,
		     void *ctx)
{
	int stid = -1;
	struct tid_info *t = &(T3C_DATA (tdev))->tid_maps;

	mtx_lock(&t->stid_lock);
	if (t->sfree) {
		union listen_entry *p = t->sfree;

		stid = (p - t->stid_tab) + t->stid_base;
		t->sfree = p->next;
		p->toe_tid.ctx = ctx;
		p->toe_tid.client = client;
		t->stids_in_use++;
	}
	mtx_unlock(&t->stid_lock);
	return stid;
}

static int
do_smt_write_rpl(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_smt_write_rpl *rpl = cplhdr(m);

	if (rpl->status != CPL_ERR_NONE)
		log(LOG_ERR,
		       "Unexpected SMT_WRITE_RPL status %u for entry %u\n",
		       rpl->status, GET_TID(rpl));

	return CPL_RET_BUF_DONE;
}

static int
do_l2t_write_rpl(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_l2t_write_rpl *rpl = cplhdr(m);

	if (rpl->status != CPL_ERR_NONE)
		log(LOG_ERR,
		       "Unexpected L2T_WRITE_RPL status %u for entry %u\n",
		       rpl->status, GET_TID(rpl));

	return CPL_RET_BUF_DONE;
}

static int
do_rte_write_rpl(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_rte_write_rpl *rpl = cplhdr(m);

	if (rpl->status != CPL_ERR_NONE)
		log(LOG_ERR,
		       "Unexpected L2T_WRITE_RPL status %u for entry %u\n",
		       rpl->status, GET_TID(rpl));

	return CPL_RET_BUF_DONE;
}

static int
do_act_open_rpl(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_act_open_rpl *rpl = cplhdr(m);
	unsigned int atid = G_TID(ntohl(rpl->atid));
	struct toe_tid_entry *toe_tid;

	toe_tid = lookup_atid(&(T3C_DATA (dev))->tid_maps, atid);
	if (toe_tid->ctx && toe_tid->client && toe_tid->client->handlers &&
		toe_tid->client->handlers[CPL_ACT_OPEN_RPL]) {
		return toe_tid->client->handlers[CPL_ACT_OPEN_RPL] (dev, m,
			toe_tid->ctx);
	} else {
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, CPL_ACT_OPEN_RPL);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
}

static int
do_stid_rpl(struct t3cdev *dev, struct mbuf *m)
{
	union opcode_tid *p = cplhdr(m);
	unsigned int stid = G_TID(ntohl(p->opcode_tid));
	struct toe_tid_entry *toe_tid;

	toe_tid = lookup_stid(&(T3C_DATA (dev))->tid_maps, stid);
	if (toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[p->opcode]) {
		return toe_tid->client->handlers[p->opcode] (dev, m, toe_tid->ctx);
	} else {
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, p->opcode);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
}

static int
do_hwtid_rpl(struct t3cdev *dev, struct mbuf *m)
{
	union opcode_tid *p = cplhdr(m);
	unsigned int hwtid;
	struct toe_tid_entry *toe_tid;
	
	DPRINTF("do_hwtid_rpl opcode=0x%x\n", p->opcode);
	hwtid = G_TID(ntohl(p->opcode_tid));

	toe_tid = lookup_tid(&(T3C_DATA (dev))->tid_maps, hwtid);
	if (toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[p->opcode]) {
		return toe_tid->client->handlers[p->opcode]
						(dev, m, toe_tid->ctx);
	} else {
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, p->opcode);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
}

static int
do_cr(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_pass_accept_req *req = cplhdr(m);
	unsigned int stid = G_PASS_OPEN_TID(ntohl(req->tos_tid));
	struct toe_tid_entry *toe_tid;

	toe_tid = lookup_stid(&(T3C_DATA (dev))->tid_maps, stid);
	if (toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[CPL_PASS_ACCEPT_REQ]) {
		return toe_tid->client->handlers[CPL_PASS_ACCEPT_REQ]
						(dev, m, toe_tid->ctx);
	} else {
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, CPL_PASS_ACCEPT_REQ);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
}

static int
do_abort_req_rss(struct t3cdev *dev, struct mbuf *m)
{
	union opcode_tid *p = cplhdr(m);
	unsigned int hwtid = G_TID(ntohl(p->opcode_tid));
	struct toe_tid_entry *toe_tid;

	toe_tid = lookup_tid(&(T3C_DATA (dev))->tid_maps, hwtid);
	if (toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[p->opcode]) {
		return toe_tid->client->handlers[p->opcode]
						(dev, m, toe_tid->ctx);
	} else {
		struct cpl_abort_req_rss *req = cplhdr(m);
		struct cpl_abort_rpl *rpl;
		
		struct mbuf *m = m_get(M_NOWAIT, MT_DATA);
		if (!m) {
			log(LOG_NOTICE, "do_abort_req_rss: couldn't get mbuf!\n");
			goto out;
		}

		m_set_priority(m, CPL_PRIORITY_DATA);
#if 0	
		__skb_put(skb, sizeof(struct cpl_abort_rpl));
#endif		
		rpl = cplhdr(m);
		rpl->wr.wr_hi = 
			htonl(V_WR_OP(FW_WROPCODE_OFLD_HOST_ABORT_CON_RPL));
		rpl->wr.wr_lo = htonl(V_WR_TID(GET_TID(req)));
		OPCODE_TID(rpl) =
			htonl(MK_OPCODE_TID(CPL_ABORT_RPL, GET_TID(req)));
		rpl->cmd = req->status;
		cxgb_ofld_send(dev, m);
 out:
		return CPL_RET_BUF_DONE;
	}
}

static int
do_act_establish(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_act_establish *req;
	unsigned int atid;
	struct toe_tid_entry *toe_tid;

	req = cplhdr(m);
	atid = G_PASS_OPEN_TID(ntohl(req->tos_tid));
	toe_tid = lookup_atid(&(T3C_DATA (dev))->tid_maps, atid);
	if (toe_tid && toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[CPL_ACT_ESTABLISH]) {
		printf("active establish callback\n");
		
		return toe_tid->client->handlers[CPL_ACT_ESTABLISH]
						(dev, m, toe_tid->ctx);
	} else {
		printf("toe_tid=%p\n", toe_tid);
	
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, CPL_PASS_ACCEPT_REQ);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
}



static int
do_set_tcb_rpl(struct t3cdev *dev, struct mbuf *m)
{
	struct cpl_set_tcb_rpl *rpl = cplhdr(m);

	if (rpl->status != CPL_ERR_NONE)
		log(LOG_ERR,
		    "Unexpected SET_TCB_RPL status %u for tid %u\n",
			rpl->status, GET_TID(rpl));
	return CPL_RET_BUF_DONE;
}

static int
do_trace(struct t3cdev *dev, struct mbuf *m)
{
#if 0
	struct cpl_trace_pkt *p = cplhdr(m);


	skb->protocol = 0xffff;
	skb->dev = dev->lldev;
	skb_pull(skb, sizeof(*p));
	skb->mac.raw = mtod(m, (char *));
	netif_receive_skb(skb);
#endif	
	return 0;
}

static int
do_term(struct t3cdev *dev, struct mbuf *m)
{
	unsigned int hwtid = ntohl(m_get_priority(m)) >> 8 & 0xfffff;
	unsigned int opcode = G_OPCODE(ntohl(m->m_pkthdr.csum_data));
	struct toe_tid_entry *toe_tid;

	toe_tid = lookup_tid(&(T3C_DATA (dev))->tid_maps, hwtid);
	if (toe_tid->ctx && toe_tid->client->handlers &&
		toe_tid->client->handlers[opcode]) {
		return toe_tid->client->handlers[opcode](dev, m, toe_tid->ctx);
	} else {
		log(LOG_ERR, "%s: received clientless CPL command 0x%x\n",
			dev->name, opcode);
		return CPL_RET_BUF_DONE | CPL_RET_BAD_MSG;
	}
	return (0);
}

static void
cxgb_arp_update_event(void *unused, struct rtentry *rt0,
    uint8_t *enaddr, struct sockaddr *sa)
{

	if (!is_offloading(rt0->rt_ifp))
		return;

	RT_ADDREF(rt0);
	RT_UNLOCK(rt0);
	cxgb_neigh_update(rt0, enaddr, sa);
	RT_LOCK(rt0);
	RT_REMREF(rt0);
}


static void
cxgb_redirect_event(void *unused, int event, struct rtentry *rt0,
    struct rtentry *rt1, struct sockaddr *sa)
{
	/* 
	 * ignore events on non-offloaded interfaces
	 */
	if (!is_offloading(rt0->rt_ifp))
		return;

	/*
	 * Cannot redirect to non-offload device.
	 */
	if (!is_offloading(rt1->rt_ifp)) {
		log(LOG_WARNING, "%s: Redirect to non-offload"
		    "device ignored.\n", __FUNCTION__);
		return;
	}

        /*
	 * avoid LORs by dropping the route lock but keeping a reference
	 * 
	 */
	RT_ADDREF(rt0);
	RT_UNLOCK(rt0);
	RT_ADDREF(rt1);
	RT_UNLOCK(rt1);
	
	cxgb_redirect(rt0, rt1, sa);
	cxgb_neigh_update(rt1, NULL, sa);

	RT_LOCK(rt0);
	RT_REMREF(rt0);
	RT_LOCK(rt1);
	RT_REMREF(rt1);
}

/*
 * Process a received packet with an unknown/unexpected CPL opcode.
 */
static int
do_bad_cpl(struct t3cdev *dev, struct mbuf *m)
{
	log(LOG_ERR, "%s: received bad CPL command 0x%x\n", dev->name,
	    0xFF & *mtod(m, uint32_t *));
	return (CPL_RET_BUF_DONE | CPL_RET_BAD_MSG);
}

/*
 * Handlers for each CPL opcode
 */
static cpl_handler_func cpl_handlers[256];

/*
 * Add a new handler to the CPL dispatch table.  A NULL handler may be supplied
 * to unregister an existing handler.
 */
void
t3_register_cpl_handler(unsigned int opcode, cpl_handler_func h)
{
	if (opcode < NUM_CPL_CMDS)
		cpl_handlers[opcode] = h ? h : do_bad_cpl;
	else
		log(LOG_ERR, "T3C: handler registration for "
		       "opcode %x failed\n", opcode);
}

/*
 * T3CDEV's receive method.
 */
int
process_rx(struct t3cdev *dev, struct mbuf **m, int n)
{
	while (n--) {
		struct mbuf *m0 = *m++;
		unsigned int opcode = G_OPCODE(ntohl(m0->m_pkthdr.csum_data));
		int ret;

		DPRINTF("processing op=0x%x m=%p data=%p\n", opcode, m0, m0->m_data);
		
		ret = cpl_handlers[opcode] (dev, m0);

#if VALIDATE_TID
		if (ret & CPL_RET_UNKNOWN_TID) {
			union opcode_tid *p = cplhdr(m0);

			log(LOG_ERR, "%s: CPL message (opcode %u) had "
			       "unknown TID %u\n", dev->name, opcode,
			       G_TID(ntohl(p->opcode_tid)));
		}
#endif
		if (ret & CPL_RET_BUF_DONE)
			m_freem(m0);
	}
	return 0;
}

/*
 * Sends an sk_buff to a T3C driver after dealing with any active network taps.
 */
int
cxgb_ofld_send(struct t3cdev *dev, struct mbuf *m)
{
	int r;

	r = dev->send(dev, m);
	return r;
}

/**
 * cxgb_ofld_recv - process n received offload packets
 * @dev: the offload device
 * @m: an array of offload packets
 * @n: the number of offload packets
 *
 * Process an array of ingress offload packets.  Each packet is forwarded
 * to any active network taps and then passed to the offload device's receive
 * method.  We optimize passing packets to the receive method by passing
 * it the whole array at once except when there are active taps.
 */
int
cxgb_ofld_recv(struct t3cdev *dev, struct mbuf **m, int n)
{

#if defined(CONFIG_CHELSIO_T3)
	if (likely(!netdev_nit))
		return dev->recv(dev, skb, n);

	for ( ; n; n--, skb++) {
		skb[0]->dev = dev->lldev;
		dev_queue_xmit_nit(skb[0], dev->lldev);
		skb[0]->dev = NULL;
		dev->recv(dev, skb, 1);
	}
	return 0;
#else
	return dev->recv(dev, m, n);
#endif
}

void
cxgb_neigh_update(struct rtentry *rt, uint8_t *enaddr, struct sockaddr *sa)
{

	if (rt->rt_ifp && is_offloading(rt->rt_ifp) && (rt->rt_ifp->if_flags & IFCAP_TOE)) {
		struct t3cdev *tdev = T3CDEV(rt->rt_ifp);

		PANIC_IF(!tdev);
		t3_l2t_update(tdev, rt, enaddr, sa);
	}
}

static void
set_l2t_ix(struct t3cdev *tdev, u32 tid, struct l2t_entry *e)
{
	struct mbuf *m;
	struct cpl_set_tcb_field *req;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (!m) {
		log(LOG_ERR, "%s: cannot allocate mbuf!\n", __FUNCTION__);
		return;
	}
	
	m_set_priority(m, CPL_PRIORITY_CONTROL);
	req = mtod(m, struct cpl_set_tcb_field *);
	m->m_pkthdr.len = m->m_len = sizeof(*req);
	
	req->wr.wr_hi = htonl(V_WR_OP(FW_WROPCODE_FORWARD));
	OPCODE_TID(req) = htonl(MK_OPCODE_TID(CPL_SET_TCB_FIELD, tid));
	req->reply = 0;
	req->cpu_idx = 0;
	req->word = htons(W_TCB_L2T_IX);
	req->mask = htobe64(V_TCB_L2T_IX(M_TCB_L2T_IX));
	req->val = htobe64(V_TCB_L2T_IX(e->idx));
	tdev->send(tdev, m);
}

void
cxgb_redirect(struct rtentry *old, struct rtentry *new, struct sockaddr *sa)
{
	struct ifnet *olddev, *newdev;
	struct tid_info *ti;
	struct t3cdev *tdev;
	u32 tid;
	int update_tcb;
	struct l2t_entry *e;
	struct toe_tid_entry *te;

	olddev = old->rt_ifp;
	newdev = new->rt_ifp;
	if (!is_offloading(olddev))
		return;
	if (!is_offloading(newdev)) {
		log(LOG_WARNING, "%s: Redirect to non-offload"
		    "device ignored.\n", __FUNCTION__);
		return;
	}
	tdev = T3CDEV(olddev);
	PANIC_IF(!tdev);
	if (tdev != T3CDEV(newdev)) {
		log(LOG_WARNING, "%s: Redirect to different "
		    "offload device ignored.\n", __FUNCTION__);
		return;
	}

	/* Add new L2T entry */
	e = t3_l2t_get(tdev, new, new->rt_ifp, sa);
	if (!e) {
		log(LOG_ERR, "%s: couldn't allocate new l2t entry!\n",
		       __FUNCTION__);
		return;
	}

	/* Walk tid table and notify clients of dst change. */
	ti = &(T3C_DATA (tdev))->tid_maps;
	for (tid=0; tid < ti->ntids; tid++) {
		te = lookup_tid(ti, tid);
		PANIC_IF(!te);
		if (te->ctx && te->client && te->client->redirect) {
			update_tcb = te->client->redirect(te->ctx, old, new,
							  e);
			if (update_tcb)  {
				l2t_hold(L2DATA(tdev), e);
				set_l2t_ix(tdev, tid, e);
			}
		}
	}
	l2t_release(L2DATA(tdev), e);
}

/*
 * Allocate a chunk of memory using kmalloc or, if that fails, vmalloc.
 * The allocated memory is cleared.
 */
void *
cxgb_alloc_mem(unsigned long size)
{

	return malloc(size, M_CXGB, M_ZERO|M_NOWAIT);
}

/*
 * Free memory allocated through t3_alloc_mem().
 */
void
cxgb_free_mem(void *addr)
{
	free(addr, M_CXGB);
}

/*
 * Allocate and initialize the TID tables.  Returns 0 on success.
 */
static int
init_tid_tabs(struct tid_info *t, unsigned int ntids,
			 unsigned int natids, unsigned int nstids,
			 unsigned int atid_base, unsigned int stid_base)
{
	unsigned long size = ntids * sizeof(*t->tid_tab) +
	    natids * sizeof(*t->atid_tab) + nstids * sizeof(*t->stid_tab);

	t->tid_tab = cxgb_alloc_mem(size);
	if (!t->tid_tab)
		return (ENOMEM);

	t->stid_tab = (union listen_entry *)&t->tid_tab[ntids];
	t->atid_tab = (union active_open_entry *)&t->stid_tab[nstids];
	t->ntids = ntids;
	t->nstids = nstids;
	t->stid_base = stid_base;
	t->sfree = NULL;
	t->natids = natids;
	t->atid_base = atid_base;
	t->afree = NULL;
	t->stids_in_use = t->atids_in_use = 0;
	atomic_set_int(&t->tids_in_use, 0);
	mtx_init(&t->stid_lock, "stid", NULL, MTX_DUPOK|MTX_DEF);
	mtx_init(&t->atid_lock, "atid", NULL, MTX_DUPOK|MTX_DEF);

	/*
	 * Setup the free lists for stid_tab and atid_tab.
	 */
	if (nstids) {
		while (--nstids)
			t->stid_tab[nstids - 1].next = &t->stid_tab[nstids];
		t->sfree = t->stid_tab;
	}
	if (natids) {
		while (--natids)
			t->atid_tab[natids - 1].next = &t->atid_tab[natids];
		t->afree = t->atid_tab;
	}
	return 0;
}

static void
free_tid_maps(struct tid_info *t)
{
	mtx_destroy(&t->stid_lock);
	mtx_destroy(&t->atid_lock);
	cxgb_free_mem(t->tid_tab);
}

static inline void
add_adapter(adapter_t *adap)
{
	rw_wlock(&adapter_list_lock);
	TAILQ_INSERT_TAIL(&adapter_list, adap, adapter_entry);
	rw_wunlock(&adapter_list_lock);
}

static inline void
remove_adapter(adapter_t *adap)
{
	rw_wlock(&adapter_list_lock);
	TAILQ_REMOVE(&adapter_list, adap, adapter_entry);
	rw_wunlock(&adapter_list_lock);
}

int
cxgb_offload_activate(struct adapter *adapter)
{
	struct t3cdev *dev = &adapter->tdev;
	int natids, err;
	struct t3c_data *t;
	struct tid_range stid_range, tid_range;
	struct mtutab mtutab;
	unsigned int l2t_capacity;

	t = malloc(sizeof(*t), M_CXGB, M_WAITOK|M_ZERO);
	if (!t)
		return (ENOMEM);

	dev->adapter = adapter;

	err = (EOPNOTSUPP);
	if (dev->ctl(dev, GET_TX_MAX_CHUNK, &t->tx_max_chunk) < 0 ||
	    dev->ctl(dev, GET_MAX_OUTSTANDING_WR, &t->max_wrs) < 0 ||
	    dev->ctl(dev, GET_L2T_CAPACITY, &l2t_capacity) < 0 ||
	    dev->ctl(dev, GET_MTUS, &mtutab) < 0 ||
	    dev->ctl(dev, GET_TID_RANGE, &tid_range) < 0 ||
	    dev->ctl(dev, GET_STID_RANGE, &stid_range) < 0) {
		device_printf(adapter->dev, "%s: dev->ctl check failed\n", __FUNCTION__);
		goto out_free;
	}
      
	err = (ENOMEM);
	L2DATA(dev) = t3_init_l2t(l2t_capacity);
	if (!L2DATA(dev)) {
		device_printf(adapter->dev, "%s: t3_init_l2t failed\n", __FUNCTION__);
		goto out_free;
	}
	natids = min(tid_range.num / 2, MAX_ATIDS);
	err = init_tid_tabs(&t->tid_maps, tid_range.num, natids,
			    stid_range.num, ATID_BASE, stid_range.base);
	if (err) {	
		device_printf(adapter->dev, "%s: init_tid_tabs failed\n", __FUNCTION__);
		goto out_free_l2t;
	}
	
	t->mtus = mtutab.mtus;
	t->nmtus = mtutab.size;

	TASK_INIT(&t->tid_release_task, 0 /* XXX? */, t3_process_tid_release_list, dev);
	mtx_init(&t->tid_release_lock, "tid release", NULL, MTX_DUPOK|MTX_DEF);
	t->dev = dev;

	T3C_DATA (dev) = t;
	dev->recv = process_rx;
	dev->arp_update = t3_l2t_update;
#if 0
	offload_proc_dev_setup(dev);
#endif	
	/* Register netevent handler once */
	if (TAILQ_EMPTY(&adapter_list)) {
#if defined(CONFIG_CHELSIO_T3_MODULE)
		if (prepare_arp_with_t3core())
			log(LOG_ERR, "Unable to set offload capabilities\n");
#endif
	}
	CTR1(KTR_CXGB, "adding adapter %p", adapter); 
	add_adapter(adapter);
	device_printf(adapter->dev, "offload started\n");
	adapter->flags |= CXGB_OFLD_INIT;
#if 0
	printf("failing as test\n");
	return (ENOMEM);
#endif
	return (0);

out_free_l2t:
	t3_free_l2t(L2DATA(dev));
	L2DATA(dev) = NULL;
out_free:
	free(t, M_CXGB);
	return (err);

}

void
cxgb_offload_deactivate(struct adapter *adapter)
{
	struct t3cdev *tdev = &adapter->tdev;
	struct t3c_data *t = T3C_DATA(tdev);

	printf("removing adapter %p\n", adapter);
	remove_adapter(adapter);
	if (TAILQ_EMPTY(&adapter_list)) {
#if defined(CONFIG_CHELSIO_T3_MODULE)
		restore_arp_sans_t3core();
#endif
	}
	free_tid_maps(&t->tid_maps);
	T3C_DATA(tdev) = NULL;
	t3_free_l2t(L2DATA(tdev));
	L2DATA(tdev) = NULL;
	mtx_destroy(&t->tid_release_lock);
	free(t, M_CXGB);
}


static inline void
register_tdev(struct t3cdev *tdev)
{
	static int unit;

	mtx_lock(&cxgb_db_lock);
	snprintf(tdev->name, sizeof(tdev->name), "ofld_dev%d", unit++);
	TAILQ_INSERT_TAIL(&ofld_dev_list, tdev, entry);
	mtx_unlock(&cxgb_db_lock);
}

static inline void
unregister_tdev(struct t3cdev *tdev)
{
	mtx_lock(&cxgb_db_lock);
	TAILQ_REMOVE(&ofld_dev_list, tdev, entry);
	mtx_unlock(&cxgb_db_lock);	
}

static __inline int
adap2type(struct adapter *adapter) 
{ 
        int type = 0; 
 
        switch (adapter->params.rev) { 
        case T3_REV_A: 
                type = T3A; 
                break; 
        case T3_REV_B: 
        case T3_REV_B2: 
                type = T3B; 
                break; 
        case T3_REV_C: 
                type = T3C; 
                break; 
        } 
        return type; 
} 
                 
void
cxgb_adapter_ofld(struct adapter *adapter)
{
	struct t3cdev *tdev = &adapter->tdev;

	cxgb_set_dummy_ops(tdev);
	tdev->send = t3_offload_tx;
	tdev->ctl = cxgb_offload_ctl;
	tdev->type = adap2type(adapter);
	
	register_tdev(tdev);
#if 0	
	offload_proc_dev_init(tdev);
#endif	
}

void
cxgb_adapter_unofld(struct adapter *adapter)
{
	struct t3cdev *tdev = &adapter->tdev;
#if 0
	offload_proc_dev_cleanup(tdev);
	offload_proc_dev_exit(tdev);
#endif	
	tdev->recv = NULL;
	tdev->arp_update = NULL;

	unregister_tdev(tdev);
}

void
cxgb_offload_init(void)
{
	int i;

	if (inited++)
		return;
	
	mtx_init(&cxgb_db_lock, "ofld db", NULL, MTX_DEF);
	rw_init(&adapter_list_lock, "ofld adap list");
	TAILQ_INIT(&client_list);
	TAILQ_INIT(&ofld_dev_list);
	TAILQ_INIT(&adapter_list);
	
	for (i = 0; i < 0x100; ++i)
		cpl_handlers[i] = do_bad_cpl;
	
	t3_register_cpl_handler(CPL_SMT_WRITE_RPL, do_smt_write_rpl);
	t3_register_cpl_handler(CPL_RTE_WRITE_RPL, do_rte_write_rpl);
	t3_register_cpl_handler(CPL_L2T_WRITE_RPL, do_l2t_write_rpl);
	t3_register_cpl_handler(CPL_PASS_OPEN_RPL, do_stid_rpl);
	t3_register_cpl_handler(CPL_CLOSE_LISTSRV_RPL, do_stid_rpl);
	t3_register_cpl_handler(CPL_PASS_ACCEPT_REQ, do_cr);
	t3_register_cpl_handler(CPL_PASS_ESTABLISH, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_ABORT_RPL_RSS, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_ABORT_RPL, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_RX_URG_NOTIFY, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_RX_DATA, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_TX_DATA_ACK, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_TX_DMA_ACK, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_ACT_OPEN_RPL, do_act_open_rpl);
	t3_register_cpl_handler(CPL_PEER_CLOSE, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_CLOSE_CON_RPL, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_ABORT_REQ_RSS, do_abort_req_rss);
	t3_register_cpl_handler(CPL_ACT_ESTABLISH, do_act_establish);
	t3_register_cpl_handler(CPL_SET_TCB_RPL, do_set_tcb_rpl);
	t3_register_cpl_handler(CPL_RDMA_TERMINATE, do_term);
	t3_register_cpl_handler(CPL_RDMA_EC_STATUS, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_TRACE_PKT, do_trace);
	t3_register_cpl_handler(CPL_RX_DATA_DDP, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_RX_DDP_COMPLETE, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_ISCSI_HDR, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_GET_TCB_RPL, do_hwtid_rpl);
	t3_register_cpl_handler(CPL_SET_TCB_RPL, do_hwtid_rpl);
	
	EVENTHANDLER_REGISTER(route_arp_update_event, cxgb_arp_update_event,
	    NULL, EVENTHANDLER_PRI_ANY);
	EVENTHANDLER_REGISTER(route_redirect_event, cxgb_redirect_event,
	    NULL, EVENTHANDLER_PRI_ANY);
	
#if 0
       if (offload_proc_init())
	       log(LOG_WARNING, "Unable to create /proc/net/cxgb3 dir\n");
#endif
}

void 
cxgb_offload_exit(void)
{

	if (--inited)
		return;

	mtx_destroy(&cxgb_db_lock);
	rw_destroy(&adapter_list_lock);
#if 0	
	offload_proc_cleanup();
#endif	
}

MODULE_VERSION(if_cxgb, 1);
