#ifndef FM_AP_NATIVE_ADAPTER_H
#define FM_AP_NATIVE_ADAPTER_H
#include "auto_pickpocket.h"
/* Experimental exact-client adapter. Startup does not claim gameplay success. */
#define FM_AP_NATIVE_STARTING 0x41500002u
#define FM_AP_NATIVE_READY 0x41500003u
#define FM_AP_NATIVE_FAILED 0x41500004u
int fm_ap_native_start(FM_AP_ENGINE *engine);
void fm_ap_native_stop(void);
unsigned long fm_ap_native_status(void);
#endif
