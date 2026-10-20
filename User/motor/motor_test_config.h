#ifndef MOTOR_TEST_CONFIG_H
#define MOTOR_TEST_CONFIG_H
/* Change only after confirming unloaded/safe travel and the motor's actual microsteps.
   These are proposed bring-up parameters, not measured mechanical calibration. */
#ifndef MOTOR_TEST_CONFIGURED
#define MOTOR_TEST_CONFIGURED 0u
#endif
#define MOTOR_TEST_RPM 10u
#define MOTOR_TEST_ACCELERATION 100u
#define MOTOR_TEST_DIRECTION 0u
#define MOTOR_TEST_LEASE_MS 1000u
/* One-shot power-on test: enable 1..4, start 1..4, then stop 1..4.
   Set to 0 to restore the previous read-only boot. No motion on 5..7. */
#ifndef MOTOR_BOOT_SEQUENCE_ENABLED
#define MOTOR_BOOT_SEQUENCE_ENABLED 1u
#endif
#define MOTOR_BOOT_RPM 10u
#define MOTOR_BOOT_STEP_MS 1000u
#define MOTOR_TEST_WHEEL_RPM {10, -12, 14, -16}
#define MOTOR_TEST_PULSES 32u
#define MOTOR_TEST_POSITION_MODE EMM_REL_ACTUAL
#ifndef MOTOR_TEST_PULSES_PER_REV
#define MOTOR_TEST_PULSES_PER_REV 0u
#endif
#define MOTOR_TEST_POSITION_TIMEOUT_MS 8000u
/* Full motor power-cycle, no other bus writer and no leftover cache must be
   confirmed before setting this to 1 for a controlled synchronous test boot.
   MCU-only reset does NOT establish a clean motor cache. No runtime unlock. */
#define MOTOR_SYNC_CLEAN_BOOT_CONFIRMED 0u
#define MOTOR_POSITION_TOLERANCE_COUNTS 128u
#define MOTOR_STOP_TIMEOUT_MS 3000u
#endif
