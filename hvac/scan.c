#define FAKE_FREERTOS
#include "scan.h"
#include <string.h>

static const event_handler_t **s_brands;
static uint8_t s_count, s_index;

void scan_start(ac_device_t *ac, const event_handler_t **brands,
                uint8_t count) {
    s_brands=brands; s_count=count; s_index=0;
    ac->evt_table=brands[0];
    if(brands[0]->on_scan)brands[0]->on_scan(ac);
}

int scan_try_match(ac_device_t *ac, uint8_t *d, uint16_t n) {
    if(s_index<s_count&&s_brands[s_index]->on_rx_frame)
        s_brands[s_index]->on_rx_frame(ac,d,n);
    s_index++;
    if(s_index>=s_count)return 0;
    ac->evt_table=s_brands[s_index];
    if(s_brands[s_index]->on_scan)s_brands[s_index]->on_scan(ac);
    return 1;
}

void scan_lock(ac_device_t *ac, const event_handler_t *table) {
    ac->evt_table=table;
    ac_set_poll_period(ac,5000);
    s_brands=NULL;
}
