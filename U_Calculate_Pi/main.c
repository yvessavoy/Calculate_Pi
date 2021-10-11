/*
 * U_Calculate_Pi.c
 *
 * Created: 28.09.2021 19:25:00
 * Author : Yves
 */ 

#include <stdio.h>
#include <limits.h>
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

#define N_TIME_TICK  (1 << 0)
#define N_TIME_RST   (1 << 1)
#define N_CALC_START (1 << 0)
#define N_CALC_STOP  (1 << 1)
#define N_CALC_RST   (1 << 2)
#define EG_CALC_RELEASED (1 << 0)

#define PI_5DECIMALS        3.14159
#define INTERRUPT_PERIOD_MS 10

typedef enum {
	State_Started,
	State_Stopped
} State_e;

typedef enum {
	LEIBNIZ,
	WALLIS,
} Algorithm_e;

Algorithm_e algorithm = LEIBNIZ;
TaskHandle_t leibnizHandle;
TaskHandle_t wallisHandle;
TaskHandle_t timeHandle;
State_e state = State_Stopped;
EventGroupHandle_t xEventGroup;

float pi;
unsigned milliseconds;

extern void vApplicationIdleHook(void);
void vCalculateLeibniz(void *pvParameters);
void vCalculateWallis(void *pvParameters);
void vInterface(void *pvParameters);
void vButtonHandler(void *pvParameters);
void vTimeHandler(void *pvParameters);

ISR(TCC1_OVF_vect) {
	xTaskNotifyFromISR(timeHandle, N_TIME_TICK, eSetBits, pdFALSE);
}

void vApplicationIdleHook(void) {}
	
void vInitTimer() {
	// Setup timer that interrupts every 10ms
	// Disable it as it will be started in vButtonHandler()
	TCC1.CTRLA = 0x00;
	TCC1.CTRLB = 0x00;
	TCC1.INTCTRLA = 0x03;
	TCC1.PER = 500 * INTERRUPT_PERIOD_MS;
}

int main(void) {
	vInitClock();
	vInitDisplay();
	vInitTimer();
	
	xEventGroup = xEventGroupCreate();
	
	xTaskCreate(vInterface, (const char *) "interface", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vButtonHandler, (const char *) "buttonHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vTimeHandler, (const char *) "timeHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, &timeHandle);
	xTaskCreate(vCalculateLeibniz, (const char *) "calculateLeibniz", configMINIMAL_STACK_SIZE+10, NULL, 1, &leibnizHandle);
	xTaskCreate(vCalculateWallis, (const char *) "calculateWallis", configMINIMAL_STACK_SIZE+10, NULL, 1, &wallisHandle);
	
	vTaskStartScheduler();
	
	return 0;
}

void vTimeHandler(void *pvParameters) {
	BaseType_t xResult;
	uint32_t ulNotifyValue;
	milliseconds = 0;
	
	for (;;) {
		xResult = xTaskNotifyWait(pdFALSE, N_TIME_TICK | N_TIME_RST, &ulNotifyValue, portMAX_DELAY);
		
		if (xResult == pdPASS) {
			if (ulNotifyValue & N_TIME_RST) {
				milliseconds = 0;
			}
			
			if (ulNotifyValue & N_TIME_TICK) {
				milliseconds += INTERRUPT_PERIOD_MS;
			}
		}
	}
}

void vInterface(void *pvParameters) {
	char cPi[15];
	char cTime[14];
	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 500 / portTICK_RATE_MS;
	xLastWakeTime = xTaskGetTickCount();
	
	for(;;) {
		vDisplayClear();
		vDisplayWriteStringAtPos(0, 0, "Calculate PI");
		
		switch (state) {
			case State_Started:
				// Wait for PI to be released from the calculation task
				xEventGroupWaitBits(xEventGroup, EG_CALC_RELEASED, pdTRUE, pdTRUE, portMAX_DELAY);
				
				if (algorithm == LEIBNIZ) {
					sprintf(cPi, "PI: %0.8f", pi * 4);
				} else {
					sprintf(cPi, "PI: %0.8f", pi);
				}
				
				sprintf(cTime, "Time: %ims", milliseconds);
				
				vDisplayWriteStringAtPos(1, 0, cPi);
				vDisplayWriteStringAtPos(2, 0, cTime);
				vDisplayWriteStringAtPos(3, 0, "START STOP     CHNG");
				break;
				
			case State_Stopped:
				switch (algorithm) {
					case LEIBNIZ:
						vDisplayWriteStringAtPos(1, 0, "Current: Leibniz");
						break;
						
					case WALLIS:
						vDisplayWriteStringAtPos(1, 0, "Current: Wallis");
						break;
					
					default:
						vDisplayWriteStringAtPos(1, 0, "Current: None");
						break;
				}
				
				vDisplayWriteStringAtPos(3, 0, "START STOP     CHNG");
				break;
				
			default:
				break;
		}
		
		vTaskDelayUntil(&xLastWakeTime, xFrequency);
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
				xTaskNotify(leibnizHandle, N_CALC_START | N_CALC_RST, eSetBits);
			} else {
				xTaskNotify(wallisHandle, N_CALC_START | N_CALC_RST, eSetBits);
			}
			
			state = State_Started;
			
			// Reset and start HW-Timer
			xTaskNotify(timeHandle, N_TIME_RST, eSetBits);
			TCC1.CTRLA = TC_CLKSEL_DIV64_gc;
		}
		
		// Stop algorithm (means deleting the currently running calculation task)
		if(getButtonState(BUTTON2, true) == buttonState_Short && state == State_Started) {
			if (algorithm == LEIBNIZ) {
				xTaskNotify(leibnizHandle, N_CALC_STOP, eSetBits);
			} else {
				xTaskNotify(wallisHandle, N_CALC_STOP, eSetBits);
			}
			
			state = State_Stopped;
			
			// Stop HW-Timer
			TCC1.CTRLA = 0x00;
		}
		
		// Change algorithm
		if(getButtonState(BUTTON4, true) == buttonState_Short && state == State_Stopped) {
			if (algorithm == LEIBNIZ) {
				algorithm = WALLIS;
			} else {
				algorithm = LEIBNIZ;
			}
		}
		
		vTaskDelay(10/portTICK_RATE_MS);
	}
}

void vCalculateLeibniz(void *pvParameters) {
	uint32_t i = 0;
	BaseType_t xResult;
	uint32_t ulNotifyValue;
	
	for (;;) {
		xResult = xTaskNotifyWait(pdFALSE, ULONG_MAX, &ulNotifyValue, portMAX_DELAY);
	
		if (xResult == pdPASS) {
			if (ulNotifyValue & N_CALC_RST) {
				pi = 1.0;
				i = 0;
			}
			
			if (ulNotifyValue & N_CALC_START) {
				for (;;) {
					// Check if the calculation got interrupted (probably by the display task)
					// and send an "empty" notification. This will cause this task to run once
					// more without actually doing anything, but this is alright for the case
					xTaskNotifyAndQuery(xTaskGetCurrentTaskHandle(), 0, eNoAction, &ulNotifyValue);
					if (ulNotifyValue & N_CALC_STOP) {
						break;
					}
					
					// Lock pi (like taking a mutex)
					xEventGroupClearBits(xEventGroup, EG_CALC_RELEASED);
					
					pi = pi - (1.0 / (3 + (4 * i))) + (1.0 / (5 + (4 * i)));
					i++;
					
					// Release pi (like releasing a mutex)
					xEventGroupSetBits(xEventGroup, EG_CALC_RELEASED);
					
					// If algorithm calculated PI up to 5 decimal places,
					// stop the timer
					if ((pi * 4) - PI_5DECIMALS < 0.00001) {
						TCC1.CTRLA = 0x00;
					}
				}
			}
		}
	}
}

void vCalculateWallis(void *pvParameters) {
	BaseType_t xResult;
	uint32_t ulNotifyValue;
	float i = 3;
	
	for(;;) {
		xResult = xTaskNotifyWait(pdFALSE, ULONG_MAX, &ulNotifyValue, portMAX_DELAY);
		
		if (xResult == pdPASS) {
			if (ulNotifyValue & N_CALC_RST) {
				pi = 4.0;
				i = 3.0;
			}
			
			if (ulNotifyValue & N_CALC_START) {
				for (;;) {
					// Check if the calculation got interrupted (probably by the display task)
					// and send an "empty" notification. This will cause this task to run once
					// more without actually doing anything, but this is alright for the case
					xTaskNotifyAndQuery(xTaskGetCurrentTaskHandle(), 0, eNoAction, &ulNotifyValue);
					if (ulNotifyValue & N_CALC_STOP) {
						break;
					}
				
					// Lock pi (like taking a mutex)
					xEventGroupClearBits(xEventGroup, EG_CALC_RELEASED);
					
					pi = pi * ((i - 1) / i) * ((i + 1) / i);
					i += 2;
					
					// Release pi (like releasing a mutex)
					xEventGroupSetBits(xEventGroup, EG_CALC_RELEASED);
					
					// If algorithm calculated PI up to 5 decimal places,
					// stop the timer
					float rest = pi - PI_5DECIMALS;
					if (rest < 0.00001 && rest > 0.0) {
						TCC1.CTRLA = 0x00;
					}
				}
			}
		}
	}
}