#ifndef SYSTEM_EVENTS_H
#define SYSTEM_EVENTS_H

#include "FreeRTOS.h"
#include "event_groups.h"

#define SYS_EVENT_USB_CONNECTED (1UL << 0)
#define SYS_EVENT_USB_MOUNTED   (1UL << 1)
#define SYS_EVENT_FILE_READY    (1UL << 2)
#define SYS_EVENT_PLAYING       (1UL << 3)
#define SYS_EVENT_PAUSED        (1UL << 4)
#define SYS_EVENT_ERROR         (1UL << 5)

BaseType_t system_events_init(void);
EventGroupHandle_t system_events_handle(void);
EventBits_t system_events_get(void);
void system_events_set(EventBits_t bits);
void system_events_clear(EventBits_t bits);

#endif /* SYSTEM_EVENTS_H */
