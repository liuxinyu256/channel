#include "hvac_init.h"
#include "brand_manager.h"

extern const brand_config_t gree_config;
extern void gree_init(void);

void hvac_start(void)
{
    brand_manager_init();

    gree_init();
    brand_manager_register(&gree_config);

    brand_manager_start_scan();
}
