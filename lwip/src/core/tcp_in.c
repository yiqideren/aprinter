/**
 * @file
 * Transmission Control Protocol, incoming traffic
 *
 * The input processing functions of the TCP layer.
 *
 * These functions are generally called in the order (ip_input() ->)
 * tcp_input() -> * tcp_process() -> tcp_receive() (-> application).
 * 
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
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

#include "lwip/opt.h"

#if LWIP_TCP /* don't build if not configured for use in lwipopts.h */

#include "lwip/tcp_impl.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/memp.h"
#include "lwip/inet_chksum.h"
#include "lwip/stats.h"
#include "lwip/ip6.h"
#include "lwip/ip6_addr.h"
#include "lwip/inet_chksum.h"
#if LWIP_ND6_TCP_REACHABILITY_HINTS
#include "lwip/nd6.h"
#endif /* LWIP_ND6_TCP_REACHABILITY_HINTS */

/** Initial CWND calculation as defined RFC 2581 */
#define LWIP_TCP_CALC_INITIAL_CWND(mss) LWIP_MIN((4U * (mss)), LWIP_MAX((2U * (mss)), 4380U));
/** Initial slow start threshold value: we use the full window */
#define LWIP_TCP_INITIAL_SSTHRESH(pcb)  ((pcb)->snd_wnd)

/* These variables are global to all functions involved in the input
   processing of TCP segments. They are set by the tcp_input()
   function. */
static struct tcp_seg inseg;
static struct tcp_hdr *tcphdr;
static u16_t tcphdr_optlen;
static u16_t tcphdr_opt1len;
static u8_t* tcphdr_opt2;
static u16_t tcp_optidx;
static u32_t seqno, ackno;
static u8_t flags;
static u16_t tcplen;

static u8_t recv_flags;
static struct pbuf *recv_data;

static tcpwnd_size_t tcp_acked;

struct tcp_pcb *tcp_input_pcb;

/* Forward declarations. */
static void tcp_process(struct tcp_pcb *pcb);
static void tcp_receive(struct tcp_pcb *pcb);
static void tcp_parseopt(struct tcp_pcb *pcb);

static void tcp_listen_input(struct tcp_pcb_listen *pcb);
static void tcp_timewait_input(struct tcp_pcb *pcb);

/* Checks if the addresses in a received TCP segment match the
 * addresses of a connection PCB. Assumingthe byte order in the
 * TCP header has been fixed up to native. */
static u8_t segment_is_for_pcb(struct tcp_pcb *pcb)
{
  return
    pcb->remote_port == tcphdr->src &&
    pcb->local_port == tcphdr->dest &&
    ip_addr_cmp(&pcb->remote_ip, ip_current_src_addr()) &&
    ip_addr_cmp(&pcb->local_ip, ip_current_dest_addr());
}

/**
 * The initial input processing of TCP. It verifies the TCP header, demultiplexes
 * the segment between the PCBs and passes it on to tcp_process(), which implements
 * the TCP finite state machine. This function is called by the IP layer (in
 * ip_input()).
 *
 * @param p received TCP segment to process (p->payload pointing to the TCP header)
 * @param inp network interface on which this segment was received
 */
void
tcp_input(struct pbuf *p, struct netif *inp)
{
  struct tcp_pcb *pcb, *prev;
  struct tcp_pcb_listen *lpcb, *lpcb_prev;
#if SO_REUSE
  struct tcp_pcb_listen *lpcb_any = NULL;
  struct tcp_pcb_listen *lpcb_any_prev = NULL;
#endif
  u8_t hdrlen_bytes;

  LWIP_UNUSED_ARG(inp);

  PERF_START;

  TCP_STATS_INC(tcp.recv);
  MIB2_STATS_INC(mib2.tcpinsegs);

  /* Check that TCP header fits in payload */
  if (p->len < TCP_HLEN) {
    /* drop short packets */
    LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: short packet (%"U16_F" bytes) discarded\n", p->tot_len));
    TCP_STATS_INC(tcp.lenerr);
    goto dropped;
  }

  /* TCP header pointer. Note this is a static variable. */
  tcphdr = (struct tcp_hdr *)p->payload;

#if TCP_INPUT_DEBUG
  tcp_debug_print(tcphdr);
  LWIP_DEBUGF(TCP_INPUT_DEBUG, ("+-+-+-+-+-+-+-+-+-+-+-+-+-+- tcp_input: flags "));
  tcp_debug_print_flags(TCPH_FLAGS(tcphdr));
  LWIP_DEBUGF(TCP_INPUT_DEBUG, ("-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"));
#endif

  /* Don't even process incoming broadcasts/multicasts. */
  if (ip_addr_isbroadcast(ip_current_dest_addr(), ip_current_netif()) ||
      ip_addr_ismulticast(ip_current_dest_addr())) {
    TCP_STATS_INC(tcp.proterr);
    goto dropped;
  }

#if CHECKSUM_CHECK_TCP
  IF__NETIF_CHECKSUM_ENABLED(inp, NETIF_CHECKSUM_CHECK_TCP) {
    /* Verify TCP checksum. */
    u16_t chksum = ip_chksum_pseudo(p, IP_PROTO_TCP, p->tot_len, ip_current_src_addr(), ip_current_dest_addr());
    if (chksum != 0) {
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: packet discarded due to failing checksum 0x%04"X16_F"\n", chksum));
      tcp_debug_print(tcphdr);
      TCP_STATS_INC(tcp.chkerr);
      goto dropped;
    }
  }
#endif

  /* sanity-check header length */
  hdrlen_bytes = TCPH_HDRLEN(tcphdr) * 4;
  if (hdrlen_bytes < TCP_HLEN || hdrlen_bytes > p->tot_len) {
    LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: invalid header length (%"U16_F")\n", (u16_t)hdrlen_bytes));
    TCP_STATS_INC(tcp.lenerr);
    goto dropped;
  }
  
  /* Move the payload pointer in the pbuf so that it points to the
     TCP data instead of the TCP header. */
  tcphdr_optlen = hdrlen_bytes - TCP_HLEN;
  tcphdr_opt2 = NULL;
  if (p->len >= hdrlen_bytes) {
    /* all options are in the first pbuf */
    tcphdr_opt1len = tcphdr_optlen;
    pbuf_unheader(p, hdrlen_bytes); /* cannot fail */
  } else {
    /* TCP header fits into first pbuf, options don't - data is in the next pbuf */
    /* there must be a next pbuf, due to hdrlen_bytes sanity check above */
    LWIP_ASSERT("p->next != NULL", p->next != NULL);
    
    /* advance over the TCP header (cannot fail) */
    pbuf_unheader(p, TCP_HLEN);
    
    /* this is equivalent to the negation of the if above which was not taken,
       add TCP_HLEN on both sides to see */
    LWIP_ASSERT("p->len < tcphdr_optlen", p->len < tcphdr_optlen);

    /* determine how long the first and second parts of the options are */
    tcphdr_opt1len = p->len;
    u16_t opt2len = tcphdr_optlen - tcphdr_opt1len;
    
    /* options continue in the next pbuf: set p to zero length and hide the
        options in the next pbuf (adjusting p->tot_len) */
    pbuf_unheader(p, tcphdr_opt1len);

    /* check that the options fit in the second pbuf */
    if (opt2len > p->next->len) {
      /* drop short packets */
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: options overflow second pbuf (%"U16_F" bytes)\n", p->next->len));
      TCP_STATS_INC(tcp.lenerr);
      goto dropped;
    }

    /* remember the pointer to the second part of the options */
    tcphdr_opt2 = (u8_t*)p->next->payload;
    
    /* advance p->next to point after the options, and manually
        adjust p->tot_len to keep it consistent with the changed p->next */
    pbuf_unheader(p->next, opt2len);
    p->tot_len -= opt2len;

    LWIP_ASSERT("p->len == 0", p->len == 0);
    LWIP_ASSERT("p->tot_len == p->next->tot_len", p->tot_len == p->next->tot_len);
  }

  /* Convert fields in TCP header to host byte order. */
  tcphdr->src = lwip_ntohs(tcphdr->src);
  tcphdr->dest = lwip_ntohs(tcphdr->dest);
  tcphdr->seqno = lwip_ntohl(tcphdr->seqno);
  tcphdr->ackno = lwip_ntohl(tcphdr->ackno);
  tcphdr->wnd = lwip_ntohs(tcphdr->wnd);

  /* Save some information about the segment to static variables. */
  seqno = tcphdr->seqno;
  ackno = tcphdr->ackno;
  flags = TCPH_FLAGS(tcphdr);
  tcplen = p->tot_len + ((flags & (TCP_FIN|TCP_SYN)) ? 1 : 0);

  /* Demultiplex an incoming segment. First, we check if it is destined
     for an active connection. */
  prev = NULL;
  for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
    LWIP_ASSERT("tcp_input: active pcb->state", tcp_state_is_active(pcb->state));
    
    if (segment_is_for_pcb(pcb)) {
      /* Move this PCB to the front of the list so that subsequent
         lookups will be faster (we exploit locality in TCP segment
         arrivals). */
      if (prev != NULL) {
        prev->next = pcb->next;
        pcb->next = tcp_active_pcbs;
        tcp_active_pcbs = pcb;
      } else {
        TCP_STATS_INC(tcp.cachehit);
      }
      break;
    }
    
    prev = pcb;
  }

  if (pcb == NULL) {
    /* If it did not go to an active connection, we check the connections
       in the TIME-WAIT state. */
    for (pcb = tcp_tw_pcbs; pcb != NULL; pcb = pcb->next) {
      LWIP_ASSERT("tcp_input: TIME-WAIT pcb->state", pcb->state == TIME_WAIT);
      
      if (segment_is_for_pcb(pcb)) {
        /* We don't really care enough to move this PCB to the front
           of the list since we are not very likely to receive that
           many segments for connections in TIME-WAIT. */
        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: packed for TIME_WAITing connection.\n"));
        tcp_timewait_input(pcb);
        goto out_free;
      }
    }

    /* Finally, if we still did not get a match, we check all PCBs that
       are LISTENing for incoming connections. */
    lpcb_prev = NULL;
    for (lpcb = tcp_listen_pcbs; lpcb != NULL; lpcb = lpcb->next) {
      if (lpcb->local_port == tcphdr->dest) {
#if LWIP_IPV4 && LWIP_IPV6
        if (lpcb->accept_any_ip_version) {
          /* found an ANY-match */
#if SO_REUSE
          lpcb_any = lpcb;
          lpcb_any_prev = lpcb_prev;
#else
          break;
#endif
        } else
#endif
        if (IP_PCB_IPVER_INPUT_MATCH(lpcb)) {
          if (ip_addr_cmp(&lpcb->local_ip, ip_current_dest_addr())) {
            /* found an exact match */
            break;
          } else if (ip_addr_isany(&lpcb->local_ip)) {
            /* found an ANY-match */
#if SO_REUSE
            lpcb_any = lpcb;
            lpcb_any_prev = lpcb_prev;
#else
            break;
#endif
          }
        }
      }
      lpcb_prev = lpcb;
    }
#if SO_REUSE
    /* first try specific local IP */
    if (lpcb == NULL) {
      /* only pass to ANY if no specific local IP has been found */
      lpcb = lpcb_any;
      lpcb_prev = lpcb_any_prev;
    }
#endif
    if (lpcb != NULL) {
      /* Move this PCB to the front of the list so that subsequent
         lookups will be faster (we exploit locality in TCP segment
         arrivals). */
      if (lpcb_prev != NULL) {
        lpcb_prev->next = lpcb->next;
        lpcb->next = tcp_listen_pcbs;
        tcp_listen_pcbs = lpcb;
      } else {
        TCP_STATS_INC(tcp.cachehit);
      }

      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_input: packed for LISTENing connection.\n"));
      tcp_listen_input(lpcb);
      goto out_free;
    }
    
    /* No matching PCB was found, send a TCP RST (reset) to the sender. */
    LWIP_DEBUGF(TCP_RST_DEBUG, ("tcp_input: no PCB match found, resetting.\n"));
    if (!(TCPH_FLAGS(tcphdr) & TCP_RST)) {
      TCP_STATS_INC(tcp.proterr);
      TCP_STATS_INC(tcp.drop);
      tcp_rst(ackno, (u32_t)(seqno + tcplen), ip_current_dest_addr(),
        ip_current_src_addr(), tcphdr->dest, tcphdr->src);
    }
    goto out_free;
  }
  
  /* The incoming segment belongs to a connection. */
#if TCP_INPUT_DEBUG
  tcp_debug_print_state(pcb->state);
#endif

  /* Set up a tcp_seg structure. */
  inseg.len = p->tot_len;
  inseg.p = p;
  inseg.tcphdr = tcphdr;

  /* Set some other state related to RX processing. */
  tcp_acked = 0;
  recv_data = NULL;
  recv_flags = 0;
  
  /* Point tcp_input_pcb to this PCB. We use this to know
   * when the PCB was freed, as tcp_pcb_free() will set it
   * to NULL in that case. */
  tcp_input_pcb = pcb;

  /* If the segment has a PSH flag, set a flag in the pbuf
   * so the application can know. */
  if ((flags & TCP_PSH)) {
    p->flags |= PBUF_FLAG_PUSH;
  }

  /* Do the main connection RX processing */
  tcp_process(pcb);
  if (tcp_input_pcb == NULL) {
    goto aborted;
  }
  
  /* If the application has registered a "sent" function to be
      called when new send buffer space is available, we call it
      now. */
  if (tcp_acked != 0 && !(pcb->flags & TF_NOUSER) && pcb->sent != NULL) {
    pcb->sent(pcb->callback_arg, pcb, tcp_acked);
    if (tcp_input_pcb == NULL) {
      goto aborted;
    }
  }

  if (recv_data != NULL) {
    if ((pcb->flags & TF_NOUSER)) {
      /* received data after tcp_close -> abort (send RST) to
       * notify the remote host that not all data has been processed */
      tcp_pcb_free(pcb, 1, NULL);
      goto aborted;
    }

    if (pcb->recv != NULL) {
      /* Passing pbuf reference to the application */
      struct pbuf *the_recv_data = recv_data;
      recv_data = NULL;
      
      /* Notify application that data has been received. */
      pcb->recv(pcb->callback_arg, pcb, the_recv_data);
      if (tcp_input_pcb == NULL) {
        goto aborted;
      }
    } else {
      /* Just eat the data. */
      tcp_recved_internal(pcb, recv_data->tot_len);
    }
  }

  /* If a FIN segment was received, we call the callback
      function with a NULL buffer to indicate EOF. */
  if ((recv_flags & TF_GOT_FIN)) {
    /* correct rcv_wnd as the application won't call tcp_recved()
        for the FIN's seqno */
    // TODO: Remove this when refactoring RX code.
    if (pcb->rcv_wnd != TCP_WND_MAX(pcb)) {
      pcb->rcv_wnd++;
    }
    
    if (!(pcb->flags & TF_NOUSER) && pcb->recv != NULL) {
      pcb->recv(pcb->callback_arg, pcb, NULL);
      if (tcp_input_pcb == NULL) {
        goto aborted;
      }
    }
  }

  /* Clear tcp_input_pcb to indicate we're no longer receiving for this PCB. */
  tcp_input_pcb = NULL;
  
  /* Try to send something out.
   * Do it after clearing tcp_input_pcb so tcp_output doesn't ignore our request. */
  tcp_output(pcb);
  
#if TCP_INPUT_DEBUG && TCP_DEBUG
  tcp_debug_print_state(pcb->state);
#endif
  
  /* Jump target if pcb has been deallocated in a callback.
   * Below this line, 'pcb' may not be dereferenced! */
aborted:
  LWIP_ASSERT("tcp_input_pcb == NULL", tcp_input_pcb == NULL);
  
  /* give up any reference to the pbuf */
  if (recv_data != NULL) {
    pbuf_free(recv_data);
  }
  else if (inseg.p != NULL) {
    pbuf_free(inseg.p);
  }
  
  goto out;

dropped:
  TCP_STATS_INC(tcp.drop);
  MIB2_STATS_INC(mib2.tcpinerrs);
out_free:
  pbuf_free(p);
out:
  LWIP_ASSERT("tcp_input: tcp_pcbs_sane()", tcp_pcbs_sane());
  PERF_STOP("tcp_input");  
}

/**
 * Called by tcp_input() when a segment arrives for a listening
 * connection (from tcp_input()).
 *
 * @param pcb the tcp_pcb_listen for which a segment arrived
 * @return ERR_OK if the segment was processed
 *         another err_t on error
 *
 * @note the return value is not (yet?) used in tcp_input()
 * @note the segment which arrived is saved in global variables, therefore only the pcb
 *       involved is passed as a parameter to this function
 */
static void
tcp_listen_input(struct tcp_pcb_listen *pcb)
{
  struct tcp_pcb *npcb;
  err_t err;

  if ((flags & TCP_RST)) {
    /* An incoming RST should be ignored. Return. */
    return;
  }

  /* In the LISTEN state, we check for incoming SYN segments,
     creates a new PCB, and responds with a SYN|ACK. */
  if ((flags & TCP_ACK)) {
    /* For incoming segments with the ACK flag set, respond with a
       RST. */
    LWIP_DEBUGF(TCP_RST_DEBUG, ("tcp_listen_input: ACK in LISTEN, sending reset\n"));
    tcp_rst(ackno, seqno + tcplen, ip_current_dest_addr(),
      ip_current_src_addr(), tcphdr->dest, tcphdr->src);
  }
  else if ((flags & TCP_SYN)) {
    LWIP_DEBUGF(TCP_DEBUG, ("TCP connection request %"U16_F" -> %"U16_F".\n", tcphdr->src, tcphdr->dest));
    
    if (pcb->accepts_pending >= pcb->backlog) {
      LWIP_DEBUGF(TCP_DEBUG, ("tcp_listen_input: listen backlog exceeded for port %"U16_F"\n", tcphdr->dest));
      tcp_rst(ackno, seqno + tcplen, ip_current_dest_addr(),
        ip_current_src_addr(), tcphdr->dest, tcphdr->src);
      return;
    }
    
    npcb = tcp_alloc(pcb->prio);
    if (npcb == NULL) {
      /* don't do anything, rely on the sender to retransmit the SYN */
      LWIP_DEBUGF(TCP_DEBUG, ("tcp_listen_input: could not allocate PCB\n"));
      TCP_STATS_INC(tcp.memerr);
      return;
    }
    
    /* Set up the new PCB. */
    pcb->accepts_pending++;
    npcb->flags |= TF_BACKLOGPEND|TF_NOUSER;
#if LWIP_IPV4 && LWIP_IPV6
    PCB_ISIPV6(npcb) = ip_current_is_v6();
#endif
    ip_addr_copy(npcb->local_ip, *ip_current_dest_addr());
    ip_addr_copy(npcb->remote_ip, *ip_current_src_addr());
    npcb->local_port = pcb->local_port;
    npcb->remote_port = tcphdr->src;
    npcb->state = SYN_RCVD;
    npcb->rcv_nxt = seqno + 1;
    npcb->rcv_ann_right_edge = npcb->rcv_nxt;
    npcb->snd_wl1 = seqno - 1;/* initialise to seqno-1 to force window update */
    npcb->callback_arg = pcb->callback_arg;
    npcb->listener = pcb;
    /* inherit socket options */
    npcb->so_options = pcb->so_options & SOF_INHERITED;
    npcb->rcv_wnd = npcb->rcv_ann_wnd = pcb->initial_rcv_wnd;
    /* Register the new PCB so that we can begin receiving segments
       for it. */
    
    /* tcp_iter_will_prepend() not needed */
    tcp_reg((struct tcp_pcb_base **)&tcp_active_pcbs, to_tcp_pcb_base(npcb));

    /* Parse any options in the SYN. */
    tcp_parseopt(npcb);
    npcb->snd_wnd = SND_WND_SCALE(npcb, tcphdr->wnd);
    npcb->snd_wnd_max = npcb->snd_wnd;
    npcb->ssthresh = LWIP_TCP_INITIAL_SSTHRESH(npcb);

#if TCP_CALCULATE_EFF_SEND_MSS
    npcb->mss = tcp_eff_send_mss(npcb->mss, &npcb->local_ip, &npcb->remote_ip, PCB_ISIPV6(npcb));
#endif

    MIB2_STATS_INC(mib2.tcppassiveopens);

    /* Send a SYN|ACK together with the MSS option. */
    err = tcp_enqueue_flags(npcb, TCP_SYN|TCP_ACK);
    if (err != ERR_OK) {
      tcp_pcb_free(npcb, 0, NULL);
      return;
    }
    
    tcp_output(npcb);
  }
}

/**
 * Called by tcp_input() when a segment arrives for a connection in
 * TIME_WAIT.
 *
 * @param pcb the tcp_pcb for which a segment arrived
 *
 * @note the segment which arrived is saved in global variables, therefore only the pcb
 *       involved is passed as a parameter to this function
 */
static void
tcp_timewait_input(struct tcp_pcb *pcb)
{
  /* RFC 1337: in TIME_WAIT, ignore RST and ACK FINs + any 'acceptable' segments */
  /* RFC 793 3.9 Event Processing - Segment Arrives:
   * - first check sequence number - we skip that one in TIME_WAIT (always
   *   acceptable since we only send ACKs)
   * - second check the RST bit (... return) */
  if ((flags & TCP_RST)) {
    return;
  }
  
  /* - fourth, check the SYN bit, */
  if ((flags & TCP_SYN)) {
    /* If an incoming segment is not acceptable, an acknowledgment
       should be sent in reply */
    if (TCP_SEQ_BETWEEN(seqno, pcb->rcv_nxt, pcb->rcv_nxt + pcb->rcv_wnd)) {
      /* If the SYN is in the window it is an error, send a reset */
      tcp_rst(ackno, seqno + tcplen, ip_current_dest_addr(),
        ip_current_src_addr(), tcphdr->dest, tcphdr->src);
      return;
    }
  }
  else if ((flags & TCP_FIN)) {
    /* - eighth, check the FIN bit: Remain in the TIME-WAIT state.
         Restart the 2 MSL time-wait timeout.*/
    pcb->tmr = tcp_ticks;
  }

  if (tcplen > 0) {
    /* Acknowledge data, FIN or out-of-window SYN */
    pcb->flags |= TF_ACK_NOW;
    tcp_output(pcb);
  }
}

/**
 * Implements the TCP state machine. Called by tcp_input. In some
 * states tcp_receive() is called to receive data. The tcp_seg
 * argument will be freed by the caller (tcp_input()) unless the
 * recv_data pointer in the pcb is set.
 *
 * @param pcb the tcp_pcb for which a segment arrived
 *
 * @note the segment which arrived is saved in global variables, therefore only the pcb
 *       involved is passed as a parameter to this function
 */
static void
tcp_process(struct tcp_pcb *pcb)
{
  /* Process incoming RST segments. */
  if ((flags & TCP_RST)) {
    u8_t acceptable = 0;
    
    /* First, determine if the reset is acceptable. */
    if (pcb->state == SYN_SENT) {
      /* "In the SYN-SENT state (a RST received in response to an initial SYN),
          the RST is acceptable if the ACK field acknowledges the SYN." */
      if (ackno == pcb->snd_nxt) {
        acceptable = 1;
      }
    } else {
      /* "In all states except SYN-SENT, all reset (RST) segments are validated
          by checking their SEQ-fields." */
      if (seqno == pcb->rcv_nxt) {
        acceptable = 1;
      }
      else if (TCP_SEQ_BETWEEN(seqno, pcb->rcv_nxt, (u32_t)(pcb->rcv_nxt + pcb->rcv_wnd))) {
        /* If the sequence number is inside the window, we only send an ACK
           and wait for a re-send with matching sequence number.
           This violates RFC 793, but is required to protection against
           CVE-2004-0230 (RST spoofing attack). */
        tcp_ack_now(pcb);
      }
    }

    if (acceptable) {
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_process: Connection RESET\n"));
      LWIP_ASSERT("tcp_input: pcb->state != CLOSED", pcb->state != CLOSED);
      /* Free the PCB but also call the error callback to inform the
         application that the connection is dead and PCB is deallocated. */
      pcb->flags &= ~TF_ACK_DELAY;
      tcp_report_err(pcb, ERR_RST);
      tcp_pcb_free(pcb, 0, NULL);
    } else {
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_process: unacceptable reset seqno %"U32_F" rcv_nxt %"U32_F"\n",
        seqno, pcb->rcv_nxt));
    }
    return;
  }

  if ((flags & TCP_SYN) && (pcb->state != SYN_SENT && pcb->state != SYN_RCVD)) {
    /* Cope with new connection attempt after remote end crashed */
    tcp_ack_now(pcb);
    return;
  }

  if (!(pcb->flags & TF_NOUSER) || pcb->state == SYN_RCVD) {
    /* Update the PCB (in)activity timer unless rx is closed */
    pcb->tmr = tcp_ticks;
  }
  pcb->keep_cnt_sent = 0;

  tcp_parseopt(pcb);

  /* Do different things depending on the TCP state. */
  switch (pcb->state) {
  case SYN_SENT:
    LWIP_DEBUGF(TCP_INPUT_DEBUG, ("SYN-SENT: ackno %"U32_F" pcb->snd_nxt %"U32_F" pcb->lastack %"U32_F"\n",
     ackno, pcb->snd_nxt, pcb->lastack));
    
    /* received SYN ACK with expected sequence number? */
    if ((flags & TCP_ACK) && (flags & TCP_SYN) && ackno == (u32_t)(pcb->lastack + 1)) {
      pcb->rcv_nxt = seqno + 1;
      pcb->rcv_ann_right_edge = pcb->rcv_nxt;
      pcb->lastack = ackno;
      pcb->snd_wnd = SND_WND_SCALE(pcb, tcphdr->wnd);
      pcb->snd_wnd_max = pcb->snd_wnd;
      pcb->snd_wl1 = seqno - 1; /* initialise to seqno - 1 to force window update */
      pcb->state = ESTABLISHED;

#if TCP_CALCULATE_EFF_SEND_MSS
      pcb->mss = tcp_eff_send_mss(pcb->mss, &pcb->local_ip, &pcb->remote_ip,
        PCB_ISIPV6(pcb));
#endif /* TCP_CALCULATE_EFF_SEND_MSS */

      /* Set ssthresh again after changing 'mss' and 'snd_wnd' */
      pcb->ssthresh = LWIP_TCP_INITIAL_SSTHRESH(pcb);

      pcb->cwnd = LWIP_TCP_CALC_INITIAL_CWND(pcb->mss);
      LWIP_DEBUGF(TCP_CWND_DEBUG, ("tcp_process (SENT): cwnd %"TCPWNDSIZE_F
                                   " ssthresh %"TCPWNDSIZE_F"\n",
                                   pcb->cwnd, pcb->ssthresh));
      
      /* Remove the SYN segment from the queue. */
      LWIP_ASSERT("pcb->sndq != NULL", pcb->sndq != NULL);
      tcp_seg_free(tcp_sndq_pop(pcb));

      /* If there's nothing left to acknowledge, stop the retransmit
         timer, otherwise reset it to start again */
      if (pcb->sndq == NULL) {
        pcb->rtime = -1;
      } else {
        pcb->rtime = 0;
        pcb->nrtx = 0;
      }

      /* Call the user specified function to call when successfully
       * connected. */
      if (pcb->connected != NULL) {
        pcb->connected(pcb->callback_arg, pcb, ERR_OK);
        if (tcp_input_pcb == NULL) {
          return;
        }
      }
      
      tcp_ack_now(pcb);
    }
    /* received ACK? possibly a half-open connection */
    else if ((flags & TCP_ACK)) {
      /* send a RST to bring the other side in a non-synchronized state. */
      tcp_rst(ackno, seqno + tcplen, ip_current_dest_addr(),
        ip_current_src_addr(), tcphdr->dest, tcphdr->src);
      /* Resend SYN immediately (don't wait for rto timeout) to establish
        connection faster */
      pcb->rtime = 0;
      tcp_rexmit_rto(pcb);
    }
    break;
    
  case SYN_RCVD:
    if ((flags & TCP_ACK)) {
      /* expected ACK number? */
      if (ackno != pcb->lastack && tcp_seq_leq_ref(ackno, pcb->snd_nxt, pcb->lastack)) {
        LWIP_DEBUGF(TCP_DEBUG, ("TCP connection established %"U16_F" -> %"U16_F".\n", inseg.tcphdr->src, inseg.tcphdr->dest));
        LWIP_ASSERT("pcb->flags & TF_NOUSER", (pcb->flags & TF_NOUSER));
        
        pcb->state = ESTABLISHED;
        
        tcp_backlog_accepted_internal(pcb);
        
        /* If the listener is gone or has no accept function,
         * then abort with an RST. */
        if (pcb->listener == NULL || pcb->listener->accept == NULL) {
          tcp_pcb_free(pcb, 1, NULL);
          return;
        }
        
        /* There is now a user reference to the PCB.
         * Do this before calling accept, so that they can
         * call tcp_close and other functions from the accept. */
        pcb->flags &= ~TF_NOUSER;
        
        /* Call the accept function. */
        pcb->listener->accept(pcb->callback_arg, pcb, ERR_OK);
        if (tcp_input_pcb == NULL) {
          return;
        }
        
        /* If there was any data contained within this ACK,
         * we'd better pass it on to the application as well. */
        tcp_receive(pcb);

        /* passive open: update initial ssthresh now that the correct window is
           known: if the remote side supports window scaling, the window sent
           with the initial SYN can be smaller than the one used later */
        pcb->ssthresh = LWIP_TCP_INITIAL_SSTHRESH(pcb);

        pcb->cwnd = LWIP_TCP_CALC_INITIAL_CWND(pcb->mss);
        LWIP_DEBUGF(TCP_CWND_DEBUG, ("tcp_process (SYN_RCVD): cwnd %"TCPWNDSIZE_F
                                     " ssthresh %"TCPWNDSIZE_F"\n",
                                     pcb->cwnd, pcb->ssthresh));

        if (recv_flags & TF_GOT_FIN) {
          tcp_ack_now(pcb);
          pcb->state = CLOSE_WAIT;
        }
      } else {
        /* incorrect ACK number, send RST */
        tcp_rst(ackno, seqno + tcplen, ip_current_dest_addr(),
          ip_current_src_addr(), tcphdr->dest, tcphdr->src);
      }
    }
    else if ((flags & TCP_SYN) && seqno == (u32_t)(pcb->rcv_nxt - 1)) {
      /* Looks like another copy of the SYN - retransmit our SYN-ACK */
      tcp_rexmit(pcb);
    }
    break;
    
  case CLOSE_WAIT:
    /* FALLTHROUGH */
  case ESTABLISHED:
    tcp_receive(pcb);
    if ((recv_flags & TF_GOT_FIN)) { /* passive close */
      tcp_ack_now(pcb);
      pcb->state = CLOSE_WAIT;
    }
    break;
    
  case FIN_WAIT_1:
    tcp_receive(pcb);
    if ((recv_flags & TF_GOT_FIN)) {
      if ((flags & TCP_ACK) && (ackno == pcb->snd_nxt)) {
        LWIP_DEBUGF(TCP_DEBUG, ("TCP connection closed: FIN_WAIT_1 %"U16_F" -> %"U16_F".\n", inseg.tcphdr->src, inseg.tcphdr->dest));
        tcp_ack_now(pcb);
        tcp_move_to_time_wait(pcb);
      } else {
        tcp_ack_now(pcb);
        pcb->state = CLOSING;
      }
    }
    else if ((flags & TCP_ACK) && (ackno == pcb->snd_nxt)) {
      pcb->state = FIN_WAIT_2;
    }
    break;
    
  case FIN_WAIT_2:
    tcp_receive(pcb);
    if ((recv_flags & TF_GOT_FIN)) {
      LWIP_DEBUGF(TCP_DEBUG, ("TCP connection closed: FIN_WAIT_2 %"U16_F" -> %"U16_F".\n", inseg.tcphdr->src, inseg.tcphdr->dest));
      tcp_ack_now(pcb);
      tcp_move_to_time_wait(pcb);
    }
    break;
    
  case CLOSING:
    tcp_receive(pcb);
    if ((flags & TCP_ACK) && ackno == pcb->snd_nxt) {
      LWIP_DEBUGF(TCP_DEBUG, ("TCP connection closed: CLOSING %"U16_F" -> %"U16_F".\n", inseg.tcphdr->src, inseg.tcphdr->dest));
      tcp_move_to_time_wait(pcb);
    }
    break;
    
  case LAST_ACK:
    tcp_receive(pcb);
    if ((flags & TCP_ACK) && ackno == pcb->snd_nxt) {
      LWIP_DEBUGF(TCP_DEBUG, ("TCP connection closed: LAST_ACK %"U16_F" -> %"U16_F".\n", inseg.tcphdr->src, inseg.tcphdr->dest));
      tcp_report_err(pcb, ERR_CLSD);
      tcp_pcb_free(pcb, 0, NULL);
    }
    break;
    
  default:
    /* can only come here from active states */
    LWIP_ASSERT("tcp_process: invalid state", 0);
    break;
  }
}


/**
 * Called by tcp_process. Checks if the given segment is an ACK for outstanding
 * data, and if so frees the memory of the buffered data. Next, it places the
 * segment on any of the receive queues (pcb->recved). If the segment
 * is buffered, the pbuf is referenced by pbuf_ref so that it will not be freed until
 * it has been removed from the buffer.
 *
 * If the incoming segment constitutes an ACK for a segment that was used for RTT
 * estimation, the RTT is estimated here as well.
 *
 * Called from tcp_process().
 */
static void
tcp_receive(struct tcp_pcb *pcb)
{
  struct tcp_seg *next;
  struct pbuf *p;
  s32_t off;
  s16_t m;
  u32_t right_wnd_edge;
  u16_t new_tot_len;
  int found_dupack = 0;

  LWIP_ASSERT("tcp_receive: wrong state", pcb->state >= ESTABLISHED);

  if ((flags & TCP_ACK)) {
    right_wnd_edge = pcb->snd_wnd + pcb->snd_wl2;

    /* Update window. */
    if (TCP_SEQ_LT(pcb->snd_wl1, seqno) ||
       (pcb->snd_wl1 == seqno && TCP_SEQ_LT(pcb->snd_wl2, ackno)) ||
       (pcb->snd_wl2 == ackno && (u32_t)SND_WND_SCALE(pcb, tcphdr->wnd) > pcb->snd_wnd)) {
      pcb->snd_wnd = SND_WND_SCALE(pcb, tcphdr->wnd);
      /* keep track of the biggest window announced by the remote host to calculate
         the maximum segment size */
      if (pcb->snd_wnd_max < pcb->snd_wnd) {
        pcb->snd_wnd_max = pcb->snd_wnd;
      }
      pcb->snd_wl1 = seqno;
      pcb->snd_wl2 = ackno;
      if (pcb->snd_wnd == 0) {
        if (pcb->persist_backoff == 0) {
          /* start persist timer */
          pcb->persist_cnt = 0;
          pcb->persist_backoff = 1;
        }
      } else if (pcb->persist_backoff > 0) {
        /* stop persist timer */
          pcb->persist_backoff = 0;
      }
      LWIP_DEBUGF(TCP_WND_DEBUG, ("tcp_receive: window update %"TCPWNDSIZE_F"\n", pcb->snd_wnd));
#if TCP_WND_DEBUG
    } else {
      if (pcb->snd_wnd != (tcpwnd_size_t)SND_WND_SCALE(pcb, tcphdr->wnd)) {
        LWIP_DEBUGF(TCP_WND_DEBUG,
                    ("tcp_receive: no window update lastack %"U32_F" ackno %"
                     U32_F" wl1 %"U32_F" seqno %"U32_F" wl2 %"U32_F"\n",
                     pcb->lastack, ackno, pcb->snd_wl1, seqno, pcb->snd_wl2));
      }
#endif /* TCP_WND_DEBUG */
    }

    /* (From Stevens TCP/IP Illustrated Vol II, p970.) Its only a
     * duplicate ack if:
     * 1) It doesn't ACK new data
     * 2) length of received packet is zero (i.e. no payload)
     * 3) the advertised window hasn't changed
     * 4) There is outstanding unacknowledged data (retransmission timer running)
     * 5) The ACK is == biggest ACK sequence number so far seen (snd_una)
     *
     * If it passes all five, should process as a dupack:
     * a) dupacks < 3: do nothing
     * b) dupacks == 3: fast retransmit
     * c) dupacks > 3: increase cwnd
     *
     * If it only passes 1-3, should reset dupack counter (and add to
     * stats, which we don't do in lwIP)
     *
     * If it only passes 1, should reset dupack counter
     *
     */

    /* Clause 1 */
    if (ackno == pcb->lastack || tcp_seq_gt_ref(ackno, pcb->snd_nxt, pcb->lastack)) {
      /* Clause 2 */
      if (tcplen == 0) {
        /* Clause 3 */
        if (pcb->snd_wl2 + pcb->snd_wnd == right_wnd_edge) {
          /* Clause 4 */
          if (pcb->rtime >= 0) {
            /* Clause 5 */
            if (ackno == pcb->lastack) {
              found_dupack = 1;
              if ((u8_t)(pcb->dupacks + 1) > pcb->dupacks) {
                ++pcb->dupacks;
              }
              if (pcb->dupacks > 3) {
                /* Inflate the congestion window, but not if it means that
                   the value overflows. */
                if ((tcpwnd_size_t)(pcb->cwnd + pcb->mss) > pcb->cwnd) {
                  pcb->cwnd += pcb->mss;
                }
              } else if (pcb->dupacks == 3) {
                /* Do fast retransmit */
                tcp_rexmit_fast(pcb);
              }
            }
          }
        }
      }
      /* If Clause (1) or more is true, but not a duplicate ack, reset
       * count of consecutive duplicate acks */
      if (!found_dupack) {
        pcb->dupacks = 0;
      }
    } else {
      /* We come here when the ACK acknowledges new data. */

      /* Reset the "IN Fast Retransmit" flag, since we are no longer
         in fast retransmit. Also reset the congestion window to the
         slow start threshold. */
      if (pcb->flags & TF_INFR) {
        pcb->flags &= ~TF_INFR;
        pcb->cwnd = pcb->ssthresh;
      }

      /* Reset the number of retransmissions. */
      pcb->nrtx = 0;

      /* Reset the retransmission time-out. */
      pcb->rto = (pcb->sa >> 3) + pcb->sv;

      /* Reset the fast retransmit. */
      pcb->dupacks = 0;

      /* Update the congestion control variables (cwnd and
         ssthresh). */
      if (pcb->state >= ESTABLISHED) {
        if (pcb->cwnd < pcb->ssthresh) {
          if ((tcpwnd_size_t)(pcb->cwnd + pcb->mss) > pcb->cwnd) {
            pcb->cwnd += pcb->mss;
          }
          LWIP_DEBUGF(TCP_CWND_DEBUG, ("tcp_receive: slow start cwnd %"TCPWNDSIZE_F"\n", pcb->cwnd));
        } else {
          tcpwnd_size_t new_cwnd = (pcb->cwnd + pcb->mss * pcb->mss / pcb->cwnd);
          if (new_cwnd > pcb->cwnd) {
            pcb->cwnd = new_cwnd;
          }
          LWIP_DEBUGF(TCP_CWND_DEBUG, ("tcp_receive: congestion avoidance cwnd %"TCPWNDSIZE_F"\n", pcb->cwnd));
        }
      }
      
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_receive: ACK for %"U32_F", sndq->seqno %"U32_F":%"U32_F"\n",
        ackno,
        pcb->sndq == NULL ? 0 : lwip_ntohl(pcb->sndq->tcphdr->seqno),
        pcb->sndq == NULL ? 0 : TCP_ENDSEQ(pcb->sndq)));
      
      /* Calculate the amount of acked data to report to the application, by summing
       * the lengths of the segments acknowledged and freed (data only not any count
       * for SYN/FIN). This may be different from the literal amount of acked data in
       * case the peer acks segments partially, and is the correct behavior because
       * we should not report data as acked until we've released the buffers.
       * See lwip bug 48543. */
      LWIP_ASSERT("tcp_receive: tcp_acked == 0", tcp_acked == 0); /* set in tcp_input() */
      
      /* Remove queued segments which have been acked. */
      while (pcb->sndq != NULL &&
             TCP_SEG_SENT(pcb, pcb->sndq) &&
             tcp_seq_leq_ref(TCP_ENDSEQ(pcb->sndq), ackno, pcb->lastack))
      {
        next = pcb->sndq;
        
        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_receive: removing %"U32_F":%"U32_F" from queue\n",
                                      TCP_TCPSEQ(next), TCP_ENDSEQ(next)));

        LWIP_DEBUGF(TCP_QLEN_DEBUG, ("tcp_receive: queuelen %"TCPWNDSIZE_F" ... ", (tcpwnd_size_t)pcb->snd_queuelen));        
        tcp_sndq_pop(pcb);
        LWIP_DEBUGF(TCP_QLEN_DEBUG, ("%"TCPWNDSIZE_F" (after freeing seg)\n", (tcpwnd_size_t)pcb->snd_queuelen));
        
        LWIP_ASSERT("tcp_receive: valid queue length", pcb->snd_queuelen == 0 || pcb->sndq != NULL);
        
        /* Overflow is impossible here due to constraint related to
         * pcb->snd_buf which is also tcpwnd_size_t (see assert below). */
        LWIP_ASSERT("tcp_receive: tcp_acked overflow", next->len <= (tcpwnd_size_t)-1 - tcp_acked);
        tcp_acked += next->len;
        
        tcp_seg_free(next);
      }
      
      /* Bump the lastack to the received ackno */
      pcb->lastack = ackno;
      
      /* Update the send buffer space. */
      LWIP_ASSERT("tcp_receive: snd_buf overflowing", tcp_acked <= TCP_SND_BUF - pcb->snd_buf);
      pcb->snd_buf += tcp_acked;

      /* If there's nothing left to acknowledge, stop the retransmit
         timer, otherwise reset it to start again */
      pcb->rtime = (pcb->sndq == NULL) ? -1 : 0;

#if LWIP_IPV6 && LWIP_ND6_TCP_REACHABILITY_HINTS
      if (PCB_ISIPV6(pcb)) {
        /* Inform neighbor reachability of forward progress. */
        nd6_reachability_hint(ip6_current_src_addr());
      }
#endif /* LWIP_IPV6 && LWIP_ND6_TCP_REACHABILITY_HINTS*/
    }

    /* End of ACK for new data processing. */

    LWIP_DEBUGF(TCP_RTO_DEBUG, ("tcp_receive: pcb->rttest %"U32_F" rtseq %"U32_F" ackno %"U32_F"\n",
                                pcb->rttest, pcb->rtseq, ackno));

    /* RTT estimation calculations. This is done by checking if the
       incoming segment acknowledges the segment we use to take a
       round-trip time measurement. */
    if (pcb->rttest && TCP_SEQ_LT(pcb->rtseq, ackno)) {
      /* diff between this shouldn't exceed 32K since this are tcp timer ticks
         and a round-trip shouldn't be that long... */
      m = (s16_t)(tcp_ticks - pcb->rttest);

      LWIP_DEBUGF(TCP_RTO_DEBUG, ("tcp_receive: experienced rtt %"U16_F" ticks (%"U16_F" msec).\n",
                                  m, (u16_t)(m * TCP_SLOW_INTERVAL)));

      /* This is taken directly from VJs original code in his paper */
      m = m - (pcb->sa >> 3);
      pcb->sa += m;
      if (m < 0) {
        m = -m;
      }
      m = m - (pcb->sv >> 2);
      pcb->sv += m;
      pcb->rto = (pcb->sa >> 3) + pcb->sv;

      LWIP_DEBUGF(TCP_RTO_DEBUG, ("tcp_receive: RTO %"U16_F" (%"U16_F" milliseconds)\n",
                                  pcb->rto, (u16_t)(pcb->rto * TCP_SLOW_INTERVAL)));

      pcb->rttest = 0;
    }
  }

  /* If the incoming segment contains data, we must process it
     further unless the pcb already received a FIN.
     (RFC 793, chapter 3.9, "SEGMENT ARRIVES" in states CLOSE-WAIT, CLOSING,
     LAST-ACK and TIME-WAIT: "Ignore the segment text.") */
  if ((tcplen > 0) && (pcb->state < CLOSE_WAIT)) {
    /* This code basically does three things:

    +) If the incoming segment contains data that is the next
    in-sequence data, this data is passed to the application. This
    might involve trimming the first edge of the data. The rcv_nxt
    variable and the advertised window are adjusted.

       First, we check if we must trim the first edge. We have to do
       this if the sequence number of the incoming segment is less
       than rcv_nxt, and the sequence number plus the length of the
       segment is larger than rcv_nxt. */
    /*    if (TCP_SEQ_LT(seqno, pcb->rcv_nxt)) {
          if (TCP_SEQ_LT(pcb->rcv_nxt, seqno + tcplen)) {*/
    if (TCP_SEQ_BETWEEN(pcb->rcv_nxt, seqno + 1, seqno + tcplen - 1)) {
      /* Trimming the first edge is done by pushing the payload
         pointer in the pbuf downwards. This is somewhat tricky since
         we do not want to discard the full contents of the pbuf up to
         the new starting point of the data since we have to keep the
         TCP header which is present in the first pbuf in the chain.

         What is done is really quite a nasty hack: the first pbuf in
         the pbuf chain is pointed to by inseg.p. Since we need to be
         able to deallocate the whole pbuf, we cannot change this
         inseg.p pointer to point to any of the later pbufs in the
         chain. Instead, we point the ->payload pointer in the first
         pbuf to data in one of the later pbufs. We also set the
         inseg.data pointer to point to the right place. This way, the
         ->p pointer will still point to the first pbuf, but the
         ->p->payload pointer will point to data in another pbuf.

         After we are done with adjusting the pbuf pointers we must
         adjust the ->data pointer in the seg and the segment
         length.*/

      off = pcb->rcv_nxt - seqno;
      p = inseg.p;
      LWIP_ASSERT("inseg.p != NULL", inseg.p);
      LWIP_ASSERT("insane offset!", (off < 0x7fff));
      if (inseg.p->len < off) {
        LWIP_ASSERT("pbuf too short!", (((s32_t)inseg.p->tot_len) >= off));
        new_tot_len = (u16_t)(inseg.p->tot_len - off);
        while (p->len < off) {
          off -= p->len;
          /* KJM following line changed (with addition of new_tot_len var)
             to fix bug #9076
             inseg.p->tot_len -= p->len; */
          p->tot_len = new_tot_len;
          p->len = 0;
          p = p->next;
        }
        pbuf_unheader(p, off);
      } else {
        pbuf_unheader(inseg.p, off);
      }
      inseg.len -= (u16_t)(pcb->rcv_nxt - seqno);
      inseg.tcphdr->seqno = seqno = pcb->rcv_nxt;
    }
    else {
      if (TCP_SEQ_LT(seqno, pcb->rcv_nxt)) {
        /* the whole segment is < rcv_nxt */
        /* must be a duplicate of a packet that has already been correctly handled */

        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_receive: duplicate seqno %"U32_F"\n", seqno));
        tcp_ack_now(pcb);
      }
    }

    /* The sequence number must be within the window (above rcv_nxt
       and below rcv_nxt + rcv_wnd) in order to be further
       processed. */
    if (TCP_SEQ_BETWEEN(seqno, pcb->rcv_nxt,
                        pcb->rcv_nxt + pcb->rcv_wnd - 1)) {
      if (pcb->rcv_nxt == seqno) {
        /* The incoming segment is the next in sequence. We check if
           we have to trim the end of the segment and update rcv_nxt
           and pass the data to the application. */
        tcplen = TCP_TCPLEN(&inseg);

        if (tcplen > pcb->rcv_wnd) {
          LWIP_DEBUGF(TCP_INPUT_DEBUG,
                      ("tcp_receive: other end overran receive window"
                       "seqno %"U32_F" len %"U16_F" right edge %"U32_F"\n",
                       seqno, tcplen, pcb->rcv_nxt + pcb->rcv_wnd));
          if (TCPH_FLAGS(inseg.tcphdr) & TCP_FIN) {
            /* Must remove the FIN from the header as we're trimming
             * that byte of sequence-space from the packet */
            TCPH_FLAGS_SET(inseg.tcphdr, TCPH_FLAGS(inseg.tcphdr) & ~(unsigned int)TCP_FIN);
          }
          /* Adjust length of segment to fit in the window. */
          TCPWND_CHECK16(pcb->rcv_wnd);
          inseg.len = (u16_t)pcb->rcv_wnd;
          if (TCPH_FLAGS(inseg.tcphdr) & TCP_SYN) {
            inseg.len -= 1;
          }
          pbuf_realloc(inseg.p, inseg.len);
          tcplen = TCP_TCPLEN(&inseg);
          LWIP_ASSERT("tcp_receive: segment not trimmed correctly to rcv_wnd\n",
                      (seqno + tcplen) == (pcb->rcv_nxt + pcb->rcv_wnd));
        }

        pcb->rcv_nxt = seqno + tcplen;

        /* Update the receiver's (our) window. */
        LWIP_ASSERT("tcp_receive: tcplen > rcv_wnd\n", pcb->rcv_wnd >= tcplen);
        pcb->rcv_wnd -= tcplen;

        tcp_update_rcv_ann_wnd(pcb);

        /* If there is data in the segment, we make preparations to
           pass this up to the application. The ->recv_data variable
           is used for holding the pbuf that goes to the
           application. The code for reassembling out-of-sequence data
           chains its data on this pbuf as well.

           If the segment was a FIN, we set the TF_GOT_FIN flag that will
           be used to indicate to the application that the remote side has
           closed its end of the connection. */
        if (inseg.p->tot_len > 0) {
          recv_data = inseg.p;
          /* Since this pbuf now is the responsibility of the
             application, we delete our reference to it so that we won't
             (mistakingly) deallocate it. */
          inseg.p = NULL;
        }
        if (TCPH_FLAGS(inseg.tcphdr) & TCP_FIN) {
          LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_receive: received FIN.\n"));
          recv_flags |= TF_GOT_FIN;
        }

        /* Acknowledge the segment(s). */
        tcp_ack(pcb);

#if LWIP_IPV6 && LWIP_ND6_TCP_REACHABILITY_HINTS
        if (PCB_ISIPV6(pcb)) {
          /* Inform neighbor reachability of forward progress. */
          nd6_reachability_hint(ip6_current_src_addr());
        }
#endif /* LWIP_IPV6 && LWIP_ND6_TCP_REACHABILITY_HINTS*/

      } else {
        /* We get here if the incoming segment is out-of-sequence. */
        tcp_send_empty_ack(pcb);
      }
    } else {
      /* The incoming segment is not within the window. */
      tcp_send_empty_ack(pcb);
    }
  } else {
    /* Segments with length 0 is taken care of here. Segments that
       fall out of the window are ACKed. */
    if (!TCP_SEQ_BETWEEN(seqno, pcb->rcv_nxt, pcb->rcv_nxt + pcb->rcv_wnd - 1)) {
      tcp_ack_now(pcb);
    }
  }
}

static u8_t
tcp_getoptbyte(void)
{
  LWIP_ASSERT("tcp_getoptbyte: index out of range", tcp_optidx < tcphdr_optlen);
  LWIP_ASSERT("tcp_getoptbyte: opt2 inconsistency", tcp_optidx < tcphdr_opt1len || tcphdr_opt2 != NULL);
  
  if (tcp_optidx < tcphdr_opt1len) {
    u8_t *opts = (u8_t *)tcphdr + TCP_HLEN;
    return opts[tcp_optidx++];
  } else {
    u8_t idx = (u8_t)(tcp_optidx++ - tcphdr_opt1len);
    return tcphdr_opt2[idx];
  }
}

static u16_t tcp_optrem(void)
{
  return tcphdr_optlen - tcp_optidx;
}

/**
 * Parses the options contained in the incoming segment.
 *
 * Called from tcp_listen_input() and tcp_process().
 * Currently, only the MSS option is supported!
 *
 * @param pcb the tcp_pcb for which a segment arrived
 */
static void
tcp_parseopt(struct tcp_pcb *pcb)
{
  u8_t data;
  u16_t mss;
#if LWIP_TCP_TIMESTAMPS
  u32_t tsval;
#endif

  for (tcp_optidx = 0; tcp_optidx < tcphdr_optlen; ) {
    u8_t opt = tcp_getoptbyte();
    switch (opt) {
    case LWIP_TCP_OPT_EOL:
      /* End of options. */
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: EOL\n"));
      return;
    case LWIP_TCP_OPT_NOP:
      /* NOP option. */
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: NOP\n"));
      break;
    case LWIP_TCP_OPT_MSS:
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: MSS\n"));
      if (tcp_optrem() < 1 ||
          tcp_getoptbyte() != LWIP_TCP_OPT_LEN_MSS ||
          tcp_optrem() < LWIP_TCP_OPT_LEN_MSS - 2) {
        /* Bad length */
        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: bad length\n"));
        return;
      }
      /* An MSS option with the right option length. */
      mss  = (u16_t)tcp_getoptbyte() << 8;
      mss |= (u16_t)tcp_getoptbyte() << 0;
      /* Limit the mss to the configured TCP_MSS and prevent division by zero */
      pcb->mss = (mss > TCP_MSS || mss == 0) ? TCP_MSS : mss;
      break;
#if LWIP_TCP_TIMESTAMPS
    case LWIP_TCP_OPT_TS:
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: TS\n"));
      if (tcp_optrem() < 1 ||
          tcp_getoptbyte() != LWIP_TCP_OPT_LEN_TS ||
          tcp_optrem() < LWIP_TCP_OPT_LEN_TS - 2) {
        /* Bad length */
        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: bad length\n"));
        return;
      }
      /* TCP timestamp option with valid length */
      tsval  = (u32_t)tcp_getoptbyte() << 24;
      tsval |= (u32_t)tcp_getoptbyte() << 16;
      tsval |= (u32_t)tcp_getoptbyte() << 8;
      tsval |= (u32_t)tcp_getoptbyte() << 0;
      if ((flags & TCP_SYN)) {
        pcb->ts_recent = tsval;
        /* Enable sending timestamps in every segment now that we know
            the remote host supports it. */
        pcb->flags |= TF_TIMESTAMP;
      }
      else if (TCP_SEQ_BETWEEN(pcb->ts_lastacksent, seqno, (u32_t)(seqno+tcplen))) {
        pcb->ts_recent = tsval;
      }
      /* Advance to next option (6 bytes already read) */
      tcp_optidx += LWIP_TCP_OPT_LEN_TS - 6;
      break;
#endif
    default:
      LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: other\n"));
      if (tcp_optrem() < 1 ||
          (data = tcp_getoptbyte()) < 2 ||
          tcp_optrem() < data - 2) {
        LWIP_DEBUGF(TCP_INPUT_DEBUG, ("tcp_parseopt: bad length\n"));
        /* If the length field is zero, the options are malformed
            and we don't process them further. */
        return;
      }
      /* All other options have a length field, so that we easily
          can skip past them. */
      tcp_optidx += data - 2;
    }
  }
}

#endif /* LWIP_TCP */
