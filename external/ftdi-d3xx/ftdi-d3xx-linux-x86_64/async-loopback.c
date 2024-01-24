/*
 * Async Write and Read Loopback test
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "ftd3xx.h"

#define MULTI_ASYNC_BUFFER_SIZE    32768 //1048576 //32768  // 8388608 //   
#define MULTI_ASYNC_NUM             128         
#define NUM_ITERATIONS              1

pthread_t   writeThread;
pthread_t   readThread;
int         writerRunning = 0;
int         readerRunning = 0;
int         r = 0;
int         exitWriter = 0;
int         exitReader = 0;
UCHAR *acWriteBuf = NULL;
UCHAR *acReadBuf = NULL;
ULONG ulBytesToWrite = MULTI_ASYNC_BUFFER_SIZE;
ULONG ulBytesToRead = MULTI_ASYNC_BUFFER_SIZE;
ULONG ulBytesWritten[MULTI_ASYNC_NUM] = {0};
ULONG ulBytesRead[MULTI_ASYNC_NUM] = {0};
OVERLAPPED vOverlappedWrite[MULTI_ASYNC_NUM] = {{0}};
OVERLAPPED vOverlappedRead[MULTI_ASYNC_NUM] = {{0}};
FT_HANDLE ftHandle;

static void* asyncRead(void *arg)
{
    int i,j;
    (void)arg;
    FT_STATUS ftStatus = FT_OK;
    BOOL bResult = TRUE;
    UCHAR *acReadBuf = calloc(MULTI_ASYNC_NUM * MULTI_ASYNC_BUFFER_SIZE, sizeof(UCHAR));
    printf("Starting %s.\n", __FUNCTION__);
    /*Create the overlapped io event for asynchronous transfer*/
    for (j=0; j<MULTI_ASYNC_NUM; j++)
    {
        ftStatus = FT_InitializeOverlapped(ftHandle, &vOverlappedRead[j]);
        if (FT_FAILED(ftStatus))
        {
            printf("FT_InitializeOverlapped failed!\n");
            bResult = FALSE;
            break;
        }
    }
    while (!exitReader) // global thread-exit flag
    {    
        /* Read loopback transfer*/
        for (i=0; i<NUM_ITERATIONS; i++)
        {
            /*Send asynchronous read transfer requests*/
            for (j=0; j<MULTI_ASYNC_NUM; j++)
            {
                memset(&acReadBuf[j*MULTI_ASYNC_BUFFER_SIZE], 0xAA+j, ulBytesToRead);
                vOverlappedRead[j].Internal = 0;
                vOverlappedRead[j].InternalHigh = 0;
                ulBytesRead[j] = 0;
                
                /*
                Read asynchronously
                FT_ReadPipe is a blocking/synchronous function.
                To make it unblocking/asynchronous operation, vOverlapped parameter is supplied.
                When FT_ReadPipe is called with overlapped io, the function will immediately return with FT_IO_PENDING
                */ 
                ftStatus = FT_ReadPipeAsync( ftHandle, 0, 
                                        &acReadBuf[j*MULTI_ASYNC_BUFFER_SIZE], ulBytesToRead, 
                                        &ulBytesRead[j], &vOverlappedRead[j]);
                if (ftStatus != FT_IO_PENDING)
                {
                    printf("FT_ReadPipe failed! Status=%d\n",ftStatus);
                    bResult = FALSE;
                    goto exit;
                }

                ftStatus = FT_GetOverlappedResult(ftHandle, &vOverlappedRead[j], &ulBytesRead[j], TRUE);
                if (FT_FAILED(ftStatus))
                {
                    bResult = FALSE;
                    goto exit;
                }
                if (ulBytesRead[j] != ulBytesToRead)
                {
                    //printf("FT_GetOverlappedResult failed! ulBytesRead[j=%d]=%d != %d ulBytesToRead  \n",j,ulBytesRead[j],ulBytesToRead);
                    goto exit;
                }

            }
        }
        exit:
        if(bResult == FALSE)
        {
            break;
        }
    }
    
    /*Delete the overlapped io event for asynchronous transfer*/
    for (j=0; j<MULTI_ASYNC_NUM; j++)
    {
        FT_ReleaseOverlapped(ftHandle, &vOverlappedRead[j]);
    }
    free(acReadBuf);
    printf("Exiting %s.\n", __FUNCTION__);

    return NULL;
}

static void* asyncWrite(void *arg)
{
    int i,j;
    (void)arg;
    FT_STATUS ftStatus = FT_OK;
    BOOL bResult = TRUE;
    UCHAR *acWriteBuf = calloc(MULTI_ASYNC_NUM * MULTI_ASYNC_BUFFER_SIZE, sizeof(UCHAR));
    printf("Starting %s.\n", __FUNCTION__);
    /* Create the overlapped io event for asynchronous transfer*/
    for (j=0; j<MULTI_ASYNC_NUM; j++)
    {
        ftStatus = FT_InitializeOverlapped(ftHandle, &vOverlappedWrite[j]);
        if (FT_FAILED(ftStatus))
        {
            printf("FT_InitializeOverlapped failed!\n");
            bResult = FALSE;
            break;
        }
    }

    while (!exitWriter) // global thread-exit flag
    {
        /*Write loopback transfer*/
        for (i=0; i<NUM_ITERATIONS; i++)
        {
            /*Send asynchronous write transfer requests*/
            for (j=0; j<MULTI_ASYNC_NUM; j++)
            {
                memset(&acWriteBuf[j*MULTI_ASYNC_BUFFER_SIZE], 0x55+j, ulBytesToWrite);
                vOverlappedWrite[j].Internal = 0;
                vOverlappedWrite[j].InternalHigh = 0;
                ulBytesWritten[j] = 0;
                
                /* Write asynchronously
                * FT_WritePipe is a blocking/synchronous function.
                * To make it unblocking/asynchronous operation, vOverlapped parameter is supplied.
                * When FT_WritePipe is called with overlapped io, the function will immediately return with FT_IO_PENDING
                */
                ftStatus = FT_WritePipeAsync(ftHandle, 0, 
                                        &acWriteBuf[j*MULTI_ASYNC_BUFFER_SIZE], ulBytesToWrite, 
                                        &ulBytesWritten[j], &vOverlappedWrite[j]);
                if (ftStatus != FT_IO_PENDING)
                {
                    printf("FT_WritePipe failed! Status=%d\n",ftStatus);
                    bResult = FALSE;
                    goto exit;
                }
            }

            /*Wait for the asynchronous write and read transfer requests to finish*/
            for (j=0; j<MULTI_ASYNC_NUM; j++)
            {
                ftStatus = FT_GetOverlappedResult(ftHandle, &vOverlappedWrite[j], &ulBytesWritten[j], TRUE);
                if (FT_FAILED(ftStatus))
                {
                    printf("FT_GetOverlappedResult failed!->Write\n");
                    bResult = FALSE;
                    ftStatus = FT_AbortPipe(ftHandle, 0x82);
                    printf("Write -> FT_AbortPipe return =%d  \n",ftStatus);
                    goto exit;
                }
                if (ulBytesWritten[j] != ulBytesToWrite)
                {
                    //printf("FT_GetOverlappedResult >> ulBytesWritten[j=%d]=%d != %d ulBytesToWrite\n",j,ulBytesWritten[j],ulBytesToWrite);
                    bResult = FALSE;
                    goto exit;  
                }
            }
        }
        exit:
        if(bResult == FALSE)
        {
            break;
        }
    }
    /*Delete the overlapped io event for asynchronous transfer*/
    for (j=0; j<MULTI_ASYNC_NUM; j++)
    {
        FT_ReleaseOverlapped(ftHandle, &vOverlappedWrite[j]);
    }
  
    free(acWriteBuf);
    printf("Exiting %s.\n", __FUNCTION__);
    return NULL;
}



int main(void)
{
    FT_STATUS ftStatus = FT_OK;
    BOOL bResult = TRUE;

    /*Open device by description*/
    ftStatus = FT_Create((PVOID)"FTDI SuperSpeed-FIFO Bridge",
                   FT_OPEN_BY_DESCRIPTION, &ftHandle);
    if (FT_FAILED(ftStatus))
    {
        printf("FT_Create failed!\n");
        goto exit;
    }

    FT_SetPipeTimeout(ftHandle,0x02,0);
    FT_SetPipeTimeout(ftHandle,0x82,0);
       // Start thread to write bytes
    r = pthread_create(&writeThread, NULL, &asyncWrite, NULL);
    if (r != 0)
    {
        printf("Failed to create write thread (%d)\n", r);
        goto exit;
    }
    writerRunning = 1;

    // Start thread to read bytes
    r = pthread_create(&readThread, NULL, &asyncRead,NULL);
    if (r != 0)
    {
        printf("Failed to create read thread (%d)\n", r);
        goto exit;
    }
    readerRunning = 1;

    // Wait for threads to finish
    (void)pthread_join(writeThread, NULL);
    writerRunning = 0;
    (void)pthread_join(readThread, NULL);
    readerRunning = 0;
    
exit:
    if (readerRunning)
    {
        exitReader = 1;
        (void)pthread_join(readThread, NULL);
    }
    if (writerRunning)
    {
        exitWriter = 1;
        (void)pthread_join(writeThread, NULL);
    }

    /*Close device*/
    FT_Close(ftHandle);

    if(acReadBuf != NULL)
        free(acReadBuf);

    if(acWriteBuf != NULL)
        free(acWriteBuf);
    
    return bResult;
}
