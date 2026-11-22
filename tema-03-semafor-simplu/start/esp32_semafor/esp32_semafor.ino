// Semafor pentru pietoni, cu buton, pe ESP32-S3 (compatibil cu scheletul tema 03).

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <string.h>

#include "config.h"
#include "connection.h"

enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

struct TrafficState {
  Phase phase;
  uint32_t phase_entered_ms;
  bool ped_request;
};

struct ButtonPressEvent {
  uint32_t press_ms;
};

static SemaphoreHandle_t state_mutex = nullptr;
static QueueHandle_t button_queue = nullptr;

static TrafficState g_state = {
  CAR_GREEN,
  0,
  false
};

static void task_control(void *pvParameters);
static void task_output(void *pvParameters);
static void task_button(void *pvParameters);
static void IRAM_ATTR button_isr(void);

static uint8_t compute_ped_countdown_sec(Phase phase, uint32_t elapsed_ms) {
  if (phase != PED_GREEN || elapsed_ms >= PED_GREEN_MS) {
    return 0;
  }
  uint32_t remaining_ms = PED_GREEN_MS - elapsed_ms;
  uint32_t sec = (remaining_ms + 999U) / 1000U;
  uint32_t max_sec = (PED_GREEN_MS + 999U) / 1000U;
  if (sec > max_sec) {
    sec = max_sec;
  }
  return (uint8_t)sec;
}

static void get_lights_for_phase(Phase phase, Light &car_light, Light &ped_light) {
  switch (phase) {
    case CAR_GREEN:
      car_light = GREEN;
      ped_light = RED;
      break;
    case CAR_YELLOW:
      car_light = YELLOW;
      ped_light = RED;
      break;
    case ALL_RED:
      car_light = RED;
      ped_light = RED;
      break;
    case PED_GREEN:
      car_light = RED;
      ped_light = GREEN;
      break;
  }
}

static void task_control(void *pvParameters) {
  (void)pvParameters;

  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t period_ticks = pdMS_TO_TICKS(CONTROL_PERIOD_MS);

  while (true) {
    vTaskDelayUntil(&last_wake_time, period_ticks);

    uint32_t now_ms = millis();
    bool phase_changed = false;
    Phase new_phase = CAR_GREEN;

    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
      uint32_t elapsed = now_ms - g_state.phase_entered_ms;

      switch (g_state.phase) {
        case CAR_GREEN:
          if (elapsed >= CAR_GREEN_MIN_MS && g_state.ped_request) {
            g_state.phase = CAR_YELLOW;
            g_state.phase_entered_ms = now_ms;
            phase_changed = true;
            new_phase = g_state.phase;
          }
          break;

        case CAR_YELLOW:
          if (elapsed >= CAR_YELLOW_MS) {
            g_state.phase = ALL_RED;
            g_state.phase_entered_ms = now_ms;
            phase_changed = true;
            new_phase = g_state.phase;
          }
          break;

        case ALL_RED:
          if (elapsed >= ALL_RED_MS) {
            g_state.phase = PED_GREEN;
            g_state.phase_entered_ms = now_ms;
            g_state.ped_request = false;
            phase_changed = true;
            new_phase = g_state.phase;
          }
          break;

        case PED_GREEN:
          if (elapsed >= PED_GREEN_MS) {
            g_state.phase = CAR_GREEN;
            g_state.phase_entered_ms = now_ms;
            phase_changed = true;
            new_phase = g_state.phase;
          }
          break;
      }

      xSemaphoreGive(state_mutex);
    }

    if (phase_changed) {
      Serial.printf("[control] fază nouă: %s\n", PHASE_NAMES[new_phase]);
    }
  }
}

static void task_output(void *pvParameters) {
  (void)pvParameters;

  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t period_ticks = pdMS_TO_TICKS(OUTPUT_PERIOD_MS);

  while (true) {
    vTaskDelayUntil(&last_wake_time, period_ticks);

    TrafficState local_state = {CAR_GREEN, 0, false};
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
      local_state = g_state;
      xSemaphoreGive(state_mutex);
    }

    uint32_t now_ms = millis();
    uint32_t elapsed = now_ms - local_state.phase_entered_ms;

    Light car_light = RED;
    Light ped_light = RED;
    get_lights_for_phase(local_state.phase, car_light, ped_light);

    uint8_t countdown = compute_ped_countdown_sec(local_state.phase, elapsed);
    uint8_t wait_flag = local_state.ped_request ? 1 : 0;

    digitalWrite(LED_PIN, local_state.ped_request ? LOW : HIGH);

    char payload[160];
    snprintf(
      payload,
      sizeof(payload),
      "%s;%lld;%s;%s;%s;%u;%u",
      DEVICE_ID,
      (long long)epoch_ms(),
      PHASE_NAMES[local_state.phase],
      LIGHT_NAMES[car_light],
      LIGHT_NAMES[ped_light],
      (unsigned)wait_flag,
      (unsigned)countdown
    );

    mqtt_publish("lights", payload);
  }
}

static void IRAM_ATTR button_isr(void) {
  ButtonPressEvent evt;
  evt.press_ms = millis();

  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(button_queue, &evt, &woken);
  portYIELD_FROM_ISR(woken);
}

static void task_button(void *pvParameters) {
  (void)pvParameters;

  bool has_accepted = false;
  uint32_t last_accepted_ms = 0;
  ButtonPressEvent evt;

  while (true) {
    if (xQueueReceive(button_queue, &evt, portMAX_DELAY) == pdTRUE) {
      if (has_accepted && (uint32_t)(evt.press_ms - last_accepted_ms) < MIN_INTERARRIVAL_MS) {
        Serial.println("[button] apăsare respinsă (interval prea mic)");
        continue;
      }

      has_accepted = true;
      last_accepted_ms = evt.press_ms;

      if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
        g_state.ped_request = true;
        xSemaphoreGive(state_mutex);
      }

      Serial.println("[button] cerere pieton înregistrată");
    }
  }
}

void command_received(const char* text) {
  Serial.printf("[mqtt] comandă primită: %s\n", text);
  if (text != nullptr && strcmp(text, "BUTTON") == 0 && button_queue != nullptr) {
    ButtonPressEvent evt;
    evt.press_ms = millis();
    xQueueSend(button_queue, &evt, 0);
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) delay(10);
  delay(200);
  Serial.println();
  Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  wifi_connect();
  time_sync();

  state_mutex = xSemaphoreCreateMutex();
  button_queue = xQueueCreate(16, sizeof(ButtonPressEvent));

  if (state_mutex != nullptr) {
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
      g_state.phase = CAR_GREEN;
      g_state.phase_entered_ms = millis();
      g_state.ped_request = false;
      xSemaphoreGive(state_mutex);
    }
  }

  mqtt_start();

  xTaskCreate(task_control, "task_control", 4096, nullptr, 5, nullptr);
  xTaskCreate(task_button,  "task_button",  4096, nullptr, 4, nullptr);
  xTaskCreate(task_output,  "task_output",  4096, nullptr, 3, nullptr);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
}

void loop() {
  delay(1000);
}