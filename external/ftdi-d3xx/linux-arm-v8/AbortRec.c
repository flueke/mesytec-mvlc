#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "ftd3xx.h"

#define FIFO_CHANNEL_1	0
#define FIFO_CHANNEL_2	1
#define FIFO_CHANNEL_3	2
#define FIFO_CHANNEL_4	3
#define TIMEOUT			1 //0xFFFFFFFF

static void show_help(const char *bin)
{
	printf("Usage: %s <read length>\r\n", bin);
	printf("  Only FT245 mode is supported in this demo\r\n");
}

int main(int argc, char *argv[])
{
	FT_HANDLE handle = NULL;
	FT_STATUS ftStatus = FT_OK;

	if (argc != 2) {
		show_help(argv[0]);
		return false;
	}
	uint32_t to_read = atoi(argv[1]);

	FT_Create(0, FT_OPEN_BY_INDEX, &handle);

	if (!handle) {
		printf("Failed to create device\r\n");
		return -1;
	}
	printf("Device created\r\n");

	uint8_t *in_buf = (uint8_t *)malloc(to_read);
	DWORD count;

	while(1)
	{
        if (FT_OK != FT_ReadPipeEx(handle, FIFO_CHANNEL_1, in_buf, to_read,
            &count, TIMEOUT)) {
            printf("Failed to read\r\n");
            goto _Exit;
        }
        printf("Read %d bytes\r\n", count);
	}

_Exit:
	free(in_buf);
	UCHAR ucDirection = (FT_GPIO_DIRECTION_OUT << FT_GPIO_0) ;
	UCHAR ucMask = (FT_GPIO_VALUE_HIGH << FT_GPIO_0);
	ftStatus = FT_EnableGPIO(handle, ucMask, ucDirection);
    if(ftStatus != FT_OK)
	{
		printf("FT_EnableGPIO is Failed...=%d\n",ftStatus);
		return 0;
	}
	ftStatus = FT_WriteGPIO(handle,ucMask, (FT_GPIO_VALUE_HIGH << FT_GPIO_0));
	    if(ftStatus != FT_OK)
	{
		printf("FT_WriteGPIO is Failed...=%d\n",ftStatus);
		return 0;
	}
	FT_AbortPipe(handle,0x82);	
	FT_WriteGPIO(handle,ucMask, (FT_GPIO_VALUE_LOW << FT_GPIO_0));

    FT_Close(handle);
	return 0;
}

