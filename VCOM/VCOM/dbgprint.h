#pragma once

#include <wudfwdm.h>

// these filters are total unreliable, so just use ERROR which is always printed
#define dbgprintf(...) KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))
#define dbgerrprintf(...) KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))
#define dbgwrnprintf(...) KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__))
