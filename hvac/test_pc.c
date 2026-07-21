#define FAKE_FREERTOS
char g_queue_buf[256];
#include "ac.h"
#include <stdio.h>
#include <assert.h>

extern ac_device_t *ac_mock_create(void);

#define T(cond,msg) do{if(cond)printf("PASS "msg"\n");else printf("FAIL "msg"\n");}while(0)

int main(void){
    printf("=== HVAC PC Test ===\n");

    ac_device_t *ac=ac_mock_create();
    T(ac!=NULL,"create");
    T(ac->evt_table!=NULL,"evt_table");
    T(ac->ability->temp_min==18,"temp_min=18");
    T(ac->ability->has_eco==1,"has_eco=1");

    ac_create_task(ac,256,3);
    T(ac->evt_queue!=NULL,"evt_queue");

    event_t ev={.type=EVENT_CONTROL_CMD,.cmd_val=0,.cmd_arg=26};
    ac_post(ac,&ev);
    ac_task(ac);
    T(ac->set_temp==26,"SET_TEMP=26");

    ev=(event_t){.type=EVENT_CONTROL_CMD,.cmd_val=1,.cmd_arg=2};
    ac_post(ac,&ev);
    ac_task(ac);
    T(ac->mode==2,"SET_MODE=2");

    uint8_t old=ac->room_temp;
    ev=(event_t){.type=EVENT_PERIODIC_SEND};
    ac_post(ac,&ev);
    ac_task(ac);
    T(ac->room_temp!=old,"room_temp updated");

    ev=(event_t){.type=EVENT_SCAN_AC};
    ac_post(ac,&ev);
    ac_task(ac);

    printf("=== ALL DONE ===\n");
    return 0;
}
