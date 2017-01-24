/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

void	r92cu_attach(struct rtwn_usb_softc *);
void	r92eu_attach(struct rtwn_usb_softc *);
void	r88eu_attach(struct rtwn_usb_softc *);
void	r12au_attach(struct rtwn_usb_softc *);
void	r21au_attach(struct rtwn_usb_softc *);

enum {
	RTWN_CHIP_RTL8192CU,
	RTWN_CHIP_RTL8192EU,
	RTWN_CHIP_RTL8188EU,
	RTWN_CHIP_RTL8812AU,
	RTWN_CHIP_RTL8821AU,
	RTWN_CHIP_MAX_USB
};

/* various supported device vendors/products */
static const STRUCT_USB_HOST_ID rtwn_devs[] = {
	/* RTL8188CE-VAU/RTL8188CUS/RTL8188RU/RTL8192CU */
#define RTWN_RTL8192CU_DEV(v,p) \
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RTWN_CHIP_RTL8192CU) }
	RTWN_RTL8192CU_DEV(ABOCOM,		RTL8188CU_1),
	RTWN_RTL8192CU_DEV(ABOCOM,		RTL8188CU_2),
	RTWN_RTL8192CU_DEV(ABOCOM,		RTL8192CU),
	RTWN_RTL8192CU_DEV(ASUS,		RTL8192CU),
	RTWN_RTL8192CU_DEV(ASUS,		USBN10NANO),
	RTWN_RTL8192CU_DEV(AZUREWAVE,		RTL8188CE_1),
	RTWN_RTL8192CU_DEV(AZUREWAVE,		RTL8188CE_2),
	RTWN_RTL8192CU_DEV(AZUREWAVE,		RTL8188CU),
	RTWN_RTL8192CU_DEV(BELKIN,		F7D2102),
	RTWN_RTL8192CU_DEV(BELKIN,		RTL8188CU),
	RTWN_RTL8192CU_DEV(BELKIN,		RTL8192CU),
	RTWN_RTL8192CU_DEV(CHICONY,		RTL8188CUS_1),
	RTWN_RTL8192CU_DEV(CHICONY,		RTL8188CUS_2),
	RTWN_RTL8192CU_DEV(CHICONY,		RTL8188CUS_3),
	RTWN_RTL8192CU_DEV(CHICONY,		RTL8188CUS_4),
	RTWN_RTL8192CU_DEV(CHICONY,		RTL8188CUS_5),
	RTWN_RTL8192CU_DEV(COREGA,		RTL8192CU),
	RTWN_RTL8192CU_DEV(DLINK,		RTL8188CU),
	RTWN_RTL8192CU_DEV(DLINK,		RTL8192CU_1),
	RTWN_RTL8192CU_DEV(DLINK,		RTL8192CU_2),
	RTWN_RTL8192CU_DEV(DLINK,		RTL8192CU_3),
	RTWN_RTL8192CU_DEV(DLINK,		DWA131B),
	RTWN_RTL8192CU_DEV(EDIMAX,		EW7811UN),
	RTWN_RTL8192CU_DEV(EDIMAX,		RTL8192CU),
	RTWN_RTL8192CU_DEV(FEIXUN,		RTL8188CU),
	RTWN_RTL8192CU_DEV(FEIXUN,		RTL8192CU),
	RTWN_RTL8192CU_DEV(GUILLEMOT,		HWNUP150),
	RTWN_RTL8192CU_DEV(HAWKING,		RTL8192CU),
	RTWN_RTL8192CU_DEV(HP3,			RTL8188CU),
	RTWN_RTL8192CU_DEV(NETGEAR,		WNA1000M),
	RTWN_RTL8192CU_DEV(NETGEAR,		RTL8192CU),
	RTWN_RTL8192CU_DEV(NETGEAR4,		RTL8188CU),
	RTWN_RTL8192CU_DEV(NOVATECH,		RTL8188CU),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8188CU_1),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8188CU_2),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8188CU_3),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8188CU_4),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8188CUS),
	RTWN_RTL8192CU_DEV(PLANEX2,		RTL8192CU),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CE_0),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CE_1),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CTV),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CU_0),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CU_1),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CU_2),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CU_3),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CU_COMBO),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188CUS),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188RU_1),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188RU_2),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8188RU_3),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8191CU),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8192CE),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8192CU),
	RTWN_RTL8192CU_DEV(REALTEK,		RTL8192CU_1),
	RTWN_RTL8192CU_DEV(SITECOMEU,		RTL8188CU_1),
	RTWN_RTL8192CU_DEV(SITECOMEU,		RTL8188CU_2),
	RTWN_RTL8192CU_DEV(SITECOMEU,		RTL8192CU),
	RTWN_RTL8192CU_DEV(TRENDNET,		RTL8188CU),
	RTWN_RTL8192CU_DEV(TRENDNET,		RTL8192CU),
	RTWN_RTL8192CU_DEV(ZYXEL,		RTL8192CU),
#undef RTWN_RTL8192CU_DEV

	/* RTL8192EU */
#define RTWN_RTL8192EU_DEV(v,p) \
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RTWN_CHIP_RTL8192EU) }
	RTWN_RTL8192EU_DEV(DLINK,		DWA131E1),
	RTWN_RTL8192EU_DEV(REALTEK,		RTL8192EU),
	RTWN_RTL8192EU_DEV(TPLINK,		WN822NV4),
	RTWN_RTL8192EU_DEV(TPLINK,		WN823NV2),
#undef RTWN_RTL8192EU_DEV

	/* RTL8188EU */
#define RTWN_RTL8188EU_DEV(v,p) \
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RTWN_CHIP_RTL8188EU) }
	RTWN_RTL8188EU_DEV(ABOCOM,		RTL8188EU),
	RTWN_RTL8188EU_DEV(DLINK,		DWA123D1),
	RTWN_RTL8188EU_DEV(DLINK,		DWA125D1),
	RTWN_RTL8188EU_DEV(ELECOM,		WDC150SU2M),
	RTWN_RTL8188EU_DEV(REALTEK,		RTL8188ETV),
	RTWN_RTL8188EU_DEV(REALTEK,		RTL8188EU),
#undef RTWN_RTL8188EU_DEV

	/* RTL8812AU */
#define RTWN_RTL8812AU_DEV(v,p) \
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RTWN_CHIP_RTL8812AU) }
	RTWN_RTL8812AU_DEV(ASUS,		USBAC56),
	RTWN_RTL8812AU_DEV(CISCOLINKSYS,	WUSB6300),
	RTWN_RTL8812AU_DEV(DLINK,		DWA182C1),
	RTWN_RTL8812AU_DEV(DLINK,		DWA180A1),
	RTWN_RTL8812AU_DEV(EDIMAX,		EW7822UAC),
	RTWN_RTL8812AU_DEV(IODATA,		WNAC867U),
	RTWN_RTL8812AU_DEV(MELCO,		WIU3866D),
	RTWN_RTL8812AU_DEV(NEC,			WL900U),
	RTWN_RTL8812AU_DEV(PLANEX2,		GW900D),
	RTWN_RTL8812AU_DEV(SENAO,		EUB1200AC),
	RTWN_RTL8812AU_DEV(SITECOMEU,		WLA7100),
	RTWN_RTL8812AU_DEV(TPLINK,		T4U),
	RTWN_RTL8812AU_DEV(TRENDNET,		TEW805UB),
	RTWN_RTL8812AU_DEV(ZYXEL,		NWD6605),
#undef RTWN_RTL8812AU_DEV

	/* RTL8821AU */
#define RTWN_RTL8821AU_DEV(v,p) \
	{ USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, RTWN_CHIP_RTL8821AU) }
	RTWN_RTL8821AU_DEV(DLINK,		DWA171A1),
	RTWN_RTL8821AU_DEV(DLINK,		DWA172A1),
	RTWN_RTL8821AU_DEV(EDIMAX,		EW7811UTC_1),
	RTWN_RTL8821AU_DEV(EDIMAX,		EW7811UTC_2),
	RTWN_RTL8821AU_DEV(HAWKING,		HD65U),
	RTWN_RTL8821AU_DEV(MELCO,		WIU2433DM),
	RTWN_RTL8821AU_DEV(NETGEAR,		A6100)
#undef RTWN_RTL8821AU_DEV
};

typedef void	(*chip_usb_attach)(struct rtwn_usb_softc *);

static const chip_usb_attach rtwn_chip_usb_attach[RTWN_CHIP_MAX_USB] = {
	[RTWN_CHIP_RTL8192CU] = r92cu_attach,
	[RTWN_CHIP_RTL8192EU] = r92eu_attach,
	[RTWN_CHIP_RTL8188EU] = r88eu_attach,
	[RTWN_CHIP_RTL8812AU] = r12au_attach,
	[RTWN_CHIP_RTL8821AU] = r21au_attach
};

static __inline void
rtwn_usb_attach_private(struct rtwn_usb_softc *uc, int chip)
{
	rtwn_chip_usb_attach[chip](uc);
}
