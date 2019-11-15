/*
 * lin1d3_driver.c
 *
 *  Created on: Sep 14, 2018
 *      Author: Nico
 */
#include "lin1d3_driver.h"
#include <string.h>
#include <fsl_debug_console.h>

#define master_stack_size_d	(256)  /* 256 */
#define master_task_priority (configMAX_PRIORITIES - 1)
#define master_queue_size_d	(8)

#define slave_stack_size_d	(256)
#define slave_task_priority (configMAX_PRIORITIES - 1)

#define size_of_uart_buffer	(10)

#define size_of_lin_header_d (2)

/*Static function prototypes */
static void master_task(void *pvParameters);
static void slave_task(void *pvParameters);



/******************************************************************************
 * Public functions
 *
 *****************************************************************************/

/*
 * Init a LIN node
 * */
lin1d3_handle_t* lin1d3_InitNode(lin1d3_nodeConfig_t config)
{
	lin1d3_handle_t* handle = NULL;
	static uint8_t node_idx = 0;
	char master_task_name[] = "linMaster0";
	char slave_task_name[] = "linSlave0";
	/* Check init parameters */
	if(config.type >= lin1d3_max_nodeType) {
		return NULL;
	}

	/* Create the handle structure and */
	handle = (lin1d3_handle_t*)pvPortMalloc(sizeof(lin1d3_handle_t));
	if(handle ==  NULL) {
		/* Failed to allocate memory for the node handle */
		return NULL;
	}
	/* Init the handle structure with 0s */
	memset(handle, 0, sizeof(lin1d3_handle_t));
	/* Copy the config */
	memcpy(&(handle->config), &config, sizeof(lin1d3_nodeConfig_t));

	/* Create the Node Task */
	if(lin1d3_master_nodeType == config.type) {
		/* Create a queue for User message requests */
		handle->node_queue = xQueueCreate( master_queue_size_d, sizeof(uint8_t));
		if(handle->node_queue == NULL){
			vPortFree(handle);
			return NULL;
		}
		/* Create a task for the node */
		master_task_name[strlen(master_task_name)-1] += node_idx++;
		if (xTaskCreate(master_task, master_task_name, master_stack_size_d, handle, master_task_priority, &(handle->task_handle)) != pdPASS) {
			vPortFree(handle);
			return NULL;
		}
	}
	else if(lin1d3_slave_nodeType == config.type) {
		/* Create a task for the node */
		slave_task_name[strlen(slave_task_name)-1] += node_idx++;
		if (xTaskCreate(slave_task, slave_task_name, slave_stack_size_d, handle, slave_task_priority, &(handle->task_handle)) != pdPASS) {
			vPortFree(handle);
			return NULL;
		}
	}

	return handle;
}

/*
 * Send a message frame from a LIN Master node
 * */
uint32_t lin1d3_masterSendMessage(lin1d3_handle_t* handle, uint8_t ID)
{
	if(handle !=  NULL) {
		/* Put the requested ID on the master queue */
		xQueueSend( handle->node_queue, &ID, ( TickType_t ) 0 );
	}
	return 0;
}


/******************************************************************************
 * Static functions
 *
 *****************************************************************************/
static void master_task(void *pvParameters)
{
	lin1d3_handle_t* handle = (lin1d3_handle_t*)pvParameters;
	uint8_t  ID;
//	uint8_t  synch_break_byte = 0xFF;
	uint8_t  lin1p3_header[] = {0x55, 0x00};  /* 0001 */
	uint8_t  lin1p3_message[size_of_uart_buffer];
	uint8_t  message_size = 0;
	size_t n;
	uint8_t  msg_idx;


	if(handle == NULL) {
		vTaskSuspend(NULL);
	}

	/* Init/Configure the UART */
	handle->uart_config.base = handle->config.uartBase;
	handle->uart_config.srcclk = handle->config.srcclk;
	handle->uart_config.baudrate = handle->config.bitrate;
	handle->uart_config.parity = kUART_ParityDisabled;
	handle->uart_config.stopbits = kUART_OneStopBit;
	handle->uart_config.buffer = pvPortMalloc(size_of_uart_buffer);
	handle->uart_config.buffer_size = size_of_uart_buffer;
	if(handle->uart_config.buffer == NULL){
		vTaskSuspend(NULL);
	}

    if (0 > UART_RTOS_Init(&(handle->uart_rtos_handle), &(handle->uart_handle), &(handle->uart_config)))
    {
        vTaskSuspend(NULL);
    }
    /* Configure 13 bits break transmission */
    handle->uart_rtos_handle.base->S2 |= (1<<2);  /* BRK13 Break Transmit Character Length "1" Break character is 13 or 14 bits long.*/

    while(1) {
    	/* Wait for messages on the Queue */
        if(xQueueReceive(handle->node_queue, &ID, portMAX_DELAY)){

        	msg_idx = 0;
        	/*Look for the ID in the message table */
        	while(msg_idx < lin1d3_max_supported_messages_per_node_cfg_d) {
        		if(handle->config.messageTable[msg_idx].ID == ID) {
        			break;
        		}
        		msg_idx++;
        	}

        	/* If the message ID was not found then ignore it */
        	if(msg_idx == lin1d3_max_supported_messages_per_node_cfg_d) continue;

        	/* Put the ID into the header */
        	lin1p3_header[1] = ID<<2;
        	/* TODO: put the parity bits */




        	/* Init the message recevie buffer */
        	memset(lin1p3_message, 0, size_of_uart_buffer);
        	/* Calc the message size */
        	switch(ID&0x03) {
        		case 0x00: message_size = 2;
        		break;
        		case 0x01: message_size = 2;
        		break;
        		case 0x02: message_size = 4;
        		break;
        		case 0x03: message_size = 8;
        		break;
        	}
        	message_size+=1;
        	/* Send a Break It is just sending one byte 0, *** CHANGE THIS WITH A REAL SYNCH BREAK ****/
        	handle->uart_rtos_handle.base->C2 |= 0x01;  /*SBK Send Break ON */
        	handle->uart_rtos_handle.base->C2 &= 0xFE;	/*SBK Send Break OFF */

        	//UART_RTOS_Send(&(handle->uart_rtos_handle), (uint8_t *)&synch_break_byte, 1);
        	//vTaskDelay(1);
        	/* Send the header */
        	UART_RTOS_Send(&(handle->uart_rtos_handle), (uint8_t *)lin1p3_header, size_of_lin_header_d);
        	/* Wait for the response */
        	UART_RTOS_Receive(&(handle->uart_rtos_handle), lin1p3_message, message_size, &n);

        	/* TODO: Check the checksum */




        	/* Call the message callback */
        	handle->config.messageTable[msg_idx].handler((void*)lin1p3_message);
        }
    }
}

static void slave_task(void *pvParameters)
{
	lin1d3_handle_t* handle = (lin1d3_handle_t*)pvParameters;
	uint8_t  ID;
	uint8_t  lin1p3_header[size_of_lin_header_d];
	uint8_t  lin1p3_message[size_of_uart_buffer];
	uint8_t  message_size = 0;
	size_t n;
	uint8_t  msg_idx;
	//uint8_t synch_break_byte = 0xFF;
	static uint8_t bit = 0;

	if(handle == NULL) {
		vTaskSuspend(NULL);
	}

	/* Init/Configure the UART */
	handle->uart_config.base = handle->config.uartBase;
	handle->uart_config.srcclk = handle->config.srcclk;
	handle->uart_config.baudrate = handle->config.bitrate;
	handle->uart_config.parity = kUART_ParityDisabled;
	handle->uart_config.stopbits = kUART_OneStopBit;
	handle->uart_config.buffer = pvPortMalloc(size_of_uart_buffer);
	handle->uart_config.buffer_size = size_of_uart_buffer;
	if(handle->uart_config.buffer == NULL){
		vTaskSuspend(NULL);
	}

    if (0 > UART_RTOS_Init(&(handle->uart_rtos_handle), &(handle->uart_handle), &(handle->uart_config)))
    {
        vTaskSuspend(NULL);
    }


    DisableIRQ(UART3_RX_TX_IRQn);
    DisableIRQ(UART4_RX_TX_IRQn);
	handle->uart_rtos_handle.base->S2 |= (1<<7);  	/*LBKDIF LIN Break Detect Interruption Flag  */
	handle->uart_rtos_handle.base->S2 |= (1<<1);	/* LBKDE LIN Break Detection Enable */
    while(1)
    {
    	char dummy;
    	/* Init the message header buffer */
    	memset(lin1p3_header, 0, size_of_lin_header_d);
    	/* Wait for a synch break This code is just waiting for one byte 0, *** CHANGE THIS WITH A REAL SYNCH BREAK ****/

        /* Enable interrupt in NVIC. */
        DisableIRQ(UART3_RX_TX_IRQn);
        DisableIRQ(UART4_RX_TX_IRQn);
        do
        {
        	handle->uart_rtos_handle.base->S2 |= (1<<1);	/* LBKDE LIN Break Detection Enable */
        }while (0x80 != ((handle->uart_rtos_handle.base->S2) & 0x80));
    	handle->uart_rtos_handle.base->S2 &= (0<<1);	/* LBKDE LIN Break Detection Disable */
    	handle->uart_rtos_handle.base->S2 |= (1<<7);  	/*LBKDIF LIN Break Detect Interruption Flag  */
    	EnableIRQ(UART3_RX_TX_IRQn);
    	DisableIRQ(UART4_RX_TX_IRQn);
    	do
    	{
    		UART_RTOS_Receive(&(handle->uart_rtos_handle), lin1p3_header,size_of_lin_header_d, &n );
    	} while(0x0 == lin1p3_header[0]);

    	//synch_break_byte = 0xFF;
    	//do {
    	//UART_RTOS_Receive(&(handle->uart_rtos_handle), &synch_break_byte, 1, &n);
    	//}while(synch_break_byte != 0);

    	/* Wait for header on the UART */
    	//UART_RTOS_Receive(&(handle->uart_rtos_handle), lin1p3_header,size_of_lin_header_d, &n );
    	/* Check header */
    	if(/*(lin1p3_header[0] != 0x00) &&*/
    	   (lin1p3_header[0] != 0x55)) {
    		/* TODO: Check ID parity bits */




    		/* Header is not correct we are ignoring the header */
    		continue;
    	}
    	/* Get the message ID */
    	ID = (lin1p3_header[1] & 0xFC)>>2;

    	/* If the header is correct, check if the message is in the table */
    	msg_idx = 0;
    	/*Look for the ID in the message table */
    	while(msg_idx < lin1d3_max_supported_messages_per_node_cfg_d) {
    		if(handle->config.messageTable[msg_idx].ID == ID) {
    			break;
    		}
    		msg_idx++;
    	}
    	/* If the message ID was not found then ignore it */
    	if(msg_idx == lin1d3_max_supported_messages_per_node_cfg_d) continue;
    	/* Init the message transmit buffer */
    	memset(lin1p3_message, 0, size_of_uart_buffer);
    	/*If the message is in the table call the message callback */
    	handle->config.messageTable[msg_idx].handler((void*)lin1p3_message);

    	/* Calc the message size */
    	switch(ID&0x03) {
    		case 0x00: message_size = 2;
    		break;
    		case 0x01: message_size = 2;
    		break;
    		case 0x02: message_size = 4;
    		break;
    		case 0x03: message_size = 8;
    		break;
    	}
    	/* TODO: Add the checksum to the message */





    	message_size+=1;
    	/* Send the message data */
    	UART_RTOS_Send(&(handle->uart_rtos_handle), (uint8_t *)lin1p3_message, message_size);
    }
}
