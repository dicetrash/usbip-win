#include "names.h"

const char* usbipc_names_vendor(uint16_t vendorid) {
	return names_vendor(vendorid);
};

const char* usbipc_names_product(uint16_t vendorid, uint16_t productid) {
	return names_product(vendorid, productid);
};