#ifndef DOAXBV_RECOMP_SAVE_ADAPTER_H
#define DOAXBV_RECOMP_SAVE_ADAPTER_H

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

RecompFunction recomp_save_lookup_manual(uint32_t guest_address);

/* DOA3: offers every costume tier and the extra characters; see save_adapter.c. */
void recomp_save_unlock_all(void);

/* DOA3: turns Analog Button on once the boot save load ends; see save_adapter.c. */
void recomp_save_analog_button_on(void);

#ifdef __cplusplus
}
#endif

#endif
