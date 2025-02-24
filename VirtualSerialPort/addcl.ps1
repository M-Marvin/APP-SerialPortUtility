$env:Path += ';C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64'
$env:WDF = 
	'/I', 'C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf\2.0',
	'/I', 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um',
	'/I', 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt',
	'/I', 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared'
