package de.m_marvin.serialportaccess;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Arrays;

public class SerialPortOutputStream extends OutputStream {

	private final SerialPort serialPort;
	private byte[] buffer;
	private int bufferPtr;
	
	public SerialPortOutputStream(SerialPort port, int bufferSize) {
		this.serialPort = port;
		this.buffer = new byte[bufferSize];
	}
	
	public SerialPort getSerialPort() {
		return serialPort;
	}
	
	@Override
	public void write(int b) throws IOException {
		if (this.bufferPtr == this.buffer.length) flush();
		this.buffer[this.bufferPtr++] = (byte) b;
	}
	
	@Override
	public void write(byte[] b, int off, int len) throws IOException {
		if (this.bufferPtr != 0 && b != this.buffer) {
			flush();
		}
		if (!this.serialPort.isOpen()) throw new IOException("lost connection on serial port " + this.serialPort.toString());
		int written = this.serialPort.writeData(Arrays.copyOfRange(b, off, off + len));
		if (written != len) throw new IOException("failed to write all bytes to the serial port " + this.serialPort.toString());
	}
	
	@Override
	public void flush() throws IOException {
		if (this.bufferPtr == 0) return;
		write(this.buffer, 0, this.bufferPtr);
		this.bufferPtr = 0;
	}
	
	@Override
	public void close() throws IOException {
		this.serialPort.closePort();
	}
}
