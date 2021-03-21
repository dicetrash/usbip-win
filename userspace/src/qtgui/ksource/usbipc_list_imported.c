// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * Modified By
 * 	2020 dice14u - striped console and made it return a result
 */
#pragma once
#include "../kcommon.h"
#include "usbip_windows.h"
#include "usbip_common.h"
#include "usbip_vhci.h"
#include <stdlib.h>

#define MAX_INTERFACES 10

void usbip_devices_free(struct usbip_devices* device) {

    free(device->product_name);
    free(device);
};

struct usbip_devices* usbip_list_imported(void)
{
    HANDLE hdev;
    ioctl_usbip_vhci_imported_dev* idevs;
    BOOL	found = FALSE;

    struct usbip_devices* last = NULL;
    struct usbip_devices* first = NULL;
    struct usbip_devices* current = NULL;

    int i;
    char product_name[100];
    char host[64] = "unknown host";
    char serv[20] = "unknown port";

    hdev = usbip_vhci_driver_open();
    if (hdev == INVALID_HANDLE_VALUE) {
        err("failed to open vhci driver");
        return NULL;
    }

    int res = usbip_vhci_get_imported_devs(hdev, &idevs);
    if (res < 0) {
        usbip_vhci_driver_close(hdev);
        err("failed to get attach information");
        return NULL;
    }

    if (usbip_names_init()) {
        dbg("failed to open usb id database");
    }


    for (i = 0; i < 127; i++) {
        if (idevs[i].port < 0) {
            break;
        }

        if (idevs[i].status == VDEV_ST_NULL || idevs[i].status == VDEV_ST_NOTASSIGNED) {
            continue;
        }

        current = (struct usbip_devices*)malloc(sizeof(struct usbip_devices));

        current->port = idevs[i].port;

        usbip_names_get_product(product_name, sizeof(product_name),
            idevs[i].vendor, idevs[i].product);

        current->product_name = strdup(product_name);

        current->next = NULL;
        if (last != NULL) {
            last->next = current;
        }
        if (first == NULL) {
            first = current;
        }
        last = current;
        
    }

    free(idevs);

    usbip_vhci_driver_close(hdev);
    usbip_names_free();

    return first;
};
