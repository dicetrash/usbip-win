// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * 
 * Modified By
 * 	2020 dice14u - striped console print to make it exportable
 */
// this has to stay here on top because kernel vrsn doesn't compile

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_vhci.h"


int usbipc_detach(int port)
{
	HANDLE	hdev;
	int	ret;

	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		err("vhci driver is not loaded");
		return 2;
	}

	ret = usbip_vhci_detach_device(hdev, port);
	usbip_vhci_driver_close(hdev);
	if (ret == 0) {
        return 0;
	}
	return 3;
}
