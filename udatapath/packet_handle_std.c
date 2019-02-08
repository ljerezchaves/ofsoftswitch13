/* Copyright (c) 2011, TrafficLab, Ericsson Research, Hungary
 * Copyright (c) 2012, CPqD, Brazil
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Ericsson Research nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include "packet_handle_std.h"
#include "packet.h"
#include "packets.h"
#include "oflib/ofl-structs.h"
#include "openflow/openflow.h"
#include "compiler.h"
#include "lib/hash.h"
#include "oflib/oxm-match.h"

void packet_parse (struct packet const *pkt, struct ofl_match *m, struct protocols_std *proto);

void
packet_parse (struct packet const *pkt, struct ofl_match *m, struct protocols_std *proto)
{
    size_t offset = 0;
    uint16_t eth_type = 0x0000;
    uint8_t next_proto = 0;

    /* Resets all protocol fields to NULL */
    protocol_reset (proto);

    /* Ethernet II */
    if (unlikely (pkt->buffer->size < offset + sizeof (struct eth_header))) return;
    proto->eth = (struct eth_header *)((uint8_t *)pkt->buffer->data + offset);
    offset += sizeof (struct eth_header);
    eth_type = ntohs(proto->eth->eth_type);

    if (eth_type >= ETH_TYPE_II_START) {
        /* Ethernet II */
        ofl_structs_match_put_eth (m, OXM_OF_ETH_SRC, proto->eth->eth_src);
        ofl_structs_match_put_eth (m, OXM_OF_ETH_DST, proto->eth->eth_dst);
        if (eth_type != ETH_TYPE_VLAN && eth_type != ETH_TYPE_VLAN_PBB) {
            ofl_structs_match_put16 (m, OXM_OF_ETH_TYPE, eth_type);
        }
    } else {
        /* Ethernet 802.3 */
        struct llc_header *llc;
        
        if (unlikely (pkt->buffer->size < offset + sizeof (struct llc_header))) return;
        llc = (struct llc_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct llc_header);

        if (unlikely (llc->llc_dsap != LLC_DSAP_SNAP || 
                      llc->llc_ssap != LLC_SSAP_SNAP ||
                      llc->llc_cntl != LLC_CNTL_SNAP)) {
            return;
        }

        if (unlikely (pkt->buffer->size < offset + sizeof (struct snap_header))) return;
        proto->eth_snap = (struct snap_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct snap_header);

        if (unlikely (memcmp (proto->eth_snap->snap_org, SNAP_ORG_ETHERNET, 
                              sizeof (SNAP_ORG_ETHERNET)) != 0)) {
            return;
        }

        eth_type = ntohs(proto->eth->eth_type);
        ofl_structs_match_put_eth (m, OXM_OF_ETH_SRC, proto->eth->eth_src);
        ofl_structs_match_put_eth (m, OXM_OF_ETH_DST, proto->eth->eth_dst);
        ofl_structs_match_put16 (m, OXM_OF_ETH_TYPE, eth_type);
    }

    /* VLAN */
    if (eth_type == ETH_TYPE_VLAN || eth_type == ETH_TYPE_VLAN_PBB) {
        uint16_t vlan_id;
        uint8_t vlan_pcp;
        
        if (unlikely (pkt->buffer->size < offset + sizeof (struct vlan_header))) return;
        proto->vlan = (struct vlan_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct vlan_header);
        proto->vlan_last = proto->vlan;
        
        vlan_id = (ntohs (proto->vlan->vlan_tci) & VLAN_VID_MASK) >> VLAN_VID_SHIFT;
        vlan_pcp = (ntohs (proto->vlan->vlan_tci) & VLAN_PCP_MASK) >> VLAN_PCP_SHIFT;
        ofl_structs_match_put16 (m, OXM_OF_VLAN_VID, vlan_id);
        ofl_structs_match_put8 (m, OXM_OF_VLAN_PCP, vlan_pcp);

        /* Skip through rest of VLAN tags */
        eth_type = ntohs (proto->vlan->vlan_next_type);
        while (eth_type == ETH_TYPE_VLAN || eth_type == ETH_TYPE_VLAN_PBB) {
            if (unlikely (pkt->buffer->size < offset + sizeof (struct vlan_header))) return;
            proto->vlan_last = (struct vlan_header *)((uint8_t *)pkt->buffer->data + offset);
            offset += sizeof (struct vlan_header);
            eth_type = ntohs (proto->vlan_last->vlan_next_type);
        }
        
        /* Set the Ethernet type */
        ofl_structs_match_put16 (m, OXM_OF_ETH_TYPE, eth_type);
    }

    /* PBB ISID */
    if (eth_type == ETH_TYPE_PBB) {
        uint32_t isid;
        
        if (unlikely (pkt->buffer->size < offset + sizeof(struct pbb_header))) return;
        proto->pbb = (struct pbb_header*)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct pbb_header);

        isid = ntohl (proto->pbb->id) & PBB_ISID_MASK;
        ofl_structs_match_put32 (m, OXM_OF_PBB_ISID, isid);

        /* No processing past PBB ISID */
        return;
    }

    if (eth_type == ETH_TYPE_MPLS || eth_type == ETH_TYPE_MPLS_MCAST) {
        uint32_t mpls_label;
        uint32_t mpls_tc;
        uint32_t mpls_bos;

        if (unlikely (pkt->buffer->size < offset + sizeof (struct mpls_header))) return;
        proto->mpls = (struct mpls_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct mpls_header);

        mpls_label = (ntohl (proto->mpls->fields) & MPLS_LABEL_MASK) >> MPLS_LABEL_SHIFT;
        mpls_tc = (ntohl (proto->mpls->fields) & MPLS_TC_MASK) >> MPLS_TC_SHIFT;
        mpls_bos = (ntohl (proto->mpls->fields) & MPLS_S_MASK) >> MPLS_S_SHIFT;
        ofl_structs_match_put32 (m, OXM_OF_MPLS_LABEL, mpls_label);
        ofl_structs_match_put8 (m, OXM_OF_MPLS_TC, mpls_tc);
        ofl_structs_match_put8 (m, OXM_OF_MPLS_BOS, mpls_bos);
        
        /* No processing past MPLS */
        return;
    }

    /* ARP */
    if (eth_type == ETH_TYPE_ARP) {
        uint32_t arp_op;

        if (unlikely (pkt->buffer->size < offset + sizeof (struct arp_eth_header))) return;
        proto->arp = (struct arp_eth_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct arp_eth_header);

        if (ntohs (proto->arp->ar_hrd) == 1 &&
            ntohs (proto->arp->ar_pro) == ETH_TYPE_IP &&
            proto->arp->ar_hln == ETH_ADDR_LEN &&
            proto->arp->ar_pln == 4) {
            
            arp_op = ntohs (proto->arp->ar_op);
            ofl_structs_match_put16 (m, OXM_OF_ARP_OP, arp_op);

            if (arp_op == ARP_OP_REQUEST || arp_op == ARP_OP_REPLY) {
                ofl_structs_match_put_eth (m, OXM_OF_ARP_SHA, proto->arp->ar_sha);
                ofl_structs_match_put_eth (m,OXM_OF_ARP_THA, proto->arp->ar_tha);
                ofl_structs_match_put32 (m, OXM_OF_ARP_SPA, proto->arp->ar_spa);
                ofl_structs_match_put32 (m, OXM_OF_ARP_TPA, proto->arp->ar_tpa);
            }
        }

        /* No processing past ARP */
        return;
    }

    /* IPv4 */
    if (eth_type == ETH_TYPE_IP) {
        if (unlikely (pkt->buffer->size < offset + sizeof (struct ip_header))) return;
        proto->ipv4 = (struct ip_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct ip_header);

        ofl_structs_match_put8 (m, OXM_OF_IP_PROTO, proto->ipv4->ip_proto);
        ofl_structs_match_put32 (m, OXM_OF_IPV4_SRC, proto->ipv4->ip_src);
        ofl_structs_match_put32 (m, OXM_OF_IPV4_DST, proto->ipv4->ip_dst);
        ofl_structs_match_put8 (m, OXM_OF_IP_ECN, (proto->ipv4->ip_tos & IP_ECN_MASK));
        ofl_structs_match_put8 (m, OXM_OF_IP_DSCP, (proto->ipv4->ip_tos >> 2));
        
        /* No further processing for fragmented IPv4 */
        if (IP_IS_FRAGMENT (proto->ipv4->ip_frag_off)) return;

        next_proto = proto->ipv4->ip_proto;
    }

    /* IPv6 */
    else if (eth_type == ETH_TYPE_IPV6) {
        uint32_t ipv6_fl;
        
        if (unlikely (pkt->buffer->size < offset + sizeof (struct ipv6_header))) return;
        proto->ipv6 = (struct ipv6_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct ipv6_header);

        ipv6_fl = IPV6_FLABEL (ntohl (proto->ipv6->ipv6_ver_tc_fl));

        ofl_structs_match_put8 (m, OXM_OF_IP_PROTO, proto->ipv6->ipv6_next_hd);
        ofl_structs_match_put_ipv6 (m, OXM_OF_IPV6_SRC, proto->ipv6->ipv6_src.s6_addr);
        ofl_structs_match_put_ipv6 (m, OXM_OF_IPV6_DST, proto->ipv6->ipv6_dst.s6_addr);            
        ofl_structs_match_put32 (m, OXM_OF_IPV6_FLABEL, ipv6_fl);
        
        next_proto = proto->ipv6->ipv6_next_hd;
        /* TODO: Check for extension headers */
    }

    /* TCP */
    if (next_proto == IP_TYPE_TCP) {
        if (unlikely (pkt->buffer->size < offset + sizeof (struct tcp_header))) return;
        proto->tcp = (struct tcp_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct tcp_header);

        ofl_structs_match_put16 (m, OXM_OF_TCP_SRC, ntohs (proto->tcp->tcp_src));
        ofl_structs_match_put16 (m, OXM_OF_TCP_DST, ntohs (proto->tcp->tcp_dst));

        /* No processing past TCP */
        return;
    }

    /* UDP */
    else if (next_proto == IP_TYPE_UDP) {
        uint16_t src_port;
        uint16_t dst_port;
        
        if (unlikely (pkt->buffer->size < offset + sizeof (struct udp_header))) return;
        proto->udp = (struct udp_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct udp_header);

        src_port = ntohs (proto->udp->udp_src);
        dst_port = ntohs (proto->udp->udp_dst);
        
        ofl_structs_match_put16 (m, OXM_OF_UDP_SRC, src_port);
        ofl_structs_match_put16 (m, OXM_OF_UDP_DST, dst_port);
        
        /* No processing past UDP */
        return;
    }
    
    /* ICMPv4 */
    else if (next_proto == IP_TYPE_ICMP) {
        if (unlikely (pkt->buffer->size < offset + sizeof (struct icmp_header))) return;
        proto->icmp = (struct icmp_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct icmp_header);

        ofl_structs_match_put8 (m, OXM_OF_ICMPV4_TYPE, proto->icmp->icmp_type);
        ofl_structs_match_put8 (m, OXM_OF_ICMPV4_CODE, proto->icmp->icmp_code);

        /* No processing past ICMPv4 */
        return;
    }
    
    /* ICMPv6 */
    else if (next_proto == IPV6_TYPE_ICMPV6) {
        if (unlikely (pkt->buffer->size < offset + sizeof (struct icmp_header))) return;
        proto->icmp = (struct icmp_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct icmp_header);

        ofl_structs_match_put8 (m, OXM_OF_ICMPV6_TYPE, proto->icmp->icmp_type);
        ofl_structs_match_put8 (m, OXM_OF_ICMPV6_CODE, proto->icmp->icmp_code);

        /* IPv6 ND (Neighbor Discovery) */
        if (proto->icmp->icmp_type == ICMPV6_NEIGHSOL ||
            proto->icmp->icmp_type == ICMPV6_NEIGHADV) {
            struct ipv6_nd_header *nd;
            struct ipv6_nd_options_hd *opt;

            if (unlikely (pkt->buffer->size < offset + sizeof (struct ipv6_nd_header))) return;
            nd = (struct ipv6_nd_header *)((uint8_t *)pkt->buffer->data + offset);
            offset += sizeof (struct ipv6_nd_header);

            ofl_structs_match_put_ipv6 (m, OXM_OF_IPV6_ND_TARGET, nd->target_addr.s6_addr);

            if (unlikely (pkt->buffer->size < offset + IPV6_ND_OPT_HD_LEN)) return;
            opt = (struct ipv6_nd_options_hd*)((uint8_t *)pkt->buffer->data + offset);

            if (opt->type == ND_OPT_SLL) {
                uint8_t nd_sll[6];
                memcpy (nd_sll, ((uint8_t *)pkt->buffer->data + offset + IPV6_ND_OPT_HD_LEN),
                        ETH_ADDR_LEN);
                ofl_structs_match_put_eth (m, OXM_OF_IPV6_ND_SLL, nd_sll);
                offset += IPV6_ND_OPT_HD_LEN + ETH_ADDR_LEN;
            } else if (opt->type == ND_OPT_TLL){
                uint8_t nd_tll[6];
                memcpy (nd_tll, ((uint8_t *)pkt->buffer->data + offset + IPV6_ND_OPT_HD_LEN),
                        ETH_ADDR_LEN);
                ofl_structs_match_put_eth (m, OXM_OF_IPV6_ND_TLL, nd_tll);
                offset += IPV6_ND_OPT_HD_LEN + ETH_ADDR_LEN;
            }
        }

        /* No processing past ICMPv6 */
        return;
    }

    /* SCTP */
    else if (next_proto == IP_TYPE_SCTP) {
        if (unlikely (pkt->buffer->size < offset + sizeof (struct sctp_header))) return;
        proto->sctp = (struct sctp_header *)((uint8_t *)pkt->buffer->data + offset);
        offset += sizeof (struct sctp_header);

        ofl_structs_match_put16 (m, OXM_OF_SCTP_SRC, ntohs (proto->sctp->sctp_src));
        ofl_structs_match_put16 (m, OXM_OF_SCTP_SRC, ntohs (proto->sctp->sctp_dst));

        /* No processing past SCTP */
        return;
    }
}

void
packet_handle_std_validate(struct packet_handle_std *handle)
{
    if (handle->valid) {
        return;
    
    } else {
        struct ofl_match *match = &handle->match;
        struct ofl_match_tlv *iter, *next, *field;
        uint64_t metadata = 0;

        match->header.type = OFPMT_OXM;
        
        /* Look for current metadata field */
        HMAP_FOR_EACH_WITH_HASH (field, struct ofl_match_tlv, hmap_node,
                                 hash_int (OXM_OF_METADATA, 0), &match->match_fields) {
            metadata = *((uint64_t *)field->value);
        }

        /* Look for current tunnel_id field */ 
        HMAP_FOR_EACH_WITH_HASH (field, struct ofl_match_tlv, hmap_node,
                                 hash_int (OXM_OF_TUNNEL_ID, 0), &match->match_fields) {
            handle->pkt->tunnel_id = *((uint64_t *)field->value);
        }

        /* Free fields previously allocated */
        HMAP_FOR_EACH_SAFE (iter, next, struct ofl_match_tlv,
                            hmap_node, &match->match_fields) {
            free (iter->value);
            free (iter);
        }

        ofl_structs_match_init (match);
        
        /* Add pipeline fields back to the respective match fields */
        ofl_structs_match_put32 (match, OXM_OF_IN_PORT, handle->pkt->in_port);
        ofl_structs_match_put64 (match, OXM_OF_METADATA, metadata);
        ofl_structs_match_put64 (match, OXM_OF_TUNNEL_ID, handle->pkt->tunnel_id);

        /* Parse the packet */
        packet_parse(handle->pkt, match, handle->proto);
        handle->valid = true;
    }
}

struct packet_handle_std *
packet_handle_std_create(struct packet *pkt) {
	struct packet_handle_std *handle = xmalloc(sizeof(struct packet_handle_std));
	handle->proto = xmalloc(sizeof(struct protocols_std));
	handle->pkt = pkt;

	hmap_init(&handle->match.match_fields);

	handle->valid = false;
	packet_handle_std_validate(handle);

	return handle;
}

struct packet_handle_std *
packet_handle_std_clone(struct packet *pkt, struct packet_handle_std *handle UNUSED) {
    struct packet_handle_std *clone = xmalloc(sizeof(struct packet_handle_std));

    clone->pkt = pkt;
    clone->proto = xmalloc(sizeof(struct protocols_std));
    hmap_init(&clone->match.match_fields);
    clone->valid = false;
    // TODO Zoltan: if handle->valid, then match could be memcpy'd, and protocol
    //              could be offset
    packet_handle_std_validate(clone);

    return clone;
}

void
packet_handle_std_destroy(struct packet_handle_std *handle) {

    struct ofl_match_tlv * iter, *next;
    HMAP_FOR_EACH_SAFE(iter, next, struct ofl_match_tlv, hmap_node, &handle->match.match_fields){
        free(iter->value);
        free(iter);
    }
    free(handle->proto);
    hmap_destroy(&handle->match.match_fields);
    free(handle);
}

bool
packet_handle_std_is_ttl_valid(struct packet_handle_std *handle) {
    packet_handle_std_validate(handle);

    if (handle->proto->mpls != NULL) {
        uint32_t ttl = ntohl(handle->proto->mpls->fields) & MPLS_TTL_MASK;
        if (ttl <= 1) {
            return false;
        }
    }
    if (handle->proto->ipv4 != NULL) {
        if (handle->proto->ipv4->ip_ttl < 1) {
            return false;
        }
    }
    if (handle->proto->ipv6 != NULL) {
        if (handle->proto->ipv6->ipv6_hop_limit < 1) {
            return false;
        }
    }
    return true;
}

bool
packet_handle_std_is_fragment(struct packet_handle_std *handle) {
    packet_handle_std_validate(handle);

    return false;
    /*return ((handle->proto->ipv4 != NULL) &&
            IP_IS_FRAGMENT(handle->proto->ipv4->ip_frag_off));*/
}


bool
packet_handle_std_match(struct packet_handle_std *handle, struct ofl_match *match){

    if (!handle->valid){
        packet_handle_std_validate(handle);
        if (!handle->valid){
            return false;
        }
    }

    return packet_match(match ,&handle->match );
}



/* If pointer is not null, returns str; otherwise returns an empty string. */
static inline const char *
pstr(void *ptr, const char *str) {
    return (ptr == NULL) ? "" : str;
}

/* Prints the names of protocols that are available in the given protocol stack. */

static void
proto_print(FILE *stream, struct protocols_std *p) {
    fprintf(stream, "{%s%s%s%s%s%s%s%s%s%s%s}",
            pstr(p->eth, "eth"), pstr(p->vlan, ",vlan"), pstr(p->mpls, ",mpls"), pstr(p->pbb, ",pbb"), pstr(p->ipv4, ",ipv4"), pstr(p->ipv6, ",ipv6"),
            pstr(p->arp, ",arp"), pstr(p->tcp, ",tcp"), pstr(p->udp, ",udp"), pstr(p->sctp, ",sctp"),
            pstr(p->icmp, ",icmp"));
}

char *
packet_handle_std_to_string(struct packet_handle_std *handle) {
    char *str;
    size_t str_size;
    FILE *stream = open_memstream(&str, &str_size);

    packet_handle_std_print(stream, handle);

    fclose(stream);
    return str;
}

void
packet_handle_std_print(FILE *stream, struct packet_handle_std *handle) {
    packet_handle_std_validate(handle);

    fprintf(stream, "{proto=");
    proto_print(stream, handle->proto);

    fprintf(stream, ", match=");
    ofl_structs_match_print(stream, (struct ofl_match_header *)(&handle->match), handle->pkt->dp->exp);
    fprintf(stream, "\"}");
}

