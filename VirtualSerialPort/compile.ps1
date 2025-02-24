
$env:Path += ';C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64'
cl virtualport.cpp 
/I "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\include" 
/I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um" 
/I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared" 
/I "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt" 
/I "C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf\2.0" 

/link 
/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64" 
/LIBPATH:"C:\Program Files (x86)\Windows Kits\10\Lib\wdf\umdf\x64\2.0" tdll.lib WdfDriverStubUm.lib /subsystem:native /driver:wdm -entry:DriverEntry
ren driver.exe driver.sys
pause
