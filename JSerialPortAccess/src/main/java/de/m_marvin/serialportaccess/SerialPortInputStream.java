package de.m_marvin.serialportaccess;

import java.io.IOException;
import java.io.InputStream;

public class SerialPortInputStream extends InputStream {
	
	private final SerialPort serialPort;
	private final int bufferSize;
	private byte[] buffer;
	private int bufferPtr;
	
	public SerialPortInputStream(SerialPort port, int bufferSize) {
		this.serialPort = port;
		this.bufferSize = bufferSize;
	}
	
	public SerialPort getSerialPort() {
		return serialPort;
	}
	
	@Override
	public int read() throws IOException {
		while (this.buffer == null || this.bufferPtr == this.buffer.length) {
			if (!this.serialPort.isOpen()) throw new IOException("lost connection on serial port " + this.serialPort.toString());
			this.buffer = this.serialPort.readData(this.bufferSize);
			if (this.buffer != null && this.buffer.length > 0)
				this.bufferPtr = 0;
			else
				try {
					Thread.sleep(100);
				} catch (InterruptedException e) {
					
				}
		}
		return this.buffer[this.bufferPtr++];
	}
	
	@Override
	public int available() throws IOException {
		return this.buffer.length - this.bufferPtr;
	}

	@Override
	public void close() throws IOException {
		this.serialPort.closePort();
		this.serialPort.dispose();
	}
	
}
