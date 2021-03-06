/*
 *
 * Copyright 2018 The wookey project team <wookey@ssi.gouv.fr>
 *   - Ryad     Benadjila
 *   - Arnauld  Michelizza
 *   - Mathieu  Renard
 *   - Philippe Thierry
 *   - Philippe Trebuchet
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * ur option) any later version.
 *
 * This package is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with this package; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */
/** @file usb_desc.h
 * \brief Contains usb descriptors
 */
#ifndef _DFU_DESC_H
#define _DFU_DESC_H

#include "autoconf.h"
#include "usb_control.h"
#include "usb_device.h"

/* Classes, subclasses and protocols for DFU Runtime */
#define USB_NB_INTERFACE        1

#define USB_CLASS_DFU		0xFE
#define USB_SUBCLASS_DFU	0x01
#define USB_PROTOCOL_DFU_RTM	0x01
#define USB_PROTOCOL_DFU_DFU	0x02
#define DFU_DETACH_TIMEOUT      1000 /* in miliseconds */

/**
 * \brief Device descriptor
 *
 * The device descriptor of a USB device represents the entire device.
 * An USB device can only have one device descriptor. It specifies information
 * about the device such as:
 *    - the supported USB version,
 *    - maximum packet size,
 *    - vendor and product IDs and the number of possible configurations the
 *      device can have.
 * The format of the device descriptor is defined below.
 */
const usb_ctrl_device_descriptor_t dfu_device_desc = {
	.bLength = 18,
	.bDescriptorType = USB_DESC_DEVICE,
	.bcdUSB = 0x0200,       /* USB 2.0 */
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize = 64, /* For EP0 */
	.idVendor = ID_VENDOR,
	.idProduct = ID_PRODUCT,
	.bcdDevice = 0x101,
	.iManufacturer = STRING_MANUFACTURER_INDEX,
	.iProduct = STRING_PRODUCT_INDEX,
	.iSerialNumber = STRING_SERIAL_INDEX,
	.bNumConfigurations = 1, /* We only have 1 interface for the DFU app */
};

/**
 * \brief Configuration descriptor
 *
 */
static usb_ctrl_full_configuration_descriptor_t dfu_configuration_desc = {
	.config_desc = {
    	.bLength = sizeof(usb_ctrl_configuration_descriptor_t),
    	.bDescriptorType = USB_DESC_CONFIG,
    	.wTotalLength = 27, // XXX DFU is only using EP0 we do not need to include others endpoints definition
        .bNumInterfaces = USB_NB_INTERFACE,
    	.bConfigurationValue = 1,
    	.iConfiguration = 0,
    	.bmAttributes.reserved7 = 0,
    	.bmAttributes.self_powered = 0,
    	.bmAttributes.remote_wakeup = 0,
    	.bmAttributes.reserved = 0,
    	.bMaxPower = 0xFA, /* 0xFA = 500mA */
    },
	.interface_desc = {
    	.bLength = sizeof(usb_ctrl_interface_descriptor_t),
    	.bDescriptorType = USB_DESC_INTERFACE,
    	.bInterfaceNumber = 0,
    	.bAlternateSetting = 0,
    	.bNumEndpoints = 0,
    	.bInterfaceClass = USB_CLASS_DFU,
    	.bInterfaceSubClass = USB_SUBCLASS_DFU,
    	.bInterfaceProtocol = USB_PROTOCOL_DFU_DFU,
    	.iInterface = 1,
    },
    /* DFU functional descriptor */
    .functional_desc = {
        .bLength = sizeof(usb_functional_descriptor_t),
        .bDescriptorType = USB_DESC_FUNCT,
        .bmAttributes.reserved = 0,
	.bmAttributes.bitWillDetach = 0,
	.bmAttributes.bitManifestationTolerant = 0,
#if CONFIG_USR_LIB_DFU_CAN_UPLOAD
	.bmAttributes.bitCanUpload = 1,
#else
	.bmAttributes.bitCanUpload = 0,
#endif
#if CONFIG_USR_LIB_DFU_CAN_DOWNLOAD
	.bmAttributes.bitCanDnload = 1,
#else
	.bmAttributes.bitCanDnload = 0,
#endif
	.wDetachTimeOut = DFU_DETACH_TIMEOUT,
	/* Big transfer size chunks */
	.wTransferSize = 0, /* Updated during dfu_init() */
	/* DFU 1.1 */
	.bcdDFUVersion = 0x0110,
    },
};

#endif /* !_DFU_DESC_H */

