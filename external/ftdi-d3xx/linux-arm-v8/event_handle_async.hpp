#ifndef EVENT_HANDLE_ASYNC_HPP
#define EVENT_HANDLE_ASYNC_HPP

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include "Types.h"

#define EVENT_SIGNATURE 0x45564E54 // ASCII hex for EVNT sequence

#define STATUS_SUCCESS			0
#define STATUS_UNSUCCESSFUL		0xC0000001
#define STATUS_TIMEOUT			0x00000102
#define STATUS_PENDING			0x00000103
#define STATUS_UNEXPECTED_IO_ERROR	0xC00000E9
#define STATUS_CANCELLED		0xC0000120
#define STATUS_DEVICE_NOT_READY		0xC00000A3
#define STATUS_PORT_DISCONNECTED	0xC0000037
#define STATUS_EXTRANEOUS_INFORMATION	0x80000017

HANDLE FT_W32_CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes,
                          BOOL                  bManualReset,
                          BOOL                  bInitialState,
                          LPCTSTR               lpName)
{
	int    pthreadRet = 0;
	Event *ev;

	(void)lpEventAttributes; // deliberately unused
	(void)lpName; // deliberately unused

	ev = (Event *)calloc(1, sizeof(Event));
	if (ev == NULL)
		return NULL;

	ev->signature = EVENT_SIGNATURE;

	if (bManualReset)
		ev->manualReset = 1;

	if (bInitialState)
		ev->signalled = 1;

	pthreadRet = pthread_cond_init(&ev->eh.eCondVar, NULL);
	if (pthreadRet)
		goto exit;

	pthreadRet = pthread_mutex_init(&ev->eh.eMutex, NULL);

exit:
	if (pthreadRet)
	{
		// One of the pthread_xxx calls failed.
		free(ev);
		ev = NULL;
	}

	return (HANDLE)ev;
}

BOOL FT_W32_SetEvent(HANDLE hEvent)
{
	Event *ev = (Event *)hEvent;
	int    pthreadRet;

	if (ev == NULL || ev->signature != EVENT_SIGNATURE)
		return FALSE;

	pthreadRet = pthread_mutex_lock(&ev->eh.eMutex);
	if (pthreadRet)
		return FALSE;

	ev->signalled = 1;

	// Unlock all threads waiting for cond.
	pthreadRet = pthread_cond_broadcast(&ev->eh.eCondVar);

	(void)pthread_mutex_unlock(&ev->eh.eMutex);

	return (pthreadRet) ? FALSE : TRUE;
}

BOOL FT_W32_ResetEvent(HANDLE hEvent)
{
	Event *ev = (Event *)hEvent;
	int    pthreadRet;

	if (ev == NULL || ev->signature != EVENT_SIGNATURE)
		return FALSE;

	pthreadRet = pthread_mutex_lock(&ev->eh.eMutex);
	if (pthreadRet)
		return FALSE;

	ev->signalled = 0;

	(void)pthread_mutex_unlock(&ev->eh.eMutex);

	return TRUE;
}

BOOL FT_W32_CloseHandle(HANDLE hObject)
{
	Event * ev = (Event *)hObject;

	if (ev == NULL || ev->signature != EVENT_SIGNATURE)
		return FALSE;

	(void)pthread_mutex_unlock(&ev->eh.eMutex);
	(void)pthread_cond_destroy(&ev->eh.eCondVar);
	(void)pthread_mutex_destroy(&ev->eh.eMutex);
	free(ev);

	return TRUE;
}


/* Windows documentation is not clear: possibly suggests that
 * WaitForSingleObject has no effect if called AFTER SetEvent
 * for an auto-reset event.  FIXME: test on real Windows.
 */
DWORD FT_W32_WaitForSingleObject(HANDLE hHandle,
                                 DWORD  dwMilliseconds)
{
	Event *ev = (Event *)hHandle;
	DWORD  winRet = WAIT_FAILED;
	int    pthreadRet = 0;

	if (ev == NULL || ev->signature != EVENT_SIGNATURE)
		return WAIT_FAILED;

	pthreadRet = pthread_mutex_lock(&ev->eh.eMutex);
	if (pthreadRet != 0)
		return WAIT_FAILED;

	if (ev->signalled == 1)
	{
		// Already signalled.  No need to wait.
		pthreadRet = 0;
		goto exit;
	}

	// Not yet signalled.  We're going to wait.

	if (dwMilliseconds == 0)
	{
		// Match Windows: consider event unsignalled within time-out period.
		pthreadRet = ETIMEDOUT;
	}
	else if (dwMilliseconds == INFINITE)
	{
		pthreadRet = pthread_cond_wait(&ev->eh.eCondVar,
				&ev->eh.eMutex);
	}
	else
	{
		// timeval is two long ints: whole seconds and additional microseconds
		struct timeval  now;
		// timespec is two long ints: whole seconds and additional nanoseconds
		struct timespec timeout;

		// Store timeout period's whole seconds
		timeout.tv_sec = dwMilliseconds / 1000;

		// For now store microseconds in the nanoseconds member
		timeout.tv_nsec = (dwMilliseconds % 1000) * 1000;

		gettimeofday(&now, NULL); // Get current time

		// Update timeout period.  Note: got current time as late as
		// possible to reduce chance of being already elapsed when
		// pthread_cond_timedwait sees it.
		timeout.tv_sec += now.tv_sec;
		timeout.tv_nsec += now.tv_usec;

		if (timeout.tv_nsec >= 1000000)
		{
			// The 'additional microseconds' has overflowed.
			// Transfer the excess to the 'whole seconds' member.
			timeout.tv_sec++;
			timeout.tv_nsec -= 1000000;
		}

		// Now express microseconds as nanoseconds.
		timeout.tv_nsec *= 1000;

		pthreadRet = pthread_cond_timedwait(&ev->eh.eCondVar,
				&ev->eh.eMutex,
				&timeout);
	}

exit:
	if (pthreadRet == 0)
	{
		// Condition was met before any timeout.
		winRet = WAIT_OBJECT_0;
	}
	else if (pthreadRet == ETIMEDOUT)
	{
		// Timed out before condition was met.
		winRet = WAIT_TIMEOUT;
	}
	else
	{
		// Any other error.
		winRet = WAIT_FAILED;
	}

	if (!ev->manualReset)
	{
		// Auto-reset once a thread is released
		ev->signalled = 0;
	}

	(void)pthread_mutex_unlock(&ev->eh.eMutex);
	return winRet;
}
#endif	//EVENT_HANLE_ASYNC_HPP
