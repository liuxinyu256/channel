  #include "hvac_init.h"
#include "brand_manager.h"

extern const event_handler_t gree_table;

void hvac_start(void)
{
    brand_manager_init();
    brand_manager_register(&gree_table);
    brand_manager_start_scan();
}
