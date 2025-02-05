package test;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.net.UnknownHostException;

import de.m_marvin.serialportaccess.soe.SerialOverEthernetSocket;

public class SOETest {
	
	public static void main(String[] args) throws UnknownHostException, IOException, InterruptedException {
		
		SerialOverEthernetSocket sock = new SerialOverEthernetSocket(new Socket(InetAddress.getByName("localhost"), 26));
		
		boolean open = sock.openPort("COM5", 9600).exceptionally(e -> {
			System.out.println("TIMEOUT");
			return null;
		}).join();

		if (!open) {
			System.err.println("ERROR");
			return;
		}
		System.out.println("port open");
		
		Thread.sleep(1000);
		
		sock.closePort("COM5").exceptionally(e -> {
			System.out.println("TIMEOUT");
			return null;
		}).join();
		System.out.println("closed");
		
		Thread.sleep(1000);
		
		sock.close();
		
	}
	
}
