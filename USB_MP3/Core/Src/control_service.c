#include "control_service.h"

#include "diagnostics.h"

#define CONTROL_COMMAND_QUEUE_LENGTH 12U

static QueueHandle_t command_queue;

BaseType_t control_service_init(void) {
  command_queue =
      xQueueCreate(CONTROL_COMMAND_QUEUE_LENGTH, sizeof(SystemCommand));
  if (command_queue != NULL) {
    vQueueAddToRegistry(command_queue, "SystemCmd");
    return pdPASS;
  }
  return pdFAIL;
}

BaseType_t control_service_send(const SystemCommand *command,
                                TickType_t timeout) {
  if ((command == NULL) || (command_queue == NULL)) {
    return pdFAIL;
  }
  if (xQueueSend(command_queue, command, timeout) != pdPASS) {
    diagnostics_record_command_queue_full();
    return pdFAIL;
  }
  return pdPASS;
}

BaseType_t control_service_send_from_isr(
    const SystemCommand *command, BaseType_t *higher_priority_task_woken) {
  if ((command == NULL) || (command_queue == NULL)) {
    return pdFAIL;
  }
  if (xQueueSendFromISR(command_queue, command, higher_priority_task_woken) !=
      pdPASS) {
    diagnostics_record_command_queue_full_from_isr();
    return pdFAIL;
  }
  return pdPASS;
}

BaseType_t control_service_receive(SystemCommand *command, TickType_t timeout) {
  if ((command == NULL) || (command_queue == NULL)) {
    return pdFAIL;
  }
  return xQueueReceive(command_queue, command, timeout);
}

UBaseType_t control_service_queue_depth(void) {
  return (command_queue != NULL) ? uxQueueMessagesWaiting(command_queue) : 0U;
}

UBaseType_t control_service_queue_capacity(void) {
  return CONTROL_COMMAND_QUEUE_LENGTH;
}
