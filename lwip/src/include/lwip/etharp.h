/**
 * @file
 * Ethernet output function - handles OUTGOING ethernet level traffic, implements
 * ARP resolving.
 * To be used in most low-level netif implementations
 */

/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * Copyright (c) 2003-2004 Leon Woestenberg <leon.woestenberg@axon.tv>
 * Copyright (c) 2003-2004 Axon Digital Design B.V., The Netherlands.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#ifndef LWIP_HDR_NETIF_ETHARP_H
#define LWIP_HDR_NETIF_ETHARP_H

#include "lwip/opt.h"

#if LWIP_ARP || LWIP_ETHERNET /* don't build if not configured for use in lwipopts.h */

#include "lwip/def.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/ip.h"

LWIP_EXTERN_C_BEGIN

#ifndef ETHARP_HWADDR_LEN
#define ETHARP_HWADDR_LEN     6
#endif

PACK_STRUCT_BEGIN
struct eth_addr {
  PACK_STRUCT_FLD_8(u8_t addr[ETHARP_HWADDR_LEN]);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

PACK_STRUCT_BEGIN
/** Ethernet header */
struct eth_hdr {
#if ETH_PAD_SIZE
  PACK_STRUCT_FLD_8(u8_t padding[ETH_PAD_SIZE]);
#endif
  PACK_STRUCT_FLD_S(struct eth_addr dest);
  PACK_STRUCT_FLD_S(struct eth_addr src);
  PACK_STRUCT_FIELD(u16_t type);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

#define SIZEOF_ETH_HDR (14 + ETH_PAD_SIZE)

#if ETHARP_SUPPORT_VLAN

PACK_STRUCT_BEGIN
/** VLAN header inserted between ethernet header and payload
 * if 'type' in ethernet header is ETHTYPE_VLAN.
 * See IEEE802.Q */
struct eth_vlan_hdr {
  PACK_STRUCT_FIELD(u16_t prio_vid);
  PACK_STRUCT_FIELD(u16_t tpid);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

#define SIZEOF_VLAN_HDR 4
#define VLAN_ID(vlan_hdr) (lwip_htons((vlan_hdr)->prio_vid) & 0xFFF)

#endif /* ETHARP_SUPPORT_VLAN */

/* A list of often ethtypes (although lwIP does not use all of them): */
#define ETHTYPE_IP        0x0800U  /* Internet protocol v4 */
#define ETHTYPE_ARP       0x0806U  /* Address resolution protocol */
#define ETHTYPE_WOL       0x0842U  /* Wake on lan */
#define ETHTYPE_VLAN      0x8100U  /* Virtual local area network */
#define ETHTYPE_IPV6      0x86DDU  /* Internet protocol v6 */
#define ETHTYPE_PPPOEDISC 0x8863U  /* PPP Over Ethernet Discovery Stage */
#define ETHTYPE_PPPOE     0x8864U  /* PPP Over Ethernet Session Stage */
#define ETHTYPE_JUMBO     0x8870U  /* Jumbo Frames */
#define ETHTYPE_PROFINET  0x8892U  /* Process field network */
#define ETHTYPE_ETHERCAT  0x88A4U  /* Ethernet for control automation technology */
#define ETHTYPE_LLDP      0x88CCU  /* Link layer discovery protocol */
#define ETHTYPE_SERCOS    0x88CDU  /* Serial real-time communication system */
#define ETHTYPE_PTP       0x88F7U  /* Precision time protocol */
#define ETHTYPE_QINQ      0x9100U  /* Q-in-Q, 802.1ad */

/** Define this to 1 and define LWIP_ARP_FILTER_NETIF_FN(pbuf, netif, type)
 * to a filter function that returns the correct netif when using multiple
 * netifs on one hardware interface where the netif's low-level receive
 * routine cannot decide for the correct netif (e.g. when mapping multiple
 * IP addresses to one hardware interface).
 */
#ifndef LWIP_ARP_FILTER_NETIF
#define LWIP_ARP_FILTER_NETIF 0
#endif

#if LWIP_IPV4 && LWIP_ARP /* don't build if not configured for use in lwipopts.h */

PACK_STRUCT_BEGIN
/** the ARP message, see RFC 826 ("Packet format") */
struct etharp_hdr {
  PACK_STRUCT_FIELD(u16_t hwtype);
  PACK_STRUCT_FIELD(u16_t proto);
  PACK_STRUCT_FLD_8(u8_t  hwlen);
  PACK_STRUCT_FLD_8(u8_t  protolen);
  PACK_STRUCT_FIELD(u16_t opcode);
  PACK_STRUCT_FLD_S(struct eth_addr shwaddr);
  PACK_STRUCT_FLD_S(struct ip4_addr2 sipaddr);
  PACK_STRUCT_FLD_S(struct eth_addr dhwaddr);
  PACK_STRUCT_FLD_S(struct ip4_addr2 dipaddr);
} PACK_STRUCT_STRUCT;
PACK_STRUCT_END

#define SIZEOF_ETHARP_HDR 28

#define SIZEOF_ETHARP_PACKET    (SIZEOF_ETH_HDR + SIZEOF_ETHARP_HDR)
#if ETHARP_SUPPORT_VLAN && defined(LWIP_HOOK_VLAN_SET)
#define SIZEOF_ETHARP_PACKET_TX (SIZEOF_ETHARP_PACKET + SIZEOF_VLAN_HDR)
#define SIZEOF_ETHARP_HEADER (SIZEOF_ETH_HDR + SIZEOF_VLAN_HDR)
#else /* ETHARP_SUPPORT_VLAN && defined(LWIP_HOOK_VLAN_SET) */
#define SIZEOF_ETHARP_PACKET_TX SIZEOF_ETHARP_PACKET
#define SIZEOF_ETHARP_HEADER SIZEOF_ETH_HDR
#endif /* ETHARP_SUPPORT_VLAN && defined(LWIP_HOOK_VLAN_SET) */

/** 1 seconds period */
#define ARP_TMR_INTERVAL 1000

/** MEMCPY-like macro to copy to/from struct eth_addr's that are local variables
 * or known to be 32-bit aligned within the protocol header. */
#ifndef ETHADDR32_COPY
#define ETHADDR32_COPY(dst, src)  SMEMCPY(dst, src, ETHARP_HWADDR_LEN)
#endif

/** MEMCPY-like macro to copy to/from struct eth_addr's that are no local
 * variables and known to be 16-bit aligned within the protocol header. */
#ifndef ETHADDR16_COPY
#define ETHADDR16_COPY(dst, src)  SMEMCPY(dst, src, ETHARP_HWADDR_LEN)
#endif

/** ARP message types (opcodes) */
#define ARP_REQUEST 1
#define ARP_REPLY   2

#if ARP_QUEUEING
/** struct for queueing outgoing packets for unknown address
  * defined here to be accessed by memp.h
  */
struct etharp_q_entry {
  struct etharp_q_entry *next;
  struct pbuf *p;
};
#endif /* ARP_QUEUEING */

#define etharp_init() /* Compatibility define, no init needed. */
void etharp_tmr(void);
err_t etharp_output(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr);
err_t etharp_query(struct netif *netif, const ip4_addr_t *ipaddr, struct pbuf *q);
err_t etharp_request(struct netif *netif, const ip4_addr_t *ipaddr);
/** For Ethernet network interfaces, we might want to send "gratuitous ARP";
 *  this is an ARP packet sent by a node in order to spontaneously cause other
 *  nodes to update an entry in their ARP cache.
 *  From RFC 3220 "IP Mobility Support for IPv4" section 4.6. */
#define etharp_gratuitous(netif) etharp_request((netif), netif_ip4_addr(netif))
void etharp_cleanup_netif(struct netif *netif);

#if ETHARP_SUPPORT_STATIC_ENTRIES
err_t etharp_add_static_entry(const ip4_addr_t *ipaddr, struct eth_addr *ethaddr);
err_t etharp_remove_static_entry(const ip4_addr_t *ipaddr);
#endif /* ETHARP_SUPPORT_STATIC_ENTRIES */

#endif /* LWIP_IPV4 && LWIP_ARP */

err_t ethernet_input(struct pbuf *p, struct netif *netif);

#define eth_addr_cmp(addr1, addr2) (memcmp((addr1)->addr, (addr2)->addr, ETHARP_HWADDR_LEN) == 0)

extern const struct eth_addr ethbroadcast, ethzero;

LWIP_EXTERN_C_END

#endif /* LWIP_ARP || LWIP_ETHERNET */

#endif /* LWIP_HDR_NETIF_ARP_H */
