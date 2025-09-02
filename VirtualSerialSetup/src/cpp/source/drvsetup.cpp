/*
 * drvsetup.cpp
 *
 *  Created on: 01.09.2025
 *      Author: marvi
 */

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

#include "vcom.hpp"
#include "VCOM/public.h"
#include "VCOM/serial.h"
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <stdio.h>
#include <string>
#include <functional>
#include <string.h>

void printError(int errorCode, const char* format) {
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg); fflush(stdout);
		LocalFree(msg);
	} else {
		printf(format, errorCode, "<no error message defined>\n"); fflush(stdout);
	}
}

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	printError(errorCode, format);
}

#define PORTS_ENUM_KEY "SYSTEM\\CurrentControlSet\\Enum\\ROOT\\PORTS"
#define PORTS_PROP_NAME_SUBKEY "\\Device Parameters"
#define PORTS_PROP_NAME_VALUE "PortName"
#define PORTS_PROP_HWID_VALUE "HardwareID"


#define PORTS_PROP_NAME_KEY "Device Parameters"

#define VCOM_DRIVER_INF "VCOM.inf"
#define INF_SECTION_HWID "standard.nt"

bool findINFClassAndHWID(std::string* fullInfPath, std::string* hardwareID, std::string* vcomClassName, GUID* vcomClassGUID) {

	// find driver INF
	char infFullPathStr[MAX_PATH] = {0};
	if (GetFullPathNameA(VCOM_DRIVER_INF, MAX_PATH, infFullPathStr, NULL) >= MAX_PATH) {
		printError("error 0x%x in drvsetup:findINFClassAndHWID:GetFullPathNameA: %s");
		return false;
	}
	if (fullInfPath != 0) *fullInfPath = std::string(infFullPathStr);

	printf("[i] load hwID and class GUID from driver: %s\n", infFullPathStr);

	// read class from driver INF
	char vcomClassNameStr[MAX_CLASS_NAME];
	GUID classGUIDdummy; // this is just for when no pointer for the GUID was provided
	if (!SetupDiGetINFClassA(infFullPathStr, vcomClassGUID == 0 ? &classGUIDdummy : vcomClassGUID, vcomClassNameStr, MAX_CLASS_NAME, NULL)) {
		printError("error 0x%x in drvsetup:findINFClassAndHWID:SetupDiGetINFClassA: %s");
		return false;
	}
	if (vcomClassName != 0) *vcomClassName = std::string(vcomClassNameStr);

	// find hwardware ID
	HINF infHandle = SetupOpenInfFileA(infFullPathStr, NULL, INF_STYLE_WIN4, NULL);
	if (infHandle == INVALID_HANDLE_VALUE) {
		printError("error 0x%x in drvsetup:findINFClassAndHWID:SetupOpenInfFileA: %s");
		return false;
	}

	DWORD sectionIndex = 0;
	do {

		// request next INF section
		char sectionName[256] = {0};
		UINT sectionNameLen = 256;
		if (!SetupEnumInfSectionsA(infHandle, sectionIndex++, sectionName, sectionNameLen, &sectionNameLen)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
			printError("error 0x%x in drvsetup:findINFClassAndHWID:SetupEnumInfSectionsA: %s");
			return false;
		}

		// check if the section defines a new device type
		if (strncmp(sectionName, INF_SECTION_HWID, strlen(INF_SECTION_HWID)) != 0)
			continue;

		// get the first (and only) line in this section
		INFCONTEXT infContext;
		if (!SetupFindFirstLineA(infHandle, sectionName, NULL, &infContext)) {
			printError("error 0x%x in drvsetup:findINFClassAndHWID:SetupFindFirstLineA: %s");
			return false;
		}

		// read the hardwareID
		char hwIDstr[256] = {0};
		DWORD hwIDstrLen = 256;
		if (!SetupGetStringFieldA(&infContext, 2, hwIDstr, hwIDstrLen, &hwIDstrLen)) {
			printError("error 0x%x in drvsetup:findINFClassAndHWID:SetupGetStringFieldA: %s");
			return false;
		}
		if (hardwareID != 0) *hardwareID = std::string(hwIDstr);

		printf("[i] hwardwareID: %s className: %s\n", hwIDstr, vcomClassNameStr);

		SetupCloseInfFile(infHandle);
		return true;

	} while (true);

	SetupCloseInfFile(infHandle);
	return false;

}

bool removeAllPorts()
{
	return removePort(""); // empty string makes it remove all ports
}

bool removePort(std::string portName)
{

	GUID vcomClassGUID;
	std::string vcomHardwareID;
	if (!findINFClassAndHWID(0, &vcomHardwareID, 0, &vcomClassGUID)) return false;

	if (portName.empty()) {
		printf("[i] remove all ports\n");
	}

	// device info set with all devices
	HDEVINFO portsInfoSet = SetupDiGetClassDevsA(&vcomClassGUID, "ROOT\\PORTS", NULL, 0);
	if (portsInfoSet == INVALID_HANDLE_VALUE) {
		printError("error 0x%x in drvsetup:removePortAction:SetupDiGetClassDevsA: %s");
		return false;
	}

	bool allSuccess = true;
	DWORD deviceInfoIndex = 0;
	do {

		// request next device info data
		SP_DEVINFO_DATA portInfoData;
		portInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
		if (!SetupDiEnumDeviceInfo(portsInfoSet, deviceInfoIndex++, &portInfoData)) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
			printError("error 0x%x in drvsetup:removePortAction:SetupDiEnumDeviceInfo: %s");
			continue;
		}

		// read hardware ID
		char hardwareIdValue[256] = {0};
		DWORD hardwareIdValueLen = 256;
		if (!SetupDiGetDeviceRegistryPropertyA(portsInfoSet, &portInfoData, SPDRP_HARDWAREID, NULL, (unsigned char*) hardwareIdValue, hardwareIdValueLen, &hardwareIdValueLen)) {
			printError("error 0x%x in drvsetup:removePortAction:SetupDiGetDeviceRegistryPropertyA(SPDRP_HARDWAREID): %s");
			continue;
		}
		std::string hardwareID(hardwareIdValue);

		// check for virtual port
		if (hardwareID != vcomHardwareID) continue;

		std::string devPortName = "<NA>";

		// open device registry key
		// DIREG_DEV is requivalent to HKLM\SYSTEM\CurrentControlSet\Enum\ROOT\PORTS\*port dev id*\Device Parameters
		HKEY portKeyHandle = SetupDiOpenDevRegKey(portsInfoSet, &portInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
		if (portKeyHandle == INVALID_HANDLE_VALUE) {
			// this indicates an uninitialized port, we are only interested in this error during debugging
			//printError("error 0x%x in drvsetup:removePortAction:SetupDiOpenDevRegKey(): %s");
		} else {

			// read port name property
			char portNameValue[256] = {0};
			DWORD portNameValueLen = 256;
			LSTATUS status = RegGetValueA(portKeyHandle, "", PORTS_PROP_NAME_VALUE, RRF_RT_REG_SZ, NULL, portNameValue, &portNameValueLen);
			if (status != ERROR_SUCCESS) {
				RegCloseKey(portKeyHandle);

				if (status != ERROR_FILE_NOT_FOUND) continue;
				printError(status, "error 0x%x in drvsetup:enumeratePorts:RegGetValueA(PORTS_PROP_NAME): %s");
			} else {
				devPortName = std::string(portNameValue);
			}

			RegCloseKey(portKeyHandle);

		}

		// check if port is the one we are looking for, this also allows for removal of broken ports using <NA>
		if (!portName.empty() && devPortName != portName) continue;

		printf("[i] remove port %s\n", devPortName.c_str());

		// remove the port
		if (!SetupDiCallClassInstaller(DIF_REMOVE, portsInfoSet, &portInfoData)) {
			printError("error 0x%x in drvsetup:enumeratePorts:SetupDiCallClassInstaller: %s");
			allSuccess = false;
		}

		if (!portName.empty())
			break; // we don't need to continue here, there should only be one port with this name

	} while (true);

	SetupDiDestroyDeviceInfoList(portsInfoSet);

	return allSuccess;

}

bool installPort(std::string portName)
{

	std::string fullInfPath;
	GUID vcomClassGUID;
	std::string vcomClassName;
	std::string vcomHardwareID;
	if (!findINFClassAndHWID(&fullInfPath, &vcomHardwareID, &vcomClassName, &vcomClassGUID)) return false;

	printf("[i] install new port %s\n", portName.c_str());

	// create device info container
	HDEVINFO portInfoSet = SetupDiCreateDeviceInfoList(&vcomClassGUID, NULL);
	if (portInfoSet == INVALID_HANDLE_VALUE) {
		printError("error 0x%x in drvsetup:installPort:SetupDiCreateDeviceInfoList: %s");
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

	// create device info in container
	SP_DEVINFO_DATA portInfoData = {0};
	portInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfoA(portInfoSet, vcomClassName.c_str(), &vcomClassGUID, NULL, NULL, DICD_GENERATE_ID, &portInfoData)) {
		printError("error 0x%x in drvsetup:installPort:SetupDiCreateDeviceInfoA: %s");
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

	// configure hardware ID
	if (!SetupDiSetDeviceRegistryPropertyA(portInfoSet, &portInfoData, SPDRP_HARDWAREID, (const unsigned char*) vcomHardwareID.c_str(), vcomHardwareID.length() + 1)) {
		printError("error 0x%x in drvsetup:installPort:SetupDiSetDeviceRegistryPropertyA(SPDRP_HARDWAREID): %s");
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

	// install device
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, portInfoSet, &portInfoData)) {
		printError("error 0x%x in drvsetup:installPort:SetupDiCallClassInstaller: %s");
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

//	// test
//	HKEY portKeyHandle;
//	LSTATUS status = RegOpenKeyA(HKEY_LOCAL_MACHINE, "SYSTEM\CurrentControlSet\Enum\ROOT\PORTS\*port dev id*", &portKeyHandle);
//	if (status != ERROR_SUCCESS) {
//
//	}
//
//	HKEY portKeyParameterHandle;
//	status = RegCreateKeyExA(portKeyHandle, "Device Parameters", NULL, NULL, NULL, 0, 0, portKeyParameterHandle, NULL);
//	if (status != ERROR_SUCCESS) {
//
//	}
//
//	RegCloseKey(portKeyParameterHandle);
//	RegCloseKey(portKeyHandle);

//	{
//
//		char hardwareIdValue[256] = {0};
//		DWORD hardwareIdValueLen = 256;
//		if (!SetupDiGetDeviceRegistryPropertyA(portInfoSet, &portInfoData, SPDRP_HARDWAREID, NULL, (unsigned char*) hardwareIdValue, hardwareIdValueLen, &hardwareIdValueLen)) {
//			printError("error 0x%x in drvsetup:installPort:SetupDiGetDeviceRegistryPropertyA(SPDRP_HARDWAREID): %s");
//
//		}
//
//	}

	// open device registry key
	// DIREG_DEV is requivalent to HKLM\SYSTEM\CurrentControlSet\Enum\ROOT\PORTS\*port dev id*\Device Parameters
	HKEY portKeyHandle = SetupDiOpenDevRegKey(portInfoSet, &portInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_SET_VALUE | KEY_CREATE_SUB_KEY );
	if (portKeyHandle == INVALID_HANDLE_VALUE) {
		printError("error 0x%x in drvsetup:installPort:SetupDiOpenDevRegKey(): %s");
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

	// write port name property
	LSTATUS status = RegSetKeyValueA(portKeyHandle, "", PORTS_PROP_NAME_VALUE, RRF_RT_REG_SZ, portName.c_str(), portName.length() + 1);
	if (status != ERROR_SUCCESS) {
		printError(status, "error 0x%x in drvsetup:installPort:RegSetKeyValueA: %s");
		RegCloseKey(portKeyHandle);
		SetupDiDestroyDeviceInfoList(portInfoSet);
		return false;
	}

	RegCloseKey(portKeyHandle);
	SetupDiDestroyDeviceInfoList(portInfoSet);

	printf("[i] new port installed\n");
	return true;
}

bool updateDriver()
{

	std::string fullInfPath;
	std::string vcomHardwareID;
	if (!findINFClassAndHWID(&fullInfPath, &vcomHardwareID, 0, 0)) return false;

	printf("[i] reload driver PnP devices ... (this might take some time)\n");

	// update driver to let it initialize the new devices
	if (!UpdateDriverForPlugAndPlayDevicesA(NULL, vcomHardwareID.c_str(), fullInfPath.c_str(), 0, NULL)) {
		printError("error 0x%x in drvsetup:createPort:UpdateDriverForPlugAndPlayDevicesA: %s");
		return false;
	}

	printf("[i] reload completed\n");
	return true;
}

bool installDriver()
{
	return false;
}
bool uninstallDriver()
{
	return false;
}
