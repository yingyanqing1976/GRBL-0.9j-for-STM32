/*
  serial.c - Low level functions for sending and recieving bytes via the serial port
  Part of Grbl

  Copyright (c) 2011-2015 Sungeun K. Jeon
  Copyright (c) 2009-2011 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "grbl.h"


uint8_t serial_rx_buffer[RX_BUFFER_SIZE];
uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_BUFFER_SIZE];
uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;


#ifdef ENABLE_XONXOFF
  volatile uint8_t flow_ctrl = XON_SENT; // Flow control state variable
#endif
  

// Returns the number of bytes used in the RX serial buffer.
uint8_t serial_get_rx_buffer_count(void)
{
  uint8_t rtail = serial_rx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_rx_buffer_head >= rtail) { return(serial_rx_buffer_head-rtail); }
  return (RX_BUFFER_SIZE - (rtail-serial_rx_buffer_head));
}


// Returns the number of bytes used in the TX serial buffer.
// NOTE: Not used except for debugging and ensuring no TX bottlenecks.
uint8_t serial_get_tx_buffer_count(void)
{
  uint8_t ttail = serial_tx_buffer_tail; // Copy to limit multiple calls to volatile
  if (serial_tx_buffer_head >= ttail) { return(serial_tx_buffer_head-ttail); }
  return (TX_BUFFER_SIZE - (ttail-serial_tx_buffer_head));
}


void serial_init(void)
{
#if defined(CPU_MAP_ATMEGA328P) || defined(CPU_MAP_ATMEGA2560)
 // Set baud rate 设置波特率
  #if BAUD_RATE < 57600
    uint16_t UBRR0_value = ((F_CPU / (8L * BAUD_RATE)) - 1)/2 ;
    UCSR0A &= ~(1 << U2X0); // baud doubler off  - Only needed on Uno XXX
  #else
    uint16_t UBRR0_value = ((F_CPU / (4L * BAUD_RATE)) - 1)/2;
    UCSR0A |= (1 << U2X0);  // baud doubler on for high baud rates, i.e. 115200
  #endif
  UBRR0H = UBRR0_value >> 8;
  UBRR0L = UBRR0_value;
            
  // enable rx and tx
  UCSR0B |= 1<<RXEN0;
  UCSR0B |= 1<<TXEN0;
	
  // enable interrupt on complete reception of a byte	 在完成一个字节的接收时使能中断
  UCSR0B |= 1<<RXCIE0;
	  
  // defaults to 8-bit, no parity, 1 stop bit	默认使用8位数据位，1位停止位，无校验的形式
#endif		//end of CPU_MAP_ATMEGA328P & CPU_MAP_ATMEGA2560

#if defined(CPU_MAP_STM32F10X)
   HW_USART_Init(BAUD_RATE);	//初始化串口，波特率可以在config.h文件中设置，默认为115200
#endif		//end of CPU_MAP_STM32F10X
 
}


// Writes one byte to the TX serial buffer. Called by main program.
// TODO: Check if we can speed this up for writing strings, rather than single bytes.
void serial_write(uint8_t data) {
  // Calculate next head 
  uint8_t next_head = serial_tx_buffer_head + 1;
  if (next_head == TX_BUFFER_SIZE) { next_head = 0; }

  // Wait until there is space in the buffer   
  while (next_head == serial_tx_buffer_tail) { 
    // TODO: Restructure st_prep_buffer() calls to be executed here during a long print.    
    if (sys_rt_exec_state & EXEC_RESET) { return; } // Only check for abort to avoid an endless loop.	
  }

  // Store data and advance head	
  serial_tx_buffer[serial_tx_buffer_head] = data;
  serial_tx_buffer_head = next_head;

#if defined(CPU_MAP_ATMEGA328P) || defined(CPU_MAP_ATMEGA2560)
  // Enable Data Register Empty Interrupt to make sure tx-streaming is running
  UCSR0B |=  (1 << UDRIE0); 
#endif		//end of CPU_MAP_ATMEGA328P & CPU_MAP_ATMEGA2560

#if defined(CPU_MAP_STM32F10X)
	USART_ITConfig(USART1,USART_IT_TXE, ENABLE);
#endif		//end of CPU_MAP_STM32F10X
}



// Fetches the first byte in the serial read buffer. Called by main program.
uint8_t serial_read(void)
{
  uint8_t tail = serial_rx_buffer_tail; 
  // Temporary serial_rx_buffer_tail (to optimize for volatile)
  if (serial_rx_buffer_head == tail) {
    return SERIAL_NO_DATA;
  } else {
    uint8_t data = serial_rx_buffer[tail];
    
    tail++;
    if (tail == RX_BUFFER_SIZE) { tail = 0; }
    serial_rx_buffer_tail = tail;

    #ifdef ENABLE_XONXOFF
      if ((serial_get_rx_buffer_count() < RX_BUFFER_LOW) && flow_ctrl == XOFF_SENT) { 
        flow_ctrl = SEND_XON;
        UCSR0B |=  (1 << UDRIE0); // Force TX
      }
    #endif
    
    return data;
  }
}

#if defined(CPU_MAP_ATMEGA328P) || defined(CPU_MAP_ATMEGA2560)

// Data Register Empty Interrupt handler
ISR(SERIAL_UDRE)
{
  uint8_t tail = serial_tx_buffer_tail; // Temporary serial_tx_buffer_tail (to optimize for volatile)
  
  #ifdef ENABLE_XONXOFF
    if (flow_ctrl == SEND_XOFF) { 
      UDR0 = XOFF_CHAR; 
      flow_ctrl = XOFF_SENT; 
    } else if (flow_ctrl == SEND_XON) { 
      UDR0 = XON_CHAR; 
      flow_ctrl = XON_SENT; 
    } else
  #endif
  { 
    // Send a byte from the buffer	
    UDR0 = serial_tx_buffer[tail];
  
    // Update tail position
    tail++;
    if (tail == TX_BUFFER_SIZE) { tail = 0; }
  
    serial_tx_buffer_tail = tail;
  }
  
  // Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
  if (tail == serial_tx_buffer_head) { UCSR0B &= ~(1 << UDRIE0); }
}


ISR(SERIAL_RX)
{
  uint8_t data = UDR0;
  uint8_t next_head;
  
  // Pick off realtime command characters directly from the serial stream. These characters are
  // not passed into the buffer, but these set system state flag bits for realtime execution.
  switch (data) {
    case CMD_STATUS_REPORT: bit_true_atomic(sys_rt_exec_state, EXEC_STATUS_REPORT); break; // Set as true
    case CMD_CYCLE_START:   bit_true_atomic(sys_rt_exec_state, EXEC_CYCLE_START); break; // Set as true
    case CMD_FEED_HOLD:     bit_true_atomic(sys_rt_exec_state, EXEC_FEED_HOLD); break; // Set as true
    case CMD_SAFETY_DOOR:   bit_true_atomic(sys_rt_exec_state, EXEC_SAFETY_DOOR); break; // Set as true
    case CMD_RESET:         mc_reset(); break; // Call motion control reset routine.
    default: // Write character to buffer    
      next_head = serial_rx_buffer_head + 1;
      if (next_head == RX_BUFFER_SIZE) { next_head = 0; }
    
      // Write data to buffer unless it is full.
      if (next_head != serial_rx_buffer_tail) {
        serial_rx_buffer[serial_rx_buffer_head] = data;
        serial_rx_buffer_head = next_head;    
        
        #ifdef ENABLE_XONXOFF
          if ((serial_get_rx_buffer_count() >= RX_BUFFER_FULL) && flow_ctrl == XON_SENT) {
            flow_ctrl = SEND_XOFF;
            UCSR0B |=  (1 << UDRIE0); // Force TX
          } 
        #endif
        
      }
      //TODO: else alarm on overflow?
  }
}

#endif		//end of CPU_MAP_ATMEGA328P & CPU_MAP_ATMEGA2560

#if defined(CPU_MAP_STM32F10X)


void USART1_IRQHandler(void)		
{
	uint8_t data;
	uint8_t next_head;
	uint8_t tail; 
	if(USART_GetFlagStatus(USART1 , USART_IT_RXNE)!=RESET)		//接收寄存器非空中断
	{
		data=USART_ReceiveData(USART1);		//读取字符
		// Pick off realtime command characters directly from the serial stream. These characters are
		// not passed into the buffer, but these set system state flag bits for realtime execution.
		//直接从串行流中选取实时命令字符。这些字符不传递到缓冲区，但这些字符设置系统状态标志位用于实时执行。
		switch (data) {
			case CMD_STATUS_REPORT: bit_true_atomic(sys_rt_exec_state, EXEC_STATUS_REPORT); break; // Set as true  设为真
			case CMD_CYCLE_START:   bit_true_atomic(sys_rt_exec_state, EXEC_CYCLE_START); break; // Set as true	   设为真
			case CMD_FEED_HOLD:     bit_true_atomic(sys_rt_exec_state, EXEC_FEED_HOLD); break; // Set as true	   设为真
			case CMD_SAFETY_DOOR:   bit_true_atomic(sys_rt_exec_state, EXEC_SAFETY_DOOR); break; // Set as true	   设为真
			case CMD_RESET:         mc_reset(); break; // Call motion control reset routine. 调用运动控制复位程序。
			default: // Write character to buffer   写字符到缓冲区 
			next_head = serial_rx_buffer_head + 1;
  		    if (next_head == RX_BUFFER_SIZE) { next_head = 0; }
    		// Write data to buffer unless it is full.	将数据写入缓冲区，除非它已满。
      		if (next_head != serial_rx_buffer_tail) {
	        	serial_rx_buffer[serial_rx_buffer_head] = data;
	        	serial_rx_buffer_head = next_head;    
        
        #ifdef ENABLE_XONXOFF
          		if ((serial_get_rx_buffer_count() >= RX_BUFFER_FULL) && flow_ctrl == XON_SENT) {
	           	 	flow_ctrl = SEND_XOFF;
	            	UCSR0B |=  (1 << UDRIE0); // Force TX
          		} 
        #endif
        
	      	}
	      	//TODO: else alarm on overflow?
  		}
	}
	if (USART_GetITStatus(USART1, USART_IT_TXE)!=RESET) 		//发送寄存器空中断
	{
		tail = serial_tx_buffer_tail; 
		// Temporary serial_tx_buffer_tail (to optimize for volatile)
		//临时的serial_rx_buffer_tail变量（最好设置为volatile类型）

		#ifdef ENABLE_XONXOFF
		if (flow_ctrl == SEND_XOFF) { 
			UDR0 = XOFF_CHAR; 
			flow_ctrl = XOFF_SENT; 
		} else if (flow_ctrl == SEND_XON) { 
			UDR0 = XON_CHAR; 
			flow_ctrl = XON_SENT; 
		} else
		#endif
		{ 
			// Send a byte from the buffer	 发送一个缓冲区的字符
			USART_SendData(USART1, serial_tx_buffer[tail]);						//发送字符
			
			// Update tail position	 更新尾下标的位置
			tail++;
			if (tail == TX_BUFFER_SIZE) { tail = 0; }
			
			serial_tx_buffer_tail = tail;
		}
		
		// Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
		//如果传输完成，关闭发送中断以停止tx流
		if (tail == serial_tx_buffer_head) 
		{ 
			USART_ITConfig(USART1, USART_IT_TXE, DISABLE);		//除能发送中断
		}
	}
}

#endif		//end of CPU_MAP_STM32F10X


void serial_reset_read_buffer(void) 
{
  serial_rx_buffer_tail = serial_rx_buffer_head;

  #ifdef ENABLE_XONXOFF
    flow_ctrl = XON_SENT;
  #endif
}
