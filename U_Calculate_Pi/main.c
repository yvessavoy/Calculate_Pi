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

#define EG_RUN_LEIB 0x04
#define EG_RUN_NILA 0x08

#define PI_5DECIMALS 3.14159

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
TaskHandle_t leibnizHandle;
TaskHandle_t nilakanthaHandle;
State_e state = State_Stopped;

float pi;

extern void vApplicationIdleHook(void);
void vCalculateLeibniz(void *pvParameters);
void vCalculateNilakantha(void *pvParameters);
void vInterface(void *pvParameters);
void vButtonHandler(void *pvParameters);

void vApplicationIdleHook(void) {}

int main(void) {
	vInitClock();
	vInitDisplay();
	
	eventGroup = xEventGroupCreate();
	
	xTaskCreate(vInterface, (const char *) "interface", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vButtonHandler, (const char *) "buttonHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vCalculateLeibniz, (const char *) "calculateLeibniz", configMINIMAL_STACK_SIZE+10, NULL, 1, &leibnizHandle);
	xTaskCreate(vCalculateNilakantha, (const char *) "calculateNilakantha", configMINIMAL_STACK_SIZE+10, NULL, 1, &nilakanthaHandle);
	
	vTaskStartScheduler();
	
	return 0;
}

void vInterface(void *pvParameters) {
	char cPi[8];
	
	for(;;) {
		vDisplayClear();
		vDisplayWriteStringAtPos(0, 0, "Calculate PI");
		
		switch (state) {
			case State_Started:
				sprintf(cPi, "PI: %f", pi);
				vDisplayWriteStringAtPos(1, 0, cPi);
				vDisplayWriteStringAtPos(3, 0, "START STOP RST CHNG");
				break;
				
			case State_Stopped:
				switch (algorithm) {
					case LEIBNIZ:
						vDisplayWriteStringAtPos(1, 0, "Current: Leibniz");
						break;
						
					case NILAKANTHA:
						vDisplayWriteStringAtPos(1, 0, "Current: Nilakantha");
						break;
					
					default:
						vDisplayWriteStringAtPos(1, 0, "Current: None");
						break;
				}
				
				vDisplayWriteStringAtPos(3, 0, "START STOP RST CHNG");
				break;
				
			default:
				break;
		}
		
		vTaskDelay(500/portTICK_RATE_MS);
	}
}

void vButtonHandler(void *pvParameters) {
	initButtonHandler();
	setupButton(BUTTON1, &PORTF, 4, 1);
	setupButton(BUTTON2, &PORTF, 5, 1);
	setupButton(BUTTON3, &PORTF, 6, 1);
	setupButton(BUTTON4, &PORTF, 7, 1);
	vTaskDelay(3000);
	
	for(;;) {
		// Start algorithm (means resuming the correct calculation task)
		if(getButtonState(BUTTON1, true) == buttonState_Short && state == State_Stopped) {
			if (algorithm == LEIBNIZ) {
				xEventGroupSetBits(eventGroup, EG_RUN_LEIB);
			} else {
				xEventGroupSetBits(eventGroup, EG_RUN_NILA);
			}
			
			state = State_Started;
		}
		
		// Stop algorithm (means deleting the currently running calculation task)
		if(getButtonState(BUTTON2, true) == buttonState_Short && state == State_Started) {
			if (algorithm == LEIBNIZ) {
				xEventGroupClearBits(eventGroup, EG_RUN_LEIB);
			} else {
				xEventGroupClearBits(eventGroup, EG_RUN_NILA);
			}
			state = State_Stopped;
		}
		
		// Reset algorithm
		if(getButtonState(BUTTON3, true) == buttonState_Short) {
			
		}
		
		// Change algorithm
		if(getButtonState(BUTTON4, true) == buttonState_Short && state == State_Stopped) {
			if (algorithm == LEIBNIZ) {
				algorithm = NILAKANTHA;
			} else {
				algorithm = LEIBNIZ;
			}
		}
		
		vTaskDelay(10/portTICK_RATE_MS);
	}
}

void vCalculateLeibniz(void *pvParameters) {
	uint32_t i;
	EventBits_t xEventGroupValue;
	
	for (;;) {
		xEventGroupWaitBits(eventGroup, EG_RUN_LEIB, pdFALSE, pdTRUE, portMAX_DELAY);
		pi = 4.0;
		i = 0;
	
		// Exit the inner loop if EG_RUN_LEIB gets cleared
		while(xEventGroupGetBits(eventGroup) & EG_RUN_LEIB) {
			pi = pi - (4.0 / (3 + (4 * i))) + (4.0 / (5 + (4 * i)));
			i++;
		}
	}
}

void vCalculateNilakantha(void *pvParameters) {
	uint32_t i;
	EventBits_t xEventGroupValue;
	
	for(;;) {
		xEventGroupWaitBits(eventGroup, EG_RUN_NILA, pdFALSE, pdTRUE, portMAX_DELAY);
		pi = 3.0;
		i = 1;
		
		// Exit the inner loop if EG_RUN_NILA gets cleared
		while(xEventGroupGetBits(eventGroup) & EG_RUN_NILA) {
			pi = pi + (4.0 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
			i++;
			pi = pi - (4.0 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
			i++;
		}
	}
}