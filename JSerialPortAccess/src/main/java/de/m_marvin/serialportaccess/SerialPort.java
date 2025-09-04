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
		public char xonChar;
		public char xoffChar;

		@Override
		public boolean equals(Object obj) {
			if (obj instanceof SerialPortConfiguration other) {
				return	this.baudRate == other.baudRate &&
						this.dataBits == other.dataBits &&
						Objects.equals(this.stopBits, other.stopBits) &&
						Objects.equals(this.parity, other.parity) &&
						Objects.equals(this.flowControl, other.flowControl) &&
						this.xonChar == other.xonChar &&
						this.xoffChar == other.xoffChar;
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
	private static native String n_readDataS(long handle, int bufferCapacity, boolean wait);
	private static native byte[] n_readDataB(long handle, int bufferCapacity, boolean wait);
	private static native int n_writeDataS(long handle, String data, boolean wait);
	private static native int n_writeDataB(long handle, byte[] data, boolean wait);
	private static native boolean n_getPortState(long handle, boolean[] state);
	private static native boolean n_setManualPortState(long handle, boolean dtrState, boolean rtsState);
	private static native boolean n_waitForEvents(long handle, boolean[] events);
	private static native void n_abortWait(long handle);
	
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
	
	/**
	 * Sets the read write timeouts for the port.
	 * An readTimeout of zero means no timeout. (instant return if no data is available)
	 * An readTimeout of less than zero causes the read method to block until at least one character has arrived.
	 * An writeTimeout of zero or less has means block until everything is written.
	 * The readTimeoutInterval defines an additional timeout that is appended to each received byte.
	 * The port has to be open for this to work.
	 * @param readTimeout The read timeout, if the requested amount of data is not received within this time, it returns with what it has (might be zero)
	 * @param readTimeoutInterval An additional timeout that is waited for after each received byte, only has an effect if readTimeout >= 0.
	 * @param writeTimeout The write timeout, if the supplied data could not be written within this time, it returns with the amount of data that could be written (might be zero)
	 * @return true if the timeouts where set, false if an error occurred
	 */
	public boolean setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) {
		return n_setTimeouts(this.handle, readTimeout, readTimeoutInterval, writeTimeout);
	}
	
	/**
	 * Sets the read write timeouts for the port.
	 * An readTimeout of zero means no timeout. (instant return if no data is available)
	 * An readTimeout of less than zero causes the read method to block until at least one character has arrived.
	 * An writeTimeout of zero or less has means block until everything is written.
	 * The readTimeoutInterval defines an additional timeout that is appended to each received byte.
	 * The port has to be open for this to work.
	 * @param timeouts The read timeouts: {readTimeout, readTimeoutInterval, writeTimeout}
	 * @return true if the timeouts where set, false if an error occurred
	 */
	public boolean setTimeouts(int[] timeouts) {
		Objects.requireNonNull(timeouts, "timeout array must not be null");
		if (timeouts.length != 3)
			throw new IllegalArgumentException("timeout array must be of length 3");
		return n_setTimeouts(this.handle, timeouts[0], timeouts[1], timeouts[2]);
	}
	
	/**
	 * Returns the current timeouts of the serial port.
	 * The port has to be open for this to work.
	 * @param timeouts The read timeouts: {readTimeout, readTimeoutInterval, writeTimeout}
	 * @return true if the port was open and timeouts where read, false if the port was closed
	 */
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
	 * @param wait If the function should block until the operation finished
	 * @return The String value of the bytest or null if nothing could be read
	 */
	public String readString(int bufferSize) {
		return n_readDataS(handle, bufferSize, true);
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
		return n_readDataB(handle, bufferSize, true);
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
	 * @return The number of bytes successfully written, -1 if the operation has not finished yet, less than -1 if an error occurred
	 */
	public int writeString(String data) {
		return n_writeDataS(handle, data, true);
	}
	
	/**
	 * Writes the bytes to the serial port.
	 * @param data The bytes to be written
	 * @return The number of bytes successfully written, -1 if the operation has not finished yet, less than -1 if an error occurred
	 */
	public int writeData(byte[] data) {
		return n_writeDataB(handle, data, true);
	}
	
	/**
	 * Reads the logical values of the serial port pins DSR and CTS.
	 * @param state The state of the pins { DSR, CTS }
	 * @return true if the state was read successfully, false otherwise
	 */
	public boolean getPortState(boolean[] state) {
		Objects.requireNonNull(state, "state array must not be null");
		if (state.length != 2)
			throw new IllegalArgumentException("state array must be of length 3");
		return n_getPortState(handle, state);
	}

	/**
	 * Assigns the current pin output states of the serial port.
	 * Only effective if the signals are not controlled by hardware flow control.
	 * @param dtrState The state of the DTR pin
	 * @param rtsState The state of the RTS pin
	 * @return true if the state was assigned successfully, false otherwise
	 */
	public boolean setManualPortState(boolean dtrState, boolean rtsState) {
		return n_setManualPortState(handle, dtrState, dtrState);
	}
	
	/**
	 * Assigns the logical values of the serial port pins DTR and RTS.
	 * @param state The state of the pins { DTR, RTS }
	 * @return true if the state was assigned successfully, false otherwise
	 */
	public boolean setManualPortState(boolean[] state) {
		Objects.requireNonNull(state, "state array must not be null");
		if (state.length != 2)
			throw new IllegalArgumentException("state array must be of length 3");
		return setManualPortState(state[0], state[1]);
	}
	
	/**
	 * Waits for the requested events.
	 * The arguments are input and outputs at the same time.
	 * The function block until an event occurred, for which the argument was set to true.
	 * The before returning, the function overrides the values in the arguments to signal which events occurred.
	 *
	 * NOTE:
	 * If wait is false, the function will always return immediately, but all event flags set to false until an event occurred.
	 * The wait operation will in this case continue until an event occurred and the event was read using this method.
	 * The wait operation is canceled and replaced by an new wait operation if this function is called with different event-arguments than the pending event.
	 * NOTE on dataTransmitted:
	 * This event behaves different on linux and windows, but in general, if it is signaled, more data can be written
	 * On windows, it is signaled once if the transmit buffer runs out of data.
	 * On linux, it continuously fires until the transmit buffer is completely full.
	 * @param events The list of booleans for the events: [comStateChanged, dataReceived, dataTransmitted]
	 * @return false if the function returned because of an error, true otherwise
	 */
	public boolean waitForEvents(boolean[] events) {
		return n_waitForEvents(handle, events);
	}
	
	public void abortWait() {
		n_abortWait(handle);
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
