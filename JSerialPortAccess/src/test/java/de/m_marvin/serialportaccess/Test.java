package de.m_marvin.serialportaccess;

public class Test {
	
	public static void main(String... args) throws InterruptedException {
		
		Test test = new Test();
		test.run();
		
	}
	
	public void run() throws InterruptedException {
		
		SerialPort port;
		
		port = new SerialPort("COM5");
		if (!port.openPort()) {
			System.err.println("Failed to open port, following commands should immediatly return error-values!");
		}
		
		port.setBaud(9600);
		port.setTimeouts(500, 500);
		
		// TODO crash when no connection to port ?
		int transmitted = port.writeData(new byte[] {'A', 'a', 'b'});
		System.out.println(port.isOpen());
		byte[] received = port.readDataConsecutive(256, 100, 1000);
		System.out.println("Transmitted " + transmitted);
		System.out.println("Received " + received.length);
		
		for (int i = 0; i < received.length; i ++) {
			System.out.println((char) received[i]);
		}
		
		port.getBaud();
		
		System.out.println("IS PORT OPEN: " + port.isOpen());
		
		
		System.out.println("TEST CONSECUTIVE TIMEOUT: 2 Seconds");
		System.out.println("READ RETURNED: " + port.readStringConsecutive(256, 100, 2000));
		System.out.println("TIMED OUT");
		
//		int i = 0;
//		while (i++ < 100)  {
//
//			int written = port.writeString("config dump 0 \r");
//			System.out.println(port.readString());
//			System.out.println("-----");
//			
//			System.out.println(port.isOpen() + " " + written);
//			
//			Thread.sleep(100);
//		}
		
//		port.writeString("control stop 0\r");
//		System.out.println(port.readStringBurst());
//		//Thread.sleep(4000);
//		port.writeString("control down 0\r");
//		System.out.println(port.readStringBurst());
//		//Thread.sleep(4000);
//		port.writeString("control stop 0\r");
//		System.out.println(port.readStringBurst());
		
		port.closePort();
		port.dispose();
		System.out.println("END");
		
	}
	
}
