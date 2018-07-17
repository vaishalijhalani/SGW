/*******************************************************************************
 *
 * Intel(R) 40-10 Gigabit Ethernet Connection Network Driver
 * Copyright(c) 2013 - 2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifdef CONFIG_DEBUG_FS

#include <linux/fs.h>
#include <linux/debugfs.h>

#include "i40e.h"

static struct dentry *i40e_dbg_root;

/**
 * i40e_dbg_find_vsi - searches for the vsi with the given seid
 * @pf - the PF structure to search for the vsi
 * @seid - seid of the vsi it is searching for
 **/
static struct i40e_vsi *i40e_dbg_find_vsi(struct i40e_pf *pf, int seid)
{
	int i;

	if (seid < 0)
		dev_info(&pf->pdev->dev, "%d: bad seid\n", seid);
	else
		for (i = 0; i < pf->num_alloc_vsi; i++)
			if (pf->vsi[i] && (pf->vsi[i]->seid == seid))
				return pf->vsi[i];

	return NULL;
}

/**
 * i40e_dbg_find_veb - searches for the veb with the given seid
 * @pf - the PF structure to search for the veb
 * @seid - seid of the veb it is searching for
 **/
static struct i40e_veb *i40e_dbg_find_veb(struct i40e_pf *pf, int seid)
{
	int i;

	for (i = 0; i < I40E_MAX_VEB; i++)
		if (pf->veb[i] && pf->veb[i]->seid == seid)
			return pf->veb[i];
	return NULL;
}

/**************************************************************
 * command
 * The command entry in debugfs is for giving the driver commands
 * to be executed - these may be for changing the internal switch
 * setup, adding or removing filters, or other things.  Many of
 * these will be useful for some forms of unit testing.
 **************************************************************/
static char i40e_dbg_command_buf[256] = "";

/**
 * i40e_dbg_command_read - read for command datum
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_command_read(struct file *filp, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	size_t buf_size = 256;
	char *buf;
	int len;

	/* don't allow partial reads */
	if (*ppos != 0)
		return 0;
	if (count < buf_size)
		return -ENOSPC;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOSPC;

	len = snprintf(buf, buf_size, "%s: %s\n",
		       pf->vsi[pf->lan_vsi]->netdev->name,
		       i40e_dbg_command_buf);

	bytes_not_copied = copy_to_user(buffer, buf, len);
	kfree(buf);

	if (bytes_not_copied)
		return -EFAULT;

	*ppos = len;
	return len;
}

static char *i40e_filter_state_string[] = {
	"INVALID",
	"NEW",
	"ACTIVE",
	"FAILED",
	"REMOVE",
};

/**
 * i40e_dbg_dump_vsi_filters - handles dump of mac/vlan filters for a VSI
 * @pf: the i40e_pf created in command write
 * @vsi: the vsi to dump
 */
static void i40e_dbg_dump_vsi_filters(struct i40e_pf *pf, struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f;
	int bkt;

	hash_for_each(vsi->mac_filter_hash, bkt, f, hlist) {
		dev_info(&pf->pdev->dev,
			 "    mac_filter_hash: %pM vid=%d, state %s\n",
			 f->macaddr, f->vlan,
			 i40e_filter_state_string[f->state]);
	}
	dev_info(&pf->pdev->dev, "    active_filters %u, promisc_threshold %u, overflow promisc %s\n",
		 vsi->active_filters, vsi->promisc_threshold,
		 (test_bit(__I40E_FILTER_OVERFLOW_PROMISC, &vsi->state) ?
		  "ON" : "OFF"));
}

/**
 * i40e_dbg_dump_all_vsi_filters - dump mac/vlan filters for all VSI on a PF
 * @pf: the i40e_pf created in command write
 */
static void i40e_dbg_dump_all_vsi_filters(struct i40e_pf *pf)
{
	int i;

	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i]) {
			dev_info(&pf->pdev->dev, "vsi seid %d\n",
				 pf->vsi[i]->seid);
			i40e_dbg_dump_vsi_filters(pf, pf->vsi[i]);
		}
}

/**
 * i40e_dbg_dump_vsi_seid - handles dump vsi seid write into command datum
 * @pf: the i40e_pf created in command write
 * @seid: the seid the user put in
 **/
static void i40e_dbg_dump_vsi_seid(struct i40e_pf *pf, int seid)
{
#ifdef HAVE_NDO_GET_STATS64
	struct rtnl_link_stats64 *nstat;
#else
	struct net_device_stats *nstat;
#endif
	struct i40e_vsi *vsi;
	int i;

	vsi = i40e_dbg_find_vsi(pf, seid);
	if (!vsi) {
		dev_info(&pf->pdev->dev,
			 "dump %d: seid not found\n", seid);
		return;
	}
	dev_info(&pf->pdev->dev, "vsi seid %d\n", seid);
	if (vsi->netdev) {
		struct net_device *nd = vsi->netdev;
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
		u32 hw_features;
#endif

		dev_info(&pf->pdev->dev, "    netdev: name = %s, state = %lu, flags = 0x%08x\n",
			 nd->name, nd->state, nd->flags);
		dev_info(&pf->pdev->dev, "        features      = 0x%08lx\n",
			 (unsigned long int)nd->features);
#ifdef HAVE_NDO_SET_FEATURES
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
		hw_features = get_netdev_hw_features(vsi->netdev);
		dev_info(&pf->pdev->dev, "        hw_features   = 0x%08x\n",
			 hw_features);
#else
		dev_info(&pf->pdev->dev, "        hw_features   = 0x%08lx\n",
			 (unsigned long int)nd->hw_features);
#endif
#endif
#ifdef HAVE_NETDEV_VLAN_FEATURES
		dev_info(&pf->pdev->dev, "        vlan_features = 0x%08lx\n",
			 (unsigned long int)nd->vlan_features);
#endif
	}
#ifdef HAVE_VLAN_RX_REGISTER
	if (vsi->vlgrp)
		dev_info(&pf->pdev->dev,
			 "    vlgrp: & = %p\n", vsi->vlgrp);
#else
	dev_info(&pf->pdev->dev,
		 "    vlgrp: & = %p\n", vsi->active_vlans);
#endif /* HAVE_VLAN_RX_REGISTER */
	dev_info(&pf->pdev->dev,
		 "    state = %li flags = 0x%08lx, netdev_registered = %i, current_netdev_flags = 0x%04x\n",
		 vsi->state, vsi->flags,
		 vsi->netdev_registered, vsi->current_netdev_flags);
	if (vsi == pf->vsi[pf->lan_vsi])
		dev_info(&pf->pdev->dev, "    MAC address: %pM SAN MAC: %pM Port MAC: %pM\n",
			 pf->hw.mac.addr,
			 pf->hw.mac.san_addr,
			 pf->hw.mac.port_addr);
	i40e_dbg_dump_vsi_filters(pf, vsi);
	nstat = i40e_get_vsi_stats_struct(vsi);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_packets = %lu, rx_bytes = %lu, rx_errors = %lu, rx_dropped = %lu\n",
		 (unsigned long int)nstat->rx_packets,
		 (unsigned long int)nstat->rx_bytes,
		 (unsigned long int)nstat->rx_errors,
		 (unsigned long int)nstat->rx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_packets = %lu, tx_bytes = %lu, tx_errors = %lu, tx_dropped = %lu\n",
		 (unsigned long int)nstat->tx_packets,
		 (unsigned long int)nstat->tx_bytes,
		 (unsigned long int)nstat->tx_errors,
		 (unsigned long int)nstat->tx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats: multicast = %lu, collisions = %lu\n",
		 (unsigned long int)nstat->multicast,
		 (unsigned long int)nstat->collisions);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_length_errors = %lu, rx_over_errors = %lu, rx_crc_errors = %lu\n",
		 (unsigned long int)nstat->rx_length_errors,
		 (unsigned long int)nstat->rx_over_errors,
		 (unsigned long int)nstat->rx_crc_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_frame_errors = %lu, rx_fifo_errors = %lu, rx_missed_errors = %lu\n",
		 (unsigned long int)nstat->rx_frame_errors,
		 (unsigned long int)nstat->rx_fifo_errors,
		 (unsigned long int)nstat->rx_missed_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_aborted_errors = %lu, tx_carrier_errors = %lu, tx_fifo_errors = %lu\n",
		 (unsigned long int)nstat->tx_aborted_errors,
		 (unsigned long int)nstat->tx_carrier_errors,
		 (unsigned long int)nstat->tx_fifo_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: tx_heartbeat_errors = %lu, tx_window_errors = %lu\n",
		 (unsigned long int)nstat->tx_heartbeat_errors,
		 (unsigned long int)nstat->tx_window_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats: rx_compressed = %lu, tx_compressed = %lu\n",
		 (unsigned long int)nstat->rx_compressed,
		 (unsigned long int)nstat->tx_compressed);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_packets = %lu, rx_bytes = %lu, rx_errors = %lu, rx_dropped = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.rx_packets,
		 (unsigned long int)vsi->net_stats_offsets.rx_bytes,
		 (unsigned long int)vsi->net_stats_offsets.rx_errors,
		 (unsigned long int)vsi->net_stats_offsets.rx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_packets = %lu, tx_bytes = %lu, tx_errors = %lu, tx_dropped = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.tx_packets,
		 (unsigned long int)vsi->net_stats_offsets.tx_bytes,
		 (unsigned long int)vsi->net_stats_offsets.tx_errors,
		 (unsigned long int)vsi->net_stats_offsets.tx_dropped);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: multicast = %lu, collisions = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.multicast,
		 (unsigned long int)vsi->net_stats_offsets.collisions);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_length_errors = %lu, rx_over_errors = %lu, rx_crc_errors = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.rx_length_errors,
		 (unsigned long int)vsi->net_stats_offsets.rx_over_errors,
		 (unsigned long int)vsi->net_stats_offsets.rx_crc_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_frame_errors = %lu, rx_fifo_errors = %lu, rx_missed_errors = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.rx_frame_errors,
		 (unsigned long int)vsi->net_stats_offsets.rx_fifo_errors,
		 (unsigned long int)vsi->net_stats_offsets.rx_missed_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_aborted_errors = %lu, tx_carrier_errors = %lu, tx_fifo_errors = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.tx_aborted_errors,
		 (unsigned long int)vsi->net_stats_offsets.tx_carrier_errors,
		 (unsigned long int)vsi->net_stats_offsets.tx_fifo_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: tx_heartbeat_errors = %lu, tx_window_errors = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.tx_heartbeat_errors,
		 (unsigned long int)vsi->net_stats_offsets.tx_window_errors);
	dev_info(&pf->pdev->dev,
		 "    net_stats_offsets: rx_compressed = %lu, tx_compressed = %lu\n",
		 (unsigned long int)vsi->net_stats_offsets.rx_compressed,
		 (unsigned long int)vsi->net_stats_offsets.tx_compressed);
	dev_info(&pf->pdev->dev,
		 "    tx_restart = %d, tx_busy = %d, rx_buf_failed = %d, rx_page_failed = %d\n",
		 vsi->tx_restart, vsi->tx_busy,
		 vsi->rx_buf_failed, vsi->rx_page_failed);
	rcu_read_lock();
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		struct i40e_ring *rx_ring = ACCESS_ONCE(vsi->rx_rings[i]);

		if (!rx_ring)
			continue;

		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: desc = %p\n",
			 i, rx_ring->desc);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: dev = %p, netdev = %p, rx_bi = %p\n",
			 i, rx_ring->dev,
			 rx_ring->netdev,
			 rx_ring->rx_bi);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: state = %li, queue_index = %d, reg_idx = %d\n",
			 i, rx_ring->state,
			 rx_ring->queue_index,
			 rx_ring->reg_idx);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_buf_len = %d\n",
			 i, rx_ring->rx_buf_len);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: next_to_use = %d, next_to_clean = %d, ring_active = %i\n",
			 i,
			 rx_ring->next_to_use,
			 rx_ring->next_to_clean,
			 rx_ring->ring_active);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_stats: packets = %lld, bytes = %lld, non_eop_descs = %lld\n",
			 i, rx_ring->stats.packets,
			 rx_ring->stats.bytes,
			 rx_ring->rx_stats.non_eop_descs);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_stats: alloc_page_failed = %lld, alloc_buff_failed = %lld\n",
			 i,
			 rx_ring->rx_stats.alloc_page_failed,
			 rx_ring->rx_stats.alloc_buff_failed);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_stats: realloc_count = %lld, page_reuse_count = %lld\n",
			 i,
			 rx_ring->rx_stats.realloc_count,
			 rx_ring->rx_stats.page_reuse_count);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: size = %i, dma = 0x%08lx\n",
			 i, rx_ring->size,
			 (unsigned long int)rx_ring->dma);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: vsi = %p, q_vector = %p\n",
			 i, rx_ring->vsi,
			 rx_ring->q_vector);
		dev_info(&pf->pdev->dev,
			 "    rx_rings[%i]: rx_itr_setting = %d (%s)\n",
			 i, rx_ring->rx_itr_setting,
			 ITR_IS_DYNAMIC(rx_ring->rx_itr_setting) ?
				"dynamic" : "fixed");
	}
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		struct i40e_ring *tx_ring = ACCESS_ONCE(vsi->tx_rings[i]);

		if (!tx_ring)
			continue;

		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: desc = %p\n",
			 i, tx_ring->desc);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: dev = %p, netdev = %p, tx_bi = %p\n",
			 i, tx_ring->dev,
			 tx_ring->netdev,
			 tx_ring->tx_bi);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: state = %li, queue_index = %d, reg_idx = %d\n",
			 i, tx_ring->state,
			 tx_ring->queue_index,
			 tx_ring->reg_idx);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: next_to_use = %d, next_to_clean = %d, ring_active = %i\n",
			 i,
			 tx_ring->next_to_use,
			 tx_ring->next_to_clean,
			 tx_ring->ring_active);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: tx_stats: packets = %lld, bytes = %lld, restart_queue = %lld\n",
			 i, tx_ring->stats.packets,
			 tx_ring->stats.bytes,
			 tx_ring->tx_stats.restart_queue);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: tx_stats: tx_busy = %lld, tx_done_old = %lld\n",
			 i,
			 tx_ring->tx_stats.tx_busy,
			 tx_ring->tx_stats.tx_done_old);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: size = %i, dma = 0x%08lx\n",
			 i, tx_ring->size,
			 (unsigned long int)tx_ring->dma);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: vsi = %p, q_vector = %p\n",
			 i, tx_ring->vsi,
			 tx_ring->q_vector);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: DCB tc = %d\n",
			 i, tx_ring->dcb_tc);
		dev_info(&pf->pdev->dev,
			 "    tx_rings[%i]: tx_itr_setting = %d (%s)\n",
			 i, tx_ring->tx_itr_setting,
			 ITR_IS_DYNAMIC(tx_ring->tx_itr_setting) ?
				"dynamic" : "fixed");
	}
	rcu_read_unlock();
	dev_info(&pf->pdev->dev,
		 "    work_limit = %d\n",
		 vsi->work_limit);
	dev_info(&pf->pdev->dev,
		 "    max_frame = %d, rx_buf_len = %d dtype = %d\n",
		 vsi->max_frame, vsi->rx_buf_len, 0);
	dev_info(&pf->pdev->dev,
		 "    num_q_vectors = %i, base_vector = %i\n",
		 vsi->num_q_vectors, vsi->base_vector);
	dev_info(&pf->pdev->dev,
		 "    seid = %d, id = %d, uplink_seid = %d\n",
		 vsi->seid, vsi->id, vsi->uplink_seid);
	dev_info(&pf->pdev->dev,
		 "    base_queue = %d, num_queue_pairs = %d, num_desc = %d\n",
		 vsi->base_queue, vsi->num_queue_pairs, vsi->num_desc);
	dev_info(&pf->pdev->dev, "    type = %i\n", vsi->type);
	if (vsi->type == I40E_VSI_SRIOV)
		dev_info(&pf->pdev->dev, "    VF ID = %i\n", vsi->vf_id);
	dev_info(&pf->pdev->dev,
		 "    info: valid_sections = 0x%04x, switch_id = 0x%04x\n",
		 vsi->info.valid_sections, vsi->info.switch_id);
	dev_info(&pf->pdev->dev,
		 "    info: sw_reserved[] = 0x%02x 0x%02x\n",
		 vsi->info.sw_reserved[0], vsi->info.sw_reserved[1]);
	dev_info(&pf->pdev->dev,
		 "    info: sec_flags = 0x%02x, sec_reserved = 0x%02x\n",
		 vsi->info.sec_flags, vsi->info.sec_reserved);
	dev_info(&pf->pdev->dev,
		 "    info: pvid = 0x%04x, fcoe_pvid = 0x%04x, port_vlan_flags = 0x%02x\n",
		 vsi->info.pvid, vsi->info.fcoe_pvid,
		 vsi->info.port_vlan_flags);
	dev_info(&pf->pdev->dev,
		 "    info: pvlan_reserved[] = 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.pvlan_reserved[0], vsi->info.pvlan_reserved[1],
		 vsi->info.pvlan_reserved[2]);
	dev_info(&pf->pdev->dev,
		 "    info: ingress_table = 0x%08x, egress_table = 0x%08x\n",
		 vsi->info.ingress_table, vsi->info.egress_table);
	dev_info(&pf->pdev->dev,
		 "    info: cas_pv_stag = 0x%04x, cas_pv_flags= 0x%02x, cas_pv_reserved = 0x%02x\n",
		 vsi->info.cas_pv_tag, vsi->info.cas_pv_flags,
		 vsi->info.cas_pv_reserved);
	dev_info(&pf->pdev->dev,
		 "    info: queue_mapping[0..7 ] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.queue_mapping[0], vsi->info.queue_mapping[1],
		 vsi->info.queue_mapping[2], vsi->info.queue_mapping[3],
		 vsi->info.queue_mapping[4], vsi->info.queue_mapping[5],
		 vsi->info.queue_mapping[6], vsi->info.queue_mapping[7]);
	dev_info(&pf->pdev->dev,
		 "    info: queue_mapping[8..15] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.queue_mapping[8], vsi->info.queue_mapping[9],
		 vsi->info.queue_mapping[10], vsi->info.queue_mapping[11],
		 vsi->info.queue_mapping[12], vsi->info.queue_mapping[13],
		 vsi->info.queue_mapping[14], vsi->info.queue_mapping[15]);
	dev_info(&pf->pdev->dev,
		 "    info: tc_mapping[] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.tc_mapping[0], vsi->info.tc_mapping[1],
		 vsi->info.tc_mapping[2], vsi->info.tc_mapping[3],
		 vsi->info.tc_mapping[4], vsi->info.tc_mapping[5],
		 vsi->info.tc_mapping[6], vsi->info.tc_mapping[7]);
	dev_info(&pf->pdev->dev,
		 "    info: queueing_opt_flags = 0x%02x  queueing_opt_reserved[0..2] = 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.queueing_opt_flags,
		 vsi->info.queueing_opt_reserved[0],
		 vsi->info.queueing_opt_reserved[1],
		 vsi->info.queueing_opt_reserved[2]);
	dev_info(&pf->pdev->dev,
		 "    info: up_enable_bits = 0x%02x\n",
		 vsi->info.up_enable_bits);
	dev_info(&pf->pdev->dev,
		 "    info: sched_reserved = 0x%02x, outer_up_table = 0x%04x\n",
		 vsi->info.sched_reserved, vsi->info.outer_up_table);
	dev_info(&pf->pdev->dev,
		 "    info: cmd_reserved[] = 0x%02x 0x%02x 0x%02x 0x0%02x 0x%02x 0x%02x 0x%02x 0x0%02x\n",
		 vsi->info.cmd_reserved[0], vsi->info.cmd_reserved[1],
		 vsi->info.cmd_reserved[2], vsi->info.cmd_reserved[3],
		 vsi->info.cmd_reserved[4], vsi->info.cmd_reserved[5],
		 vsi->info.cmd_reserved[6], vsi->info.cmd_reserved[7]);
	dev_info(&pf->pdev->dev,
		 "    info: qs_handle[] = 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x 0x%04x\n",
		 vsi->info.qs_handle[0], vsi->info.qs_handle[1],
		 vsi->info.qs_handle[2], vsi->info.qs_handle[3],
		 vsi->info.qs_handle[4], vsi->info.qs_handle[5],
		 vsi->info.qs_handle[6], vsi->info.qs_handle[7]);
	dev_info(&pf->pdev->dev,
		 "    info: stat_counter_idx = 0x%04x, sched_id = 0x%04x\n",
		 vsi->info.stat_counter_idx, vsi->info.sched_id);
	dev_info(&pf->pdev->dev,
		 "    info: resp_reserved[] = 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		 vsi->info.resp_reserved[0], vsi->info.resp_reserved[1],
		 vsi->info.resp_reserved[2], vsi->info.resp_reserved[3],
		 vsi->info.resp_reserved[4], vsi->info.resp_reserved[5],
		 vsi->info.resp_reserved[6], vsi->info.resp_reserved[7],
		 vsi->info.resp_reserved[8], vsi->info.resp_reserved[9],
		 vsi->info.resp_reserved[10], vsi->info.resp_reserved[11]);
	if (vsi->back)
		dev_info(&pf->pdev->dev, "    PF = %p\n", vsi->back);
	dev_info(&pf->pdev->dev, "    idx = %d\n", vsi->idx);
	dev_info(&pf->pdev->dev,
		 "    tc_config: numtc = %d, enabled_tc = 0x%x\n",
		 vsi->tc_config.numtc, vsi->tc_config.enabled_tc);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev,
			 "    tc_config: tc = %d, qoffset = %d, qcount = %d, netdev_tc = %d\n",
			 i, vsi->tc_config.tc_info[i].qoffset,
			 vsi->tc_config.tc_info[i].qcount,
			 vsi->tc_config.tc_info[i].netdev_tc);
	}
	dev_info(&pf->pdev->dev,
		 "    bw: bw_limit = %d, bw_max_quanta = %d\n",
		 vsi->bw_limit, vsi->bw_max_quanta);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev,
			 "    bw[%d]: ets_share_credits = %d, ets_limit_credits = %d, max_quanta = %d\n",
			 i, vsi->bw_ets_share_credits[i],
			 vsi->bw_ets_limit_credits[i],
			 vsi->bw_ets_max_quanta[i]);
	}
#ifdef I40E_FCOE
	if (vsi->type == I40E_VSI_FCOE) {
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: rx_packets = %llu, rx_dwords = %llu, rx_dropped = %llu\n",
			 vsi->fcoe_stats.rx_fcoe_packets,
			 vsi->fcoe_stats.rx_fcoe_dwords,
			 vsi->fcoe_stats.rx_fcoe_dropped);
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: tx_packets = %llu, tx_dwords = %llu\n",
			 vsi->fcoe_stats.tx_fcoe_packets,
			 vsi->fcoe_stats.tx_fcoe_dwords);
		dev_info(&pf->pdev->dev,
			 "    fcoe_stats: bad_crc = %llu, last_error = %llu\n",
			 vsi->fcoe_stats.fcoe_bad_fccrc,
			 vsi->fcoe_stats.fcoe_last_error);
		dev_info(&pf->pdev->dev, "    fcoe_stats: ddp_count = %llu\n",
			 vsi->fcoe_stats.fcoe_ddp_count);
	}
#endif
}

/**
 * i40e_dbg_dump_aq_desc - handles dump aq_desc write into command datum
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_aq_desc(struct i40e_pf *pf)
{
	struct i40e_adminq_ring *ring;
	struct i40e_hw *hw = &pf->hw;
	char hdr[32];
	int i;

	snprintf(hdr, sizeof(hdr), "%s %s:         ",
		 dev_driver_string(&pf->pdev->dev),
		 dev_name(&pf->pdev->dev));

	/* first the send (command) ring, then the receive (event) ring */
	dev_info(&pf->pdev->dev, "AdminQ Tx Ring\n");
	ring = &(hw->aq.asq);
	for (i = 0; i < ring->count; i++) {
		struct i40e_aq_desc *d = I40E_ADMINQ_DESC(*ring, i);

		dev_info(&pf->pdev->dev,
			 "   at[%02d] flags=0x%04x op=0x%04x dlen=0x%04x ret=0x%04x cookie_h=0x%08x cookie_l=0x%08x\n",
			 i, d->flags, d->opcode, d->datalen, d->retval,
			 d->cookie_high, d->cookie_low);
		print_hex_dump(KERN_INFO, hdr, DUMP_PREFIX_NONE,
			       16, 1, d->params.raw, 16, 0);
	}

	dev_info(&pf->pdev->dev, "AdminQ Rx Ring\n");
	ring = &(hw->aq.arq);
	for (i = 0; i < ring->count; i++) {
		struct i40e_aq_desc *d = I40E_ADMINQ_DESC(*ring, i);

		dev_info(&pf->pdev->dev,
			 "   ar[%02d] flags=0x%04x op=0x%04x dlen=0x%04x ret=0x%04x cookie_h=0x%08x cookie_l=0x%08x\n",
			 i, d->flags, d->opcode, d->datalen, d->retval,
			 d->cookie_high, d->cookie_low);
		print_hex_dump(KERN_INFO, hdr, DUMP_PREFIX_NONE,
			       16, 1, d->params.raw, 16, 0);
	}
}

/**
 * i40e_dbg_dump_desc - handles dump desc write into command datum
 * @cnt: number of arguments that the user supplied
 * @vsi_seid: vsi id entered by user
 * @ring_id: ring id entered by user
 * @desc_n: descriptor number entered by user
 * @pf: the i40e_pf created in command write
 * @is_rx_ring: true if rx, false if tx
 **/
static void i40e_dbg_dump_desc(int cnt, int vsi_seid, int ring_id, int desc_n,
			       struct i40e_pf *pf, bool is_rx_ring)
{
	struct i40e_tx_desc *txd;
	union i40e_rx_desc *rxd;
	struct i40e_ring ring;
	struct i40e_vsi *vsi;
	int i;

	vsi = i40e_dbg_find_vsi(pf, vsi_seid);
	if (!vsi) {
		dev_info(&pf->pdev->dev, "vsi %d not found\n", vsi_seid);
		return;
	}
	if (ring_id >= vsi->num_queue_pairs || ring_id < 0) {
		dev_info(&pf->pdev->dev, "ring %d not found\n", ring_id);
		return;
	}
	if (!vsi->tx_rings || !vsi->tx_rings[0]->desc) {
		dev_info(&pf->pdev->dev,
			 "descriptor rings have not been allocated for vsi %d\n",
			 vsi_seid);
		return;
	}
	if (is_rx_ring)
		ring = *vsi->rx_rings[ring_id];
	else
		ring = *vsi->tx_rings[ring_id];
	if (cnt == 2) {
		void *head = (struct i40e_tx_desc *)ring.desc + ring.count;
		u32 tx_head = le32_to_cpu(*(volatile __le32 *)head);

		dev_info(&pf->pdev->dev, "vsi = %02i %s ring = %02i\n",
			 vsi_seid, is_rx_ring ? "rx" : "tx", ring_id);
		dev_info(&pf->pdev->dev, "head = %04x tail = %04x\n",
			 is_rx_ring ? 0 : tx_head, readl(ring.tail));
		dev_info(&pf->pdev->dev, "ntc = %04x ntu = %04x\n",
			 ring.next_to_clean, ring.next_to_use);
		for (i = 0; i < ring.count; i++) {
			if (!is_rx_ring) {
				txd = I40E_TX_DESC(&ring, i);
				dev_info(&pf->pdev->dev,
					 "   d[%03x] = 0x%016llx 0x%016llx\n",
					 i, txd->buffer_addr,
					 txd->cmd_type_offset_bsz);
			} else {
				rxd = I40E_RX_DESC(&ring, i);
				dev_info(&pf->pdev->dev,
					 "   d[%03x] = 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
					 i, rxd->read.pkt_addr,
					 rxd->read.hdr_addr,
					 rxd->read.rsvd1, rxd->read.rsvd2);
			}
		}
	} else if (cnt == 3) {
		if (desc_n >= ring.count || desc_n < 0) {
			dev_info(&pf->pdev->dev,
				 "descriptor %d not found\n", desc_n);
			return;
		}
		if (!is_rx_ring) {
			txd = I40E_TX_DESC(&ring, desc_n);
			dev_info(&pf->pdev->dev,
				 "vsi = %02i tx ring = %02i d[%03x] = 0x%016llx 0x%016llx\n",
				 vsi_seid, ring_id, desc_n,
				 txd->buffer_addr, txd->cmd_type_offset_bsz);
		} else {
			rxd = I40E_RX_DESC(&ring, desc_n);
			dev_info(&pf->pdev->dev,
				 "vsi = %02i rx ring = %02i d[%03x] = 0x%016llx 0x%016llx 0x%016llx 0x%016llx\n",
				 vsi_seid, ring_id, desc_n,
				 rxd->read.pkt_addr, rxd->read.hdr_addr,
				 rxd->read.rsvd1, rxd->read.rsvd2);
		}
	} else {
		dev_info(&pf->pdev->dev, "dump desc rx/tx <vsi_seid> <ring_id> [<desc_n>]\n");
	}
}

/**
 * i40e_dbg_dump_vsi_no_seid - handles dump vsi write into command datum
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_vsi_no_seid(struct i40e_pf *pf)
{
	int i;

	for (i = 0; i < pf->num_alloc_vsi; i++)
		if (pf->vsi[i])
			dev_info(&pf->pdev->dev, "dump vsi[%d]: %d\n",
				 i, pf->vsi[i]->seid);
}

/**
 * i40e_dbg_dump_resources - handles dump resources request
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_resources(struct i40e_pf *pf)
{
	struct i40e_aqc_switch_resource_alloc_element_resp *buf;
	int buf_len;
	u16 count = 32;
	u8 num_entries;
	int ret, i;

	buf_len = count * sizeof(*buf);
	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		dev_err(&pf->pdev->dev, "Can't get memory\n");
		return;
	}

	ret = i40e_aq_get_switch_resource_alloc(&pf->hw, &num_entries,
						buf, count, NULL);
	if (ret) {
		dev_err(&pf->pdev->dev,
			"fail to get resources, err %s aq_err %s\n",
			i40e_stat_str(&pf->hw, ret),
			i40e_aq_str(&pf->hw, pf->hw.aq.asq_last_status));
		kfree(buf);
		return;
	}

	dev_info(&pf->pdev->dev, "  resources:\n");
	dev_info(&pf->pdev->dev, "  guar  total  used unalloc   name\n");
	for (i = 0; i < num_entries; i++) {
		char *p;

		switch (buf[i].resource_type) {
		case I40E_AQ_RESOURCE_TYPE_VEB:
			p = "vebs";
			break;
		case I40E_AQ_RESOURCE_TYPE_VSI:
			p = "vsis";
			break;
		case I40E_AQ_RESOURCE_TYPE_MACADDR:
			p = "macaddrs";
			break;
		case I40E_AQ_RESOURCE_TYPE_STAG:
			p = "stags";
			break;
		case I40E_AQ_RESOURCE_TYPE_ETAG:
			p = "etags";
			break;
		case I40E_AQ_RESOURCE_TYPE_MULTICAST_HASH:
			p = "multicast hash";
			break;
		case I40E_AQ_RESOURCE_TYPE_UNICAST_HASH:
			p = "unicast hash";
			break;
		case I40E_AQ_RESOURCE_TYPE_VLAN:
			p = "vlans";
			break;
		case I40E_AQ_RESOURCE_TYPE_VSI_LIST_ENTRY:
			p = "vsi list entries";
			break;
		case I40E_AQ_RESOURCE_TYPE_ETAG_LIST_ENTRY:
			p = "etag list entries";
			break;
		case I40E_AQ_RESOURCE_TYPE_VLAN_STAT_POOL:
			p = "vlan stat pools";
			break;
		case I40E_AQ_RESOURCE_TYPE_MIRROR_RULE:
			p = "mirror rules";
			break;
		case I40E_AQ_RESOURCE_TYPE_QUEUE_SETS:
			p = "queue sets";
			break;
		case I40E_AQ_RESOURCE_TYPE_VLAN_FILTERS:
			p = "vlan filters";
			break;
		case I40E_AQ_RESOURCE_TYPE_INNER_MAC_FILTERS:
			p = "inner mac filters";
			break;
		case I40E_AQ_RESOURCE_TYPE_IP_FILTERS:
			p = "ip filters";
			break;
		case I40E_AQ_RESOURCE_TYPE_GRE_VN_KEYS:
			p = "gre vn keys";
			break;
		case I40E_AQ_RESOURCE_TYPE_VN2_KEYS:
			p = "vn2 keys";
			break;
		case I40E_AQ_RESOURCE_TYPE_TUNNEL_PORTS:
			p = "tunnel ports";
			break;
		default:
			p = "unknown";
			break;
		}

		dev_info(&pf->pdev->dev, "  %4d   %4d  %4d  %4d   %s\n",
			 buf[i].guaranteed, buf[i].total, buf[i].used,
			 buf[i].total_unalloced, p);
	}

	kfree(buf);
}

/**
 * i40e_dbg_dump_capabilities - handles dump capabilities request
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_capabilities(struct i40e_pf *pf)
{
	struct i40e_hw_capabilities *p;

	p = (struct i40e_hw_capabilities *)&pf->hw.func_caps;
	dev_info(&pf->pdev->dev, "  capabilities:\n");
	dev_info(&pf->pdev->dev,
		 "    switch_mode = %d\tmgmt_mode = %d\tnpar = %d\tos2bmc = %d\n",
		 p->switch_mode, p->management_mode, p->npar_enable, p->os2bmc);
	dev_info(&pf->pdev->dev,
		 "    valid_functions = 0x%04x\tsr_iov_1_1 = %d\tnum_vfs = %d\tvf_base_id = %d\n",
		 p->valid_functions, p->sr_iov_1_1, p->num_vfs, p->vf_base_id);
	dev_info(&pf->pdev->dev, "    nvm_image_type = %d\n", p->nvm_image_type);
	dev_info(&pf->pdev->dev,
		 "    num_vsis = %d\tvmdq = %d\tflex10_enable = %d\tflex10_capable = %d\n",
		 p->num_vsis, p->vmdq, p->flex10_enable, p->flex10_capable);
	dev_info(&pf->pdev->dev,
		 "    evb_802_1_qbg = %d\tevb_802_1_qbh = %d\tmgmt_cem = %d\tieee_1588 = %d\n",
		 p->evb_802_1_qbg, p->evb_802_1_qbh, p->mgmt_cem, p->ieee_1588);
	dev_info(&pf->pdev->dev,
		 "    fcoe = %d\tiwarp = %d\tmdio_port_num = %d\tmdio_port_mode = %d\n",
		 p->fcoe, p->iwarp, p->mdio_port_num, p->mdio_port_mode);
	dev_info(&pf->pdev->dev,
		 "    dcb = %d\tenabled_tcmap = %d\tmaxtc = %d\tiscsi = %d\n",
		 p->dcb, p->enabled_tcmap, p->maxtc, p->iscsi);
	dev_info(&pf->pdev->dev,
		 "    fd = %d\tfd_filters_guaranteed = %d\tfd_filters_best_effort = %d\tnum_flow_director_filters = %d\n",
		 p->fd, p->fd_filters_guaranteed, p->fd_filters_best_effort,
		 p->num_flow_director_filters);
	dev_info(&pf->pdev->dev,
		 "    rss = %d\trss_table_size = %d\trss_table_entry_width = %d\n",
		 p->rss, p->rss_table_size, p->rss_table_entry_width);
	dev_info(&pf->pdev->dev,
		 "    led[0] = %d\tsdp[0] = %d\tled_pin_num = %d\tsdp_pin_num = %d\n",
		 p->led[0], p->sdp[0], p->led_pin_num, p->sdp_pin_num);
	dev_info(&pf->pdev->dev,
		 "    num_rx_qp = %d\tnum_tx_qp = %d\tbase_queue = %d\n",
		 p->num_rx_qp, p->num_tx_qp, p->base_queue);
	dev_info(&pf->pdev->dev,
		 "    num_msix_vectors = %d\tnum_msix_vectors_vf = %d\trx_buf_chain_len = %d\n",
		 p->num_msix_vectors, p->num_msix_vectors_vf,
		 p->rx_buf_chain_len);
}

/**
 * i40e_dbg_dump_eth_stats - handles dump stats write into command datum
 * @pf: the i40e_pf created in command write
 * @estats: the eth stats structure to be dumped
 **/
static void i40e_dbg_dump_eth_stats(struct i40e_pf *pf,
				    struct i40e_eth_stats *estats)
{
	dev_info(&pf->pdev->dev, "  ethstats:\n");
	dev_info(&pf->pdev->dev,
		 "    rx_bytes = \t%lld \trx_unicast = \t\t%lld \trx_multicast = \t%lld\n",
		estats->rx_bytes, estats->rx_unicast, estats->rx_multicast);
	dev_info(&pf->pdev->dev,
		 "    rx_broadcast = \t%lld \trx_discards = \t\t%lld\n",
		 estats->rx_broadcast, estats->rx_discards);
	dev_info(&pf->pdev->dev,
		 "    rx_unknown_protocol = \t%lld \ttx_bytes = \t%lld\n",
		 estats->rx_unknown_protocol, estats->tx_bytes);
	dev_info(&pf->pdev->dev,
		 "    tx_unicast = \t%lld \ttx_multicast = \t\t%lld \ttx_broadcast = \t%lld\n",
		 estats->tx_unicast, estats->tx_multicast, estats->tx_broadcast);
	dev_info(&pf->pdev->dev,
		 "    tx_discards = \t%lld \ttx_errors = \t\t%lld\n",
		 estats->tx_discards, estats->tx_errors);
}

/**
 * i40e_dbg_dump_veb_seid - handles dump stats of a single given veb
 * @pf: the i40e_pf created in command write
 * @seid: the seid the user put in
 **/
static void i40e_dbg_dump_veb_seid(struct i40e_pf *pf, int seid)
{
	struct i40e_veb *veb;
	int i;

	veb = i40e_dbg_find_veb(pf, seid);
	if (!veb) {
		dev_info(&pf->pdev->dev, "can't find veb %d\n", seid);
		return;
	}
#ifdef HAVE_BRIDGE_ATTRIBS
	dev_info(&pf->pdev->dev,
		 "veb idx=%d,%d stats_ic=%d  seid=%d uplink=%d mode=%s\n",
		 veb->idx, veb->veb_idx, veb->stats_idx, veb->seid,
		 veb->uplink_seid,
		 veb->bridge_mode == BRIDGE_MODE_VEPA ? "VEPA" : "VEB");
#else
	dev_info(&pf->pdev->dev,
		 "veb idx=%d,%d stats_ic=%d  seid=%d uplink=%d mode=%s\n",
		 veb->idx, veb->veb_idx, veb->stats_idx, veb->seid,
		 veb->uplink_seid,
		"VEB");
#endif
	dev_info(&pf->pdev->dev,
		 "veb bw: enabled_tc=0x%x bw_limit=%d bw_max_quanta=%d is_abs_credits=%d\n",
		 veb->enabled_tc, veb->bw_limit, veb->bw_max_quanta,
		 veb->is_abs_credits);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev, "veb bw: tc=%d bw_share=%d bw_limit=%d max_quanta=%d\n",
			 i, veb->bw_tc_share_credits[i],
			 veb->bw_tc_limit_credits[i], veb->bw_tc_max_quanta[i]);
	}
	i40e_dbg_dump_eth_stats(pf, &veb->stats);
}

/**
 * i40e_dbg_dump_veb_all - dumps all known veb's stats
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_veb_all(struct i40e_pf *pf)
{
	struct i40e_veb *veb;
	int i;

	for (i = 0; i < I40E_MAX_VEB; i++) {
		veb = pf->veb[i];
		if (veb)
			i40e_dbg_dump_veb_seid(pf, veb->seid);
	}
}

/**
 * i40e_dbg_dump_vf - dump VF info
 * @pf: the i40e_pf created in command write
 * @vf_id: the vf_id from the user
 **/
static void i40e_dbg_dump_vf(struct i40e_pf *pf, int vf_id)
{
	struct i40e_vf *vf;
	struct i40e_vsi *vsi;

	if (!pf->num_alloc_vfs) {
		dev_info(&pf->pdev->dev, "no VFs allocated\n");
	} else if ((vf_id >= 0) && (vf_id < pf->num_alloc_vfs)) {
		vf = &pf->vf[vf_id];
		vsi = pf->vsi[vf->lan_vsi_idx];
		dev_info(&pf->pdev->dev, "vf %2d: VSI id=%d, seid=%d, qps=%d\n",
			 vf_id, vf->lan_vsi_id, vsi->seid, vf->num_queue_pairs);
		dev_info(&pf->pdev->dev, "       num MDD=%lld, invalid msg=%lld, valid msg=%lld\n",
			 vf->num_mdd_events,
			 vf->num_invalid_msgs,
			 vf->num_valid_msgs);
	} else {
		dev_info(&pf->pdev->dev, "invalid VF id %d\n", vf_id);
	}
}

/**
 * i40e_dbg_dump_vf_all - dump VF info for all VFs
 * @pf: the i40e_pf created in command write
 **/
static void i40e_dbg_dump_vf_all(struct i40e_pf *pf)
{
	int i;

	if (!pf->num_alloc_vfs)
		dev_info(&pf->pdev->dev, "no VFs enabled!\n");
	else
		for (i = 0; i < pf->num_alloc_vfs; i++)
			i40e_dbg_dump_vf(pf, i);
}

/**
 * i40e_dbg_dump_dcb_cfg - Dump DCB config data struct
 * @pf: the corresponding PF
 * @cfg: DCB Config data structure
 * @prefix: Prefix string
 **/
static void i40e_dbg_dump_dcb_cfg(struct i40e_pf *pf,
				  struct i40e_dcbx_config *cfg,
				  char *prefix)
{
	int i;

	dev_info(&pf->pdev->dev,
		 "%s ets_cfg: willing=%d cbs=%d, maxtcs=%d\n",
		 prefix, cfg->etscfg.willing, cfg->etscfg.cbs,
		 cfg->etscfg.maxtcs);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev, "%s ets_cfg: up=%d tc=%d\n",
			 prefix, i, cfg->etscfg.prioritytable[i]);
	}
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev, "%s ets_cfg: tc=%d tcbw=%d tctsa=%d\n",
			 prefix, i, cfg->etscfg.tcbwtable[i],
			 cfg->etscfg.tsatable[i]);
	}
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev, "%s ets_rec: up=%d tc=%d\n",
			 prefix, i, cfg->etsrec.prioritytable[i]);
	}
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		dev_info(&pf->pdev->dev, "%s ets_rec: tc=%d tcbw=%d tctsa=%d\n",
			 prefix, i, cfg->etsrec.tcbwtable[i],
			 cfg->etsrec.tsatable[i]);
	}
	dev_info(&pf->pdev->dev,
		 "%s pfc_cfg: willing=%d mbc=%d, pfccap=%d pfcenable=0x%x\n",
		 prefix, cfg->pfc.willing, cfg->pfc.mbc,
		 cfg->pfc.pfccap, cfg->pfc.pfcenable);

	dev_info(&pf->pdev->dev,
		 "%s app_table: num_apps=%d\n", prefix, (int)cfg->numapps);
	for (i = 0; i < (int)cfg->numapps; i++) {
		dev_info(&pf->pdev->dev, "%s app_table: %d prio=%d selector=%d protocol=0x%x\n",
			 prefix, i, cfg->app[i].priority,
			 cfg->app[i].selector,
			 cfg->app[i].protocolid);
	}
}

/**
 * i40e_dbg_dump_fdir_filter - Dump out flow director filter contents
 * @pf: the corresponding PF
 * @f: the flow director filter
 **/
static inline void i40e_dbg_dump_fdir_filter(struct i40e_pf *pf,
					     struct i40e_fdir_filter *f)
{
	dev_info(&pf->pdev->dev, "fdir filter %d:\n", f->fd_id);
	dev_info(&pf->pdev->dev, "    flow_type=%d ip4_proto=%d\n",
		 f->flow_type, f->ip4_proto);
	dev_info(&pf->pdev->dev, "    dst_ip= %pi4  dst_port=%d\n",
		 &f->dst_ip, f->dst_port);
	dev_info(&pf->pdev->dev, "    src_ip= %pi4  src_port=%d\n",
		 &f->src_ip, f->src_port);
	dev_info(&pf->pdev->dev, "    sctp_v_tag=%d q_index=%d flex_off=%d\n",
		 f->sctp_v_tag, f->q_index, f->flex_off);
	dev_info(&pf->pdev->dev, "    pctype=%d dest_vsi=%d dest_ctl=%d\n",
		 f->pctype, f->dest_vsi, f->dest_ctl);
	dev_info(&pf->pdev->dev, "    fd_status=%d cnt_index=%d\n",
		 f->fd_status, f->cnt_index);
	if (i40e_is_flex_filter(f))
		dev_info(&pf->pdev->dev, "    pattern(user-def N)=%u offset_in_payload(user-def m)=%u\n",
			 be16_to_cpu(f->flex_bytes[3]),
			 (u16)~(be16_to_cpu(f->flex_mask[3])));
}

/**
 * i40e_dbg_dump_cloud_filter - Dump out cloud filter contents
 * @pf: the corresponding PF
 * @f: the flow director filter
 **/
static inline void i40e_dbg_dump_cloud_filter(struct i40e_pf *pf,
					      struct i40e_cloud_filter *f)
{
	dev_info(&pf->pdev->dev, "cloud filter %d:\n", f->id);
	dev_info(&pf->pdev->dev, "    outer_mac[]=%pM  inner_mac=%pM\n",
		 f->outer_mac, f->inner_mac);
	dev_info(&pf->pdev->dev, "    inner_vlan %d, inner_ip[0] %pi4\n",
		 be16_to_cpu(f->inner_vlan), f->inner_ip);
	dev_info(&pf->pdev->dev, "    tenant_id=%d flags=0x%02x, tunnel_type=0x%02x\n",
		 f->tenant_id, f->flags, f->tunnel_type);
	dev_info(&pf->pdev->dev, "    seid=%d queue_id=%d\n",
		 f->seid, f->queue_id);
}

#define I40E_MAX_DEBUG_OUT_BUFFER (4096*4)
/**
 * i40e_dbg_command_write - write into command datum
 * @filp: the opened file
 * @buffer: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_command_write(struct file *filp,
				      const char __user *buffer,
				      size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	char *cmd_buf, *cmd_buf_tmp;
	int bytes_not_copied;
	struct i40e_vsi *vsi;
	int vsi_seid;
	int veb_seid;
	int vf_id;
	int cnt;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;

	cmd_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!cmd_buf)
		return count;
	bytes_not_copied = copy_from_user(cmd_buf, buffer, count);
	if (bytes_not_copied) {
		kfree(cmd_buf);
		return -EFAULT;
	}
	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = cmd_buf_tmp - cmd_buf + 1;
	}

	if (strncmp(cmd_buf, "read", 4) == 0) {
		u32 address;
		u32 value;

		cnt = sscanf(&cmd_buf[4], "%i", &address);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "read <reg>\n");
			goto command_write_done;
		}

		/* check the range on address */
		if (address > (pf->ioremap_len - sizeof(u32))) {
			dev_info(&pf->pdev->dev, "read reg address 0x%08x too large, max=0x%08lx\n",
				 address, (pf->ioremap_len - sizeof(u32)));
			goto command_write_done;
		}

		value = rd32(&pf->hw, address);
		dev_info(&pf->pdev->dev, "read: 0x%08x = 0x%08x\n",
			 address, value);

	} else if (strncmp(cmd_buf, "write", 5) == 0) {
		u32 address, value;

		cnt = sscanf(&cmd_buf[5], "%i %i", &address, &value);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev, "write <reg> <value>\n");
			goto command_write_done;
		}

		/* check the range on address */
		if (address > (pf->ioremap_len - sizeof(u32))) {
			dev_info(&pf->pdev->dev, "write reg address 0x%08x too large, max=0x%08lx\n",
				 address, (pf->ioremap_len - sizeof(u32)));
			goto command_write_done;
		}
		wr32(&pf->hw, address, value);
		value = rd32(&pf->hw, address);
		dev_info(&pf->pdev->dev, "write: 0x%08x = 0x%08x\n",
			 address, value);

	} else if (strncmp(cmd_buf, "add vsi", 7) == 0) {
		vsi_seid = -1;
		cnt = sscanf(&cmd_buf[7], "%i", &vsi_seid);
		if (cnt == 0) {
			/* default to PF VSI */
			vsi_seid = pf->vsi[pf->lan_vsi]->seid;
		} else if (vsi_seid < 0) {
			dev_info(&pf->pdev->dev, "add VSI %d: bad vsi seid\n",
				 vsi_seid);
			goto command_write_done;
		}

		/* By default we are in VEPA mode, if this is the first VF/VMDq
		 * VSI to be added switch to VEB mode.
		 */
		if (!(pf->flags & I40E_FLAG_VEB_MODE_ENABLED)) {
			pf->flags |= I40E_FLAG_VEB_MODE_ENABLED;
			i40e_do_reset_safe(pf,
					   BIT_ULL(__I40E_PF_RESET_REQUESTED));
		}

		vsi = i40e_vsi_setup(pf, I40E_VSI_VMDQ2, vsi_seid, 0);
		if (vsi)
			dev_info(&pf->pdev->dev, "added VSI %d to relay %d\n",
				 vsi->seid, vsi->uplink_seid);
		else
			dev_info(&pf->pdev->dev, "'%s' failed\n", cmd_buf);

	} else if (strncmp(cmd_buf, "del vsi", 7) == 0) {
		cnt = sscanf(&cmd_buf[7], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "del vsi: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "del VSI %d: seid not found\n",
				 vsi_seid);
			goto command_write_done;
		}

		dev_info(&pf->pdev->dev, "deleting VSI %d\n", vsi_seid);
		i40e_vsi_release(vsi);

	} else if (strncmp(cmd_buf, "add relay", 9) == 0) {
		struct i40e_veb *veb;
		int uplink_seid, i;

		cnt = sscanf(&cmd_buf[9], "%i %i", &uplink_seid, &vsi_seid);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add relay: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		} else if (uplink_seid < 0) {
			dev_info(&pf->pdev->dev,
				 "add relay %d: bad uplink seid\n",
				 uplink_seid);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "add relay: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i] && pf->veb[i]->seid == uplink_seid)
				break;
		if (i >= I40E_MAX_VEB && uplink_seid != 0 &&
		    uplink_seid != pf->mac_seid) {
			dev_info(&pf->pdev->dev,
				 "add relay: relay uplink %d not found\n",
				 uplink_seid);
			goto command_write_done;
		}

		veb = i40e_veb_setup(pf, 0, uplink_seid, vsi_seid,
				     vsi->tc_config.enabled_tc);
		if (veb)
			dev_info(&pf->pdev->dev, "added relay %d\n", veb->seid);
		else
			dev_info(&pf->pdev->dev, "add relay failed\n");

	} else if (strncmp(cmd_buf, "del relay", 9) == 0) {
		int i;

		cnt = sscanf(&cmd_buf[9], "%i", &veb_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "del relay: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		} else if (veb_seid < 0) {
			dev_info(&pf->pdev->dev,
				 "del relay %d: bad relay seid\n", veb_seid);
			goto command_write_done;
		}

		/* find the veb */
		for (i = 0; i < I40E_MAX_VEB; i++)
			if (pf->veb[i] && pf->veb[i]->seid == veb_seid)
				break;
		if (i >= I40E_MAX_VEB) {
			dev_info(&pf->pdev->dev,
				 "del relay: relay %d not found\n", veb_seid);
			goto command_write_done;
		}

		dev_info(&pf->pdev->dev, "deleting relay %d\n", veb_seid);
		i40e_veb_release(pf->veb[i]);
	} else if (strncmp(cmd_buf, "add pvid", 8) == 0) {
		i40e_status ret;
		u16 vid;
		int v;

		cnt = sscanf(&cmd_buf[8], "%i %d", &vsi_seid, &v);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add pvid: bad command string, cnt=%d\n", cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "add pvid: VSI %d not found\n",
				 vsi_seid);
			goto command_write_done;
		}

		vid = (unsigned)v;
		ret = i40e_vsi_add_pvid(vsi, vid);
		if (!ret)
			dev_info(&pf->pdev->dev,
				 "add pvid: %d added to VSI %d\n",
				 vid, vsi_seid);
		else
			dev_info(&pf->pdev->dev,
				 "add pvid: %d to VSI %d failed, ret=%d\n",
				 vid, vsi_seid, ret);

	} else if (strncmp(cmd_buf, "del pvid", 8) == 0) {

		cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "del pvid: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "del pvid: VSI %d not found\n", vsi_seid);
			goto command_write_done;
		}

		i40e_vsi_remove_pvid(vsi);
		dev_info(&pf->pdev->dev,
			 "del pvid: removed from VSI %d\n", vsi_seid);

	} else if (strncmp(cmd_buf, "dump", 4) == 0) {
		if (strncmp(&cmd_buf[5], "switch", 6) == 0) {
			i40e_fetch_switch_configuration(pf, true);
		} else if (strncmp(&cmd_buf[5], "resources", 9) == 0) {
			i40e_dbg_dump_resources(pf);
		} else if (strncmp(&cmd_buf[5], "capabilities", 7) == 0) {
			i40e_dbg_dump_capabilities(pf);
		} else if (strncmp(&cmd_buf[5], "vsi", 3) == 0) {
			cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
			if (cnt > 0)
				i40e_dbg_dump_vsi_seid(pf, vsi_seid);
			else
				i40e_dbg_dump_vsi_no_seid(pf);
		} else if (strncmp(&cmd_buf[5], "veb", 3) == 0) {
			cnt = sscanf(&cmd_buf[8], "%i", &vsi_seid);
			if (cnt > 0)
				i40e_dbg_dump_veb_seid(pf, vsi_seid);
			else
				i40e_dbg_dump_veb_all(pf);
		} else if (strncmp(&cmd_buf[5], "vf", 2) == 0) {
			cnt = sscanf(&cmd_buf[7], "%i", &vf_id);
			if (cnt > 0)
				i40e_dbg_dump_vf(pf, vf_id);
			else
				i40e_dbg_dump_vf_all(pf);
		} else if (strncmp(&cmd_buf[5], "desc", 4) == 0) {
			int ring_id, desc_n;

			if (strncmp(&cmd_buf[10], "rx", 2) == 0) {
				cnt = sscanf(&cmd_buf[12], "%i %i %i",
					     &vsi_seid, &ring_id, &desc_n);
				i40e_dbg_dump_desc(cnt, vsi_seid, ring_id,
						   desc_n, pf, true);
			} else if (strncmp(&cmd_buf[10], "tx", 2)
					== 0) {
				cnt = sscanf(&cmd_buf[12], "%i %i %i",
					     &vsi_seid, &ring_id, &desc_n);
				i40e_dbg_dump_desc(cnt, vsi_seid, ring_id,
						   desc_n, pf, false);
			} else if (strncmp(&cmd_buf[10], "aq", 2) == 0) {
				i40e_dbg_dump_aq_desc(pf);
			} else {
				dev_info(&pf->pdev->dev,
					 "dump desc tx <vsi_seid> <ring_id> [<desc_n>]\n");
				dev_info(&pf->pdev->dev,
					 "dump desc rx <vsi_seid> <ring_id> [<desc_n>]\n");
				dev_info(&pf->pdev->dev, "dump desc aq\n");
			}
		} else if (strncmp(&cmd_buf[5], "reset stats", 11) == 0) {
			dev_info(&pf->pdev->dev,
				 "core reset count: %d\n", pf->corer_count);
			dev_info(&pf->pdev->dev,
				 "global reset count: %d\n", pf->globr_count);
			dev_info(&pf->pdev->dev,
				 "emp reset count: %d\n", pf->empr_count);
			dev_info(&pf->pdev->dev,
				 "pf reset count: %d\n", pf->pfr_count);
			dev_info(&pf->pdev->dev,
				 "pf tx sluggish count: %d\n",
				 pf->tx_sluggish_count);
		} else if (strncmp(&cmd_buf[5], "port", 4) == 0) {
			struct i40e_aqc_query_port_ets_config_resp *bw_data;
			struct i40e_dcbx_config *cfg =
						&pf->hw.local_dcbx_config;
			struct i40e_dcbx_config *r_cfg =
						&pf->hw.remote_dcbx_config;
			struct i40e_dcbx_config *d_cfg =
						&pf->hw.desired_dcbx_config;
			int i, ret;
			u32 switch_id;

			bw_data = kzalloc(sizeof(
				    struct i40e_aqc_query_port_ets_config_resp),
					  GFP_KERNEL);
			if (!bw_data) {
				ret = -ENOMEM;
				goto command_write_done;
			}

			vsi = pf->vsi[pf->lan_vsi];
			switch_id =
				vsi->info.switch_id & I40E_AQ_VSI_SW_ID_MASK;

			ret = i40e_aq_query_port_ets_config(&pf->hw,
							    switch_id,
							    bw_data, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Query Port ETS Config AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(bw_data);
				bw_data = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev,
				 "port bw: tc_valid=0x%x tc_strict_prio=0x%x, tc_bw_max=0x%04x,0x%04x\n",
				 bw_data->tc_valid_bits,
				 bw_data->tc_strict_priority_bits,
				 le16_to_cpu(bw_data->tc_bw_max[0]),
				 le16_to_cpu(bw_data->tc_bw_max[1]));
			for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
				dev_info(&pf->pdev->dev, "port bw: tc_bw_share=%d tc_bw_limit=%d\n",
					 bw_data->tc_bw_share_credits[i],
					 le16_to_cpu(bw_data->tc_bw_limits[i]));
			}

			kfree(bw_data);
			bw_data = NULL;

			if (cfg->dcbx_mode == I40E_DCBX_MODE_CEE) {
				dev_info(&pf->pdev->dev,
					 "CEE DCBX mode with Oper TLV Status = 0x%x\n",
					 cfg->tlv_status);
				i40e_dbg_dump_dcb_cfg(pf, d_cfg, "DesiredCfg");
			} else {
				dev_info(&pf->pdev->dev, "IEEE DCBX mode\n");
			}

			i40e_dbg_dump_dcb_cfg(pf, cfg, "OperCfg");
			i40e_dbg_dump_dcb_cfg(pf, r_cfg, "PeerCfg");

		} else if (strncmp(&cmd_buf[5], "debug fwdata", 12) == 0) {
			int cluster_id, table_id;
			int index, ret;
			u16 buff_len = 4096;
			u32 next_index;
			u8 next_table;
			u8 *buff;
			u16 rlen;

			cnt = sscanf(&cmd_buf[18], "%i %i %i",
				     &cluster_id, &table_id, &index);
			if (cnt != 3) {
				dev_info(&pf->pdev->dev,
					 "dump debug fwdata <cluster_id> <table_id> <index>\n");
				goto command_write_done;
			}

			dev_info(&pf->pdev->dev,
				 "AQ debug dump fwdata params %x %x %x %x\n",
				 cluster_id, table_id, index, buff_len);
			buff = kzalloc(buff_len, GFP_KERNEL);
			if (!buff)
				goto command_write_done;

			ret = i40e_aq_debug_dump(&pf->hw, cluster_id, table_id,
						 index, buff_len, buff, &rlen,
						 &next_table, &next_index,
						 NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "debug dump fwdata AQ Failed %d 0x%x\n",
					 ret, pf->hw.aq.asq_last_status);
				kfree(buff);
				buff = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev,
				 "AQ debug dump fwdata rlen=0x%x next_table=0x%x next_index=0x%x\n",
				 rlen, next_table, next_index);
			print_hex_dump(KERN_INFO, "AQ buffer WB: ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       buff, rlen, true);
			kfree(buff);
			buff = NULL;
		} else if (strncmp(&cmd_buf[5], "filters", 7) == 0) {
			struct i40e_fdir_filter *f_rule;
			struct i40e_cloud_filter *c_rule;
			struct hlist_node *node2;

			hlist_for_each_entry_safe(f_rule, node2,
						  &pf->fdir_filter_list,
						  fdir_node) {
				i40e_dbg_dump_fdir_filter(pf, f_rule);
			}

			/* find the cloud filter rule ids */
			hlist_for_each_entry_safe(c_rule, node2,
						  &pf->cloud_filter_list,
						  cloud_node) {
				i40e_dbg_dump_cloud_filter(pf, c_rule);
			}
			i40e_dbg_dump_all_vsi_filters(pf);
		} else {
			dev_info(&pf->pdev->dev,
				 "dump desc tx <vsi_seid> <ring_id> [<desc_n>], dump desc rx <vsi_seid> <ring_id> [<desc_n>],\n");
			dev_info(&pf->pdev->dev, "dump switch\n");
			dev_info(&pf->pdev->dev, "dump vsi [seid]\n");
			dev_info(&pf->pdev->dev, "dump capabilities\n");
			dev_info(&pf->pdev->dev, "dump resources\n");
			dev_info(&pf->pdev->dev, "dump reset stats\n");
			dev_info(&pf->pdev->dev, "dump port\n");
			dev_info(&pf->pdev->dev, "dump VF [vf_id]\n");
			dev_info(&pf->pdev->dev,
				 "dump debug fwdata <cluster_id> <table_id> <index>\n");
			dev_info(&pf->pdev->dev, "dump filters\n");
		}

	} else if (strncmp(cmd_buf, "msg_enable", 10) == 0) {
		u32 level;

		cnt = sscanf(&cmd_buf[10], "%i", &level);
		if (cnt) {
			if (I40E_DEBUG_USER & level) {
				pf->hw.debug_mask = level;
				dev_info(&pf->pdev->dev,
					 "set hw.debug_mask = 0x%08x\n",
					 pf->hw.debug_mask);
			}
			pf->msg_enable = level;
			dev_info(&pf->pdev->dev, "set msg_enable = 0x%08x\n",
				 pf->msg_enable);
		} else {
			dev_info(&pf->pdev->dev, "msg_enable = 0x%08x\n",
				 pf->msg_enable);
		}
	} else if (strncmp(cmd_buf, "pfr", 3) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing PFR\n");
		i40e_do_reset_safe(pf, BIT(__I40E_PF_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "corer", 5) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing CoreR\n");
		i40e_do_reset_safe(pf, BIT(__I40E_CORE_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "globr", 5) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing GlobR\n");
		i40e_do_reset_safe(pf, BIT(__I40E_GLOBAL_RESET_REQUESTED));
	} else if (strncmp(cmd_buf, "defport on", 10) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing PFR with defport enabled\n");
		pf->cur_promisc = true;
		i40e_do_reset_safe(pf, BIT(__I40E_PF_RESET_REQUESTED));
	} else if (strncmp(cmd_buf, "defport off", 11) == 0) {
		dev_info(&pf->pdev->dev, "debugfs: forcing PFR with defport disabled\n");
		pf->cur_promisc = false;
		i40e_do_reset_safe(pf, BIT(__I40E_PF_RESET_REQUESTED));

	} else if (strncmp(cmd_buf, "clear_stats", 11) == 0) {
		if (strncmp(&cmd_buf[12], "vsi", 3) == 0) {
			cnt = sscanf(&cmd_buf[15], "%i", &vsi_seid);
			if (cnt == 0) {
				int i;

				for (i = 0; i < pf->num_alloc_vsi; i++)
					i40e_vsi_reset_stats(pf->vsi[i]);
				dev_info(&pf->pdev->dev, "vsi clear stats called for all vsi's\n");
			} else if (cnt == 1) {
				vsi = i40e_dbg_find_vsi(pf, vsi_seid);
				if (!vsi) {
					dev_info(&pf->pdev->dev,
						 "clear_stats vsi: bad vsi %d\n",
						 vsi_seid);
					goto command_write_done;
				}
				i40e_vsi_reset_stats(vsi);
				dev_info(&pf->pdev->dev,
					 "vsi clear stats called for vsi %d\n",
					 vsi_seid);
			} else {
				dev_info(&pf->pdev->dev, "clear_stats vsi [seid]\n");
			}
		} else if (strncmp(&cmd_buf[12], "port", 4) == 0) {
			if (pf->hw.partition_id == 1) {
				i40e_pf_reset_stats(pf);
				dev_info(&pf->pdev->dev, "port stats cleared\n");
			} else {
				dev_info(&pf->pdev->dev, "clear port stats not allowed on this port partition\n");
			}
		} else {
			dev_info(&pf->pdev->dev, "clear_stats vsi [seid] or clear_stats port\n");
		}
	} else if (strncmp(cmd_buf, "send aq_cmd", 11) == 0) {
		struct i40e_aq_desc *desc;
		i40e_status ret;

		desc = kzalloc(sizeof(struct i40e_aq_desc), GFP_KERNEL);
		if (!desc)
			goto command_write_done;
		cnt = sscanf(&cmd_buf[11],
			     "%hi %hi %hi %hi %i %i %i %i %i %i",
			     &desc->flags,
			     &desc->opcode, &desc->datalen, &desc->retval,
			     &desc->cookie_high, &desc->cookie_low,
			     &desc->params.internal.param0,
			     &desc->params.internal.param1,
			     &desc->params.internal.param2,
			     &desc->params.internal.param3);
		if (cnt != 10) {
			dev_info(&pf->pdev->dev,
				 "send aq_cmd: bad command string, cnt=%d\n",
				 cnt);
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		ret = i40e_asq_send_command(&pf->hw, desc, NULL, 0, NULL);
		if (!ret) {
			dev_info(&pf->pdev->dev, "AQ command sent Status : Success\n");
		} else if (ret == I40E_ERR_ADMIN_QUEUE_ERROR) {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x AQ Error: %d\n",
				 desc->opcode, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x Status: %d\n",
				 desc->opcode, ret);
		}
		dev_info(&pf->pdev->dev,
			 "AQ desc WB 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 desc->flags, desc->opcode, desc->datalen, desc->retval,
			 desc->cookie_high, desc->cookie_low,
			 desc->params.internal.param0,
			 desc->params.internal.param1,
			 desc->params.internal.param2,
			 desc->params.internal.param3);
		kfree(desc);
		desc = NULL;
	} else if (strncmp(cmd_buf, "send indirect aq_cmd", 20) == 0) {
		struct i40e_aq_desc *desc;
		i40e_status ret;
		u16 buffer_len;
		u8 *buff;

		desc = kzalloc(sizeof(struct i40e_aq_desc), GFP_KERNEL);
		if (!desc)
			goto command_write_done;
		cnt = sscanf(&cmd_buf[20],
			     "%hi %hi %hi %hi %i %i %i %i %i %i %hi",
			     &desc->flags,
			     &desc->opcode, &desc->datalen, &desc->retval,
			     &desc->cookie_high, &desc->cookie_low,
			     &desc->params.internal.param0,
			     &desc->params.internal.param1,
			     &desc->params.internal.param2,
			     &desc->params.internal.param3,
			     &buffer_len);
		if (cnt != 11) {
			dev_info(&pf->pdev->dev,
				 "send indirect aq_cmd: bad command string, cnt=%d\n",
				 cnt);
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		/* Just stub a buffer big enough in case user messed up */
		if (buffer_len == 0)
			buffer_len = 1280;

		buff = kzalloc(buffer_len, GFP_KERNEL);
		if (!buff) {
			kfree(desc);
			desc = NULL;
			goto command_write_done;
		}
		desc->flags |= CPU_TO_LE16((u16)I40E_AQ_FLAG_BUF);
		ret = i40e_asq_send_command(&pf->hw, desc, buff,
					    buffer_len, NULL);
		if (!ret) {
			dev_info(&pf->pdev->dev, "AQ command sent Status : Success\n");
		} else if (ret == I40E_ERR_ADMIN_QUEUE_ERROR) {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x AQ Error: %d\n",
				 desc->opcode, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "AQ command send failed Opcode %x Status: %d\n",
				 desc->opcode, ret);
		}
		dev_info(&pf->pdev->dev,
			 "AQ desc WB 0x%04x 0x%04x 0x%04x 0x%04x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
			 desc->flags, desc->opcode, desc->datalen, desc->retval,
			 desc->cookie_high, desc->cookie_low,
			 desc->params.internal.param0,
			 desc->params.internal.param1,
			 desc->params.internal.param2,
			 desc->params.internal.param3);
		print_hex_dump(KERN_INFO, "AQ buffer WB: ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buff, buffer_len, true);
		kfree(buff);
		buff = NULL;
		kfree(desc);
		desc = NULL;
	} else if (strncmp(cmd_buf, "fd current cnt", 14) == 0) {
		dev_info(&pf->pdev->dev, "FD current total filter count for this interface: %d\n",
			 i40e_get_current_fd_count(pf));
	/* vf base mode on/off hooks needs to be used by validation only to
	 * make sure vf base mode driver is not broken
	 */
	} else if (strncmp(cmd_buf, "vf base mode on", 15) == 0) {
		if (!pf->num_alloc_vfs) {
			pf->vf_base_mode_only = true;
			dev_info(&pf->pdev->dev, "VF Base mode is enabled\n");
		} else
			dev_info(&pf->pdev->dev,
				 "cannot configure VF Base mode when VFs are allocated\n");
	} else if (strncmp(cmd_buf, "vf base mode off", 16) == 0) {
		if (!pf->num_alloc_vfs) {
			pf->vf_base_mode_only = false;
			dev_info(&pf->pdev->dev, "VF Base mode is disabled\n");
		} else
			dev_info(&pf->pdev->dev,
				 "cannot configure VF Base mode when VFs are allocated\n");
	} else if ((strncmp(cmd_buf, "add ethtype filter", 18) == 0) ||
		   (strncmp(cmd_buf, "rem ethtype filter", 18) == 0)) {
		u16 ethtype;
		u16 queue;
		bool add = false;
		int ret;

		if (strncmp(cmd_buf, "add", 3) == 0)
			add = true;

		cnt = sscanf(&cmd_buf[18],
			     "%hi %hi",
			     &ethtype, &queue);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "%s ethtype filter: bad command string, cnt=%d\n",
				 add ? "add" : "rem",
				 cnt);
			goto command_write_done;
		}
		ret = i40e_aq_add_rem_control_packet_filter(&pf->hw,
					pf->hw.mac.addr,
					ethtype, 0,
					pf->vsi[pf->lan_vsi]->seid,
					queue, add, NULL, NULL);
		if (ret) {
			dev_info(&pf->pdev->dev,
				"%s: add/rem Control Packet Filter AQ command failed =0x%x\n",
				add ? "add" : "rem",
				pf->hw.aq.asq_last_status);
			goto command_write_done;
		}

	} else if (strncmp(cmd_buf, "dcb off", 7) == 0) {
		u8 tc = i40e_pf_get_num_tc(pf);
		/* Allow disabling only when in single TC mode */
		if (tc > 1) {
			dev_info(&pf->pdev->dev, "Failed to disable DCB as TC count(%d) is greater than 1.\n",
				 tc);
			goto command_write_done;
		}
		pf->flags &= ~I40E_FLAG_DCB_ENABLED;
	} else if (strncmp(cmd_buf, "dcb on", 6) == 0) {
		pf->flags |= I40E_FLAG_DCB_ENABLED;
	} else if (strncmp(cmd_buf, "lldp", 4) == 0) {
		if (strncmp(&cmd_buf[5], "stop", 4) == 0) {
			int ret;

			ret = i40e_aq_stop_lldp(&pf->hw, false, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Stop LLDP AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
			ret = i40e_aq_add_rem_control_packet_filter(&pf->hw,
						pf->hw.mac.addr,
						I40E_ETH_P_LLDP, 0,
						pf->vsi[pf->lan_vsi]->seid,
						0, true, NULL, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					"%s: Add Control Packet Filter AQ command failed =0x%x\n",
					__func__, pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
#ifdef CONFIG_DCB
#ifdef HAVE_DCBNL_IEEE
			pf->dcbx_cap = DCB_CAP_DCBX_HOST |
				       DCB_CAP_DCBX_VER_IEEE;
#endif /* HAVE_DCBNL_IEEE */
#endif /* CONFIG_DCB */
		} else if (strncmp(&cmd_buf[5], "start", 5) == 0) {
			int ret;

			ret = i40e_aq_add_rem_control_packet_filter(&pf->hw,
						pf->hw.mac.addr,
						I40E_ETH_P_LLDP, 0,
						pf->vsi[pf->lan_vsi]->seid,
						0, false, NULL, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					"%s: Remove Control Packet Filter AQ command failed =0x%x\n",
					__func__, pf->hw.aq.asq_last_status);
				/* Continue and start FW LLDP anyways */
			}

			ret = i40e_aq_start_lldp(&pf->hw, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Start LLDP AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
#ifdef CONFIG_DCB
#ifdef HAVE_DCBNL_IEEE
			pf->dcbx_cap = DCB_CAP_DCBX_LLD_MANAGED |
				       DCB_CAP_DCBX_VER_IEEE;
#endif /* HAVE_DCBNL_IEEE */
#endif /* CONFIG_DCB */
		} else if (strncmp(&cmd_buf[5],
			   "get local", 9) == 0) {
			u16 llen, rlen;
			int ret;
			u8 *buff;

			buff = kzalloc(I40E_LLDPDU_SIZE, GFP_KERNEL);
			if (!buff)
				goto command_write_done;

			ret = i40e_aq_get_lldp_mib(&pf->hw, 0,
						   I40E_AQ_LLDP_MIB_LOCAL,
						   buff, I40E_LLDPDU_SIZE,
						   &llen, &rlen, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Get LLDP MIB (local) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(buff);
				buff = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev, "LLDP MIB (local)\n");
			print_hex_dump(KERN_INFO, "LLDP MIB (local): ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       buff, I40E_LLDPDU_SIZE, true);
			kfree(buff);
			buff = NULL;
		} else if (strncmp(&cmd_buf[5], "get remote", 10) == 0) {
			u16 llen, rlen;
			int ret;
			u8 *buff;

			buff = kzalloc(I40E_LLDPDU_SIZE, GFP_KERNEL);
			if (!buff)
				goto command_write_done;

			ret = i40e_aq_get_lldp_mib(&pf->hw,
					I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE,
					I40E_AQ_LLDP_MIB_REMOTE,
					buff, I40E_LLDPDU_SIZE,
					&llen, &rlen, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Get LLDP MIB (remote) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				kfree(buff);
				buff = NULL;
				goto command_write_done;
			}
			dev_info(&pf->pdev->dev, "LLDP MIB (remote)\n");
			print_hex_dump(KERN_INFO, "LLDP MIB (remote): ",
				       DUMP_PREFIX_OFFSET, 16, 1,
				       buff, I40E_LLDPDU_SIZE, true);
			kfree(buff);
			buff = NULL;
		} else if (strncmp(&cmd_buf[5], "event on", 8) == 0) {
			int ret;

			ret = i40e_aq_cfg_lldp_mib_change_event(&pf->hw,
								true, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Config LLDP MIB Change Event (on) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
		} else if (strncmp(&cmd_buf[5], "event off", 9) == 0) {
			int ret;

			ret = i40e_aq_cfg_lldp_mib_change_event(&pf->hw,
								false, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "Config LLDP MIB Change Event (off) AQ command failed =0x%x\n",
					 pf->hw.aq.asq_last_status);
				goto command_write_done;
			}
		}
	} else if (strncmp(cmd_buf, "nvm read", 8) == 0) {
		u16 buffer_len, bytes;
		u16 module;
		u32 offset;
		u16 *buff;
		int ret;

		cnt = sscanf(&cmd_buf[8], "%hx %x %hx",
			     &module, &offset, &buffer_len);
		if (cnt == 0) {
			module = 0;
			offset = 0;
			buffer_len = 0;
		} else if (cnt == 1) {
			offset = 0;
			buffer_len = 0;
		} else if (cnt == 2) {
			buffer_len = 0;
		} else if (cnt > 3) {
			dev_info(&pf->pdev->dev,
				 "nvm read: bad command string, cnt=%d\n", cnt);
			goto command_write_done;
		}

		/* set the max length */
		buffer_len = min_t(u16, buffer_len, I40E_MAX_AQ_BUF_SIZE/2);

		bytes = 2 * buffer_len;

		/* read at least 1k bytes, no more than 4kB */
		bytes = clamp(bytes, (u16)1024, (u16)I40E_MAX_AQ_BUF_SIZE);
		buff = kzalloc(bytes, GFP_KERNEL);
		if (!buff)
			goto command_write_done;

		ret = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_READ);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Failed Acquiring NVM resource for read err=%d status=0x%x\n",
				 ret, pf->hw.aq.asq_last_status);
			kfree(buff);
			goto command_write_done;
		}

		ret = i40e_aq_read_nvm(&pf->hw, module, (2 * offset),
				       bytes, (u8 *)buff, true, NULL);
		i40e_release_nvm(&pf->hw);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "Read NVM AQ failed err=%d status=0x%x\n",
				 ret, pf->hw.aq.asq_last_status);
		} else {
			dev_info(&pf->pdev->dev,
				 "Read NVM module=0x%x offset=0x%x words=%d\n",
				 module, offset, buffer_len);
			if (bytes)
				print_hex_dump(KERN_INFO, "NVM Dump: ",
					DUMP_PREFIX_OFFSET, 16, 2,
					buff, bytes, true);
		}
		kfree(buff);
		buff = NULL;
	} else if (strncmp(cmd_buf, "set rss_size", 12) == 0) {
		int q_count;

		cnt = sscanf(&cmd_buf[12], "%i", &q_count);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev,
				 "set rss_size: bad command string, cnt=%d\n", cnt);
			goto command_write_done;
		}
		if (q_count <= 0) {
			dev_info(&pf->pdev->dev,
				 "set rss_size: %d is too small\n",
				 q_count);
			goto command_write_done;
		}
		dev_info(&pf->pdev->dev,
			 "set rss_size requesting %d queues\n", q_count);
		rtnl_lock();
		i40e_reconfig_rss_queues(pf, q_count);
		rtnl_unlock();
		dev_info(&pf->pdev->dev, "new rss_size %d\n",
			 pf->alloc_rss_size);
	} else if (strncmp(cmd_buf, "get bw", 6) == 0) {
		i40e_status status;
		u32 max_bw, min_bw;
		bool min_valid, max_valid;

		status = i40e_read_bw_from_alt_ram(&pf->hw, &max_bw, &min_bw,
						   &min_valid, &max_valid);

		if (status) {
			dev_info(&pf->pdev->dev, "get bw failed with status %d\n",
				status);
			goto command_write_done;
		}
		if (!min_valid) {
			dev_info(&pf->pdev->dev, "min bw invalid\n");
		} else if (min_bw & I40E_ALT_BW_RELATIVE_MASK) {
			dev_info(&pf->pdev->dev, "relative min bw = %d%%\n",
				min_bw & I40E_ALT_BW_VALUE_MASK);
		} else {
			dev_info(&pf->pdev->dev, "absolute min bw = %dMb/s\n",
				(min_bw & I40E_ALT_BW_VALUE_MASK)*128);
		}
		if (!max_valid) {
			dev_info(&pf->pdev->dev, "max bw invalid\n");
		} else if (max_bw & I40E_ALT_BW_RELATIVE_MASK) {
			dev_info(&pf->pdev->dev, "relative max bw = %d%%\n",
				max_bw & I40E_ALT_BW_VALUE_MASK);
		} else {
			dev_info(&pf->pdev->dev, "absolute max bw = %dMb/s\n",
				(max_bw & I40E_ALT_BW_VALUE_MASK)*128);
		}
	} else if (strncmp(cmd_buf, "set bw", 6) == 0) {
		struct i40e_aqc_configure_partition_bw_data bw_data;
		i40e_status status;
		u32 max_bw, min_bw;

		/* Set the valid bit for this PF */
		bw_data.pf_valid_bits = cpu_to_le16(BIT(pf->hw.pf_id));

		/* Get the bw's */
		cnt = sscanf(&cmd_buf[7], "%u %u", &max_bw, &min_bw);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,"set bw <MAX> <MIN>\n");
			goto command_write_done;
		}
		bw_data.max_bw[pf->hw.pf_id] = max_bw;
		bw_data.min_bw[pf->hw.pf_id] = min_bw;

		/* Set the new bandwidths */
		status = i40e_aq_configure_partition_bw(&pf->hw, &bw_data, NULL);
		if (status) {
			dev_info(&pf->pdev->dev, "configure partition bw failed with status %d\n",
				 status);
			goto command_write_done;
		}
	} else if (strncmp(cmd_buf, "commit bw", 9) == 0) {
		/* Commit temporary BW setting to permanent NVM image */
		enum i40e_admin_queue_err last_aq_status;
		i40e_status aq_status;
		u16 nvm_word;

		if (pf->hw.partition_id != 1) {
			dev_info(&pf->pdev->dev,
				 "Commit BW only works on first partition!\n");
			goto command_write_done;
		}

		/* Acquire NVM for read access */
		aq_status = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_READ);
		if (aq_status) {
			dev_info(&pf->pdev->dev,
				 "Error %d: Cannot acquire NVM for Read Access\n",
				 aq_status);
			goto command_write_done;
		}

		/* Read word 0x10 of NVM - SW compatibility word 1 */
		aq_status = i40e_aq_read_nvm(&pf->hw,
					     I40E_SR_NVM_CONTROL_WORD,
					     0x10, sizeof(nvm_word), &nvm_word,
					     false, NULL);
		/* Save off last admin queue command status before releasing
		 * the NVM
		 */
		last_aq_status = pf->hw.aq.asq_last_status;
		i40e_release_nvm(&pf->hw);
		if (aq_status) {
			dev_info(&pf->pdev->dev, "NVM read error %d:%d\n",
				 aq_status, last_aq_status);
			goto command_write_done;
		}

		/* Wait a bit for NVM release to complete */
		msleep(100);

		/* Acquire NVM for write access */
		aq_status = i40e_acquire_nvm(&pf->hw, I40E_RESOURCE_WRITE);
		if (aq_status) {
			dev_info(&pf->pdev->dev,
				 "Error %d: Cannot acquire NVM for Write Access\n",
				 aq_status);
			goto command_write_done;
		}
		/* Write it back out unchanged to initiate update NVM,
		 * which will force a write of the shadow (alt) RAM to
		 * the NVM - thus storing the bandwidth values permanently.
		 */
		aq_status = i40e_aq_update_nvm(&pf->hw,
					       I40E_SR_NVM_CONTROL_WORD,
					       0x10, sizeof(nvm_word),
					       &nvm_word, true, NULL);
		/* Save off last admin queue command status before releasing
		 * the NVM
		 */
		last_aq_status = pf->hw.aq.asq_last_status;
		i40e_release_nvm(&pf->hw);
		if (aq_status)
			dev_info(&pf->pdev->dev,
				 "BW settings NOT SAVED - error %d:%d updating NVM\n",
				 aq_status, last_aq_status);
	} else if (strncmp(cmd_buf, "add switch ingress mirror", 25) == 0) {
		u16 rule_type = I40E_AQC_MIRROR_RULE_TYPE_ALL_INGRESS;
		u16 switch_seid, dst_vsi_seid, rule_id;
		i40e_status aq_status;

		cnt = sscanf(&cmd_buf[25], "%hu %hu",
			     &switch_seid, &dst_vsi_seid);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add mirror: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		aq_status =
			i40e_aq_add_mirrorrule(&pf->hw,
					       switch_seid, rule_type,
					       dst_vsi_seid, 0, NULL, NULL,
					       &rule_id, NULL, NULL);
		if (aq_status)
			dev_info(&pf->pdev->dev,
				 "add ingress mirror failed with status %d\n",
				 aq_status);
		else
			dev_info(&pf->pdev->dev,
				 "Ingress mirror rule %d added\n", rule_id);
	} else if (strncmp(cmd_buf, "add switch egress mirror", 24) == 0) {
		u16 rule_type = I40E_AQC_MIRROR_RULE_TYPE_ALL_EGRESS;
		u16 switch_seid, dst_vsi_seid, rule_id;
		i40e_status aq_status;

		cnt = sscanf(&cmd_buf[24], "%hu %hu",
			     &switch_seid, &dst_vsi_seid);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "add mirror: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		aq_status =
			i40e_aq_add_mirrorrule(&pf->hw,
					       switch_seid, rule_type,
					       dst_vsi_seid, 0, NULL, NULL,
					       &rule_id, NULL, NULL);
		if (aq_status)
			dev_info(&pf->pdev->dev,
				 "add egress mirror failed with status %d\n",
				 aq_status);
		else
			dev_info(&pf->pdev->dev,
				 "Egress mirror rule %d added\n", rule_id);
	} else if (strncmp(cmd_buf, "del switch ingress mirror", 25) == 0) {
		u16 rule_type = I40E_AQC_MIRROR_RULE_TYPE_ALL_INGRESS;
		i40e_status aq_status;
		u16 switch_seid, rule_id;

		cnt = sscanf(&cmd_buf[25], "%hu %hu",
			     &switch_seid, &rule_id);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "del mirror: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		aq_status =
			i40e_aq_delete_mirrorrule(&pf->hw, switch_seid,
						  rule_type, rule_id, 0, NULL,
						  NULL, NULL, NULL);
		if (aq_status)
			dev_info(&pf->pdev->dev,
				 "mirror rule remove failed with status %d\n",
				 aq_status);
		else
			dev_info(&pf->pdev->dev,
				 "Mirror rule %d removed\n", rule_id);
	} else if (strncmp(cmd_buf, "del switch egress mirror", 24) == 0) {
		u16 rule_type = I40E_AQC_MIRROR_RULE_TYPE_ALL_EGRESS;
		i40e_status aq_status;
		u16 switch_seid, rule_id;

		cnt = sscanf(&cmd_buf[24], "%hu %hu",
			     &switch_seid, &rule_id);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev,
				 "del mirror: bad command string, cnt=%d\n",
				 cnt);
			goto command_write_done;
		}

		aq_status =
			i40e_aq_delete_mirrorrule(&pf->hw, switch_seid,
						  rule_type, rule_id, 0, NULL,
						  NULL, NULL, NULL);
		if (aq_status)
			dev_info(&pf->pdev->dev,
				 "mirror rule remove failed with status %d\n",
				 aq_status);
		else
			dev_info(&pf->pdev->dev,
				 "Mirror rule %d removed\n", rule_id);

	} else {
		dev_info(&pf->pdev->dev, "unknown command '%s'\n", cmd_buf);
		dev_info(&pf->pdev->dev, "available commands\n");
		dev_info(&pf->pdev->dev, "  add vsi [relay_seid]\n");
		dev_info(&pf->pdev->dev, "  del vsi [vsi_seid]\n");
		dev_info(&pf->pdev->dev, "  add relay <uplink_seid> <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  del relay <relay_seid>\n");
		dev_info(&pf->pdev->dev, "  add pvid <vsi_seid> <vid>\n");
		dev_info(&pf->pdev->dev, "  del pvid <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  dump switch\n");
		dev_info(&pf->pdev->dev, "  dump vsi [seid]\n");
		dev_info(&pf->pdev->dev, "  dump capabilities\n");
		dev_info(&pf->pdev->dev, "  dump resources\n");
		dev_info(&pf->pdev->dev, "  dump desc tx <vsi_seid> <ring_id> [<desc_n>]\n");
		dev_info(&pf->pdev->dev, "  dump desc rx <vsi_seid> <ring_id> [<desc_n>]\n");
		dev_info(&pf->pdev->dev, "  dump desc aq\n");
		dev_info(&pf->pdev->dev, "  dump reset stats\n");
		dev_info(&pf->pdev->dev, "  dump debug fwdata <cluster_id> <table_id> <index>\n");
		dev_info(&pf->pdev->dev, "  msg_enable [level]\n");
		dev_info(&pf->pdev->dev, "  read <reg>\n");
		dev_info(&pf->pdev->dev, "  write <reg> <value>\n");
		dev_info(&pf->pdev->dev, "  clear_stats vsi [seid]\n");
		dev_info(&pf->pdev->dev, "  clear_stats port\n");
		dev_info(&pf->pdev->dev, "  pfr\n");
		dev_info(&pf->pdev->dev, "  corer\n");
		dev_info(&pf->pdev->dev, "  globr\n");
		dev_info(&pf->pdev->dev, "  defport on\n");
		dev_info(&pf->pdev->dev, "  defport off\n");
		dev_info(&pf->pdev->dev, "  send aq_cmd <flags> <opcode> <datalen> <retval> <cookie_h> <cookie_l> <param0> <param1> <param2> <param3>\n");
		dev_info(&pf->pdev->dev, "  send indirect aq_cmd <flags> <opcode> <datalen> <retval> <cookie_h> <cookie_l> <param0> <param1> <param2> <param3> <buffer_len>\n");
		dev_info(&pf->pdev->dev, "  fd current cnt");
		dev_info(&pf->pdev->dev, "  vf base mode on\n");
		dev_info(&pf->pdev->dev, "  vf base mode off\n");
		dev_info(&pf->pdev->dev, "  add ethtype filter <ethtype> <to_queue>");
		dev_info(&pf->pdev->dev, "  rem ethtype filter <ethtype> <to_queue>");
		dev_info(&pf->pdev->dev, "  lldp start\n");
		dev_info(&pf->pdev->dev, "  lldp stop\n");
		dev_info(&pf->pdev->dev, "  lldp get local\n");
		dev_info(&pf->pdev->dev, "  lldp get remote\n");
		dev_info(&pf->pdev->dev, "  lldp event on\n");
		dev_info(&pf->pdev->dev, "  lldp event off\n");
		dev_info(&pf->pdev->dev, "  nvm read [module] [word_offset] [word_count]\n");
		dev_info(&pf->pdev->dev, "  set rss_size <count>\n");
		dev_info(&pf->pdev->dev, "  dcb off\n");
		dev_info(&pf->pdev->dev, "  dcb on\n");
		dev_info(&pf->pdev->dev, "  get bw\n");
		dev_info(&pf->pdev->dev, "  set bw <MAX> <MIN>\n");
		dev_info(&pf->pdev->dev, "  commit bw\n");
		dev_info(&pf->pdev->dev, "  add switch ingress mirror <sw_seid> <dst_seid>\n");
		dev_info(&pf->pdev->dev, "  add switch egress mirror <sw_seid> <dst_seid>\n");
		dev_info(&pf->pdev->dev, "  del switch ingress mirror <sw_seid> <rule_id>\n");
		dev_info(&pf->pdev->dev, "  del switch egress mirror <sw_seid> <rule_id>\n");
	}

command_write_done:
	kfree(cmd_buf);
	cmd_buf = NULL;
	return count;
}

static const struct file_operations i40e_dbg_command_fops = {
	.owner = THIS_MODULE,
	.open =  simple_open,
	.read =  i40e_dbg_command_read,
	.write = i40e_dbg_command_write,
};

/**************************************************************
 * netdev_ops
 * The netdev_ops entry in debugfs is for giving the driver commands
 * to be executed from the netdev operations.
 **************************************************************/
static char i40e_dbg_netdev_ops_buf[256] = "";

/**
 * i40e_dbg_netdev_ops - read for netdev_ops datum
 * @filp: the opened file
 * @buffer: where to write the data for the user to read
 * @count: the size of the user's buffer
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_netdev_ops_read(struct file *filp, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	size_t buf_size = 256;
	char *buf;
	int len;

	/* don't allow partal reads */
	if (*ppos != 0)
		return 0;
	if (count < buf_size)
		return -ENOSPC;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOSPC;

	len = snprintf(buf, buf_size, "%s: %s\n",
		       pf->vsi[pf->lan_vsi]->netdev->name,
		       i40e_dbg_netdev_ops_buf);

	bytes_not_copied = copy_to_user(buffer, buf, len);
	kfree(buf);

	if (bytes_not_copied)
		return -EFAULT;

	*ppos = len;
	return len;
}

/**
 * i40e_dbg_netdev_ops_write - write into netdev_ops datum
 * @filp: the opened file
 * @buffer: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 **/
static ssize_t i40e_dbg_netdev_ops_write(struct file *filp,
					 const char __user *buffer,
					 size_t count, loff_t *ppos)
{
	struct i40e_pf *pf = filp->private_data;
	int bytes_not_copied;
	struct i40e_vsi *vsi;
	char *buf_tmp;
	int vsi_seid;
	int i, cnt;

	/* don't allow partial writes */
	if (*ppos != 0)
		return 0;
	if (count >= sizeof(i40e_dbg_netdev_ops_buf))
		return -ENOSPC;

	memset(i40e_dbg_netdev_ops_buf, 0, sizeof(i40e_dbg_netdev_ops_buf));
	bytes_not_copied = copy_from_user(i40e_dbg_netdev_ops_buf,
					  buffer, count);
	if (bytes_not_copied)
		return -EFAULT;
	i40e_dbg_netdev_ops_buf[count] = '\0';

	buf_tmp = strchr(i40e_dbg_netdev_ops_buf, '\n');
	if (buf_tmp) {
		*buf_tmp = '\0';
		count = buf_tmp - i40e_dbg_netdev_ops_buf + 1;
	}

	if (strncmp(i40e_dbg_netdev_ops_buf, "tx_timeout", 10) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "tx_timeout <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "tx_timeout: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "tx_timeout: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (test_bit(__I40E_DOWN, &vsi->state)) {
			dev_info(&pf->pdev->dev, "tx_timeout: VSI %d not UP\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_tx_timeout(vsi->netdev);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "tx_timeout called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}
	} else if (strncmp(i40e_dbg_netdev_ops_buf, "change_mtu", 10) == 0) {
		int mtu;

		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i %i",
			     &vsi_seid, &mtu);
		if (cnt != 2) {
			dev_info(&pf->pdev->dev, "change_mtu <vsi_seid> <mtu>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "change_mtu: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "change_mtu: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_change_mtu(vsi->netdev,
								mtu);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "change_mtu called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}

	} else if (strncmp(i40e_dbg_netdev_ops_buf, "set_rx_mode", 11) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[11], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "set_rx_mode <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev,
				 "set_rx_mode: VSI %d not found\n", vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "set_rx_mode: no netdev for VSI %d\n",
				 vsi_seid);
		} else if (rtnl_trylock()) {
			vsi->netdev->netdev_ops->ndo_set_rx_mode(vsi->netdev);
			rtnl_unlock();
			dev_info(&pf->pdev->dev, "set_rx_mode called\n");
		} else {
			dev_info(&pf->pdev->dev, "Could not acquire RTNL - please try again\n");
		}

	} else if (strncmp(i40e_dbg_netdev_ops_buf, "napi", 4) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[4], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "napi <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "napi: VSI %d not found\n",
				 vsi_seid);
		} else if (!vsi->netdev) {
			dev_info(&pf->pdev->dev, "napi: no netdev for VSI %d\n",
				 vsi_seid);
		} else {
			for (i = 0; i < vsi->num_q_vectors; i++)
				napi_schedule(&vsi->q_vectors[i]->napi);
			dev_info(&pf->pdev->dev, "napi called\n");
		}
	} else if (strncmp(i40e_dbg_netdev_ops_buf,
			   "toggle_tx_timeout", 17) == 0) {
		cnt = sscanf(&i40e_dbg_netdev_ops_buf[17], "%i", &vsi_seid);
		if (cnt != 1) {
			dev_info(&pf->pdev->dev, "toggle_tx_timeout <vsi_seid>\n");
			goto netdev_ops_write_done;
		}
		vsi = i40e_dbg_find_vsi(pf, vsi_seid);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "toggle_tx_timeout: VSI %d not found\n",
				 vsi_seid);
		} else {
			if (vsi->block_tx_timeout)
				vsi->block_tx_timeout = false;
			else
				vsi->block_tx_timeout = true;
			dev_info(&pf->pdev->dev, "toggle_tx_timeout: block_tx_timeout = %d\n",
				 vsi->block_tx_timeout);
		}
	} else {
		dev_info(&pf->pdev->dev, "unknown command '%s'\n",
			 i40e_dbg_netdev_ops_buf);
		dev_info(&pf->pdev->dev, "available commands\n");
		dev_info(&pf->pdev->dev, "  tx_timeout <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  change_mtu <vsi_seid> <mtu>\n");
		dev_info(&pf->pdev->dev, "  set_rx_mode <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  napi <vsi_seid>\n");
		dev_info(&pf->pdev->dev, "  toggle_tx_timeout <vsi_seid>\n");
	}
netdev_ops_write_done:
	return count;
}

static const struct file_operations i40e_dbg_netdev_ops_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = i40e_dbg_netdev_ops_read,
	.write = i40e_dbg_netdev_ops_write,
};

/**
 * i40e_dbg_pf_init - setup the debugfs directory for the PF
 * @pf: the PF that is starting up
 **/
void i40e_dbg_pf_init(struct i40e_pf *pf)
{
	struct dentry *pfile;
	const char *name = pci_name(pf->pdev);
	const struct device *dev = &pf->pdev->dev;

	pf->i40e_dbg_pf = debugfs_create_dir(name, i40e_dbg_root);
	if (!pf->i40e_dbg_pf)
		return;

	pfile = debugfs_create_file("command", 0600, pf->i40e_dbg_pf, pf,
				    &i40e_dbg_command_fops);
	if (!pfile)
		goto create_failed;

	pfile = debugfs_create_file("netdev_ops", 0600, pf->i40e_dbg_pf, pf,
				    &i40e_dbg_netdev_ops_fops);
	if (!pfile)
		goto create_failed;

	return;

create_failed:
	dev_info(dev, "debugfs dir/file for %s failed\n", name);
	debugfs_remove_recursive(pf->i40e_dbg_pf);
}

/**
 * i40e_dbg_pf_exit - clear out the PF's debugfs entries
 * @pf: the PF that is stopping
 **/
void i40e_dbg_pf_exit(struct i40e_pf *pf)
{
	debugfs_remove_recursive(pf->i40e_dbg_pf);
	pf->i40e_dbg_pf = NULL;
}

/**
 * i40e_dbg_init - start up debugfs for the driver
 **/
void i40e_dbg_init(void)
{
	if (i40e_dbg_root == NULL)
		i40e_dbg_root = debugfs_create_dir(i40e_driver_name, NULL);
	if (!i40e_dbg_root)
		pr_info("init of debugfs failed\n");
}

/**
 * i40e_dbg_exit - clean out the driver's debugfs entries
 **/
void i40e_dbg_exit(void)
{
	debugfs_remove_recursive(i40e_dbg_root);
	i40e_dbg_root = NULL;
}

#endif /* CONFIG_DEBUG_FS */
