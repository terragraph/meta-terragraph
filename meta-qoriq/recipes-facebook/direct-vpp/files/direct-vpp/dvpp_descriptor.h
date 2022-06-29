/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _UAPI_LINUX_DIRECT_VPP_DESCRIPTOR_H
#define _UAPI_LINUX_DIRECT_VPP_DESCRIPTOR_H

/* seg.lo to byte offset shift - match 64Bytes cache line */
#define DVPP_LO_SHIFT 6
/* Headrood must match with VPP, vlib_buffer_t */
#define DVPP_DATA_HEADROOM 256

typedef struct {
	union {
		struct {
			union {
				struct {
					/* (1<<24) * 64Bytes = 1GB of packet memory */
					u32 lo : 24;
					u32 hi : 4;
					u32 flags : 4;
				} __attribute__((packed));
				u32 index;
			};
			u16 len : 14;
			u16 eop : 1;
			u16 special : 1;
			u16 offset : 14;
			u16 mflags : 2; /* Report Rx errors */
		} __attribute__((packed));
		u64 desc;
	};
} segment_desc_t;

/*
 * The 16 bytes descriptors, representing
 * individual packet fragments:
 * - exchanged at the VPP <-> direct-vpp.ko interface
 * - exchanged at the wil6210 <-> direct-vpp.ko interface
 *
 * Note that the dvv_desc_t must remain 16Bytes aligned so as
 * four can fit within a single 64Bytes cache line, as well the
 * descriptor should fit within two 64 bits registers for
 * optimization purpose.
 *
 * Fields marked Intra-Layer should not be relied on for passing information
 * across layers (i.e. Kernel <=> User-Land)
 */
typedef struct {
	segment_desc_t seg;
	union {
		struct {
			u8 port_id;
			u8 pipe_id;
			u8 flow_id;
			u8 num_in_chain; /* Intra-Layer field */
			u16 total_len; /* Intra-Layer field */
			u16 res;
		} __attribute__((packed));
		u64 data;
	};
} dvpp_desc_t;

static inline void dvpp_desc_clear(dvpp_desc_t *desc)
{
	desc->seg.desc = 0;
	desc->data = 0;
}
#endif
