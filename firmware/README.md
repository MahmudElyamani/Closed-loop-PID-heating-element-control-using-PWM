# Firmware

Arduino-framework firmware for the ATmega328P that runs the heater's safety monitoring, button/UI logic, and PID temperature control.

## Requirements

- Arduino IDE or PlatformIO
- Board: any ATmega328P target (e.g. "Arduino Uno" / "Arduino Nano" board definition, since this project uses the ATmega328P directly rather than a full Arduino module)
- No external libraries — only `Arduino.h`

## Pin Map

| Pin | Signal | Direction | Notes |
|---|---|---|---|
| D2 | Button | Input | Momentary push button |
| D3 | PWM_Pin | Output | Timer2 OC2B — MOSFET gate drive (~20 kHz) |
| D8 | LED1 | Output | Stage 1 (85 °C) indicator |
| D9 | LED2 | Output | Stage 2 (90 °C) indicator |
| D10 | LED3 | Output | Stage 3 (95 °C) indicator |
| D11 | Buzzer | Output | Audible alerts |
| A0 | THERM | Analog In | Thermistor voltage divider |

## Flashing

1. Open `src/main.cpp` in the Arduino IDE (rename to `.ino` or open as a sketch folder) or a PlatformIO project.
2. Select the correct board/programmer for your ATmega328P (ISP programmer if flashing a bare chip, or the appropriate serial port if using a bootloader).
3. Upload.
4. Optionally open the Serial Monitor at 9600 baud to view live duty cycle / current temperature / target temperature readings, useful for tuning.

## Control Logic

### 1. Temperature acquisition

`getAveragedReading()` maintains a circular buffer of 100 ADC samples (5 ms apart, 500 ms total window) and returns a running average, refreshed non-blockingly on every `loop()` call. The averaged ADC value is converted to resistance from the voltage divider, then to °C via the Beta-parameter equation:

```
1/T = 1/T0 + (1/B) * ln(R/R0)
```

with `R0 = 100 kΩ`, `T0 = 25 °C`, `B = 3950`.

### 2. Safety gating

A reading is considered safe if the computed temperature is below the current danger threshold **and** the raw ADC value is below 850 (guards against a disconnected/open thermistor, which reads as a near-maximum ADC value in this divider configuration). The danger threshold itself changes context: it's relaxed while running normally and tightened after a fault, so the system won't silently resume right at the edge of the fault condition.

### 3. Button handling

- **Held ≥ 2 s while the system is active:** toggles `operationActive` (heater run/stop). Stopping immediately zeroes duty cycle and target temperature and cycles the buzzer.
- **Short press/release (while operation is active):** advances `ButtonState` (0 → 1 → 2 → 0), selecting between the three temperature stages. Each stage sets its own `targetTemp` (85 / 90 / 95 °C) and drives its corresponding LED via `Blinktil()`, which blinks the LED until the stage's temperature threshold is reached, then holds it solid.

### 4. PID loop

`MePID()` runs a standard PID controller:

```
Kp = 5.35
Ki = 0.5
Kd = 0.26
```

- Integral term is clamped to ±80 to limit windup.
- Output (PWM duty cycle) is clamped to **0–80%**.
- `dt` is computed from `millis()` deltas each call.

The resulting duty percentage is written to the MOSFET gate via `pwmWrite_HF()`.

### 5. PWM generation

`pwmWrite_HF()` configures Timer2 directly (rather than using `analogWrite()`) for a higher, more consistent switching frequency than the default ~490 Hz/980 Hz Arduino PWM:

- Fast PWM, TOP = `OCR2A` (Mode 7)
- Prescaler = 8 → `f = 16 MHz / (8 × (1 + 99)) ≈ 20 kHz`
- Duty cycle written to `OCR2B` (non-inverting, `OC2B` / D3)

## Tuning Notes

The PID gains and the 0–80% output clamp are tuned for this specific heater's thermal mass and the 24 V/parallel-MOSFET power stage — if you build your own heater assembly with different thermal characteristics, re-tune `Kp`/`Ki`/`Kd` using the Serial Monitor output as feedback.
