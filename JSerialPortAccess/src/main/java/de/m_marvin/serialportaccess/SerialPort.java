package de.m_marvin.serialportaccess;

import java.util.Objects;

public class SerialPort {
	
	static {
		NativeLoader.setTempLibFolder(System.getProperty("java.io.tmpdir") + "/jserialportaccess");
		NativeLoader.setLibLoadConfig("/libload_serialportaccess.cfg");
		NativeLoader.loadNative("serialportaccess");
	}
	
	public static enum SerialPortParity {
		PARITY_NONE(1),
		PARITY_ODD(2),
		PARITY_EVEN(3),
		PARITY_MARK(4),
		PARITY_SPACE(5),
		PARITY_UNDEFINED(0);
		
		private int value;
		
		private SerialPortParity(int value) {
			this.value = value;
		}
		
		public int getValue() {
			return value;
		}
		
		public static SerialPortParity fromValue(int value) {
			for (var e : values())
				if (e.value == value) return e;
			return PARITY_UNDEFINED;
		}
		
	}

	public static enum SerialPortFlowControl {
		FLOW_NONE(1),
		FLOW_XON_XOFF(2),
		FLOW_RTS_CTS(3),
		FLOW_DSR_DTS(4),
		FLOW_UNDEFINED(0);
		
		private int value;
		
		private SerialPortFlowControl(int value) {
			this.value = value;
		}
		
		public int getValue() {
			return value;
		}
		
		public static SerialPortFlowControl fromValue(int value) {
			for (var e : values())
				if (e.value == value) return e;
			return FLOW_UNDEFINED;
		}
		
	}

	public static enum SerialPortStopBits {
		STOPB_ONE(1),
		STOPB_ONE_HALF(2),
		STOPB_TWO(3),
		STOPB_UNDEFINED(0);
		
		private int value;
		
		private SerialPortStopBits(int value) {
			this.value = value;
		}
		
		public int getValue() {
			return value;
		}
		
		public static SerialPortStopBits fromValue(int value) {
			for (var e : values())
				if (e.value == value) return e;
			return STOPB_UNDEFINED;
		}
		
	}
	
	public static class SerialPortConfiguration {
		
		public long baudRate = 9600;
		public byte dataBits = 8;
		public SerialPortStopBits stopBits = SerialPortStopBits.STOPB_ONE;
		public SerialPortParity parity = SerialPortParity.PARITY_NONE;
		public SerialPortFlowControl flowControl = SerialPortFlowControl.FLOW_NONE;

		@Override
		public boolean equals(Object obj) {
			if (obj instanceof SerialPortConfiguration other) {
				return	this.baudRate == other.baudRate &&
						this.dataBits == other.dataBits &&
						Objects.equals(this.stopBits, other.stopBits) &&
						Objects.equals(this.parity, other.parity) &&
						Objects.equals(this.flowControl, other.flowControl);
			}
			return false;
		}
		
		@Override
		public int hashCode() {
			return Objects.hash(this.baudRate, this.dataBits, this.stopBits, this.parity, this.flowControl);
		}
		
	}
	
	public static final int DEFAULT_BUFFER_SIZE = 256;
	public static final long DEFAULT_CONSECUTIVE_LOOP_DELAY = 100;
	public static final long DEFAULT_CONSECUTIVE_RECEPTION_TIMEOUT = 1000;
	public static final SerialPortConfiguration DEFAULT_PORT_CONFIGURATION = new SerialPortConfiguration();
	
	protected static native long n_createSerialPort(String portFile);
	protected static native void n_disposeSerialPort(long handle);
	protected static native boolean n_setBaud(long handle, int baud);
	protected static native int n_getBaud(long handle);
	protected static native boolean n_setConfig(long handle, SerialPortConfiguration config);
	protected static native boolean n_getConfig(long handle, SerialPortConfiguration config);
	protected static native boolean n_setTimeouts(long handle, int readTimeout, int readTimeoutInterval, int writeTimeout);
	protected static native boolean n_getTimeouts(long handle, int[] timeouts);
	protected static native boolean n_openPort(long handle);
	protected static native void n_closePort(long handle);
	protected static native boolean n_isOpen(long handle);
	protected static native String n_readDataS(long handle, int bufferCapacity);
	protected static native byte[] n_readDataB(long handle, int bufferCapacity);
	protected static native String n_readDataConsecutiveS(long handle, int bufferCapacity, long consecutiveDelay, long receptionWaitTimeout);
	protected static native byte[] n_readDataConsecutiveB(long handle, int bufferCapacity, long consecutiveDelay, long receptionWaitTimeout);
	protected static native int n_writeDataS(long handle, String data);
	protected static native int n_writeDataB(long handle, byte[] data);
	
	private final long handle;
	private final String portName;
	
	public SerialPort(String portFile) {
		this.portName = portFile;
		this.handle = n_createSerialPort(portFile);
	}
	
	@Override
	public int hashCode() {
		return Long.hashCode(this.handle);
	}
	
	@Override
	public boolean equals(Object obj) {
		if (obj instanceof SerialPort other) {
			return other.handle == this.handle;
		}
		return false;
	}
	
	@Override
	public String toString() {
		return this.portName;
	}
	
	public void dispose() {
		n_disposeSerialPort(this.handle);
	}
	
	public boolean setBaud(int baud) {
		return n_setBaud(handle, baud);
	}
	
	public boolean setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) {
		return n_setTimeouts(this.handle, readTimeout, readTimeoutInterval, writeTimeout);
	}
	
	public boolean setTimeouts(int[] timeouts) {
		Objects.requireNonNull(timeouts, "timeout array must not be null");
		if (timeouts.length != 3)
			throw new IllegalArgumentException("timeout array must be of length 3");
		return n_setTimeouts(this.handle, timeouts[0], timeouts[1], timeouts[2]);
	}
	
	public boolean getTimeouts(int[] timeouts) {
		Objects.requireNonNull(timeouts, "timeout array must not be null");
		if (timeouts.length != 3)
			throw new IllegalArgumentException("timeout array must be of length 3");
		return n_getTimeouts(this.handle, timeouts);
	}
	
	public int getReadTimeout() {
		int[] timeouts = new int[3];
		getTimeouts(timeouts);
		return timeouts[0];
	}

	public int getReadTimeoutInterval() {
		int[] timeouts = new int[3];
		getTimeouts(timeouts);
		return timeouts[1];
	}

	public int getWriteTimeout() {
		int[] timeouts = new int[3];
		getTimeouts(timeouts);
		return timeouts[2];
	}
	
	public boolean openPort() {
		return n_openPort(handle);
	}
	
	public void closePort() {
		n_closePort(handle);
	}
	
	public boolean isOpen() {
		return n_isOpen(handle);
	}
	
	public int getBaud() {
		return n_getBaud(handle);
	}
	
	public boolean setConfig(SerialPortConfiguration config) {
		return n_setConfig(handle, config);
	}
	
	public SerialPortConfiguration getConfig() {
		SerialPortConfiguration config = new SerialPortConfiguration();
		if (!n_getConfig(handle, config)) return null;
		return config;
	}
	
	/**
	 * Reads up to bufferSize bytes, can also return with zero read bytes.
	 * Returns the read bytes as string, or null if no bytes could be read.
	 * @param bufferSize The max bytes that can be read in on go
	 * @return The String value of the bytest or null if nothing could be read
	 */
	public String readString(int bufferSize) {
		return n_readDataS(handle, bufferSize);
	}
	
	/**
	 * Reads up to {@link SerialPort.DEFAULT_BUFFER_SIZE} bytes, can also return with zero read bytes.
	 * Returns the read bytes as string, or null if no bytes could be read
	 * @return The String value of the bytes or null if nothing could be read
	 */
	public String readString() {
		return readString(DEFAULT_BUFFER_SIZE);
	}
	
	/**
	 * Reads up to bufferSize bytes, can also return with zero read bytes.
	 * @param bufferSize The max bytes that can be read in on go
	 * @return An byte array containing the read bytes or null if nothing could be read (length of array = number of bytes read)
	 */
	public byte[] readData(int bufferSize) {
		return n_readDataB(handle, bufferSize);
	}

	/**
	 * Reads up to {@link SerialPort.DEFAULT_BUFFER_SIZE} bytes, can also return with zero read bytes.
	 * @return An byte array containing the read bytes or null if nothing could be read (length of array = number of bytes read)
	 */
	public byte[] readData() {
		return readData(DEFAULT_BUFFER_SIZE);
	}
	
	/**
	 * Waits until at least on byte got received and continues to read until an error occurred, the buffer is full or no more consecutive bytes could be read.
	 * Two received bytes are considered consecutive if the delay of reception between both is smaller than the receptionLoopDelay.
	 * If after receptionWaitTimeout milliseconds no bytes could be received, null is returned.
	 * Returns the read bytes as string, or null if no bytes could be read.
	 * @param bufferSize The max bytes that can be read in on go
	 * @param consecutiveDelay The number of milliseconds to wait between each read operation
	 * @param receptionWaitTimeout The number of milliseconds to wait for the first byte
	 * @return The String value of the bytes or null if nothing could be read
	 */
	public String readStringConsecutive(int bufferSize, long consecutiveDelay, long receptionWaitTimeout) {
		return n_readDataConsecutiveS(handle, bufferSize, consecutiveDelay, receptionWaitTimeout);
	}
	
	/**
	 * Waits until at least on byte got received and continues to read until an error occurred, the buffer is full or no more consecutive bytes could be read.
	 * Two received bytes are considered consecutive if the delay of reception between both is smaller than the {@link SerialPort.DEFAULT_LOOP_DELAY}.
	 * If after {@link SerialPort.DEFAULT_CONSECUTIVE_RECEPTION_TIMEOUT} milliseconds no bytes could be received, null is returned.
	 * Returns the read bytes as string, or null if no bytes could be read.
	 * @return The String value of the bytes or null if nothing could be read
	 */
	public String readStringConsecutive() {
		return readStringConsecutive(DEFAULT_BUFFER_SIZE, DEFAULT_CONSECUTIVE_LOOP_DELAY, DEFAULT_CONSECUTIVE_RECEPTION_TIMEOUT);
	}

	/**
	 * Waits until at least on byte got received and continues to read until an error occurred, the buffer is full or no more consecutive bytes could be read.
	 * Two received bytes are considered consecutive if the delay of reception between both is smaller than the receptionLoopDelay.
	 * If after receptionWaitTimeout milliseconds no bytes could be received, null is returned.
	 * @param bufferSize The max bytes that can be read in on go
	 * @param receptionLoopDelay The number of milliseconds to wait between each read operation
	 * @param receptionWaitTimeout The number of milliseconds to wait for the first byte
	 * @return An byte array containing the read bytes or null if nothing could be read (length of array = number of bytes read)
	 */
	public byte[] readDataConsecutive(int bufferSize, long consecutiveDelay, long receptionWaitTimeout) {
		return n_readDataConsecutiveB(handle, bufferSize, consecutiveDelay, receptionWaitTimeout);
	}
	
	/**
	 * Waits until at least on byte got received and continues to read until an error occurred, the buffer is full or no more consecutive bytes could be read.
	 * Two received bytes are considered consecutive if the delay of reception between both is smaller than the {@link SerialPort.DEFAULT_LOOP_DELAY}.
	 * If after {@link SerialPort.DEFAULT_CONSECUTIVE_RECEPTION_TIMEOUT} milliseconds no bytes could be received, null is returned.
	 * @return An byte array containing the read bytes or null if nothing could be read (length of array = number of bytes read)
	 */
	public byte[] readDataConsecutive() {
		return readDataConsecutive(DEFAULT_BUFFER_SIZE, DEFAULT_CONSECUTIVE_LOOP_DELAY, DEFAULT_CONSECUTIVE_RECEPTION_TIMEOUT);
	}
	
	/**
	 * Writes the string as bytes to the serial port.
	 * @param data The string to be written
	 * @return The number of bytes successfully written (can be smaller than the length of the string if an error occurred!)
	 */
	public int writeString(String data) {
		int writtenBytes = n_writeDataS(handle, data);
		if (writtenBytes == 0 && !data.isEmpty()) closePort(); // Connection has been lost, close port to make isOpen() respond correctly
		return writtenBytes;
	}
	
	/**
	 * Writes the bytes to the serial port.
	 * @param data The bytes to be written
	 * @return The number of bytes successfully written (can be smaller than the length of the data array if an error occurred!)
	 */
	public int writeData(byte[] data) {
		int writtenBytes = n_writeDataB(handle, data);
		if (writtenBytes == 0 && data.length > 0) closePort(); // Connection has been lost, close port to make isOpen() respond correctly
		return writtenBytes;
	}
	
	public SerialPortInputStream getInputStream(int bufferSize) {
		return new SerialPortInputStream(this, bufferSize);
	}
	
	public SerialPortInputStream getInputStream() {
		return getInputStream(DEFAULT_BUFFER_SIZE);
	}
	
	public SerialPortOutputStream getOutputStream(int bufferSize) {
		return new SerialPortOutputStream(this, bufferSize);
	}
	
	public SerialPortOutputStream getOutputStream() {
		return getOutputStream(DEFAULT_BUFFER_SIZE);
	}
	
}
