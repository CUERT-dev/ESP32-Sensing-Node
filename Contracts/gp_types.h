#ifndef CUERT_GP_TYPES_H
#define CUERT_GP_TYPES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define GP_API_REVISION 10u
#define GP_UART_VERSION 2u
#define GP_UART_FRAME_BYTES 14u
#define GP_CAN_COMMAND_ID 0x105u
#define GP_FLAG_VALID 0x01u
#define GP_FLAG_ARM 0x02u
#define GP_FLAG_DEADMAN 0x04u
typedef enum { GP_OK=0, GP_PENDING, GP_BAD_ARG,
  GP_INVALID, GP_STALE, GP_IO_ERROR, GP_NO_SPACE } GpResult;
typedef struct {
  uint16_t sequence, throttle_pm;
  int16_t steering_pm;
  uint8_t flags;
} TargetSample;
typedef struct {
  uint16_t min[2], center[2], max[2], deadband[2];
} JoystickCalibration;
typedef bool (*AdcReadFn)(void *user, uint8_t axis,
  uint16_t *raw);
typedef GpResult (*UartWriteFn)(void *user,
  const uint8_t *bytes, size_t length);
typedef struct { UartWriteFn write; void *user; bool initialized;
} UartSender;
typedef struct { uint8_t bytes[GP_UART_FRAME_BYTES]; size_t used;
} UartRx;
typedef struct { uint16_t id; uint8_t dlc; uint8_t data[8];
} GpCanFrame;
typedef GpResult (*CanWriteFn)(void *user,
  const GpCanFrame *frame);
typedef struct { CanWriteFn write; void *user;
  uint32_t max_queue_ms; uint16_t last_sequence;
  bool seen, initialized;
} Gateway;
typedef struct { int16_t throttle_duty_pm, steering_rate_pm;
} MotorOutput;
typedef struct {
  uint16_t throttle_cap_pm, steering_cap_pm;
  uint16_t throttle_slew_pm_s, steering_slew_pm_s;
  uint32_t max_dt_ms;
} CommandMapConfig;
typedef struct { CommandMapConfig config; MotorOutput previous;
  uint32_t throttle_remainder, steering_remainder;
  bool initialized;
} CommandMap;
typedef struct { bool estop_ok, enabled; } MotorSafety;
typedef GpResult (*MotorWriteFn)(void *user,
  const MotorOutput *output);
typedef void (*MotorOffFn)(void *user);
typedef struct {
  MotorWriteFn write; MotorOffFn off; void *user;
  uint16_t max_duty_pm; uint32_t max_step_hz;
  uint16_t step_pulse_us, dir_setup_us, enable_setup_us;
  uint32_t reversal_deadtime_ms;
} MotorDriverConfig;
typedef struct { MotorDriverConfig config; MotorOutput previous;
  uint32_t zero_since_ms; int8_t last_steering_direction;
  bool initialized;
} MotorDriver;

GpResult Joystick_ReadSample(AdcReadFn read, void *user,
  const JoystickCalibration *cal, uint16_t sequence,
  uint8_t request_flags, TargetSample *out);
GpResult UartSender_Init(UartSender *sender,
  UartWriteFn write, void *user);
GpResult UartSender_Send(UartSender *sender,
  const TargetSample *command);
void UartRx_Init(UartRx *rx);
GpResult UartRx_Feed(UartRx *rx, const uint8_t *bytes,
  size_t length, size_t *consumed, TargetSample *out);
GpResult GP_EncodeUart(const TargetSample *command,
  uint8_t *out, size_t capacity, size_t *written);
GpResult GP_DecodeUart(const uint8_t *bytes,
  size_t length, TargetSample *out);
GpResult GP_EncodeCan(const TargetSample *command,
  GpCanFrame *out);
GpResult GP_DecodeCan(const GpCanFrame *frame,
  TargetSample *out);
GpResult Gateway_Init(Gateway *gateway, CanWriteFn write,
  void *user, uint32_t max_queue_ms);
GpResult Gateway_Forward(Gateway *gateway,
  const TargetSample *command, uint32_t queued_ms);
void Gateway_Reset(Gateway *gateway);
GpResult CommandMap_Init(CommandMap *map,
  const CommandMapConfig *config);
GpResult CommandMap_Update(CommandMap *map,
  const TargetSample *command, uint32_t dt_ms,
  bool enabled, MotorOutput *out);
void CommandMap_Reset(CommandMap *map);
GpResult MotorDriver_Init(MotorDriver *driver,
  const MotorDriverConfig *config);
GpResult MotorDriver_Apply(MotorDriver *driver,
  const MotorOutput *request, const MotorSafety *safety,
  uint32_t now_ms, MotorOutput *applied);
void MotorDriver_Stop(MotorDriver *driver);
#endif
