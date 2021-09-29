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
void vButtonHandler(void *pvParameters);

void vApplicationIdleHook(void) {}

int main(void) {
	vInitClock();
	vInitDisplay();
	
	eventGroup = xEventGroupCreate();
	
	xTaskCreate(vButtonHandler, (const char *) "buttonHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	
	vTaskStartScheduler();
	
	return 0;
}

void vButtonHandler(void *pvParameters) {
	initButtonHandler();
	setupButton(BUTTON1, &PORTF, 4, 1);
	setupButton(BUTTON2, &PORTF, 5, 1);
	setupButton(BUTTON3, &PORTF, 6, 1);
	setupButton(BUTTON4, &PORTF, 7, 1);
	
	// Start algorithm (means starting the correct calculation task)
	if(getButtonState(BUTTON1, false) == buttonState_Short && state == State_Stopped) {
		// We use one handle for both tasks as we only have one calculation running at any time
		// which means that calculationHandle always points to the currently running calculation
		if (algorithm == LEIBNIZ) {
			xTaskCreate(vCalculateLeibniz, (const char *) "calculateLeibniz", configMINIMAL_STACK_SIZE+50, NULL, 1, &calculationHandle);
		} else {
			xTaskCreate(vCalculateNilakantha, (const char *) "calculateNilakantha", configMINIMAL_STACK_SIZE+50, NULL, 1, &calculationHandle);
		}
		
		state = State_Started;
	}
	
	// Stop algorithm (means deleting the currently running calculation task)
	if(getButtonState(BUTTON2, false) == buttonState_Short && state == State_Started) {
		vTaskDelete(calculationHandle);
		calculationHandle = NULL;
		state = State_Stopped;
	}
	
	// Reset algorithm (Nothing to do here as we initialize the algorithm as soon as it starts)
	if(getButtonState(BUTTON3, false) == buttonState_Short && state == State_Stopped) {
		
	}
	
	// Change algorithm
	if(getButtonState(BUTTON4, false) == buttonState_Short && state == State_Stopped) {
		if (algorithm == LEIBNIZ) {
			algorithm = NILAKANTHA;
		} else {
			algorithm = LEIBNIZ;
		}
	}
	
	vTaskDelay(10/portTICK_RATE_MS);
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