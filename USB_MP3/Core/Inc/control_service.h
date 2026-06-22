#ifndef CONTROL_SERVICE_H
#define CONTROL_SERVICE_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

typedef enum {
  CMD_PLAY = 0,
  CMD_PAUSE,
  CMD_STOP,
  CMD_NEXT,
  CMD_PREV,
  CMD_VOLUME_SET,
  CMD_VOLUME_UP,
  CMD_VOLUME_DOWN,
  CMD_PRINT_STATS,
  CMD_RESET_STATS,
  /* Legacy compatibility only; CLI 不直接暴露 index selection。 */
  CMD_TRACK_SELECT
} SystemCommandType;

typedef struct {
  SystemCommandType type;
  int32_t value;
} SystemCommand;

BaseType_t control_service_init(void);
BaseType_t control_service_send(const SystemCommand *command,
                                TickType_t timeout);
BaseType_t control_service_send_from_isr(
    const SystemCommand *command, BaseType_t *higher_priority_task_woken);
BaseType_t control_service_receive(SystemCommand *command, TickType_t timeout);
UBaseType_t control_service_queue_depth(void);
UBaseType_t control_service_queue_capacity(void);

#endif /* CONTROL_SERVICE_H */
