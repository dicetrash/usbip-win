#pragma once

#define MAX_INTERFACES 10
#define NAME_SIZES 100

struct usbip_devices {
    int port;
    char* product_name;
    struct usbip_devices* next;
};

struct usbip_external_list {
    char* product_name;
    char* path;
    char* busid;
    char* interfaces[MAX_INTERFACES];
    int num_interfaces;
    struct usbip_external_list* next;
};

int usbipc_attach(char* host, char* busid);
int usbipc_detach(int port);

struct usbip_devices* usbip_list_imported(void);
void usbip_devices_free(struct usbip_devices* device);

struct usbip_external_list* usbip_list_remote(char* host);
void usbip_external_list_free(struct usbip_external_list* device);

const char* usbipc_names_vendor(uint16_t);
const char* usbipc_names_product(uint16_t, uint16_t);