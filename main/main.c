#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "soc/soc_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

#include "wifi.h"

#define ROW1_GPIO 2
#define ROW2_GPIO 4
#define ROW3_GPIO 5
#define ROW4_GPIO 18

#define COL1_GPIO 12
#define COL2_GPIO 13
#define COL3_GPIO 14

#define BUZZER_PIN 15  // Conecte o pino do buzzer a este pino GPIO

static const char *TAG = "painel_ferramentas";

TimerHandle_t keypadTimer = NULL;  // Variável global inicializada com NULL
BaseType_t timerStarted = pdFALSE; // Variável global para controlar se o temporizador está iniciado

char keypadValues[4][3] = {
    { '1', '2', '3' },
    { '4', '5', '6' },
    { '7', '8', '9' },
    { '*', '0', '#' }
};

QueueHandle_t queue_adc;

typedef struct {
    char* id_ferramenta;
    adc_channel_t channel;
    adc_atten_t atten;
    bool is_retira;
    bool result;
} SensorConfig;

// Defina as configurações para os sensores
SensorConfig sensor_configs[] = {
    {"1", ADC_CHANNEL_0, ADC_ATTEN_DB_11, false, false}, // Sensor 1
    {"2", ADC_CHANNEL_3, ADC_ATTEN_DB_11, false, false}, // Sensor 2 
    {"3", ADC_CHANNEL_6, ADC_ATTEN_DB_11, false, false}, // Sensor 3 
};

void stop_keypad_timer() 
{
    if (timerStarted) {
        xTimerStop(keypadTimer, 0);
        timerStarted = pdFALSE;
        ESP_LOGI(TAG, "Temporizador cancelado.");
    }
}

char* set_parameters(const char *posicao, const char *tipoOperacao, const char *codigo)
{
    // Use snprintf para determinar o tamanho necessário
    int post_data_size = snprintf(NULL, 0, "{\"posicao\":\"%s\",\"tipoOperacao\":\"%s\",\"codigo\":\"%s\"}", posicao, tipoOperacao, codigo);

    if (post_data_size < 0) {
        // Erro ao calcular o tamanho da string formatada
        ESP_LOGE(TAG, "Erro ao calcular o tamanho da string formatada.");
        return NULL;
    }

    // Aloque dinamicamente a memória necessária
    char *post_data = (char *)malloc(post_data_size + 1);  // +1 para o caractere nulo de terminação

    if (post_data == NULL) {
        // Erro ao alocar memória
        ESP_LOGE(TAG, "Erro ao alocar memória para post_data.");
        return NULL;
    }

    // Cria a string formatada no espaço alocado
    snprintf(post_data, post_data_size + 1, "{\"posicao\":\"%s\",\"tipoOperacao\":\"%s\",\"codigo\":\"%s\"}", posicao, tipoOperacao, codigo);

    // Retorna a string formatada
    return post_data;
}

//! 403 - Código do usuário não encontrado
//! 402 - Código do ferramenta não encontrado
//! 201 - Autenticado com sucesso
void send_request(const char *posicao, const char *tipoOperacao, const char *codigo)
{
    bool request_status = false;

    // Configurar os parâmetros
    char *post_data = set_parameters(posicao, tipoOperacao, codigo);

    // Erro ao configurar os parâmetros
    if (post_data == NULL) {
        ESP_LOGE(TAG, "Erro ao configurar os parâmetros.");
        return;
    }

    esp_http_client_config_t config = {
        .url = "http://main--incomparable-cobbler-553924.netlify.app/api/painel",
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        request_status = true;
        ESP_LOGE(TAG, "Erro na requisição HTTP");
    } else {
        ESP_LOGI(TAG, "Requisição HTTP enviada com sucesso.");

        int status =  esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "%d", status);

        if(status == 403) {
            ESP_LOGI(TAG, "Código do usuário não encontrado.");
        } else if(status == 402) {
            ESP_LOGI(TAG, "Código da ferramenta não encontrado.");
        } else if(status == 201) {
            ESP_LOGI(TAG, "Autenticado com sucesso.");
        }

        stop_keypad_timer();
    }

    free(post_data);

    esp_http_client_cleanup(client);
}

void keypad_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Tempo esgotado. Nenhum código inserido.");

    timerStarted = pdFALSE;
}

void start_keypad_timer()
{   
    keypadTimer = xTimerCreate("keypadTimer", pdMS_TO_TICKS(30000), pdFALSE, (void *)0, keypad_timer_callback);
    if(keypadTimer != NULL){
        if (xTimerStart(keypadTimer, 0) == pdPASS) {
            timerStarted = pdTRUE;
            ESP_LOGI(TAG, "Temporizador iniciado.");
        } else {
            timerStarted = pdFALSE;
            ESP_LOGE(TAG, "Falha ao iniciar o temporizador.");
        }
    } else {
        timerStarted = pdFALSE;
        ESP_LOGE(TAG, "Falha ao criar o temporizador.");
    }
}

void configure_keypad()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ROW1_GPIO) | (1ULL << ROW2_GPIO) | (1ULL << ROW3_GPIO) | (1ULL << ROW4_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,  // Alterado para GPIO_INTR_DISABLE
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << COL1_GPIO) | (1ULL << COL2_GPIO) | (1ULL << COL3_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
}

char read_keypad()
{
    for (int i = 0; i < 4; i++) {
        // Set the current row to LOW
        gpio_set_level(ROW1_GPIO, i == 0 ? 0 : 1);
        gpio_set_level(ROW2_GPIO, i == 1 ? 0 : 1);
        gpio_set_level(ROW3_GPIO, i == 2 ? 0 : 1);
        gpio_set_level(ROW4_GPIO, i == 3 ? 0 : 1);

        // Check each column
        for (int j = 0; j < 3; j++) {
            if (gpio_get_level(COL1_GPIO + j) == 0) {
                // Key pressed, return the corresponding value
                return keypadValues[i][j];
            }
        }
    }

    return '\0'; // No key pressed
}

int process_key(char key, char *result)
{
    // Verifica a quantidade de caracteres na string result
    size_t len = strlen(result);

    if (key == '*') {
        // Apagar o último caractere da string (se houver algum)
        if (len > 0) {
            result[len - 1] = '\0';
        }

        return 0;
    } else if (key == '#') {
        ESP_LOGI(TAG, "Código enviado: %s", result);

        return 1;
    } else {
        // Adicionar o caractere à string result
        if (len < sizeof(result) - 1) {
            result[len] = key;
            result[len + 1] = '\0';
        }

        return 0;
    }
}

void task_adc(void* pvParameter) {
    // Inicialização do ADC
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1, // Escolha a unidade ADC adequada
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Configurar os sensores fora do loop principal
    for (int i = 0; i < sizeof(sensor_configs) / sizeof(sensor_configs[0]); i++) {
        SensorConfig config = sensor_configs[i];
        adc_oneshot_chan_cfg_t adc_config = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = config.atten,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, config.channel, &adc_config));
    }

    while(1) {
        for (int i = 0; i < sizeof(sensor_configs) / sizeof(sensor_configs[0]); i++) {
            int sensor_value;

            // Leitura do sensor
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, sensor_configs[i].channel, &sensor_value));
            ESP_LOGI(TAG, "Sensor %d - Value: %d", i + 1, sensor_value);
            
            // Retirada
            if (sensor_configs[i].is_retira) {
                if(sensor_value != 0) {
                    sensor_configs[i].result = true;
                    sensor_configs[i].is_retira = false;
                    
                    ESP_LOGI(TAG, "IS_RETIRA: %d", sensor_configs[i].is_retira);
                    ESP_LOGI(TAG, "RESULT: %d", sensor_configs[i].result);

                    xQueueSend(queue_adc, ( void * )&sensor_configs[i], 0);
                }
                else {
                    sensor_configs[i].result = false;
                }
            } else {
                if(sensor_value == 0) {
                    sensor_configs[i].result = true;
                    sensor_configs[i].is_retira = true;

                    ESP_LOGI(TAG, "IS_RETIRA: %d", sensor_configs[i].is_retira);
                    ESP_LOGI(TAG, "RESULT: %d", sensor_configs[i].result);

                    xQueueSend(queue_adc, ( void * )&sensor_configs[i], 0);
                } else {
                    sensor_configs[i].result = false;
                }
            }	
        }

        vTaskDelay(500);
    }

    return;
}

void app_main(void)
{
    queue_adc = xQueueCreate(1, sizeof(SensorConfig));

    xTaskCreate(task_adc, "task_adc", 2048, NULL, 10, NULL);

    // Configuração do LEDC
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT, // Resolução do PWM
        .freq_hz = 4000,                      // Frequência em Hz (aumentada para tornar mais agudo)
        .speed_mode = LEDC_HIGH_SPEED_MODE,   // Modo de alta velocidade
        .timer_num = LEDC_TIMER_0             // Timer 0
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "iniciando WiFi do ESP32");
    wifi_init_sta();

    configure_keypad();

    SensorConfig sensorConfig;
    char result[10] = "";

    while (1) {
        xQueueReceive(queue_adc, &sensorConfig, portMAX_DELAY);

        if(sensorConfig.result) {
            ESP_LOGI(TAG, "INSIRA O CÓDIGO:");

            start_keypad_timer();

            bool valid;
            while (1){
                char key = read_keypad();
                        
                if (key != '\0') {
                    int ret = process_key(key, result);
                    if (ret == 1) {
                        valid = true;
                        break;
                    }

                    ESP_LOGI(TAG, "Tecla pressionada: %c", key);
                    ESP_LOGI(TAG, "Código atual: %s", result);
                }

                // se timer estourou, da um break;
                if(timerStarted == pdFALSE) {
                    valid = false;
                    break;
                }

                vTaskDelay(50);
            }

            if (valid) {
                // Manda request de sucesso
                ESP_LOGI(TAG, "REQUEST ENVIADO");

                char *posicao = sensorConfig.id_ferramenta;
                char *tipoOperacao = sensorConfig.is_retira ? "Retirada" : "Devolução";
                char *codigo = result;

                send_request(posicao, tipoOperacao, codigo);
            } else {
                // Manda request do timeout
                ESP_LOGI(TAG, "REQUEST DE TEMPO EXCEDIDO");

                char *posicao = sensorConfig.id_ferramenta;
                char *tipoOperacao = sensorConfig.is_retira ? "Retirada" : "Devolução";
                char *codigo = "0";

                send_request(posicao, tipoOperacao, codigo);

                // Produza um som no buzzer
                ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 5000); // Duty cycle máximo
                ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
                vTaskDelay(200);  // Aguarde 200ms
                ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0); // Desligue o buzzer
                ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
                vTaskDelay(200);  // Aguarde 200ms
            }

            result[0] = '\0';
        }
    }
}