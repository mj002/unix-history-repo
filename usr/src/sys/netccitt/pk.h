/*
 * Copyright (c) University of British Columbia, 1984
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Laboratory for Computation Vision and the Computer Science Department
 * of the University of British Columbia.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)pk.h	7.4 (Berkeley) %G%
 */

/*
 *
 *  X.25 Packet Level Definitions:
 *
 */

/* Packet type identifier field defintions. */

#define X25_CALL                         11
#define X25_CALL_ACCEPTED                15   
#define X25_CLEAR                        19
#define X25_CLEAR_CONFIRM                23  
#define X25_DATA                          0   
#define X25_INTERRUPT                    35   
#define X25_INTERRUPT_CONFIRM            39   

#define X25_RR                            1   
#define X25_RNR                           5   
#define X25_RESET                        27 
#define X25_RESET_CONFIRM                31   

#define X25_RESTART                     251     
#define X25_RESTART_CONFIRM		255 

/* Restart cause field definitions. */

#define X25_RESTART_LOCAL_PROCEDURE_ERROR 1
#define X25_RESTART_NETWORK_CONGESTION	  3
#define X25_RESTART_NETWORK_OPERATIONAL	  7

/* Miscellaneous definitions. */

#define DATA_PACKET_DESIGNATOR		0x01
#define RR_OR_RNR_PACKET_DESIGNATOR	0x02
#define RR_PACKET_DESIGNATOR		0x04

#define DEFAULT_WINDOW_SIZE		2
#define MODULUS				8

#define ADDRLN				1
#define MAXADDRLN			15
#define FACILITIESLN			1
#define MAXFACILITIESLN			10
#define MAXUSERDATA			16
#define MAXCALLINFOLN			1+15+1+10+16

#define PACKET_OK			0
#define IGNORE_PACKET			1
#define ERROR_PACKET			2

typedef char    bool;
#define FALSE	0
#define TRUE	1

/*
 *  X.25 Packet format definitions
 *  This will eventually have to be rewritten without reference
 *  to bit fields, to be ansi C compliant and allignment safe.
 */

#if BYTE_ORDER == BIG_ENDIAN
#define ORDER2(a, b) a , b
#define ORDER4(a, b, c, d) a , b , c , d
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define ORDER2(a, b) b , a
#define ORDER4(a, b, c, d) d , c , b , a
#endif

typedef u_char octet;

struct x25_calladdr {
	octet ORDER2(calling_addrlen:4, called_addrlen:4);
	octet address_field[MAXADDRLN];
};

struct x25_packet {
	octet ORDER4(q_bit:1, d_bit:1, fmt_identifier:2, lc_group_number:4);
	octet logical_channel_number;
	octet packet_type;
	octet packet_data;
};

struct data_packet {
	octet ORDER4(pr:3, m_bit:1, ps:3, z:1);
};

#define FACILITIES_REVERSE_CHARGE	0x1
#define FACILITIES_THROUGHPUT		0x2
#define FACILITIES_PACKETSIZE		0x42
#define FACILITIES_WINDOWSIZE		0x43

#define PKHEADERLN	3


#define PR(xp)		(((struct data_packet *)&xp -> packet_type)->pr)
#define PS(xp)		(((struct data_packet *)&xp -> packet_type)->ps)
#define MBIT(xp)	(((struct data_packet *)&xp -> packet_type)->m_bit)

struct x25_packet *pk_template ();

/* Define X.25 packet level states. */

/* Call setup and clearing substates.  */

#define LISTEN           0
#define READY            1
#define RECEIVED_CALL    2
#define SENT_CALL        3
#define DATA_TRANSFER    4
#define RECEIVED_CLEAR   5
#define SENT_CLEAR       6

/* DTE states. */

#define DTE_WAITING		7
#define DTE_RECEIVED_RESTART	8
#define DTE_SENT_RESTART	9
#define DTE_READY		0

#define MAXSTATES		10

/*
 *  The following definitions are used in a switch statement after
 *  determining the packet type.  These values are returned by the
 *  pk_decode procedure. 
 */

#define CALL             0 * MAXSTATES
#define CALL_ACCEPTED    1 * MAXSTATES
#define CLEAR            2 * MAXSTATES
#define CLEAR_CONF       3 * MAXSTATES
#define DATA             4 * MAXSTATES
#define INTERRUPT        5 * MAXSTATES
#define INTERRUPT_CONF   6 * MAXSTATES
#define RR               7 * MAXSTATES
#define RNR              8 * MAXSTATES
#define RESET            9 * MAXSTATES
#define RESET_CONF      10 * MAXSTATES
#define RESTART         11 * MAXSTATES
#define RESTART_CONF    12 * MAXSTATES
#define INVALID_PACKET  13 * MAXSTATES
#define DELETE_PACKET	INVALID_PACKET
