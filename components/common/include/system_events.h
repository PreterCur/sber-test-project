#ifndef SYSTEM_EVENTS_H
#define SYSTEM_EVENTS_H

#include <stdint.h>

typedef enum {
    COMP_ID_BUTTON,
    COMP_ID_MEASURE,
    COMP_ID_LED
} component_id_t;

typedef struct 
{
    component_id_t  comp_id; // Who sent this?
    int             event_id;           // What happened? (Component enum cast to int)
    uint32_t        param;         // Optional payload
} generic_event_t;


#endif