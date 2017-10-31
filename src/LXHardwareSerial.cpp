/**************************************************************************/
/*!
    @file     LXHardwareSerial.cpp
    @author   Claude Heintz
    @license  BSD (see LXESP32DMX.h)
    @copyright 2017 by Claude Heintz

    Exposes functionality in HardwareSerial class for LXESP32DMX driver

    @section  HISTORY

    v1.0 - First release
*/
/**************************************************************************/

#include "LXHardwareSerial.h"
#include "freertos/portmacro.h"
#include "esp_task_wdt.h"

// uart_struct_t is also defined in esp32-hal-uart.c
// defined here because to access fields in uart_t* in the following mods
struct uart_struct_t {
    uart_dev_t * dev;
#if !CONFIG_DISABLE_HAL_LOCKS
    xSemaphoreHandle lock;
#endif
    uint8_t num;
    xQueueHandle queue;
};

#if CONFIG_DISABLE_HAL_LOCKS
#define UART_MUTEX_LOCK()
#define UART_MUTEX_UNLOCK()
#else
#define UART_MUTEX_LOCK()    do {} while (xSemaphoreTake(uart->lock, portMAX_DELAY) != pdPASS)
#define UART_MUTEX_UNLOCK()  xSemaphoreGive(uart->lock)
#endif


uint8_t testCtr = 0;

/***************************** privateDelayMicroseconds allows sendBreak to be called on non-main task/thread 
*
* reason for private function is
* portENTER_CRITICAL_ISR has a spin lock
* delayMicroseconds() can only be called on main loop 
* because micros() is called on main loop task
*
*/

portMUX_TYPE privateMicrosMux = portMUX_INITIALIZER_UNLOCKED;

unsigned long IRAM_ATTR privateMicros()
{
    static unsigned long lccount = 0;     //because this depends of these static variables does each task need a private micros()/delayMicroseconds()?
    static unsigned long overflow = 0;
    unsigned long ccount;
    portENTER_CRITICAL_ISR(&privateMicrosMux);
    __asm__ __volatile__ ( "rsr     %0, ccount" : "=a" (ccount) );
    if(ccount < lccount){
        overflow += UINT32_MAX / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
    }
    lccount = ccount;
    portEXIT_CRITICAL_ISR(&privateMicrosMux);
    return overflow + (ccount / CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ);
}

void IRAM_ATTR hardwareSerialDelayMicroseconds(uint32_t us) {
    uint32_t m = privateMicros();
    if(us){
        uint32_t e = (m + us);
        if(m > e){ //overflow
            while(privateMicros() > e){
                NOP();
            }
        }
        while(privateMicros() < e){
            NOP();
        }
    }
}

// *****************************

// wait for FIFO to be empty
void IRAM_ATTR uartWaitFIFOEmpty(uart_t* uart) {
	if ( uart == NULL ) {
		return;
	}
	
    while(uart->dev->status.txfifo_cnt != 0x00) {
    	esp_task_wdt_feed();
    	taskYIELD();
    }
}

void IRAM_ATTR uartWaitRxFIFOEmpty(uart_t* uart) {
	if ( uart == NULL ) {
		return;
	}
	
    while(uart->dev->status.rxfifo_cnt != 0x00) {
        esp_task_wdt_feed();
    	taskYIELD();
    }
}

void IRAM_ATTR uartWaitTXDone(uart_t* uart) {
	if ( uart == NULL ) {
		return;
	}
    
    while (uart->dev->int_raw.tx_done == 0) {
        esp_task_wdt_feed();
		taskYIELD();
	}
	uart->dev->int_clr.tx_done = 1;
}

void IRAM_ATTR uartWaitTXBrkDone(uart_t* uart) {
	if ( uart == NULL ) {
		return;
	}
    
    while (uart->dev->int_raw.tx_brk_idle_done == 0) {
        esp_task_wdt_feed();
		taskYIELD();
	}
	
	uart->dev->int_clr.tx_brk_idle_done = 1;
}

void uartConfigureRS485(uart_t* uart, uint8_t en) {
	UART_MUTEX_LOCK();
	uart->dev->rs485_conf.en = en;
	UART_MUTEX_UNLOCK();
}

void uartConfigureSendBreak(uart_t* uart, uint8_t en, uint8_t len, uint16_t idle) {
	UART_MUTEX_LOCK();
	uart->dev->conf0.txd_brk = en;
	uart->dev->idle_conf.tx_brk_num=len;
	uart->dev->idle_conf.tx_idle_num = idle;
	UART_MUTEX_UNLOCK();
}

// ****************************************************
//			uartConfigureTwoStopBits
//          known issue with two stop bits now handled in IDF and Arduino Core
//          see
//				https://github.com/espressif/esp-idf/blob/master/components/driver/uart.c
//				uart_set_stop_bits(), lines 118-127
//          see also
//				https://esp32.com/viewtopic.php?f=2&t=1431

void uartSetToTwoStopBits(uart_t* uart) {
	UART_MUTEX_LOCK();
	uart->dev->conf0.stop_bit_num = 1;
	uart->dev->rs485_conf.dl1_en = 1;
	UART_MUTEX_UNLOCK();
}

void uartEnableBreakDetect(uart_t* uart) {
	UART_MUTEX_LOCK();
	uart->dev->int_ena.brk_det = 1;
    uart->dev->conf1.rxfifo_full_thrhd = 1;
    uart->dev->auto_baud.val = 0;
    UART_MUTEX_UNLOCK();
}

void uartDisableBreakDetect(uart_t* uart) {
	UART_MUTEX_LOCK();
	uart->dev->int_ena.brk_det = 0;
    UART_MUTEX_UNLOCK();
}

void uartDisableInterrupts(uart_t* uart) {
    UART_MUTEX_LOCK();
    //uart->dev->conf1.val = 0;
    uart->dev->int_ena.val = 0;
    uart->dev->int_clr.val = 0xffffffff;
    UART_MUTEX_UNLOCK();
}

void uartSetInterrupts(uart_t* uart, uint32_t value) {
	UART_MUTEX_LOCK();
	uart->dev->int_ena.val = value;
	UART_MUTEX_UNLOCK();
}

void uartClearInterrupts(uart_t* uart) {
    UART_MUTEX_LOCK();
    uart->dev->int_clr.val = 0xffffffff;
    UART_MUTEX_UNLOCK();
}

void uartLockMUTEX(uart_t* uart) {
    UART_MUTEX_LOCK();
}

void uartUnlockMUTEX(uart_t* uart) {
    UART_MUTEX_UNLOCK();
}

LXHardwareSerial::LXHardwareSerial(int uart_nr):HardwareSerial(uart_nr) {}

void LXHardwareSerial::end() {
	uartDisableInterrupts(_uart);
	
    if(uartGetDebug() == _uart_nr) {
        uartSetDebug(0);
    }
    uartEnd(_uart);
    _uart = 0;
}

void LXHardwareSerial::waitFIFOEmpty() {
	uartWaitFIFOEmpty(_uart);
}

void LXHardwareSerial::waitRxFIFOEmpty() {
	uartWaitRxFIFOEmpty(_uart);
}

void LXHardwareSerial::waitTXDone() {
	uartWaitTXDone(_uart);
}

void LXHardwareSerial::waitTXBrkDone() {
	uartWaitTXBrkDone(_uart);
}

void LXHardwareSerial::sendBreak(uint32_t length) {
	uint8_t txPin;
	if( _uart_nr == 1 ) {
        txPin = 10;
    } else if( _uart_nr == 2 ) {
        txPin = 17;
    } else {
    	txPin = 1;
    }
    
    uint32_t save_interrupts = _uart->dev->int_ena.val;
    uartDisableInterrupts(_uart);
    
    // detach 
    pinMatrixOutDetach(txPin, false, false);
  
  	pinMode(txPin, OUTPUT);
  	
  	digitalWrite(txPin, LOW);
  	hardwareSerialDelayMicroseconds(length);		//<<---lockup problem is here, replace delayMicroseconds with alternate
  	digitalWrite(txPin, HIGH);
  	
  	//reattach
  	pinMatrixOutAttach(txPin, U2TXD_OUT_IDX, false, false);
  	
  	uartSetInterrupts(_uart, save_interrupts);
}

void LXHardwareSerial::setBaudRate(uint32_t rate) {
	uartSetBaudRate(_uart, rate);
}

void LXHardwareSerial::configureRS485(uint8_t en) {
	uartConfigureRS485(_uart, en);
}

void LXHardwareSerial::configureSendBreak(uint8_t en, uint8_t len, uint16_t idle) {
	uartConfigureSendBreak(_uart, en, len, idle);
}

void LXHardwareSerial::setToTwoStopBits() {
	uartSetToTwoStopBits(_uart);
}

void LXHardwareSerial::enableBreakDetect() {
	uartEnableBreakDetect(_uart);
}

void LXHardwareSerial::disableBreakDetect() {
	uartDisableBreakDetect(_uart);
}

void LXHardwareSerial::clearInterrupts() {
	uartClearInterrupts(_uart);
}
