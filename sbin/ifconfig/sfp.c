/*-
 * Copyright (c) 2014 Alexander V. Chernikov. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>

#include <math.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ifconfig.h"

/* 2wire addresses */
#define	SFP_ADDR_MSA	0xA0	/* Identification data */
#define	SFP_ADDR_DDM	0xA2	/* digital monitoring interface */

/* Definitions from Table 3.1 */
#define SFP_MSA_IDENTIFIER	0	/* Type of transceiver (T. 3.2), 1B */
#define	SFP_MSA_CONNECTOR	2	/* Connector type (T. 3.3), 1B */

#define	SFP_MSA_TRANSCEIVER_CLASS	3	/* Ethernet/Sonet/IB code, 1B */

#define	SFP_MSA_VENDOR_NAME	20	/* ASCII vendor name, 16B */
#define	SFP_MSA_VENDOR_PN	40	/* ASCII vendor partnum, 16B */
#define	SFP_MSA_VENDOR_SN	68	/* ASCII vendor serialnum, 16B */
#define	SFP_MSA_VENDOR_DATE	84	/* Vendor's date code, 8B */
#define	SFP_MSA_DMONTYPE	92	/* Type of disagnostic monitoring, 1B */

/* Definitions from table 3.17 */
#define	SFP_DDM_TEMP		96	/* Module temperature, 2B */
#define	SFP_DDM_TXPOWER		102	/* Measured TX output power, 2B */
#define	SFP_DDM_RXPOWER		104	/* Measured RX input power, 2B */

struct i2c_info;
typedef int (read_i2c)(struct i2c_info *ii, uint8_t addr, uint8_t off,
    uint8_t len, caddr_t buf);

struct i2c_info {
	int s;
	int error;
	struct ifreq *ifr;
	read_i2c *f;
	uint8_t diag_type;
	char *textbuf;
	size_t bufsize;
};

struct _nv {
	int v;
	const char *n;
};

const char *find_value(struct _nv *x, int value);
const char *find_zero_bit(struct _nv *x, int value, int sz);


/* SFF-8472 Rev. 11.4 table 3.2: Identifier values */
static struct _nv ids[] = {
	{ 0x00, "Unknown" },
	{ 0x01, "GBIC" },
	{ 0x02, "SFF" },
	{ 0x03, "SFP/SFP+" },
	{ 0x04, "300 pin XBI" },
	{ 0x05, "Xenpak" },
	{ 0x06, "XFP" },
	{ 0x07, "XFF" },
	{ 0x08, "XFP-E" },
	{ 0x09, "XPak" },
	{ 0x0A, "X2" },
	{ 0x0B, "DWDM-SFP/DWDM-SFP+" },
	{ 0x0C, "QSFP" },
	{ 0, NULL, },
};

/* SFF-8472 Rev. 11.4 table 3.4: Connector values */
static struct _nv conn[] = {
	{ 0x00, "Unknown" },
	{ 0x01, "SC" },
	{ 0x02, "Fibre Channel Style 1 copper" },
	{ 0x03, "Fibre Channel Style 2 copper" },
	{ 0x04, "BNC/TNC" },
	{ 0x05, "Fibre Channel coaxial" },
	{ 0x06, "FiberJack" },
	{ 0x07, "LC" },
	{ 0x08, "MT-RJ" },
	{ 0x09, "MU" },
	{ 0x0A, "SG" },
	{ 0x0B, "Optical pigtail" },
	{ 0x0C, "MPO Parallel Optic" },
	{ 0x20, "HSSDC II" },
	{ 0x21, "Copper pigtail" },
	{ 0x22, "RJ45" },
	{ 0, NULL }
};

const char *
find_value(struct _nv *x, int value)
{
	for (; x->n != NULL; x++)
		if (x->v == value)
			return (x->n);
	return (NULL);
}

const char *
find_zero_bit(struct _nv *x, int value, int sz)
{
	int v, m;
	const char *s;

	v = 1;
	for (v = 1, m = 1 << (8 * sz); v < m; v *= 2) {
		if ((value & v) == 0)
			continue;
		if ((s = find_value(x, value & v)) != NULL) {
			value &= ~v;
			return (s);
		}
	}

	return (NULL);
}

static void
get_sfp_identifier(struct i2c_info *ii, char *buf, size_t size)
{
	const char *x;
	uint8_t data;

	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_IDENTIFIER, 1, (caddr_t)&data);

	if ((x = find_value(ids, data)) == NULL) {
		if (data > 0x80)
			x = "Vendor specific";
		else
			x = "Reserved";
	}

	snprintf(buf, size, "%s", x);
}

static void
get_sfp_connector(struct i2c_info *ii, char *buf, size_t size)
{
	const char *x;
	uint8_t data;

	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_CONNECTOR, 1, (caddr_t)&data);

	if ((x = find_value(conn, data)) == NULL) {
		if (data >= 0x0D && data <= 0x1F)
			x = "Unallocated";
		else if (data >= 0x23 && data <= 0x7F)
			x = "Unallocated";
		else
			x = "Vendor specific";
	}

	snprintf(buf, size, "%s", x);
}

/* SFF-8472 Rev. 11.4 table 3.5: Transceiver codes */
/* 10G Ethernet compliance codes, byte 3 */
static struct _nv eth_10g[] = {
	{ 0x80, "10G Base-ER" },
	{ 0x40, "10G Base-LRM" },
	{ 0x20, "10G Base-LR" },
	{ 0x10, "10G Base-SR" },
	{ 0x08, "1X SX" },
	{ 0x04, "1X LX" },
	{ 0x02, "1X Copper Active" },
	{ 0x01, "1X Copper Passive" },
	{ 0, NULL }
};

/* Ethernet compliance codes, byte 6 */
static struct _nv eth_compat[] = {
	{ 0x80, "BASE-PX" },
	{ 0x40, "BASE-BX10" },
	{ 0x20, "100BASE-FX" },
	{ 0x10, "100BASE-LX/LX10" },
	{ 0x08, "1000BASE-T" },
	{ 0x04, "1000BASE-CX" },
	{ 0x02, "1000BASE-LX" },
	{ 0x01, "1000BASE-SX" },
	{ 0, NULL }
};

/* FC link length, byte 7 */
static struct _nv fc_len[] = {
	{ 0x80, "very long distance" },
	{ 0x40, "short distance" },
	{ 0x20, "intermediate distance" },
	{ 0x10, "long distance" },
	{ 0x08, "medium distance" },
	{ 0, NULL }
};

/* Channel/Cable technology, byte 7-8 */
static struct _nv cab_tech[] = {
	{ 0x0400, "Shortwave laser (SA)" },
	{ 0x0200, "Longwave laser (LC)" },
	{ 0x0100, "Electrical inter-enclosure (EL)" },
	{ 0x80, "Electrical intra-enclosure (EL)" },
	{ 0x40, "Shortwave laser (SN)" },
	{ 0x20, "Shortwave laser (SL)" },
	{ 0x10, "Longwave laser (LL)" },
	{ 0x08, "Active Cable" },
	{ 0x04, "Passive Cable" },
	{ 0, NULL }
};

/* FC Transmission media, byte 9 */
static struct _nv fc_media[] = {
	{ 0x80, "Twin Axial Pair" },
	{ 0x40, "Twisted Pair" },
	{ 0x20, "Miniature Coax" },
	{ 0x10, "Viao Coax" },
	{ 0x08, "Miltimode, 62.5um" },
	{ 0x04, "Multimode, 50um" },
	{ 0x02, "" },
	{ 0x01, "Single Mode" },
	{ 0, NULL }
};

/* FC Speed, byte 10 */
static struct _nv fc_speed[] = {
	{ 0x80, "1200 MBytes/sec" },
	{ 0x40, "800 MBytes/sec" },
	{ 0x20, "1600 MBytes/sec" },
	{ 0x10, "400 MBytes/sec" },
	{ 0x08, "3200 MBytes/sec" },
	{ 0x04, "200 MBytes/sec" },
	{ 0x01, "100 MBytes/sec" },
	{ 0, NULL }
};

static void
printf_sfp_transceiver_descr(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[12];
	const char *tech_class, *tech_len, *tech_tech, *tech_media, *tech_speed;

	tech_class = NULL;
	tech_len = NULL;
	tech_tech = NULL;
	tech_media = NULL;
	tech_speed = NULL;

	/* Read bytes 3-10 at once */
	ii->f(ii, SFP_ADDR_MSA, 3, 8, &xbuf[3]);

	/* Check 10G first */
	tech_class = find_zero_bit(eth_10g, xbuf[3], 1);
	if (tech_class == NULL) {
		/* No match. Try 1G */
		tech_class = find_zero_bit(eth_compat, xbuf[6], 1);
	}

	tech_len = find_zero_bit(fc_len, xbuf[7], 1);
	tech_tech = find_zero_bit(cab_tech, xbuf[7] << 8 | xbuf[8], 2);
	tech_media = find_zero_bit(fc_media, xbuf[9], 1);
	tech_speed = find_zero_bit(fc_speed, xbuf[10], 1);

	printf("Class: %s\n", tech_class);
	printf("Length: %s\n", tech_len);
	printf("Tech: %s\n", tech_tech);
	printf("Media: %s\n", tech_media);
	printf("Speed: %s\n", tech_speed);
}

static void
get_sfp_transceiver_class(struct i2c_info *ii, char *buf, size_t size)
{
	const char *tech_class;
	uint8_t code;

	/* Check 10G Ethernet/IB first */
	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_TRANSCEIVER_CLASS, 1, (caddr_t)&code);
	tech_class = find_zero_bit(eth_10g, code, 1);
	if (tech_class == NULL) {
		/* No match. Try Ethernet 1G */
		ii->f(ii, SFP_ADDR_MSA, 6, 1, (caddr_t)&code);
		tech_class = find_zero_bit(eth_compat, code, 1);
	}

	if (tech_class == NULL)
		tech_class = "Unknown";

	snprintf(buf, size, "%s", tech_class);
}


static void
get_sfp_vendor_name(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[17], *p;

	memset(xbuf, 0, sizeof(xbuf));
	/* ASCII String, right-padded with 0x20 */
	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_VENDOR_NAME, 16, xbuf);
	for (p = &xbuf[16]; *(p - 1) == 0x20; p--)
		;
	*p = '\0';

	snprintf(buf, size, "%s", xbuf);
}

static void
get_sfp_vendor_pn(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[17], *p;

	memset(xbuf, 0, sizeof(xbuf));
	/* ASCII String, right-padded with 0x20 */
	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_VENDOR_PN, 16, xbuf);
	for (p = &xbuf[16]; *(p - 1) == 0x20; p--)
		;
	*p = '\0';

	snprintf(buf, size, "%s", xbuf);
}

static void
get_sfp_vendor_sn(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[17], *p;

	memset(xbuf, 0, sizeof(xbuf));
	/* ASCII String, right-padded with 0x20 */
	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_VENDOR_SN, 16, xbuf);
	for (p = &xbuf[16]; *(p - 1) == 0x20; p--)
		;
	*p = '\0';
	snprintf(buf, size, "%s", xbuf);
}

static void
get_sfp_vendor_date(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[6];

	memset(xbuf, 0, sizeof(xbuf));
	/* Date code, see Table 3.8 for description */
	ii->f(ii, SFP_ADDR_MSA, SFP_MSA_VENDOR_DATE, 6, xbuf);
	snprintf(buf, size, "20%c%c-%c%c-%c%c", xbuf[0], xbuf[1],
	    xbuf[2], xbuf[3], xbuf[4], xbuf[5]);
}

static void
print_sfp_vendor(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[80];

	memset(xbuf, 0, sizeof(xbuf));
	get_sfp_vendor_name(ii, xbuf, 20);
	get_sfp_vendor_pn(ii, &xbuf[20], 20);
	get_sfp_vendor_sn(ii, &xbuf[40], 20);
	get_sfp_vendor_date(ii, &xbuf[60], 20);

	snprintf(buf, size, "vendor: %s PN: %s SN: %s DATE: %s",
	    xbuf, &xbuf[20],  &xbuf[40], &xbuf[60]);
}

static void
get_sfp_temp(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[2];

	int8_t major;
	uint8_t minor;
	int k;

	memset(xbuf, 0, sizeof(xbuf));
	ii->f(ii, SFP_ADDR_DDM, SFP_DDM_TEMP, 2, xbuf);

	/* Convert temperature to string according to table 3.13 */
	major = (int8_t)xbuf[0];
	minor = (uint8_t)buf[1];
	k = minor * 1000 / 256;

	snprintf(buf, size, "%d.%d C", major, k / 100);
}

static void
convert_power(struct i2c_info *ii, char *xbuf, char *buf, size_t size)
{
	uint16_t mW;
	double dbm;

	mW = ((uint8_t)xbuf[0] << 8) + (uint8_t)xbuf[1];

	/* Convert mw to dbm */
	dbm = 10.0 * log10(1.0 * mW / 10000);

	/* Table 3.9, bit 5 is set, internally calibrated */
	if ((ii->diag_type & 0x20) != 0) {
		snprintf(buf, size, "%d.%02d mW (%.2f dBm)",
	    	    mW / 10000, (mW % 10000) / 100, dbm);
	}
}

static void
get_sfp_rx_power(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[2];

	memset(xbuf, 0, sizeof(xbuf));
	ii->f(ii, SFP_ADDR_DDM, SFP_DDM_RXPOWER, 2, xbuf);
	convert_power(ii, xbuf, buf, size);
}

static void
get_sfp_tx_power(struct i2c_info *ii, char *buf, size_t size)
{
	char xbuf[2];

	memset(xbuf, 0, sizeof(xbuf));
	ii->f(ii, SFP_ADDR_DDM, SFP_DDM_TXPOWER, 2, xbuf);
	convert_power(ii, xbuf, buf, size);
}

/* Intel ixgbe-specific structures and handlers */
struct ixgbe_i2c_req {
	uint8_t dev_addr;
	uint8_t	offset;
	uint8_t len;
	uint8_t data[8];
};
#define	SIOCGI2C	SIOCGIFGENERIC

static int
read_i2c_ixgbe(struct i2c_info *ii, uint8_t addr, uint8_t off, uint8_t len,
    caddr_t buf)
{
	struct ixgbe_i2c_req ixreq;
	int i;

	if (ii->error != 0)
		return (ii->error);

	ii->ifr->ifr_data = (caddr_t)&ixreq;

	memset(&ixreq, 0, sizeof(ixreq));
	ixreq.dev_addr = addr;

	for (i = 0; i < len; i += 1) {
		ixreq.offset = off + i;
		ixreq.len = 1;

		if (ioctl(ii->s, SIOCGI2C, ii->ifr) != 0) {
			ii->error = errno;
			return (errno);
		}
		memcpy(&buf[i], ixreq.data, 1);
	}

	return (0);
}

void
sfp_status(int s, struct ifreq *ifr, int verbose)
{
	struct i2c_info ii;
	char buf[80], buf2[40], buf3[40];

	/*
	 * Check if we have i2c support for particular driver.
	 * TODO: Determine driver by original name.
	 */
	memset(&ii, 0, sizeof(ii));
	if (strncmp(ifr->ifr_name, "ix", 2) == 0) {
		ii.f = read_i2c_ixgbe;
	} else
		return;

	/* Prepare necessary into to pass to NIC handler */
	ii.s = s;
	ii.ifr = ifr;

	/* Read diagnostic monitoring type */
	ii.f(&ii, SFP_ADDR_MSA, SFP_MSA_DMONTYPE, 1, (caddr_t)&ii.diag_type);

	/* Transceiver type */
	get_sfp_identifier(&ii, buf, sizeof(buf));
	get_sfp_transceiver_class(&ii, buf2, sizeof(buf2));
	get_sfp_connector(&ii, buf3, sizeof(buf3));
	if (ii.error == 0)
		printf("\ti2c: %s %s (%s)\n", buf, buf2, buf3);
	if (verbose > 2)
		printf_sfp_transceiver_descr(&ii, buf, sizeof(buf));
	print_sfp_vendor(&ii, buf, sizeof(buf));
	if (ii.error == 0)
		printf("\t%s\n", buf);
	/*
	 * Request current measurements iff they are implemented:
	 * Bit 6 must be set.
	 */
	if ((ii.diag_type & 0x40) != 0) {
		get_sfp_temp(&ii, buf, sizeof(buf));
		get_sfp_rx_power(&ii, buf2, sizeof(buf2));
		get_sfp_tx_power(&ii, buf3, sizeof(buf3));
		printf("\tTemp: %s RX: %s TX: %s\n", buf, buf2, buf3);
	}

	close(s);
}

