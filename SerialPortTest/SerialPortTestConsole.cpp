// SerialPortTestConsole.cpp : Diese Datei enthält die Funktion "main". Hier beginnt und endet die Ausführung des Programms.
//

#include <iostream>
#include <tchar.h>
#include <windows.h>
#include <serial_port.h>
#include <chrono>
#include <thread>

int main()
{
    std::cout << "Hello World!\n";

    SerialPort port("COM5");
    port.openPort();
    port.setTimeouts(500, 500);
    port.setBaud(9600);

    if (port.isOpen()) std::cout << "TETESTETT\r\n";
    
    port.writeBytes("control up 0\r", 13);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    port.writeBytes("control stop 0\r", 15);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    port.writeBytes("control down 0\r", 15);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    port.writeBytes("control stop 0\r", 15);

    char readBuffer[256];
    SecureZeroMemory(readBuffer, 256);
    int bytesReceived = port.readBytesBurst(readBuffer, 256, 10);

    printf("Received Bytes: %d\r\n", bytesReceived);
    printf(readBuffer);
    printf("\r\n");

    port.closePort();
    
}

// Programm ausführen: STRG+F5 oder Menüeintrag "Debuggen" > "Starten ohne Debuggen starten"
// Programm debuggen: F5 oder "Debuggen" > Menü "Debuggen starten"

// Tipps für den Einstieg: 
//   1. Verwenden Sie das Projektmappen-Explorer-Fenster zum Hinzufügen/Verwalten von Dateien.
//   2. Verwenden Sie das Team Explorer-Fenster zum Herstellen einer Verbindung mit der Quellcodeverwaltung.
//   3. Verwenden Sie das Ausgabefenster, um die Buildausgabe und andere Nachrichten anzuzeigen.
//   4. Verwenden Sie das Fenster "Fehlerliste", um Fehler anzuzeigen.
//   5. Wechseln Sie zu "Projekt" > "Neues Element hinzufügen", um neue Codedateien zu erstellen, bzw. zu "Projekt" > "Vorhandenes Element hinzufügen", um dem Projekt vorhandene Codedateien hinzuzufügen.
//   6. Um dieses Projekt später erneut zu öffnen, wechseln Sie zu "Datei" > "Öffnen" > "Projekt", und wählen Sie die SLN-Datei aus.
