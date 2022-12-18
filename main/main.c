#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "lora.h"
#include "dht.h"
#include "ultrasonic.h"

/******************************************************
 *      LIST OF PIN      *   CONNECT WITH LORA MODULE *
 * DEVICE            PIN *    LoRa              PIN   *
 * DHT11             21  *    NSS               15    *
 * TRIGGER_1         19  *    RST               32    *
 * ECHO_1            18  *    MISO              13    *
 * TRIGGER_2         23  *    MOSI              12    *
 * ECHO_2            22  *    SCK               14    *
 * DOOR              33  *    Vcc               3.3V  * 
 *                       *    GND               GND   *
 ******************************************************/

/**********************************
 *     SEND DATA THROUGH LORA     *
 *                                *
 * || SOC || ID   VALUE || EOC || *
 *     !     ID : VALUE     #     *
 **********************************/

/*****************************
 *       LIST OF ID          *
 *   ID          VALUE       *
 *   0           TEMPERATURE *
 *   1           PPL_CNT     *
 *   2           PPL_IN      *
 *   3           PPL_OUT     *
 *****************************/


//DHT11 PIN
#define SENSOR_TYPE DHT_TYPE_DHT11
#define CONFIG_EXAMPLE_DATA_GPIO 21

//HC-SR04 PIN
#define MAX_DISTANCE_CM 500 // 5m max

#define TRIGGER_GPIO_1  19
#define ECHO_GPIO_1     18

#define TRIGGER_GPIO_2  23
#define ECHO_GPIO_2     22

//DOOR PIN
#define DOOR_PIN 33

// Queue
xQueueHandle DHT_queue;

int ppl_cnt = 0;     // Number of people in the bus
int ppl_in  = 0;     // Number of people get on the bus at each station
int ppl_out = 0;     // Number of people get off the bus at each station

int door = 0;

char Temp_ID[2]   = "0";
char Cnt_ID[1]    = "1";
char In_ID[2]     = "2";
char Out_ID[2]    = "3";

char colon[2]  = ":";
char soc[2]    = "!";
char eoc[2]    = "#";

TaskHandle_t TaskUltra_1;
TaskHandle_t TaskUltra_2;


typedef struct {
   float temperature;
   float humidity;
} DHT_data;

void task_tx(void *p)                        // Send data to LoRa
{
   DHT_data DHT_Receiver;

   // DHT11 string
   char str[10];
   char humidity[20];
   char temperature[20];

   while(1) 
   {
      if(xQueueReceive(DHT_queue,&DHT_Receiver,pdMS_TO_TICKS(500)) == pdPASS)
      {
      
         sprintf(humidity, "%g", DHT_Receiver.humidity);
         sprintf(temperature, "%g", DHT_Receiver.temperature);
         
         strcpy(str,soc);           // !
         strcat(str,Temp_ID);       // !Temp_ID
         strcat(str,colon);         // !Temp_ID:
         strcat(str,temperature);   // !Temp_ID:temperature
         strcat(str,eoc);           // !Temp_ID:temperature#
         
         printf("%s\n",str);
         lora_send_packet((uint8_t*)str, 30);
         printf("packet sent...\n");
      }

      vTaskDelay(pdMS_TO_TICKS(2000));
   }
}

void dht_test(void *pvParameters)            // DHT11 data
{
   float temperature, humidity;
   DHT_data DHT_Sender;

   #ifdef CONFIG_EXAMPLE_INTERNAL_PULLUP
      gpio_set_pull_mode(dht_gpio, GPIO_PULLUP_ONLY);
   #endif

   while (1)
   {
      if (dht_read_float_data(SENSOR_TYPE, CONFIG_EXAMPLE_DATA_GPIO, &humidity, &temperature) == ESP_OK)
      {
         printf("Humidity: %.1f%% Temp: %.1fC\n", humidity, temperature);
         DHT_Sender.humidity = humidity;
         DHT_Sender.temperature = temperature;
         xQueueSend(DHT_queue,&DHT_Sender,pdMS_TO_TICKS(1000));
      }
      vTaskDelay(pdMS_TO_TICKS(2000));  // If you read the sensor data too often, it will heat up
   }
}


void ultrasonic_test_1(void *pvParameters)   // Entrance 1
{
   ultrasonic_sensor_t sensor_1 = {
      .trigger_pin = TRIGGER_GPIO_1,
      .echo_pin = ECHO_GPIO_1
   };

   ultrasonic_init(&sensor_1);
   int currentState1 = 0;
   int previousState1 = 0;

   while (true)
   {
      if (door == 1)
      {
         float distance_1;
         esp_err_t res = ultrasonic_measure(&sensor_1, MAX_DISTANCE_CM, &distance_1);
         if (res == ESP_OK)
         {
            distance_1 = distance_1 * 100;
            printf("Distance_1: %0.02f cm\n", distance_1);

            if (distance_1 <= 9)    currentState1 = 1; 
            else                    
            {
                  currentState1 = 0;
                  previousState1 = 0;
            }

            if(currentState1 == 1 && currentState1 != previousState1)
            {    
                  previousState1 = currentState1;
                  ppl_cnt++;
                  ppl_in++;
                  printf("\n ppl_in: %d      ppl_cnt: %d \n",ppl_in,ppl_cnt);
            }
         }
      }
      vTaskDelay(pdMS_TO_TICKS(500));
   }
}

void ultrasonic_test_2(void *pvParameters)   // Entrance 2
{
   ultrasonic_sensor_t sensor_2 = {
      .trigger_pin = TRIGGER_GPIO_2,
      .echo_pin = ECHO_GPIO_2
   };

   ultrasonic_init(&sensor_2);
   int currentState2 = 0;
   int previousState2 = 0;

   while (true)
   {
      if(door == 1)
      {
         float distance_2;
         esp_err_t res = ultrasonic_measure(&sensor_2, MAX_DISTANCE_CM, &distance_2);
         if (res == ESP_OK)
         {
            distance_2 = distance_2 * 100;
            printf("Distance_2: %0.02f cm\n", distance_2);

            if (distance_2 <= 9)    currentState2 = 1; 
            else                    
            {
                  currentState2 = 0;
                  previousState2 = 0;
            }

            if(currentState2 == 1 && currentState2 != previousState2)
            {    
                  previousState2 = currentState2;
                  ppl_cnt--;
                  ppl_cnt = ppl_cnt < 0 ? 0 : ppl_cnt;
                  ppl_out++;
                  printf("\n ppl_out: %d      ppl_cnt: %d \n",ppl_out,ppl_cnt);
            }
         }
      }
      vTaskDelay(pdMS_TO_TICKS(500));
   }
}

void door_check(void *pvParameters)          // Door check
{
   gpio_pad_select_gpio(DOOR_PIN);
   gpio_set_direction(DOOR_PIN,GPIO_MODE_INPUT);
   gpio_set_pull_mode(DOOR_PIN,GPIO_PULLUP_ONLY);

//   ************************************************
//    *    33-----------|||||||--------------GND     *
//    *                                              *
//    *             DOOR CLOSED => 0                 *
//    *             DOOR OPENED => 1                 *
//    ************************************************

   while (1)
   {
      if (gpio_get_level(DOOR_PIN) == 1)    
      {
         door = 1;
         printf("DOOR OPEN !!!\n");
      }
      else
      {
         door = 0;
         printf("DOOR CLOSE !!!\n");
      }
      vTaskDelay (pdMS_TO_TICKS(500));
   }
}


void app_main()
{
   lora_init();
   lora_set_frequency(434000000);
   lora_enable_crc();

   DHT_queue = xQueueCreate(20,sizeof(DHT_data));

   xTaskCreatePinnedToCore(&task_tx, "task_tx", 2048, NULL, 5, NULL,1);
   xTaskCreate(&door_check, "door_check", 2048, NULL, 5, NULL);

   xTaskCreate(dht_test, "dht_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);

   xTaskCreate(ultrasonic_test_1, "ultrasonic_1", configMINIMAL_STACK_SIZE * 3, NULL, 2, &TaskUltra_1);
   xTaskCreate(ultrasonic_test_2, "ultrasonic_2", configMINIMAL_STACK_SIZE * 3, NULL, 2, &TaskUltra_2);
}