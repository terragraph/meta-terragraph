/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
/* @lint-ignore-every TXT2 Tab Literal */

#include "tg_hwsim_fw.h"
#include "tg_hwsim_nl.h"
#include "tg_hwsim.h"
#include <linux/etherdevice.h>

static int tg_hwsim_fw_assoc(tgfPtMsg *msg, struct baseband_data *bb)
{
	tgfPtAssocMsg *assoc_msg;
	u64 link_addr;

	assoc_msg = &msg->data.assoc;
	link_addr = ether_addr_to_u64(assoc_msg->addr);

	return tg_hwsim_assoc_on_baseband(bb, link_addr);
}

static int tg_hwsim_fw_dissoc(tgfPtMsg *msg, struct baseband_data *bb)
{
	tgfPtDissocMsg *dissoc_msg;
	u64 link_addr;

	dissoc_msg = &msg->data.dissoc;
	link_addr = ether_addr_to_u64(dissoc_msg->addr);

	return tg_hwsim_dissoc_on_baseband(bb, link_addr);
}

static struct tg_hwsim_fw_op tg_hwsim_fw_ops[] = {
	{
		.cmd	= TGF_PT_SB_ASSOC,
		.cb	= tg_hwsim_fw_assoc,
	},
	{
		.cmd	= TGF_PT_SB_DISSOC,
		.cb	= tg_hwsim_fw_dissoc,
	},
};

int tg_hwsim_send_nb_fw_ack(struct baseband_data *bb, tgfPtMsgTypes msg_type,
			    int fw_op_err)
{
	tgfPtMsg ack_msg;
	memset(&ack_msg, 0, sizeof(tgfPtMsg));
	ack_msg.driverType = TG_NB_PASSTHRU;
	ack_msg.msgType = TGF_PT_NB_ACK;
	ack_msg.dest = TGF_PT_DEST_E2E;
	ack_msg.data.ack.msgType = msg_type;
	/* for firmware messages 1 indicates success, 0 indicates failure.
	 * see fb_tg_fw_driver_if.c:175 */
	ack_msg.data.ack.success = !fw_op_err;

	return tg_hwsim_send_nl_nb_passthru(bb, (unsigned char*)&ack_msg,
					    sizeof(tgfPtMsg));
}

int tg_hwsim_handle_fw_msg(struct baseband_data *bb, unsigned char *var_data,
			   int var_data_len)
{
	tgfPtMsg *msg;
	int err = 0;
	bool found_handler = false;
	int i;

	msg = (tgfPtMsg*) var_data;

	printk(KERN_DEBUG "tg_hwsim: received fw msg with type: %d",
	       msg->msgType);

	for (i = 0; i < ARRAY_SIZE(tg_hwsim_fw_ops); i++) {
		if (tg_hwsim_fw_ops[i].cmd == msg->msgType) {
			err = tg_hwsim_fw_ops[i].cb(msg, bb);
			found_handler = true;
			break;
		}
	}

	if (err) {
		printk(KERN_DEBUG "tg_hwsim: err %d occured while handling fw "
		       "msg of type %d", err, msg->msgType);
	}

	if (!found_handler) {
		printk(KERN_DEBUG "tg_hwsim: unexpected fw msg of type %d "
		       "was not handled", msg->msgType);
	}

	tg_hwsim_send_nb_fw_ack(bb, msg->msgType, err);

	/* this is the return code of the SB ack not the return code of the
	 * fw operation or the return code of the NB ack*/
	return 1;
}
