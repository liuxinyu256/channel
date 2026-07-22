#ifndef BRAND_MANAGER_H
#define BRAND_MANAGER_H
#include "ac.h"

void  brand_manager_init(void);
void  brand_manager_register(const event_handler_t *table);
void  brand_manager_start_scan(void);
void  brand_manager_lock(void);
int   brand_manager_locked(void);
const event_handler_t *brand_manager_table(void);

#endif
