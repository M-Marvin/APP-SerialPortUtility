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
	
	private static native long n_createSerialPort(String portFile);
	private static native void n_disposeSerialPort(long handle);
	private static native boolean n_setBaud(long handle, int baud);
	private static native int n_getBaud(long handle);
	private static native boolean n_setConfig(long handle, SerialPortConfiguration config);
	private static native boolean n_getConfig(long handle, SerialPortConfiguration config);
	private static native boolean n_setTimeouts(long handle, int readTimeout, int readTimeoutInterval, int writeTimeout);
	private static native boolean n_getTimeouts(long handle, int[] timeouts);
	private static native boolean n_openPort(long handle);
	private static native void n_closePort(long handle);
	private static native boolean n_isOpen(long handle);
	private static native String n_readDataS(long handle, int bufferCapacity);
	private static native byte[] n_readDataB(long handle, int bufferCapacity);
	private static native int n_writeDataS(long handle, String data);
	private static native int n_writeDataB(long handle, byte[] data);
	private static native boolean n_getRawPortState(long handle, boolean[] state);
	private static native boolean n_setRawPortState(long handle, boolean dtrState, boolean rtsState);
	private static native boolean n_getFlowControl(long handle, boolean[] state);
	private static native boolean n_setFlowControl(long handle, boolean state);
	
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
	
	/**
	 * Reads the logical values of the serial port pins DSR and CTS.
	 * @param state The state of the pins { DSR, CTS }
	 * @return true if the state was read successfully, false otherwise
	 */
	public boolean getRawPortState(boolean[] state) {
		Objects.requireNonNull(state, "state array must not be null");
		if (state.length != 2)
			throw new IllegalArgumentException("state array must be of length 3");
		return n_getRawPortState(handle, state);
	}

	/**
	 * Assigns the logical values of the serial port pins DTR and RTS.
	 * @param dtrState The state of the DTR pin
	 * @param rtsState The state of the RTS pin
	 * @return true if the state was assigned successfully, false otherwise
	 */
	public boolean setRawPortState(boolean dtrState, boolean rtsState) {
		return n_setRawPortState(handle, dtrState, dtrState);
	}
	
	/**
	 * Assigns the logical values of the serial port pins DTR and RTS.
	 * @param state The state of the pins { DTR, RTS }
	 * @return true if the state was assigned successfully, false otherwise
	 */
	public boolean setRawPortState(boolean[] state) {
		Objects.requireNonNull(state, "state array must not be null");
		if (state.length != 2)
			throw new IllegalArgumentException("state array must be of length 3");
		return setRawPortState(state[0], state[1]);
	}
	
	/**
	 * Reads the current hardware flow control state.
	 * @return true if the hardware signals that the other end is ready to receive data, false if it signals that no data can be received or if the operation to read the value failed
	 */
	public boolean getFlowControl() {
		boolean[] state = new boolean[1];
		if (!n_getFlowControl(handle, state)) return false;
		return state[0];
	}
	
	/**
	 * Assigns the current hardware flow control state.
	 * @param state true to signal that data can be received, false to signal that no data can be received
	 * @return true if the state was assigned successfully, false otherwise
	 */
	public boolean setFlowControl(boolean state) {
		return n_setFlowControl(handle, state);
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
