#include <Arduino.h>

//Pin definitions:
#define BUTTON 2
#define LED1 8
#define LED2 9
#define LED3 10
#define Buzzer 11
#define PWM_Pin 3
bool systemActive = false;
bool operationActive = false;
bool faultTriggered = false;
unsigned long pressStartTime = 0;
bool buttonWasPressed = false;
bool actionTriggered = false;
short ButtonState = 0;

// Thermistor parameters
enum Temperatures
{
  Low = 83,          //465 = 21.03C,   923 = 85C
  Medium = 88,       //511 = 24.96C,   936 = 90C
  High = 93,         //546 = 28.07C,   947 = 95C
  Danger = 103       //567 = 30.09C,   1000 = ???
};
#define THERM A0
#define SERIES_RESISTOR 100000.0   // 100K resistor
#define NOMINAL_RESISTANCE 100000.0 // resistance at 25°C
#define NOMINAL_TEMPERATURE 25.0    // 25°C
#define BETA_COEFFICIENT 3950.0
int adcValue;
int rawADC;
int avg;
int DangerTemp = Danger;
const uint16_t SAMPLE_INTERVAL_MS = 5;   // sample every 5ms
const uint16_t WINDOW_TIME_MS = 500;     // total averaging window
const uint16_t NUM_SAMPLES = WINDOW_TIME_MS / SAMPLE_INTERVAL_MS;
uint16_t buffer[NUM_SAMPLES];
uint16_t index = 0;
uint32_t sum = 0;
unsigned long lastSampleTime = 0;

//PWM Parameters
static uint8_t pwm_top = 99;   // Default for ~20 kHz
static bool pwm_initialized = false;

//My PID Parameters
float duty;
float targetTemp;

//Function delarations:
void Blinktil(int,int,int);
void BuzzerStart();
void stopOperation();
void pwmWrite_HF(uint8_t);
float getAveragedReading();
void initAveraging();
float MePID(float, float);

void setup() 
{
  Serial.begin(9600);
  pinMode(BUTTON, INPUT);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(Buzzer, OUTPUT);
  pinMode(PWM_Pin, OUTPUT);
  initAveraging();
}

void loop() 
{
  rawADC = getAveragedReading();
  adcValue = 1023 - rawADC;
  // Convert ADC value to resistance
  float voltage = adcValue / 1023.0;
  float resistance = SERIES_RESISTOR * (1.0 / voltage - 1.0);   //for reverse reading: float resistance = SERIES_RESISTOR * (voltage / (1.0 - voltage));
  // Apply Beta formula
  float steinhart;
  steinhart = resistance / NOMINAL_RESISTANCE;     // (R/R0)
  steinhart = log(steinhart);                      // ln(R/R0)
  steinhart /= BETA_COEFFICIENT;                   // 1/B * ln(...)
  steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15); // + (1/T0)
  steinhart = 1.0 / steinhart;                     // Invert
  steinhart -= 273.15;                             // Convert to °C
  bool SafeTemp = (steinhart<DangerTemp && rawADC < 850);
  bool buttonPressed = digitalRead(BUTTON);

  //Temperature value printing:
  Serial.print("PWMDuty: ");
  Serial.print(duty);
  Serial.print(" | CurrentTemp: ");
  Serial.print(steinhart);
  Serial.print(" °C");
  Serial.print(" | TargetTemp: ");
  Serial.print(targetTemp);
  Serial.println(" °C");

  //Checks if thermistor is connected or if temperature is not above expected.
  if (SafeTemp && !systemActive)
  {
    systemActive = true;
    DangerTemp = Danger;
    faultTriggered = false;
    BuzzerStart();
  }
  if (!SafeTemp && !faultTriggered)
  {
    faultTriggered = true;
    systemActive = false;
    DangerTemp = Medium;
    stopOperation();
  }

  //Operation Mode (Waits for button to be held for 3s first)
  if(systemActive)
  {
        // Button just pressed
    if (buttonPressed && !buttonWasPressed)
    {
        pressStartTime = millis();
        actionTriggered = false;
    }
    // Button is being held
    if (buttonPressed && !actionTriggered)
    {
        if (millis() - pressStartTime >= 2000)
        {
            operationActive = !operationActive;  // toggle ON/OFF
            if (!operationActive)
              stopOperation();

            actionTriggered = true;             // prevent retrigger while holding
        }
    }

        if (!buttonPressed && buttonWasPressed)
    {
        if (!actionTriggered && operationActive)
        {
            ButtonState++;
            if (ButtonState >= 3)
            {
              ButtonState = 0;
              digitalWrite(LED2, LOW);
              digitalWrite(LED3, LOW);
            }
        }
    }
    buttonWasPressed = buttonPressed;
  }

  if (operationActive)
  {
    duty = MePID(steinhart, targetTemp); //PID Control 
    pwmWrite_HF((uint8_t)duty);
    if (ButtonState == 0)
    {
      targetTemp = 85.00;
      Blinktil(LED1, Low, steinhart);
    }
    if (ButtonState == 1)
    {
      targetTemp = 90.00;
      digitalWrite(LED1, HIGH);
      Blinktil(LED2, Medium, steinhart);
    }
    if (ButtonState == 2)
    {
      targetTemp = 95.00;
      digitalWrite(LED2, HIGH);
      Blinktil(LED3, High, steinhart);
    }
  }
}

void Blinktil(int LedNum, int Templimit, int tempRead)
{
  static unsigned long lastToggle[14]; // enough for all pins
  static bool ledState[14];

  if (tempRead < Templimit)
  {
    if (millis() - lastToggle[LedNum] >= 1000)
    {
      ledState[LedNum] = !ledState[LedNum];
      digitalWrite(LedNum, ledState[LedNum]);
      lastToggle[LedNum] = millis();
    }
  }
  else
  {
    digitalWrite(LedNum, HIGH);
  }
}

void BuzzerStart()
{
   digitalWrite(Buzzer, HIGH);
   delay(300);
   digitalWrite(Buzzer, LOW);
   delay(300);
}

void stopOperation()
{
  operationActive = false;
  ButtonState = 0;
  duty = 0;
  targetTemp = 0;
  pwmWrite_HF(0);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
    for (int i = 0; i<3; i++)
  {
    digitalWrite(Buzzer, HIGH);
    delay(100);
    digitalWrite(Buzzer, LOW);
    delay(30);
  }
}

void pwmWrite_HF(uint8_t dutyPercent) {
  if (!pwm_initialized) {
    pinMode(PWM_Pin, OUTPUT);

    // Stop Timer2
    TCCR2A = 0;
    TCCR2B = 0;

    // Fast PWM, Mode 7 (TOP = OCR2A)
    TCCR2A |= (1 << WGM20) | (1 << WGM21);
    TCCR2B |= (1 << WGM22);

    // Non-inverting mode on OC2B (Pin 3)
    TCCR2A |= (1 << COM2B1);

    // No prescaler
    TCCR2B |= (1 << CS20);

    // Set TOP for ~20 kHz
    // f = 16MHz / (1 * (1 + TOP)) → TOP = 799 for 20kHz
    // BUT Timer2 is 8-bit → max TOP = 255
    // So we approximate:
    // TOP = 99 → ~16MHz / 100 = 160 kHz (too high)
    // We must use prescaler

    // Use prescaler = 8:
    // f = 16MHz / (8 * (1 + TOP))
    // For 20kHz → TOP ≈ 99

    TCCR2B = (1 << WGM22) | (1 << CS21); // prescaler = 8

    pwm_top = 99;
    OCR2A = pwm_top;

    pwm_initialized = true;
  }

  // Clamp input
  if (dutyPercent > 100) dutyPercent = 100;

  // Convert % to compare value
  OCR2B = (pwm_top * dutyPercent) / 100;
}

float getAveragedReading() {
  unsigned long now = millis();

  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    uint16_t newValue = analogRead(THERM);

    // Remove old value from sum
    sum -= buffer[index];

    // Add new value
    buffer[index] = newValue;
    sum += newValue;

    // Move index
    index++;
    if (index >= NUM_SAMPLES) index = 0;
  }

  // Return current average anytime
  return (float)sum / NUM_SAMPLES;
}

void initAveraging() 
{
  sum = 0;

  for (unsigned int i = 0; i < NUM_SAMPLES; i++) {
    uint16_t val = analogRead(THERM);
    buffer[i] = val;
    sum += val;
    delay(5); // small spacing between samples
  }
}

float MePID(float currentTemp, float targetTemp) {
  static float integral = 0;
  static float lastError = 0;
  static unsigned long lastTime = 0;

  float Kp = 5.35;  //1.93
  float Ki = 0.5;  //0.49
  float Kd = 0.26;  //0.35 most stable 

  unsigned long now = millis();
  float dt = (now - lastTime) / 1000.0;
  if (dt <= 0) dt = 0.001;

  float error = targetTemp - currentTemp;

  // --- Optional: fast heat-up ---
  // if (currentTemp < targetTemp - 10) {
  //  return 65;  // max allowed duty
  //}

  // --- PID ---
  integral += error * dt;

  // Anti-windup (important since output is capped at 50%)
  if (integral > 80) integral = 80;
  if (integral < -80) integral = -80;

  float derivative = (error - lastError) / dt;

  float output = Kp * error + Ki * integral + Kd * derivative;

  lastError = error;
  lastTime = now;

  // Clamp to 0–50% ONLY
  if (output > 80) output = 80;
  if (output < 0) output = 0;

  return output;
}