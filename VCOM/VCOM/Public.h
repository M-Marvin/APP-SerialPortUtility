/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    driver and application

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID (GUID_DEVINTERFACE_VCOM,
    0x16b70986,0xd5c5,0x4b92,0xba,0xf4,0x57,0x3e,0x17,0xe4,0xb9,0xd1);
// {16b70986-d5c5-4b92-baf4-573e17e4b9d1}
