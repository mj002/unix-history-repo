/* @(#) $Header: /tcpdump/master/tcpdump/oui.h,v 1.3 2005/04/06 20:13:13 hannes Exp $ (LBL) */
/* 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Hannes Gredler (hannes@juniper.net)
 */

extern struct tok oui_values[];
extern struct tok smi_values[];

#define OUI_ENCAP_ETHER 0x000000        /* encapsulated Ethernet */
#define OUI_CISCO       0x00000c        /* Cisco protocols */
#define OUI_CISCO_90    0x0000f8        /* Cisco bridging */
#define OUI_RFC2684     0x0080c2        /* RFC 2684 bridged Ethernet */
#define OUI_APPLETALK   0x080007        /* Appletalk */
#define OUI_JUNIPER     0x009069        /* Juniper */

#define SMI_ACC                      5
#define SMI_CISCO                    9
#define SMI_SHIVA                    166
#define SMI_LIVINGSTON               307
#define SMI_MICROSOFT                311
#define SMI_3COM                     429
#define SMI_ASCEND                   529
#define SMI_BAY                      1584
#define SMI_FOUNDRY                  1991
#define SMI_VERSANET                 2180
#define SMI_REDBACK                  2352
#define SMI_JUNIPER                  2636
#define SMI_APTIS                    2637
#define SMI_COSINE                   3085
#define SMI_SHASTA                   3199
#define SMI_NOMADIX                  3309
#define SMI_UNISPHERE                4874
#define SMI_ISSANNI                  5948
#define SMI_QUINTUM                  6618
#define SMI_COLUBRIS                 8744
#define SMI_COLUMBIA_UNIVERSITY      11862
#define SMI_THE3GPP                  10415
