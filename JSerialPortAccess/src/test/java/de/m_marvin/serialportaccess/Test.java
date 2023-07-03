package de.m_marvin.serialportaccess;

public class Test {
	
	public static void main(String... args) throws InterruptedException {
		
		Test test = new Test();
		test.run();
		
	}
	
	public void run() throws InterruptedException {
		
		SerialPort port;
		
		port = new SerialPort("/dev/ttyS3");
		if (!port.openPort()) System.err.println("Failed!");
		
		port.setBaud(9600);
		port.setTimeouts(500, 500);
		
		System.out.println(port.isOpen());
		
		int i = 0;
		while (i++ < 5)  {

			port.writeString("config dump 0 \r");
			System.out.println(port.readStringBurst());
			System.out.println("-----");
			
			Thread.sleep(1000);
		}
		
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
