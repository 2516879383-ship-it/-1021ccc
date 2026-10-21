# ICM42688 driver provenance

Adapted from Zephyr **v3.7.0**, commit `36940db938a8f4a1e919496793ed439850a221c2` (tag resolved through GitHub's commit API).

Read and used:

- [icm42688_common.c](https://github.com/zephyrproject-rtos/zephyr/blob/36940db938a8f4a1e919496793ed439850a221c2/drivers/sensor/tdk/icm42688/icm42688_common.c), blob `3a0c79869be9c4665eba0b1fc915234366b1448f`.
- [icm42688.c](https://github.com/zephyrproject-rtos/zephyr/blob/36940db938a8f4a1e919496793ed439850a221c2/drivers/sensor/tdk/icm42688/icm42688.c), blob `febb4612efaf8167c5731fcb59d9797fcc30491a`.
- [icm42688_reg.h](https://github.com/zephyrproject-rtos/zephyr/blob/36940db938a8f4a1e919496793ed439850a221c2/drivers/sensor/tdk/icm42688/icm42688_reg.h), blob `5f3b58f0b3c074d69ee029cf1d956f4f73dd554e`.

Apache-2.0. Original copyright notices are retained in `icm42688.c`; full license is in `LICENSE-Apache-2.0.txt`. This directory contains a **modified subset**, not an unmodified vendor SDK. Register values and added behavior were checked against TDK DS-000347 rev 1.6.

## Preserved core logic

| Upstream function | Local implementation | Preserved behavior |
|---|---|---|
| `icm42688_reset` | `Icm_Begin` / `Icm_Step` phases 0–6 | POR allowance, soft reset, wait, RESET_DONE and WHO_AM_I verification |
| `icm42688_configure` | `Icm_Step` settings sequence | Gyro power mode / FSR / ODR, startup settling, FIFO bypass, FSYNC off, DRDY source |
| `icm42688_sample_fetch` / `icm42688_read_all` | `Icm_Fetch` | Status → DRDY gate → one burst → signed big-endian decoding; no-data is distinct from a new sample |
| Register header | Relevant constants in `icm42688.c` | Register addresses, WHO_AM_I and status bits |

## Deliberate changes

- Replace Zephyr device/SPI/logging/sleep/byteorder glue with a small injected `GyroIo`, implemented by independent STM32 hardware I2C1. No RTOS or new toolchain.
- Split sleeps and configuration into deadlines and one transaction per initialization step. Check identity before reset as well as after it. Verify bank, reset-bit clear and every written setting.
- Configure filters and interface **before** powering the gyro (TDK §12.9). Retain the upstream 50 ms startup allowance and perform no writes after gyro ON during that wait; satisfy the separate 200 µs / 45 ms constraints.
- Explicit TDK §12.3 I2C settings: bank 1 INTF_CONFIG4/5/6, return bank 0, DRIVE_CONFIG. Preserve documented reset values in reserved bits. Use internal PLL/RC, no external clock.
- Gyro only: read six contiguous gyro bytes instead of fourteen temp/accel/gyro bytes. Accelerometer and temperature are off. Preserve signed conversion while removing the Zephyr host-endian helper (ARM target and host tests both decode explicitly).
- Set DRDY latched / clear-on-sensor-read, so a status read does not prematurely clear an event before the gyro burst. Equal raw values still count as new samples when DRDY is set. Guard invalid sentinel codes and near-full-scale saturation.
- Add periodic ID/bank/power/FSR/interface checks. Reject mismatches; do not silently accept another ICM variant or the module's MCU protocol.
- Omit FIFO, RTIO, triggers, accelerometer channels and runtime attribute APIs outside this stage. The calibration/integration service, UI and all bus code are local implementations, not upstream algorithms.

Selection rationale: the inspected Zephyr driver provides a licensed, compact C register path and clear reset/fetch contracts; its RTOS bindings can be removed without importing an application framework. The other inspected candidate, [oldsheep68/icm42688](https://github.com/oldsheep68/icm42688), is Rust/embedded-hal and would require a different toolchain. The WDS601M images advertise Arduino/ROS examples but do not supply their sources. No module MCU example or attitude-fusion output is used here.

The retrieved upstream originals are retained under ignored `Output/gyro-reference/` for this implementation session; the pinned public links above make the provenance independently reproducible.
