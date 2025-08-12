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
		if (this.buffer == null || this.bufferPtr == this.buffer.length) {
			if (!fillBuffer()) return -1;
		}
		return this.buffer[this.bufferPtr++];
	}
	
	@Override
	public int read(byte[] b, int off, int len) throws IOException {
		int read = 0;
		if (available() > 0) {
			read = Math.min(len, this.buffer.length - this.bufferPtr);
			System.arraycopy(this.buffer, this.bufferPtr, b, off, read);
			this.bufferPtr += read;
		}
		if (read == len) return read;
		int read2 = 0;
		do {
			if (fillBuffer()) {
				read2 = Math.min(len - read, this.buffer.length - this.bufferPtr);
				System.arraycopy(this.buffer, this.bufferPtr, b, off + read, read2);
				this.bufferPtr += read2;
			}
		} while (read + read2 == 0);
		return read + read2;
	}
	
	@Override
	public int available() throws IOException {
		if (this.buffer == null || this.bufferPtr == this.buffer.length) {
			if (!fillBuffer()) return 0;
		}
		return this.buffer.length - this.bufferPtr;
	}
	
	private boolean fillBuffer() throws IOException {
		while (this.buffer == null || this.bufferPtr == this.buffer.length) {
			if (!this.serialPort.isOpen()) throw new IOException("lost connection on serial port " + this.serialPort.toString());
			this.buffer = this.serialPort.readData(this.bufferSize);
			if (this.buffer != null && this.buffer.length > 0)
				this.bufferPtr = 0;
			else
				return false;
		}
		return true;
	}
	
	@Override
	public void close() throws IOException {
		this.serialPort.closePort();
	}
	
}
