package de.m_marvin.serialportaccess;

public class SerialPort {
	
	static {
		NativeLoader.setTempLibFolder(System.getProperty("java.io.tmpdir") + "/jserialportaccess");
		NativeLoader.setLibLoadConfig("/libload_serialportaccess.cfg");
		NativeLoader.loadNative("serialportaccess");
	}
	
	public static final int DEFAULT_BUFFER_SIZE = 256;
	public static final long DEFAULT_LOOP_DELAY = 100;
	
	protected static native long n_createSerialPort(String portFile);
	protected static native void n_disposeSerialPort(long handle);
	protected static native void n_setBaud(long handle, int baud);
	protected static native int n_getBaud(long handle);
	protected static native void n_setTimeouts(long handle, int readTimeout, int writeTimeout);
	protected static native boolean n_openPort(long handle);
	protected static native void n_closePort(long handle);
	protected static native boolean n_isOpen(long handle);
	protected static native String n_readDataS(long handle, int bufferCapacity);
	protected static native byte[] n_readDataB(long handle, int bufferCapacity);
	protected static native String n_readDataBurstS(long handle, int bufferCapacity, long receptionLoopDelay);
	protected static native byte[] n_readDataBurstB(long handle, int bufferCapacity, long receptionLoopDelay);
	protected static native int n_writeDataS(long handle, String data);
	protected static native int n_writeDataB(long handle, byte[] data);
	
	private final long handle;
	
	public SerialPort(String portFile) {
		this.handle = n_createSerialPort(portFile);
	}
	
	public void dispose() {
		n_disposeSerialPort(this.handle);
	}
	
	public void setBaud(int baud) {
		n_setBaud(handle, baud);
	}
	
	public void setTimeouts(int readTimeout, int writeTimeout) {
		n_setTimeouts(this.handle, readTimeout, writeTimeout);
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
	
	public String readString(int bufferSize) {
		return n_readDataS(handle, bufferSize);
	}
	
	public String readString() {
		return readString(DEFAULT_BUFFER_SIZE);
	}
	
	public byte[] readData(int bufferSize) {
		return n_readDataB(handle, bufferSize);
	}

	public byte[] readData() {
		return readData(DEFAULT_BUFFER_SIZE);
	}
	
	public String readStringBurst(int bufferSize, long receptionLoopDelay) {
		return n_readDataBurstS(handle, bufferSize, receptionLoopDelay);
	}
	
	public String readStringBurst() {
		return readStringBurst(DEFAULT_BUFFER_SIZE, DEFAULT_LOOP_DELAY);
	}

	public byte[] readDataBurst(int bufferSize, long receptionLoopDelay) {
		return n_readDataBurstB(handle, bufferSize, receptionLoopDelay);
	}
	
	public byte[] readDataBurst() {
		return readDataBurst(DEFAULT_BUFFER_SIZE, DEFAULT_LOOP_DELAY);
	}
	
	public int writeString(String data) {
		int writtenBytes = n_writeDataS(handle, data);
		if (writtenBytes == 0 && !data.isEmpty()) closePort(); // Connection has been lost, close port to make isOpen() respond correctly
		return writtenBytes;
	}
	
	public int writeData(byte[] data) {
		int writtenBytes = n_writeDataB(handle, data);
		if (writtenBytes == 0 && data.length > 0) closePort(); // Connection has been lost, close port to make isOpen() respond correctly
		return writtenBytes;
	}
	
}
