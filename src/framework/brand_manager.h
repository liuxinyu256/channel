#ifndef BRAND_MANAGER_H
#define BRAND_MANAGER_H
#include "packet.h"
#include "ac.h"

typedef struct brand_config {
    const char             *name;
    const event_handler_t  *evt_table;
    const ac_ability_t     *ability;
    packetizer_t           *packetizer;
} brand_config_t;

void  brand_manager_init(void);
void  brand_manager_register(const brand_config_t *config);
void  brand_manager_start_scan(void);
void  brand_manager_on_frame(uint8_t *data, uint16_t length);
void  brand_manager_lock(void);
int   brand_manager_locked(void);
const brand_config_t *brand_manager_current(void);

#endif
