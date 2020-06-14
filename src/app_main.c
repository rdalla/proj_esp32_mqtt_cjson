//Bibliotecas C

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

//Bibliotecas do ESP32

#include <esp_wifi.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include "driver/gpio.h"

//Bibliotecas do FreeRTOS

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

//Bibliotecas de rede

#include <lwip\sockets.h>
#include <lwip\dns.h>
#include <lwip\netdb.h>

#include "DHT.h"
#include "cJSON.h"

// Defines de referencia para o codigo
#define DHTPIN                4
#define DHTTYPE               DHT22
#define LED_CONNECTION_STATUS 2
#define GPIO_COMMAND_CTRL     18

#define LIGA_LED()            gpio_set_level(LED_CONNECTION_STATUS, 1)
#define DESLIGA_LED()         gpio_set_level(LED_CONNECTION_STATUS, 0)

#define ID1                   "DallaValle_ESP32_LEITURA_SENSOR"
#define ID2                   "DallaValle_ESP32_LEITURA_SWITCH"
#define ID3                   "DallaValle_ESP32_LEITURA_LED"

/* Funções auxiliares */
void readMessage(char *data);

/*handle do Semaforo*/
//SemaphoreHandle_t xMutex = 0;

/*handle do Queue*/
QueueHandle_t xSensor_Control = 0;
QueueHandle_t xSwitch_Control = 0;
QueueHandle_t xLed_Control = 0;

/* Variáveis para Armazenar o handle da Task */
TaskHandle_t xPublishTask;
TaskHandle_t xSensorTask;
TaskHandle_t xSwitchTask;
TaskHandle_t xLedTask;

/*Prototipos das Tasks*/
void vPublishTask(void *pvParameter);
void vSensorTask(void *pvParameter);
void vSwitchTask(void *pvParameter);
void vLedTask(void *pvParameter);

//Inicializacao de TAGs
static const char *TAG = "MQTT_IOT";
static const char *TAG1 = "TASK";
static const char *TAG2 = "LEITURA_SENSOR";
static const char *TAG3 = "LEITURA_SWITCH";
static const char *TAG4 = "LEITURA_LED";

//Inicializacao de elementos
static const char *topic_mqtt_cmd = "unisal/cps/iot/dallavalle/cmd";
static const char *topic_mqtt_data = "unisal/cps/iot/dallavalle/data";
char *printed_sensor = NULL;
char *printed_switch = NULL;
char *printed_led = NULL;

//Referencia para saber status de conexao
static EventGroupHandle_t wifi_event_group;
const static int CONNECTION_STATUS = BIT0;

//Cliente MQTT
esp_mqtt_client_handle_t mqtt_client;

//---------------------------------------------------------------------------
//Espaco para criacao de tasks para o FreeRTOS

/* Task de Publish MQTT */
void vPublishTask(void *pvParameter)
{

  
  while(1)
  {

    ESP_LOGI(TAG, "Enviando dados para o topico %s...", topic_mqtt_data);

    if (!xQueueReceive(xSensor_Control, &printed_sensor, 3000))
    {
      ESP_LOGI(TAG, "Falha ao receber o valor da fila xSensor_Control.\n");
    }

    if (!xQueueReceive(xLed_Control, &printed_led, 3000))
    {
      ESP_LOGI(TAG, "Falha ao receber o valor da fila xLed_Control.\n");
    }

    if (!xQueueReceive(xSwitch_Control, &printed_switch, 3000))
    {
      ESP_LOGI(TAG, "Falha ao receber o valor da fila xSwitch_Control.\n");
    }

    //Sanity check do mqtt_client antes de publicar
    esp_mqtt_client_publish(mqtt_client, topic_mqtt_data, printed_sensor, 0, 0, 0);
    esp_mqtt_client_publish(mqtt_client, topic_mqtt_data, printed_switch, 0, 0, 0);
    esp_mqtt_client_publish(mqtt_client, topic_mqtt_data, printed_led, 0, 0, 0);

    free(printed_sensor);
    free(printed_switch);
    free(printed_led);

    vTaskDelay(5000 / portTICK_PERIOD_MS);

  }
}

/* Tarefa de leitura do estado do botão */
void vSwitchTask(void *pvParameter)
{

  int msg = 0; //numero de mensagens
  cJSON *tactil_switch;

  ESP_LOGI(TAG, "Iniciando task Leitura Botão...");

  while (1)
  {
    msg = msg + 1;
    ESP_LOGI(TAG, "Lendo dados de Leitura Switch...\n");

    // Cria a estrutura de dados TACTIL_SWITCH a ser enviado por JSON
    tactil_switch = cJSON_CreateObject();
    cJSON_AddStringToObject(tactil_switch, "ID", ID2);
    cJSON_AddNumberToObject(tactil_switch, "MSG", msg);

    if (gpio_get_level(GPIO_COMMAND_CTRL) == 0) // Se a tecla for pressionada
    {
      vTaskDelay(pdMS_TO_TICKS(200)); // Aguarda 200ms por causa do bouncing do botao

      if (gpio_get_level(GPIO_COMMAND_CTRL) == 0) // Depois do debounce, verifico se o botao está apertado ainda.
      {
        cJSON_AddItemToObject(tactil_switch, "Switch_Status", cJSON_CreateString("TRUE"));
      }
    } else {
      cJSON_AddItemToObject(tactil_switch, "Switch_Status", cJSON_CreateString("FALSE"));
    }

    ESP_LOGI(TAG3, "Info data:%s\n", cJSON_Print(tactil_switch));

    printed_switch = cJSON_Print(tactil_switch);

    if (!xQueueSend(xSwitch_Control, &printed_switch, 3000))
    {
      ESP_LOGI(TAG, "\nFalha ao enviar o valor para a fila xSwitch_Control.\n");
    }

    // Apaga a estrutura de dados JSON
    cJSON_Delete(tactil_switch);

    vTaskDelay(5000 / portTICK_RATE_MS);
  }
}

/* Tarefa de leitura do Led */
void vLedTask(void *pvParameter)
{
  DESLIGA_LED(); //inicializacao do estado do led
  int msg = 0; //numero de mensagens
  cJSON *led;

  ESP_LOGI(TAG, "Iniciando task Leitura Led...");

  while (1)
  {
    msg = msg + 1;
    ESP_LOGI(TAG, "Lendo dados de Leitura Led...\n");

    /* Cria a estrutura de dados LED a ser enviado por JSON */
    led = cJSON_CreateObject();
    cJSON_AddStringToObject(led, "ID", ID3);
    cJSON_AddNumberToObject(led, "MSG", msg);

    if (gpio_get_level(LED_CONNECTION_STATUS) == 1) // Se o led estiver ligado
    {
      cJSON_AddItemToObject(led, "LED_Status", cJSON_CreateString("ON"));
    } else {
      cJSON_AddItemToObject(led, "LED_Status", cJSON_CreateString("OFF"));
    }

    ESP_LOGI(TAG4, "Info data:%s\n", cJSON_Print(led));

    printed_led = cJSON_Print(led);

    if (!xQueueSend(xLed_Control, &printed_led, 3000))
    {
      ESP_LOGI(TAG, "\nFalha ao enviar o valor para a fila xLed_Control.\n");
    }

    /* Apaga a estrutura de dados JSON */
    cJSON_Delete(led);

    vTaskDelay(5000 / portTICK_RATE_MS);
  }
}

/* Task do sensor DHT22 */
void vSensorTask(void *pvParameter)
{
  setDHTgpio(DHTPIN);
  int msg = 0; //numero de mensagens
  int temperature = 0;
  int humidity = 0;
  cJSON *monitor;
  
  ESP_LOGI(TAG, "Iniciando task DHT22...");

  while (1)
  {
    msg = msg + 1;

    ESP_LOGI(TAG, "Lendo dados de Temp e Umid...\n");
    int ret = readDHT();
    errorHandler(ret);
    temperature = getTemperature();
    humidity = getHumidity();

    /* Cria a estrutura de dados MONITOR a ser enviado por JSON */
    monitor = cJSON_CreateObject();
    cJSON_AddStringToObject(monitor, "ID", ID1);
    cJSON_AddNumberToObject(monitor, "MSG", msg);
    cJSON_AddNumberToObject(monitor, "TEMP", temperature);
    cJSON_AddNumberToObject(monitor, "UMID", humidity);

    ESP_LOGI(TAG2, "Info data:%s\n", cJSON_Print(monitor));

    printed_sensor = cJSON_Print(monitor);

    if (!xQueueSend(xSensor_Control, &printed_sensor, 3000))
    {
      ESP_LOGI(TAG, "\nFalha ao enviar o valor para a fila xSensor_Control.\n");
    }

    /* Apaga a estrutura de dados JSON */
    cJSON_Delete(monitor);

    vTaskDelay(5000 / portTICK_RATE_MS);
  }
}

//---------------------------------------------------------------------------
//Callback para tratar eventos MQTT

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{

  switch (event->event_id)
  {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Conexao Realizada com Broker MQTT");
    esp_mqtt_client_subscribe(mqtt_client, topic_mqtt_cmd, 0);
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Desconexao Realizada com Broker MQTT");
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "Subscribe Realizado com Broker MQTT");
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "Publish Realizado com Broker MQTT");
    break;

  //Evento de chegada de mensagens
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "Dados recebidos via MQTT");
    /* Verifica se o comando recebido é válido */
    if (strncmp(topic_mqtt_cmd, event->topic, event->topic_len) == 0)
    {
      
      readMessage(event->data);
      ESP_LOGI(TAG, "OK Analisada...");
      
    } else {
      ESP_LOGI(TAG, "Topico invalido");
    }
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    break;

  default:
    break;
  }
  return ESP_OK;
}

//---------------------------------------------------------------------------
//Callback para tratar eventos WiFi

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{

  switch (event->event_id)
  {
  case SYSTEM_EVENT_STA_START:
    esp_wifi_connect(); //inicia conexao Wi-Fi
    break;

  case SYSTEM_EVENT_STA_GOT_IP:
    xEventGroupSetBits(wifi_event_group, CONNECTION_STATUS); // "seta" status de conexao
    break;

  case SYSTEM_EVENT_STA_DISCONNECTED:
    esp_wifi_connect();                                        //tenta conectar de novo
    xEventGroupClearBits(wifi_event_group, CONNECTION_STATUS); //limpa status de conexao
    break;

  default:
    break;
  }
  return ESP_OK;
}

//---------------------------------------------------------------------------
// Funcao de verificacao de mensagens recebidas

void readMessage(char *data)
{
  /* Variaveis para armazenar comandos recebidos via Broker */
  cJSON *root;
  cJSON *cmd;

  ESP_LOGI(TAG, "Analisando uma mensagem...");

  /* Parseamento do JSON */
  root = cJSON_Parse(data);
  ESP_LOGI(TAG, "Esta parseado...");

  /* Parseamento do JSON procuando um comando válido*/
  cmd = cJSON_GetObjectItem(root, "CMD");
  ESP_LOGI(TAG, "Encontrei CMD...");

  /* Caso ele encontre um comando válido*/
  /* liga LED*/
  if (strcmp(cmd->valuestring, "1") == 0)
  {
    ESP_LOGI(TAG, "Liguei o LED...");

    LIGA_LED();
  }

  /* desliga LED */
  else if (strcmp(cmd->valuestring, "0") == 0)
  {
    ESP_LOGI(TAG, "Desliguei o LED...");
    DESLIGA_LED();
  }
  else
  {
    /* Envia uma mensagem caso o valor não seja válido */
    ESP_LOGI(TAG, "Comando invalido");
  }

  /* Deleta o valor atribuido na variavel */
  cJSON_Delete(root);
}

//---------------------------------------------------------------------------
//Inicializacao WiFi

static void wifi_init(void)
{

  tcpip_adapter_init();
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  wifi_config_t wifi_config = {
      .sta = {
          .ssid = "BETO_CLARO",   //a ssid da sua rede wifi
          .password = "hhgdsw12", //o password da sua rede wifi
      }};

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); //ESP32 em modo station
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_LOGI(TAG, "Iniciando Conexao com Rede WiFi...");
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "Conectando...");
  xEventGroupWaitBits(wifi_event_group, CONNECTION_STATUS, false, true, portMAX_DELAY);
}

//---------------------------------------------------------------------------
//Inicializacao MQTT Service

static void mqtt_init(void)
{
  const esp_mqtt_client_config_t mqtt_cfg = {
      .uri = "mqtt://test.mosquitto.org:1883",
      .event_handle = mqtt_event_handler,
      .client_id = "DallaValle_ESP32_IoT", //cada um use o seu! não repita o do colega!
  };

  //Inicializa cliente mqtt
  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_start(mqtt_client);
  
}

//---------------------------------------------------------------------------
//APP_MAIN

void app_main()
{
  /*Configurando LED como saida*/
  gpio_pad_select_gpio(LED_CONNECTION_STATUS);
  gpio_set_direction(LED_CONNECTION_STATUS, GPIO_MODE_INPUT_OUTPUT); //GPIO_MODE_INPUT_OUTPUT , para possibilitar leitura do estado do LED

  /*Configurando botao como entrada*/
  gpio_pad_select_gpio(GPIO_COMMAND_CTRL);
  gpio_set_direction(GPIO_COMMAND_CTRL, GPIO_MODE_INPUT);

  ESP_LOGI(TAG, "Iniciando ESP32 IoT App...");
  // Setup de logs de outros elementos
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);

  /* Inicializacao de ponteiros de dados JSON */
  printed_sensor = malloc(100 * sizeof(int32_t));
  printed_switch = malloc(100 * sizeof(int32_t));
  printed_led = malloc(100 * sizeof(int32_t));

  // Inicializacao da NVS = Non-Volatile-Storage (NVS)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init();
  mqtt_init();

  //criação de fila do xSensor_Control (vSensorTask <--> vPublishTask)
  xSensor_Control = xQueueCreate(10, sizeof(int));
  if (xSensor_Control == NULL)
  {
    ESP_LOGI(TAG1, "Erro na criação da Queue.\n");
  }

  //criação de fila do xSwitch_Control (vSwitchTask <--> vPublishTask)
  xSwitch_Control = xQueueCreate(10, sizeof(int));
  if (xSwitch_Control == NULL)
  {
   ESP_LOGI(TAG1, "Erro na criação da Queue.\n");
  }

  //criação de fila do xLed_Control (vLedTask <--> vPublishTask)
  xLed_Control = xQueueCreate(10, sizeof(int));
  if (xLed_Control == NULL)
  {
    ESP_LOGI(TAG1, "Erro na criação da Queue.\n");
  }

  if (xTaskCreate(&vPublishTask, "vPublishTask", configMINIMAL_STACK_SIZE + 4096, NULL, 5, xPublishTask) != pdTRUE)
  {
    ESP_LOGE("Erro", "error - nao foi possivel alocar vPublishTask.\n");
    while (1);
  }

  if (xTaskCreate(&vSensorTask, "vSensorTask", configMINIMAL_STACK_SIZE + 4096, NULL, 5, xSensorTask) != pdTRUE)
  {
    ESP_LOGE("Erro", "error - nao foi possivel alocar vSensorTask.\n");
    while (1);
  }

  if (xTaskCreate(&vSwitchTask, "vSwitchTask", configMINIMAL_STACK_SIZE + 4096, NULL, 5, xSwitchTask) != pdTRUE)
  {
    ESP_LOGE("Erro", "error - nao foi possivel alocar vSwitchTask.\n");
    while (1);
  }

  if (xTaskCreate(&vLedTask, "vLedTask", configMINIMAL_STACK_SIZE + 4096, NULL, 5, xLedTask) != pdTRUE)
  {
    ESP_LOGE("Erro", "error - nao foi possivel alocar vLedTask.\n");
    while (1);
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(3000)); /* Delay de 3 segundos */
  }

}