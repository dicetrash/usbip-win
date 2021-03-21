// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 * Modified By
 * 	2020 dice14u - striped console print to make it exportable
 */

#include "usbip_windows.h"

#include <stdlib.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip_vhci.h"
#include "usbip_forward.h"
#include "dbgcode.h"

#include "usbip_dscr.h"

#define MAX_BUFF 100
#define VHCI_DIR_PERMISSION 0700

static int
import_device(SOCKET sockfd, pvhci_pluginfo_t pluginfo, HANDLE* phdev)
{
	HANDLE	hdev;
	int	port;
	int	rc;

	hdev = usbip_vhci_driver_open();
	if (hdev == INVALID_HANDLE_VALUE) {
		dbg("failed to open vhci driver");
		return ERR_DRIVER;
	}

	port = usbip_vhci_get_free_port(hdev);
	if (port < 0) {
		dbg("no free port");
		usbip_vhci_driver_close(hdev);
		return ERR_PORTFULL;
	}

	dbg("got free port: %d", port);

	pluginfo->port = port;

	rc = usbip_vhci_attach_device(hdev, pluginfo);
	if (rc < 0) {
		dbg("failed to attach device: %d", rc);
		usbip_vhci_driver_close(hdev);
		return ERR_GENERAL;
	}

	*phdev = hdev;
	return port;
}

static pvhci_pluginfo_t
build_pluginfo(SOCKET sockfd, unsigned devid)
{
	pvhci_pluginfo_t	pluginfo;
	unsigned long	pluginfo_size;
	unsigned short	conf_dscr_len;

	if (fetch_conf_descriptor(sockfd, devid, NULL, &conf_dscr_len) < 0) {
		dbg("failed to get configuration descriptor size");
		return NULL;
	}

	pluginfo_size = sizeof(vhci_pluginfo_t) + conf_dscr_len - 9;
	pluginfo = (pvhci_pluginfo_t)malloc(pluginfo_size);
	if (pluginfo == NULL) {
		dbg("out of memory or invalid vhci pluginfo size");
		return NULL;
	}
	if (fetch_device_descriptor(sockfd, devid, (char*)pluginfo->dscr_dev) < 0) {
		dbg("failed to fetch device descriptor");
		free(pluginfo);
		return NULL;
	}
	if (fetch_conf_descriptor(sockfd, devid, (char*)pluginfo->dscr_conf, &conf_dscr_len) < 0) {
		dbg("failed to fetch configuration descriptor");
		free(pluginfo);
		return NULL;
	}

	pluginfo->size = pluginfo_size;
	pluginfo->devid = devid;

	return pluginfo;
}

static int
query_import_device(SOCKET sockfd, const char* busid, HANDLE* phdev, const char* serial)
{
	struct op_import_request request;
	struct op_import_reply   reply;
	pvhci_pluginfo_t	pluginfo;
	uint16_t code = OP_REP_IMPORT;
	unsigned	devid;
	int	status;
	int	rc;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));

	/* send a request */
	rc = usbip_net_send_op_common(sockfd, OP_REQ_IMPORT, 0);
	if (rc < 0) {
		dbg("failed to send common header: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	strncpy_s(request.busid, USBIP_BUS_ID_SIZE, busid, sizeof(request.busid));

	PACK_OP_IMPORT_REQUEST(0, &request);

	rc = usbip_net_send(sockfd, (void*)&request, sizeof(request));
	if (rc < 0) {
		dbg("failed to send import request: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	/* recieve a reply */
	rc = usbip_net_recv_op_common(sockfd, &code, &status);
	if (rc < 0) {
		dbg("failed to recv common header: %s", dbg_errcode(rc));
		if (rc == ERR_STATUS) {
			dbg("op code error: %s", dbg_opcode_status(status));

			switch (status) {
			case ST_NODEV:
				return ERR_NOTEXIST;
			case ST_DEV_BUSY:
				return ERR_EXIST;
			default:
				break;
			}
		}
		return rc;
	}

	rc = usbip_net_recv(sockfd, (void*)&reply, sizeof(reply));
	if (rc < 0) {
		dbg("failed to recv import reply: %s", dbg_errcode(rc));
		return ERR_NETWORK;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, sizeof(reply.udev.busid)) != 0) {
		dbg("recv different busid: %s", reply.udev.busid);
		return ERR_PROTOCOL;
	}

	devid = reply.udev.busnum << 16 | reply.udev.devnum;
	pluginfo = build_pluginfo(sockfd, devid);
	if (pluginfo == NULL)
		return ERR_GENERAL;

	if (serial != NULL)
		mbstowcs_s(NULL, pluginfo->wserial, MAX_VHCI_SERIAL_ID, serial, _TRUNCATE);
	else
		pluginfo->wserial[0] = L'\0';

	/* import a device */
	rc = import_device(sockfd, pluginfo, phdev);
	free(pluginfo);
	return rc;
}

static BOOL
write_handle_value(HANDLE hInWrite, HANDLE handle)
{
	DWORD	nwritten;
	BOOL	res;

	res = WriteFile(hInWrite, &handle, sizeof(HANDLE), &nwritten, NULL);
	if (!res || nwritten != sizeof(HANDLE)) {
		dbg("failed to write handle value");
		return FALSE;
	}
	return TRUE;
}

static BOOL
create_pipe(HANDLE* phRead, HANDLE* phWrite)
{
	SECURITY_ATTRIBUTES	saAttr;

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	if (!CreatePipe(phRead, phWrite, &saAttr, 0)) {
		dbg("failed to create stdin pipe: 0x%lx", GetLastError());
		return FALSE;
	}
	return TRUE;
}

static int
execute_attacher(HANDLE hdev, SOCKET sockfd, int rhport)
{
	STARTUPINFO	si;
	PROCESS_INFORMATION	pi;
	HANDLE	hRead, hWrite;
	HANDLE	hdev_attacher, sockfd_attacher;
	BOOL	res;
	int	ret = ERR_GENERAL;

	if (!create_pipe(&hRead, &hWrite))
		return ERR_GENERAL;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.hStdInput = hRead;
	si.dwFlags = STARTF_USESTDHANDLES;
	ZeroMemory(&pi, sizeof(pi));

	res = CreateProcess((LPCWSTR)"attacher.exe", (LPWSTR)"attacher.exe", NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
	if (!res) {
		DWORD	err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND)
			ret = ERR_NOTEXIST;
		dbg("failed to create process: 0x%lx", err);
		goto out;
	}
	res = DuplicateHandle(GetCurrentProcess(), hdev, pi.hProcess, &hdev_attacher, 0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		dbg("failed to dup hdev: 0x%lx", GetLastError());
		goto out_proc;
	}
	res = DuplicateHandle(GetCurrentProcess(), (HANDLE)sockfd, pi.hProcess, &sockfd_attacher, 0, FALSE, DUPLICATE_SAME_ACCESS);
	if (!res) {
		dbg("failed to dup sockfd: 0x%lx", GetLastError());
		goto out_proc;
	}
	if (!write_handle_value(hWrite, hdev_attacher) || !write_handle_value(hWrite, sockfd_attacher))
		goto out_proc;
	ret = 0;
out_proc:
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
out:
	CloseHandle(hRead);
	CloseHandle(hWrite);
	return ret;
}

static int
attach_device(const char* host, const char* busid, const char* serial, BOOL terse)
{
	SOCKET	sockfd;
	int	rhport, ret;
	HANDLE	hdev = INVALID_HANDLE_VALUE;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd == INVALID_SOCKET) {
		err("failed to connect a remote host: %s", host);
		return 2;
	}

	rhport = query_import_device(sockfd, busid, &hdev, serial);
	if (rhport < 0) {
		switch (rhport) {
		case ERR_DRIVER:
			err("vhci driver is not loaded");
			break;
		case ERR_EXIST:
			err("already used bus id: %s", busid);
			break;
		case ERR_NOTEXIST:
			err("non-existent bus id: %s", busid);
			break;
		case ERR_PORTFULL:
			err("no available port");
			break;
		default:
			err("failed to attach");
			break;
		}
		return 3;
	}

	ret = execute_attacher(hdev, sockfd, rhport);
	if (ret == 0) {
		if (terse) {
			printf("%d\n", rhport);
		}
		else {
			printf("succesfully attached to port %d\n", rhport);
		}
	}
	else {
		switch (ret) {
		case ERR_NOTEXIST:
			err("attacher.exe not found");
			break;
		default:
			err("failed to running attacher.exe");
			break;
		}
		ret = 4;
	}
	usbip_vhci_driver_close(hdev);
	closesocket(sockfd);

	return ret;
}

int usbipc_attach(char* host, char* busid)
{
	return attach_device(host, busid, NULL, NULL);
}
