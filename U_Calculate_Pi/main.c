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

#define EG_RUN_LEIB  (1 << 0)
#define EG_RUN_NILA  (1 << 1)
#define EG_SKIP_CALC (1 << 2)
#define EG_TT		 (1 << 3)
#define EG_RST_TIMER (1 << 4)

#define PI_5DECIMALS 3.14159

typedef enum {
	State_Started,
	State_Stopped
} State_e;

typedef enum {
	LEIBNIZ,
	NILAKANTHA,
} Algorithm_e;

EventGroupHandle_t xEventGroup;
Algorithm_e algorithm = LEIBNIZ;
TaskHandle_t leibnizHandle;
TaskHandle_t nilakanthaHandle;
State_e state = State_Stopped;

float pi;
int seconds;
int milliseconds;

extern void vApplicationIdleHook(void);
void vCalculateLeibniz(void *pvParameters);
void vCalculateNilakantha(void *pvParameters);
void vInterface(void *pvParameters);
void vButtonHandler(void *pvParameters);
void vTimeHandler(void *pvParameters);

ISR(TCC1_OVF_vect) {
	xEventGroupSetBitsFromISR(xEventGroup, EG_TT, pdFALSE);
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
	
	xEventGroup = xEventGroupCreate();
	
	xTaskCreate(vInterface, (const char *) "interface", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vButtonHandler, (const char *) "buttonHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vTimeHandler, (const char *) "timeHandler", configMINIMAL_STACK_SIZE + 50, NULL, 2, NULL);
	xTaskCreate(vCalculateLeibniz, (const char *) "calculateLeibniz", configMINIMAL_STACK_SIZE+10, NULL, 1, &leibnizHandle);
	xTaskCreate(vCalculateNilakantha, (const char *) "calculateNilakantha", configMINIMAL_STACK_SIZE+10, NULL, 1, &nilakanthaHandle);
	
	vTaskStartScheduler();
	
	return 0;
}

void vTimeHandler(void *pvParameters) {
	int i = 0;
	seconds = 0;
	milliseconds = 0;
	
	for (;;) {
		xEventGroupWaitBits(xEventGroup, EG_TT, pdTRUE, pdTRUE, portMAX_DELAY);
		
		// Check if time reset is requested
		if (xEventGroupClearBits(xEventGroup, EG_RST_TIMER) & EG_RST_TIMER) {
			i = 0;
			seconds = 0;
			milliseconds = 0;
		}
		
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

void vInterface(void *pvParameters) {
	char cPi[15];
	char cTime[14];
	
	for(;;) {
		vDisplayClear();
		vDisplayWriteStringAtPos(0, 0, "Calculate PI");
		
		switch (state) {
			case State_Started:
				// Temporarily skip calculation while fetching PI value and time
				xEventGroupSetBits(xEventGroup, EG_SKIP_CALC);
				
				if (algorithm == LEIBNIZ) {
					sprintf(cPi, "PI: %0.8f", pi * 4);
				} else {
					sprintf(cPi, "PI: %0.8f", pi);
				}
				
				// Re-Enable calculation after fetching is complete
				xEventGroupClearBits(xEventGroup, EG_SKIP_CALC);
				
				sprintf(cTime, "Time: %i.%is", seconds, milliseconds);
				
				vDisplayWriteStringAtPos(1, 0, cPi);
				vDisplayWriteStringAtPos(2, 0, cTime);
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
				xEventGroupSetBits(xEventGroup, EG_RUN_LEIB);
			} else {
				xEventGroupSetBits(xEventGroup, EG_RUN_NILA);
			}
			
			state = State_Started;
			
			// Start HW-Timer
			TCC1.CTRLA = TC_CLKSEL_DIV64_gc;
		}
		
		// Stop algorithm (means deleting the currently running calculation task)
		if(getButtonState(BUTTON2, true) == buttonState_Short && state == State_Started) {
			if (algorithm == LEIBNIZ) {
				xEventGroupClearBits(xEventGroup, EG_RUN_LEIB);
			} else {
				xEventGroupClearBits(xEventGroup, EG_RUN_NILA);
			}
			
			state = State_Stopped;
			
			// Stop HW-Timer
			TCC1.CTRLA = 0x00;
			xEventGroupSetBits(xEventGroup, EG_RST_TIMER);
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
		xEventGroupWaitBits(xEventGroup, EG_RUN_LEIB, pdFALSE, pdTRUE, portMAX_DELAY);
		pi = 1.0;
		i = 0;
	
		xEventGroupValue = xEventGroupGetBits(xEventGroup);
		
		// Exit the inner loop if EG_RUN_LEIB gets cleared
		while(xEventGroupValue & EG_RUN_LEIB) {
			// If the display needs to fetch the current PI value, we stop
			// the calculation temporarily to avoid incomplete float values
			if ((xEventGroupValue & EG_SKIP_CALC) == 0) {
				pi = pi - (1.0 / (3 + (4 * i))) + (1.0 / (5 + (4 * i)));
				i++;
				
				// If algorithm calculated PI up to 5 decimal places,
				// stop the timer
				if ((pi * 4) - PI_5DECIMALS < 0.00001) {
					TCC1.CTRLA = 0x00;
					xEventGroupSetBits(xEventGroup, EG_RST_TIMER);
				}
			}
			
			xEventGroupValue = xEventGroupGetBits(xEventGroup);
		}
	}
}

void vCalculateNilakantha(void *pvParameters) {
	uint32_t i;
	EventBits_t xEventGroupValue;
	
	for(;;) {
		xEventGroupWaitBits(xEventGroup, EG_RUN_NILA, pdFALSE, pdTRUE, portMAX_DELAY);
		pi = 3.0;
		i = 1;
		
		xEventGroupValue = xEventGroupGetBits(xEventGroup);
		
		// Exit the inner loop if EG_RUN_NILA gets cleared
		while(xEventGroupValue & EG_RUN_NILA) {
			// If the display needs to fetch the current PI value, we stop
			// the calculation temporarily to avoid incomplete float values
			if ((xEventGroupValue & EG_SKIP_CALC) == 0) {
				pi = pi + (4.0 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
				i++;
				pi = pi - (4.0 / ((2 * i) * (2 * i + 1) * (2 * i + 2)));
				i++;
				
				// If algorithm calculated PI up to 5 decimal places,
				// stop the timer
				if (pi - PI_5DECIMALS < 0.00001) {
					TCC1.CTRLA = 0x00;
					xEventGroupSetBits(xEventGroup, EG_RST_TIMER);
				}
			}
		
			xEventGroupValue = xEventGroupGetBits(xEventGroup);
		}
	}
}