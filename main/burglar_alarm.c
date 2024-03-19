/* UART Echo Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <string.h>
#include "rom/gpio.h"
#include <unistd.h>
#include "esp_adc_cal.h"
#include "../includes/config.h"

/**
 * This is an example which echos any data it receives on configured UART back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: configured UART
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 * - Pin assignment: see defines below (See Kconfig)
 */

#define SMS_TXD (GPIO_NUM_4)
#define SMS_RXD (GPIO_NUM_5)
#define SMS_RTS (UART_PIN_NO_CHANGE)
#define SMS_CTS (UART_PIN_NO_CHANGE)
#define SMS_UART_PORT_NUM (2)
#define SMS_UART_BAUD_RATE (9600)
#define SMS_TASK_STACK_SIZE (2048)
#define BUF_SIZE (1024)
#define PIR_PIN GPIO_NUM_18
#define ALARM 2
#define TURN_ON 1
#define TURN_OFF 0

void init_sms_task(void)
{
	uart_config_t uart_config = {
        .baud_rate = SMS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

	#if CONFIG_UART_ISR_IN_IRAM
		intr_alloc_flags = ESP_INTR_FLAG_IRAM;
	#endif
	
    ESP_ERROR_CHECK(uart_driver_install(SMS_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(SMS_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SMS_UART_PORT_NUM, SMS_TXD, SMS_RXD, SMS_RTS, SMS_CTS));
}

// Configure pin for LED
static void init_led()
{
	gpio_pad_select_gpio(19);
	gpio_set_level(19,0);
	gpio_set_direction(19, GPIO_MODE_OUTPUT);
}

// Configure pin for PIR sensor
static void init_pir(void)
{
	gpio_pad_select_gpio(PIR_PIN);
	gpio_set_level(PIR_PIN, 0);
	gpio_set_direction(PIR_PIN, GPIO_MODE_INPUT);
}

// Delete message with index number 1 from SIM-card
static void delete_message()
{
	int index = 0;
	char *query[4] = {"AT\r\n", "AT+CMGF=1\r\n", "AT+CMGD=1\r\n", NULL};
	while(query[index] != NULL)
	{
		uart_write_bytes(SMS_UART_PORT_NUM, query[index], strlen(query[index]));
		vTaskDelay(500 / portTICK_PERIOD_MS);
		index++;
	}
}

// Send message from SIM-card, PHONENUM is defined in .h file, which I don't push to github
static void send_message(uint8_t status)
{
	int index = 0;
	char *query[6] = {"AT\r\n", "AT+CMGF=1\r\n", PHONENUM, "Heraa pahvi, nyt on tosi kyseessa!!\r\n", "\x1A", NULL};
	while(query[index] != NULL)
	{
		uart_write_bytes(SMS_UART_PORT_NUM, query[index], strlen(query[index]));
		vTaskDelay(500 / portTICK_PERIOD_MS);
		index++;
	}
}

// Getting data from SIM-card, checking messages from index 1
static void sms_task(int *status)
{
    // Configure a temporary buffer for the incoming data and fill buffer with zeros
	uint8_t data[BUF_SIZE];
	bzero((void *)data, BUF_SIZE);
	int index = 0;
	int len = 0;
	
	// AT commands
	char *query[4] = {"AT\r\n", "AT+CMGF=1\r\n", "AT+CMGR=1\r\n", NULL};

	while(query[index] != NULL)
	{
		// Send commands to SMS module, writes answer into buffer and returns -1 if error occured, otherwise number of bytes pushed to FIFO
		uart_write_bytes(SMS_UART_PORT_NUM, query[index], strlen(query[index]));
		vTaskDelay(500 / portTICK_PERIOD_MS);
		index++;
	}

	len = uart_read_bytes(SMS_UART_PORT_NUM, &data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);

	if (len > 0)
	{
		data[len] = '\0';
		write(1, (char *)data, strlen((char *)data));
		write(1, "\n", 1);
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if (strstr((char *)data, "TURN ON") != NULL)
		{
			gpio_set_level(19,1);
			vTaskDelay(200 / portTICK_PERIOD_MS);
			delete_message();
			vTaskDelay(200 / portTICK_PERIOD_MS);
			*status = TURN_ON;
		}
		else if (strstr((char *)data, "TURN OFF") != NULL)
		{
			gpio_set_level(19,0);
			vTaskDelay(200 / portTICK_PERIOD_MS);
			delete_message();
			vTaskDelay(200 / portTICK_PERIOD_MS);
			*status = TURN_OFF;
		}
	}
	vTaskDelay(5000 / portTICK_PERIOD_MS);
}

void app_main(void)
{
	int status = TURN_OFF;
	init_pir();
	init_led();
	init_sms_task();
	while (1)
	{
		sms_task(&status);
		vTaskDelay(200 / portTICK_PERIOD_MS);
		if (status == TURN_ON)
		{
			if (gpio_get_level(PIR_PIN))
			{
				send_message(status);
				vTaskDelay(200 / portTICK_PERIOD_MS);
				status = ALARM;
			}
		}
	}
}

