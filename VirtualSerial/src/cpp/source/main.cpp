/*
 * main.cpp
 *
 *  Created on: 20.08.2025
 *      Author: marvi
 */

#include <stdio.h>
#include <string>
#include <windows.h>
#include <fileapi.h>
#include <winioctl.h>
#include "public.h"
#include <string.h>

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		setbuf(stdout, NULL); // Work around for errors printed during JNI
		printf(format, errorCode, msg);
		LocalFree(msg);
	}
}

int main(int argn, const char** argv) {

	if (argn <= 1) {
		printf("vserial [port path]\n");
		return 1;
	}

	printf("open port ...\n");

	std::string portFile(argv[1]);

	HANDLE handle = CreateFileA(portFile.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		printError("win error: %lu %s");
		return -1;
	}

	printf("write buffers ...\n");

	char inBuffer[32];
	size_t inBufferLen = 32;

	const char* testText = "TEST TEXT DATA";
	strncpy(inBuffer, testText, strlen(testText));

	char outBuffer[32];
	size_t outBufferLen = 32;

	unsigned long int bytesReturned = 0;

	printf("send IOCTL ...\n");

	if (!DeviceIoControl(handle, IOCTL_APPLINK_WRITE_BUFFER, inBuffer, inBufferLen, outBuffer, outBufferLen, &bytesReturned, NULL)) {
		printError("DeviceIoControl failed: %lu %s");
		CloseHandle(handle);
		return -1;
	}

	printf("return buffer: %.*s\n", bytesReturned, outBuffer);

	CloseHandle(handle);
	return 0;

}

