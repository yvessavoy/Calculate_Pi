/*
 * U_Calculate_Pi.c
 *
 * Created: 28.09.2021 19:25:00
 * Author : Yves
 */ 

#include <stdio.h>
#include "avr_compiler.h"
#include "pmic_driver.h"
#include "TC_driver.h"
#include "clksys_driver.h"
#include "sleepConfig.h"
#include "port_driver.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "stack_macros.h"

#include "mem_check.h"

#include "init.h"
#include "utils.h"
#include "errorHandler.h"
#include "NHD0420Driver.h"

#include "rtos_buttonhandler.h"

#define EG_START_CALC 0x01
#define EG_STOP_CALC  0x02
#define EG_RESET_CALC 0x04
#define EG_S1_PRESSED 0x08

typedef enum {
	State_Started,
	State_Stopped
} State_e;

typedef enum {
	LEIBNIZ,
	NILAKANTHA,
} Algorithm_e;

EventGroupHandle_t eventGroup;
Algorithm_e algorithm = LEIBNIZ;
TaskHandle_t calculationHandle;
State_e state = State_Stopped;

float pi4;

extern void vApplicationIdleHook(void);
void vCalculateLeibniz(void *pvParameters);
void vCalculateNilakantha(void *pvParameters);

void vApplicationIdleHook(void) {}

int main(void) {
	vInitClock();
	vInitDisplay();
	
	eventGroup = xEventGroupCreate();
	
	vTaskStartScheduler();
	
	return 0;
}

void vCalculateLeibniz(void *pvParameters) {
	uint32_t i = 0;
	pi4 = 1.0;
	
	for (;;) {
		pi4 = pi4 - (1.0 / (3 + (4 * i))) + (1.0 / (5 + (4 * i)));
		i++;
	}
}

void vCalculateNilakantha(void *pvParameters) {
	uint32_t i = 1;
	pi4 = 3.0;
	
	for(;;) {
		// Check for reset
		
		pi4 = pi4 + (1 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
		i++;
		pi4 = pi4 - (1 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
		i++;
	}
}