package de.m_marvin.serialportaccess;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;

import de.m_marvin.serialportaccess.SerialPort.SerialPortConfiguration;
import de.m_marvin.serialportaccess.SerialPort.SerialPortFlowControl;
import de.m_marvin.serialportaccess.SerialPort.SerialPortParity;
import de.m_marvin.serialportaccess.SerialPort.SerialPortStopBits;

public class Test {
	
	public static void main(String... args) throws InterruptedException, IOException {
		System.exit(run());
	}
	
	public static int run() throws InterruptedException, IOException {
		
		BufferedReader cli = new BufferedReader(new InputStreamReader(System.in));
		
		System.out.print("Enter loopback port for testing: ");
		String portName = cli.readLine();
		
		System.out.println("--- " + portName + " ---");
		
		System.out.println("create SerialPort classes.");
		SerialPort port = new SerialPort(portName);
		SerialPort port2 = new SerialPort(portName);

		System.out.println("check is open");
		if (port.isOpen()) {
			System.err.println("failed, should return false!");
			return -1;
		}
		
		System.out.println("open port");
		if (!port.openPort()) {
			System.err.println("failed, either port not available or lib not functional!");
			return -1;
		}
		
		System.out.println("open port again");
		if (port.openPort()) {
			System.err.println("failed, should return false since port already open!");
			return -1;
		}

		System.out.println("open port again with other instance");
		if (port2.openPort()) {
			System.err.println("failed, should return false since different port instance!");
			return -1;
		}
		
		System.out.println("check is open");
		if (!port.isOpen()) {
			System.err.println("failed, should return true!");
			return -1;
		}
		
		System.out.println("try open port again");
		if (port.openPort()) {
			System.err.println("failed, this should not work!");
			return -1;
		}
		
		System.out.println("configure port baud");
		port.setBaud(9600);

		System.out.println("read back baud");
		int baud = port.getBaud();
		if (baud != 9600) {
			System.err.println("failed, returned baud " + baud + " does not match set 9600!");
			return -1;
		}
		
		System.out.println("configure port timeouts");
		port.setTimeouts(500, 500);
		
		System.out.println("set null configuration");
		if (port.setConfig(null)) {
			System.err.println("failed, should return false!");
			return -1;
		}

		System.out.println("set null field configuration");
		SerialPortConfiguration nconfig = new SerialPortConfiguration();
		nconfig.flowControl = null;
		nconfig.parity = null;
		nconfig.stopBits = null;
		if (port.setConfig(nconfig)) {
			System.err.println("failed, should return false!");
			return -1;
		}
		
		System.out.println("set new configuration");
		SerialPortConfiguration config1 = new SerialPortConfiguration();
		config1.baudRate = 4800;
		config1.dataBits = 8;
		config1.parity = SerialPortParity.PARITY_EVEN;
		config1.stopBits = SerialPortStopBits.STOPB_TWO;
		config1.flowControl = SerialPortFlowControl.FLOW_XON_XOFF;
		port.setConfig(config1);
		
		System.out.println("read configuration");
		SerialPortConfiguration config2 = port.getConfig();
		if (!config1.equals(config2)) {
			System.err.println("failed, configuration does not match set one!");
			return -1;
		}
		
		System.out.println("reset config to default");
		port.setConfig(SerialPort.DEFAULT_PORT_CONFIGURATION);
		
		System.out.println("write test data");
		String data = "1234567890987654321\nABCDEFGHIJKLMNOPQRSTUVWXYZ\n";
		port.writeString(data);
		Thread.sleep(1000);
		
		System.out.println("read back data");
		String readBack = port.readString();
		if (readBack == null || !readBack.equals(data)) {
			System.out.println("missmatch:\nsend:\t" + data + "\nread:\t" + readBack);
		}
		
		System.out.println("send test data");
		port.writeString(data);
		Thread.sleep(1000);
		
		System.out.println("read back consecutive data");
		String readBack2 = port.readString();
		if (readBack2 == null || !readBack2.equals(data)) {
			System.out.println("missmatch:\nsend:\t" + data + "\nread:\t" + readBack2);
		}
		
		System.out.println("make streams");
		InputStream in = port.getInputStream();
		OutputStream out = port.getOutputStream();

		System.out.println("send test data");
		out.write(data.getBytes());
		Thread.sleep(1000);
		
		System.out.println("read back data");
		String readBack3 = new String(in.readNBytes(data.length()));
		if (readBack3 == null || !readBack3.equals(data)) {
			System.out.println("missmatch:\nsend:\t" + data + "\nread:\t" + readBack3);
		}
		
		System.out.println("close port");
		port.closePort();
		if (port.isOpen()) {
			System.err.println("failed, this should be close the port!");
			return -1;
		}
		
		System.out.println("try read port");
		port.readData();
		
		System.out.println("--- COMPLETED, NO ERRORS ---");
		return 0;
		
	}
	
}
