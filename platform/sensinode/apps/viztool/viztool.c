/*
 * Copyright (c) 2010, Loughborough University - Computer Science
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \file
 *         Small UDP app used to retrieve neighbor cache and routing table
 *         entries and send them to an external endpoint
 *
 * \author
 *         George Oikonomou - <oikonomou@users.sourceforge.net>
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"

#include <string.h>

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

#define MAX_PAYLOAD_LEN 120

static struct uip_udp_conn *server_conn;
static char buf[MAX_PAYLOAD_LEN];
static int8_t len;

#define SERVER_PORT       60001

/* Request Bits */
#define REQUEST_TYPE_ND    1
#define REQUEST_TYPE_RT    2
#define REQUEST_TYPE_DRT   3

extern uip_ds6_route_t uip_ds6_routing_table[UIP_DS6_ROUTE_NB];
extern uip_ds6_nbr_t uip_ds6_nbr_cache[UIP_DS6_NBR_NB];
extern uip_ds6_defrt_t uip_ds6_defrt_list[UIP_DS6_DEFRT_NB];
extern u16_t uip_len;
/*---------------------------------------------------------------------------*/
static uint8_t
process_request()
{
  uint8_t len;
  uint8_t count; /* How many did we pack? */
  uint8_t i;
  uint8_t left;
  uint8_t entry_size;

  left = MAX_PAYLOAD_LEN - 1;
  len = 2; /* start filling the buffer from position [2] */
  count = 0;
  if(buf[0] == REQUEST_TYPE_ND) {
    /* Neighbors */
    PRINTF("Neighbors\n");
    for(i = buf[1]; i < UIP_DS6_NBR_NB; i++) {
      if(uip_ds6_nbr_cache[i].isused) {
        entry_size = sizeof(i) + sizeof(uip_ipaddr_t) + sizeof(uip_lladdr_t)
            + sizeof(uip_ds6_nbr_cache[i].state);
        PRINTF("%02u: ", i);
        PRINT6ADDR(&uip_ds6_nbr_cache[i].ipaddr);
        PRINTF(" - ");
        PRINTLLADDR(&uip_ds6_nbr_cache[i].lladdr);
        PRINTF(" - %u\n", uip_ds6_nbr_cache[i].state);

        memcpy(buf + len, &i, sizeof(i));
        len += sizeof(i);
        memcpy(buf + len, &uip_ds6_nbr_cache[i].ipaddr, sizeof(uip_ipaddr_t));
        len += sizeof(uip_ipaddr_t);
        memcpy(buf + len, &uip_ds6_nbr_cache[i].lladdr, sizeof(uip_lladdr_t));
        len += sizeof(uip_lladdr_t);
        memcpy(buf + len, &uip_ds6_nbr_cache[i].state,
            sizeof(uip_ds6_nbr_cache[i].state));
        len += sizeof(uip_ds6_nbr_cache[i].state);

        count++;
        left -= entry_size;

        if(left < entry_size) {
          break;
        }
      }
    }
  } else if(buf[0] == REQUEST_TYPE_RT) {
    uint32_t flip = 0;
    PRINTF("Routing table\n");
    for(i = buf[1]; i < UIP_DS6_ROUTE_NB; i++) {
      if(uip_ds6_defrt_list[i].isused) {
        entry_size = sizeof(i) + sizeof(uip_ds6_routing_table[i].ipaddr)
            + sizeof(uip_ds6_routing_table[i].length)
            + sizeof(uip_ds6_routing_table[i].metric)
            + sizeof(uip_ds6_routing_table[i].nexthop)
            + sizeof(uip_ds6_routing_table[i].state.lifetime)
            + sizeof(uip_ds6_routing_table[i].state.learned_from);

        memcpy(buf + len, &i, sizeof(i));
        len += sizeof(i);
        memcpy(buf + len, &uip_ds6_routing_table[i].ipaddr,
            sizeof(uip_ds6_routing_table[i].ipaddr));
        len += sizeof(uip_ds6_routing_table[i].ipaddr);
        memcpy(buf + len, &uip_ds6_routing_table[i].length,
            sizeof(uip_ds6_routing_table[i].length));
        len += sizeof(uip_ds6_routing_table[i].length);
        memcpy(buf + len, &uip_ds6_routing_table[i].metric,
            sizeof(uip_ds6_routing_table[i].metric));
        len += sizeof(uip_ds6_routing_table[i].metric);
        memcpy(buf + len, &uip_ds6_routing_table[i].nexthop,
            sizeof(uip_ds6_routing_table[i].nexthop));
        len += sizeof(uip_ds6_routing_table[i].nexthop);

        PRINT6ADDR(&uip_ds6_routing_table[i].ipaddr);
        PRINTF(" - %02x", uip_ds6_routing_table[i].length);
        PRINTF(" - %02x", uip_ds6_routing_table[i].metric);
        PRINTF(" - ");
        PRINT6ADDR(&uip_ds6_routing_table[i].nexthop);

        flip = uip_htonl(uip_ds6_routing_table[i].state.lifetime);
        memcpy(buf + len, &flip, sizeof(flip));
        len += sizeof(flip);
        PRINTF(" - %08lx", uip_ds6_routing_table[i].state.lifetime);

        memcpy(buf + len, &uip_ds6_routing_table[i].state.learned_from,
            sizeof(uip_ds6_routing_table[i].state.learned_from));
        len += sizeof(uip_ds6_routing_table[i].state.learned_from);

        PRINTF(" - %02x [%u]\n", uip_ds6_routing_table[i].state.learned_from,
             entry_size);

        count++;
        left -= entry_size;

        if(left < entry_size) {
          break;
        }
      }
    }
  } else if (buf[0] == REQUEST_TYPE_DRT) {
    uint32_t flip = 0;
    PRINTF("Default Routes\n");
    for(i = buf[1]; i < UIP_DS6_DEFRT_NB; i++) {
      if(uip_ds6_defrt_list[i].isused) {
        entry_size = sizeof(i) + sizeof(uip_ds6_defrt_list[i].ipaddr)
            + sizeof(uip_ds6_defrt_list[i].isinfinite);

        memcpy(buf + len, &i, sizeof(i));
        len += sizeof(i);
        memcpy(buf + len, &uip_ds6_defrt_list[i].ipaddr,
            sizeof(uip_ds6_defrt_list[i].ipaddr));
        len += sizeof(uip_ds6_defrt_list[i].ipaddr);
        memcpy(buf + len, &uip_ds6_defrt_list[i].isinfinite,
            sizeof(uip_ds6_defrt_list[i].isinfinite));
        len += sizeof(uip_ds6_defrt_list[i].isinfinite);

        PRINT6ADDR(&uip_ds6_defrt_list[i].ipaddr);
        PRINTF(" - %u\n", uip_ds6_defrt_list[i].isinfinite);
        count++;
        left -= entry_size;

        if(left < entry_size) {
          break;
        }
      }
    }
  } else {
    return 0;
  }
  buf[1] = count;
  return len;
}
/*---------------------------------------------------------------------------*/
PROCESS(viztool_process, "Network Visualization Tool Process");
/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  if(uip_newdata()) {
    memset(buf, 0, MAX_PAYLOAD_LEN);

    PRINTF("%u bytes from [", uip_datalen());
    PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
    PRINTF("]:%u\n", UIP_HTONS(UIP_UDP_BUF->srcport));

    memcpy(buf, uip_appdata, uip_datalen());

    len = process_request();
    if( len ) {
      server_conn->rport = UIP_UDP_BUF->srcport;
      uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
      uip_udp_packet_send(server_conn, buf, len);
      PRINTF("Sent %u bytes\n", len);
    }

    /* Restore server connection to allow data from any node */
    uip_create_unspecified(&server_conn->ripaddr);
    server_conn->rport = 0;
  }
  return;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(viztool_process, ev, data)
{

  PROCESS_BEGIN();

  server_conn = udp_new(NULL, UIP_HTONS(0), NULL);
  udp_bind(server_conn, UIP_HTONS(SERVER_PORT));

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/