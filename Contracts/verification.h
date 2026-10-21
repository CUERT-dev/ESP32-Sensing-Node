#ifndef CUERT_VERIFICATION_H
#define CUERT_VERIFICATION_H
#include "gp_types.h"
typedef struct { char id[12], node[12], event[20];
  uint32_t tick_ms; uint16_t sequence;
  int32_t expected, actual; bool check_ok;
} VerificationEvidence;
typedef bool (*VerifyWriteFn)(void *user,
  const char *line, size_t length);
typedef struct { bool (*read_fault)(void *user, bool *fault);
  void (*write_led)(void *user, bool on); void *user;
} DiagnosticsLedArgs;
GpResult Verification_Init(VerifyWriteFn write, void *user);
bool Verification_Log(const VerificationEvidence *e);
uint32_t Verification_DroppedCount(void);
void VerificationSerialTask(void *unused);
void DiagnosticsLedTask(void *argument);
#endif
