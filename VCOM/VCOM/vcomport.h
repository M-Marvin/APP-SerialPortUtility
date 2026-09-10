#pragma once

#include <Windows.h>
#include <wdf.h>
#include "ringbuffer.h"
#include "serial.h"

/* Context of an port device (an virtual port).
 * Stores information such as the ports file name and serial and event configuration.
 */
typedef struct
{

    WDFDEVICE           Device;
    PWSTR               PdoName;            // Physical Device Object name

    ULONG               BaudRate;           // The baud rate currently configured
    SERIAL_LINE_CONTROL LineControl;        // The line control register currently configured (stop, data, parity)
    SERIAL_HANDFLOW     FlowControl;        // The flow control currently configured
    SERIAL_TIMEOUTS     Timeouts;           // The Tx/Rx timeouts currently configured
    SERIAL_CHARS        FlowChars;          // The XON/XOFF characters currently configured
    ULONG               FlowControlState;   // The manual flow control state currently configured (DTS, RTS)

    SERIAL_TIMEOUTS     AppTimeouts;        // The Read/Write timeouts for the application side of the port
    ULONG               SerialWaitMask;     // The evnet mask for the wait event
    ULONG               AppWaitMask;        // The event mask for the application side of the port

    Ringbuffer          SerialTxBuffer;     // Ringbuffer for incomming data from serial port
    Ringbuffer          SerialRxBuffer;     // Ringbuffer for outgoing data from serial port

} DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetPortDeviceContext);

#define SERIAL_DEVICE_MAP     L"SERIALCOMM"
#define REG_VALUENAME_PORNAME L"PortName"

/// <summary>
/// Creates a new driver object for an new virtual serial port
/// </summary>
/// <param name="driverHandle">A handle for the driver object</param>
/// <param name="deviceInit">A opaque struct from the framework</param>
/// <param name="deviceContext">A pointer to store the address of the newly created device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS PortDeviceCreate(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit, DEVICE_CONTEXT** deviceContext);

/// <summary>
/// Cleanup for the driver object to free all alocated resources
/// </summary>
/// <param name="deviceHandle">A handle for the device object</param>
/// <returns></returns>
VOID PortDeviceCleanup(WDFDEVICE deviceHandle);

/// <summary>
/// Configures the new virtual serial port device
/// </summary>
/// <param name="context">The objects device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS PortDeviceConfigure(DEVICE_CONTEXT* context);

/* Creates all IO queues and timers required for operation */
NTSTATUS PortDeviceCreateIOQueues(DEVICE_CONTEXT* deviceContext);

/* Stores all IO queue and timer information instances required during IO operations */
typedef struct
{

    WDFQUEUE        Queue;              // Default parallel queue
    WDFQUEUE        SerialWriteQueue;
    WDFQUEUE        SerialReadQueue;
    WDFQUEUE        SerialWaitQueue;
    WDFQUEUE        AppWriteQueue;
    WDFQUEUE        AppReadQueue;
    WDFQUEUE        AppWaitQueue;

    WDFTIMER        SerialReadTimer;
    WDFTIMER        SerialReadIntervalTimer;
    WDFTIMER        SerialWriteTimer;
    WDFTIMER        AppReadTimer;
    WDFTIMER        AppReadIntervalTimer;
    WDFTIMER        AppWriteTimer;
    
    DEVICE_CONTEXT* DeviceContext;

} QUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, GetQueueContext);

/* An helper context attached to every IO request handled, used to store information when the context can not be fully completed immediatly */
typedef union
{
    struct {
        SIZE_T BytesTransfered;
    } ReadWrite;

} REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext);

/* Handle IO read operation on the serial port */
void PortDeviceIOReadSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length);
/* Handle IO read timeout event on the serial port */
void PortDeviceEvtReadSerialTimedOut(WDFTIMER timerHandle);
/* Handle IO write operation on the serial port */
void PortDeviceIOWriteSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length);
/* Handle IO write timeout event on the serial port */
void PortDeviceEvtWriteSerialTimedOut(WDFTIMER timerHandle);
/* Handle IO read operation from the application */
void PortDeviceIOReadApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length);
/* Handle IO read timeout event for the application */
void PortDeviceEvtReadAppTimedOut(WDFTIMER timerHandle);
/* Handle IO write operation from the application */
void PortDeviceIOWriteApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length);
/* Handle IO write timeout event for the application */
void PortDeviceEvtWriteAppTimedOut(WDFTIMER timerHandle);

/* Handle generic IO operation, decide weather to pass to IOCTLSerial or IOCTLApp */
void PortDeviceIOControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode);
/* Handle serial port IO operation */
void PortDeviceIOCTLSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode);
/* Handle application IO operation */
void PortDeviceIOCTLApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode);


/* Copy data from the byte buffer supplied with an IO request */
NTSTATUS PortDeviceCopyFromRequest(WDFREQUEST requestHandle, SIZE_T requestBufferOffset, void* buffer, SIZE_T bytesToCopy);
/* Copy data to the byte buffer supplied with an IO request */
NTSTATUS PortDeviceCopyToRequest(WDFREQUEST requestHandle, SIZE_T requestBufferOffset, void* buffer, SIZE_T bytesToCopy);

/* Take all requests from the queue and put them back on the default queue to initiate an new processing attempt */
void PortDeviceReInvokeRequests(QUEUE_CONTEXT* queueContext, WDFQUEUE queue);
/* Complete all requests in the queue with the timed out status */
void PortDeviceTimeoutRequests(WDFQUEUE queue);
/* Complete the wait on mask request for the serial port with the given mask if applicable */
void PortDeviceTriggerSerialWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask);
/* Complete the wait on mask request for the application with the given mask if applicable */
void PortDeviceTriggerAppWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask);
