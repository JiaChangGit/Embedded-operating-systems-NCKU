#include "system_events.h"

static EventGroupHandle_t system_event_group;

BaseType_t system_events_init(void) {
  system_event_group = xEventGroupCreate();
  return (system_event_group != NULL) ? pdPASS : pdFAIL;
}

EventGroupHandle_t system_events_handle(void) { return system_event_group; }

EventBits_t system_events_get(void) {
  if (system_event_group == NULL) {
    return 0;
  }
  return xEventGroupGetBits(system_event_group);
}

void system_events_set(EventBits_t bits) {
  if (system_event_group != NULL) {
    (void)xEventGroupSetBits(system_event_group, bits);
  }
}

void system_events_clear(EventBits_t bits) {
  if (system_event_group != NULL) {
    (void)xEventGroupClearBits(system_event_group, bits);
  }
}
