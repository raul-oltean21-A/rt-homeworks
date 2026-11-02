#include "config.h"
#include "connection.h"

volatile uint32_t current_period_ms = PERIOD_MS_INITIAL;
volatile float current_threshold = THRESHOLD_INITIAL;

typedef struct {
    float temperature;
    float threshold;
} AlarmEvent;

typedef struct {
    char text[COMMAND_MAX_LEN];
} CommandEvent;

QueueHandle_t alarmQueue;
QueueHandle_t commandQueue;


float read_temperature(void) {
    return temperatureRead();
}

// TODO 1: TASK PERIODIC
void task_periodic(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    for (;;) {
        float temp = read_temperature();
        long long ts = (long long)epoch_ms();
        
        char payload[128];
        snprintf(payload, sizeof(payload), "%s;%lld;%.2f", DEVICE_ID, ts, temp);
        mqtt_publish("telemetry", payload);
        Serial.printf("[Periodic] Telemetrie: %.2f C\n", temp);

        if (temp > current_threshold) {
            AlarmEvent alarm = {temp, current_threshold};
            if (xQueueSend(alarmQueue, &alarm, 0) == pdPASS) {
                Serial.println("[Periodic] Alarma pusa in coada.");
            }
        }

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(current_period_ms));
    }
}

// TODO 2: TASK SPORADIC
void task_sporadic(void *pvParameters) {
    AlarmEvent alarm;
    uint32_t last_alarm_time = 0; // Folosim uint32_t pentru millis()
    
    for (;;) {
        if (xQueueReceive(alarmQueue, &alarm, portMAX_DELAY) == pdPASS) {
            uint32_t now = millis();
            
            if (now - last_alarm_time >= MIN_INTERARRIVAL_MS || last_alarm_time == 0) {
                last_alarm_time = now;
                long long ts = (long long)epoch_ms(); // Timestampul pentru mesaj
                
                char payload[128];
                snprintf(payload, sizeof(payload), "%s;%lld;%.2f;%.2f", 
                         DEVICE_ID, ts, alarm.temperature, alarm.threshold);
                mqtt_publish("alarm", payload);
                Serial.println("[Sporadic] Alarma publicata.");
            } else {
                Serial.println("[Sporadic] Alarma respinsa (prea devreme).");
            }
        }
    }
}

// TODO 3: TASK APERIODIC
void task_aperiodic(void *pvParameters) {
    CommandEvent cmd;
    
    for (;;) {
        if (xQueueReceive(commandQueue, &cmd, portMAX_DELAY) == pdPASS) {
            long long ts = (long long)epoch_ms();
            char payload[128];
            char response_status[64] = "OK";
            
            Serial.printf("[Aperiodic] Comanda procesata: %s\n", cmd.text);

            if (strncmp(cmd.text, "SET_INTERVAL", 12) == 0) {
                int new_val;
                if (sscanf(cmd.text, "SET_INTERVAL %d", &new_val) == 1 && new_val > 0) {
                    current_period_ms = new_val;
                } else {
                    strcpy(response_status, "ERROR PARAMETER");
                }
            } 
            else if (strncmp(cmd.text, "SET_THRESHOLD", 13) == 0) {
                float new_val;
                if (sscanf(cmd.text, "SET_THRESHOLD %f", &new_val) == 1) {
                    current_threshold = new_val;
                } else {
                    strcpy(response_status, "ERROR PARAMETER");
                }
            } 
            else if (strcmp(cmd.text, "RESET") == 0) {
                current_period_ms = PERIOD_MS_INITIAL;
                current_threshold = THRESHOLD_INITIAL;
            } 
            else if (strcmp(cmd.text, "STATUS") == 0) {
                snprintf(response_status, sizeof(response_status), "OK T=%d TH=%.2f", 
                         current_period_ms, current_threshold);
            } 
            else {
                strcpy(response_status, "ERROR UNKNOWN COMMAND");
            }

            snprintf(payload, sizeof(payload), "%s;%lld;%s %s", DEVICE_ID, ts, response_status, cmd.text);
            mqtt_publish("status", payload);
        }
    }
}


void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    CommandEvent cmd;
    strncpy(cmd.text, text, COMMAND_MAX_LEN);
    cmd.text[COMMAND_MAX_LEN - 1] = '\0';
    
    xQueueSend(commandQueue, &cmd, 0); 
}

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    // TODO 0 (a)
    alarmQueue = xQueueCreate(10, sizeof(AlarmEvent));
    commandQueue = xQueueCreate(10, sizeof(CommandEvent));

    mqtt_start();

    // TODO 0 (b)
    xTaskCreate(task_periodic, "Periodic", 4096, NULL, 5, NULL);
    xTaskCreate(task_sporadic, "Sporadic", 4096, NULL, 4, NULL);
    xTaskCreate(task_aperiodic, "Aperiodic", 4096, NULL, 3, NULL);
}

void loop() {
    delay(1000);
}