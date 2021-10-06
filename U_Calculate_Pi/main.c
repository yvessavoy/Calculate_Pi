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

#define PI_5DECIMALS 3.14159

typedef enum {
	State_Started,
	State_Stopped
} State_e;

typedef enum {
	LEIBNIZ,
	NILAKANTHA,
} Algorithm_e;

Algorithm_e algorithm = LEIBNIZ;
TaskHandle_t leibnizHandle;
TaskHandle_t nilakanthaHandle;
TaskHandle_t timeHandle;
State_e state = State_Stopped;

float pi;
unsigned seconds;
unsigned milliseconds;

extern void vApplicationIdleHook(void);
void vCalculateLeibniz(void *pvParameters);
void vCalculateNilakantha(void *pvParameters);
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
	TCC1.PER = 5000;
}

int main(void) {
	vInitClock();
	vInitDisplay();
	vInitTimer();
	
	xTaskCreate(vInterface, (const char *) "interface", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vButtonHandler, (const char *) "buttonHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vTimeHandler, (const char *) "timeHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, &timeHandle);
	xTaskCreate(vCalculateLeibniz, (const char *) "calculateLeibniz", configMINIMAL_STACK_SIZE+10, NULL, 1, &leibnizHandle);
	xTaskCreate(vCalculateNilakantha, (const char *) "calculateNilakantha", configMINIMAL_STACK_SIZE+10, NULL, 1, &nilakanthaHandle);
	
	vTaskStartScheduler();
	
	return 0;
}

void vTimeHandler(void *pvParameters) {
	int i = 0;
	BaseType_t xResult;
	uint32_t ulNotifyValue;
	seconds = 0;
	milliseconds = 0;
	
	for (;;) {
		xResult = xTaskNotifyWait(pdFALSE, N_TIME_TICK | N_TIME_RST, &ulNotifyValue, portMAX_DELAY);
		
		if (xResult == pdPASS) {
			if (ulNotifyValue & N_TIME_RST) {
				i = 0;
				seconds = 0;
				milliseconds = 0;
			}
			
			if (ulNotifyValue & N_TIME_TICK) {
				if (i < 100) {
					milliseconds += 10;
					i++;
				} else {
					seconds++;
					milliseconds = 0;
					i = 0;
				}
			}
		}
	}
}

void vInterface(void *pvParameters) {
	char cPi[15];
	char cTime[14];
	
	for(;;) {
		vDisplayClear();
		vDisplayWriteStringAtPos(0, 0, "Calculate PI");
		
		switch (state) {
			case State_Started:
				// Temporarily skip calculation while fetching PI value
				if (algorithm == LEIBNIZ) {
					xTaskNotify(leibnizHandle, N_CALC_STOP, eSetBits);
					sprintf(cPi, "PI: %0.8f", pi * 4);
					xTaskNotify(leibnizHandle, N_CALC_START, eSetBits);
				} else {
					xTaskNotify(nilakanthaHandle, N_CALC_STOP, eSetBits);
					sprintf(cPi, "PI: %0.8f", pi);
					xTaskNotify(nilakanthaHandle, N_CALC_START, eSetBits);
				}
				
				sprintf(cTime, "Time: %i.%is", seconds, milliseconds);
				
				vDisplayWriteStringAtPos(1, 0, cPi);
				vDisplayWriteStringAtPos(2, 0, cTime);
				vDisplayWriteStringAtPos(3, 0, "START STOP     CHNG");
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
				
				vDisplayWriteStringAtPos(3, 0, "START STOP     CHNG");
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
				xTaskNotify(leibnizHandle, N_CALC_START | N_CALC_RST, eSetBits);
			} else {
				xTaskNotify(nilakanthaHandle, N_CALC_START | N_CALC_RST, eSetBits);
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
				xTaskNotify(nilakanthaHandle, N_CALC_STOP, eSetBits);
			}
			
			state = State_Stopped;
			
			// Stop HW-Timer
			TCC1.CTRLA = 0x00;
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
					
					pi = pi - (1.0 / (3 + (4 * i))) + (1.0 / (5 + (4 * i)));
					i++;
					
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

void vCalculateNilakantha(void *pvParameters) {
	BaseType_t xResult;
	uint32_t ulNotifyValue;
	uint32_t i = 2;
	int s = 1;
	
	for(;;) {
		xResult = xTaskNotifyWait(pdFALSE, ULONG_MAX, &ulNotifyValue, portMAX_DELAY);
		
		if (xResult == pdPASS) {
			if (ulNotifyValue & N_CALC_RST) {
				pi = 3.0;
				i = 2;
				s = 1;
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
					
					pi = pi + s * (4.0 / (i * (i + 1) * (i + 2)));
					s = -s;
					i += 2;
					
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