package de.m_marvin.serialportaccess.soe;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.Socket;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

public class SerialOverEthernetSocket implements Closeable{
	
	public static enum OpCode {
		OPC_ERROR(0x0),
		OPC_OPEN(0x1),
		OPC_OPENED(0x2),
		OPC_CLOSE(0x3),
		OPC_CLOSED(0x4),
		OPC_STREAM(0x5),
		OPC_TX_CONFIRM(0x6),
		OPC_RX_CONFIRM(0x7);

		private final byte opc;
		
		private OpCode(int opc) {
			this.opc = (byte) opc;
		}
		
		public byte opc() {
			return opc;
		}
		
		public static OpCode valueOf(int id) {
			for (OpCode e : values())
				if (e.opc() == id) return e;
			return null;
		}
		
	}
	
	protected final Socket socket;
	protected final List<String> openPorts = new ArrayList<String>();
	protected final Map<String, CompletableFuture<Boolean>> pendingOpen = new HashMap<String, CompletableFuture<Boolean>>();
	protected final Map<String, CompletableFuture<Void>> pendingClose = new HashMap<String, CompletableFuture<Void>>();
	protected final Map<String, List<IOException>> streamErrors = new HashMap<String, List<IOException>>();
	protected final Map<String, Map<Integer, byte[]>> pendingTransmissions = new HashMap<String, Map<Integer,byte[]>>();
	protected long timeout = 1;
	protected TimeUnit timeoutUnit = TimeUnit.SECONDS;
	protected final Thread rxThread;
	
 	public SerialOverEthernetSocket(Socket socket) {
		this.socket = socket;
		this.rxThread = new Thread(this::handleServerReception, "SOE-RX@" + this.socket.getInetAddress().toString());
		this.rxThread.setDaemon(true);
		this.rxThread.start();
	}

	@Override
	public void close() throws IOException {
		this.socket.close();
		try {
			this.rxThread.join();
		} catch (InterruptedException e) {}
	}
	
	protected void handleServerReception() {
		try {
			InputStream in = this.socket.getInputStream();
			rxl: while (true) {
				
				// Read frame start byte
				int frameStart = in.read();
				OpCode opc = OpCode.valueOf(frameStart & 0x7);
				int len = (frameStart & 0xF7) >> 3;
				
				// Check for valid
				if (opc == null) {
					System.out.println("invalid op code");
					break;
				}
				
				// Read additional length field
				if (len > 30) {
					len = 0;
					len |= in.read() << 24;
					len |= in.read() << 16;
					len |= in.read() << 8;
					len |= in.read() << 0;
				}
				
				System.out.println("received: " + opc + " " + len);
				
				// Read payload and prepare for reading
				byte[] payload = in.readNBytes(len);
				DataInputStream reader = new DataInputStream(new ByteArrayInputStream(payload));
				
				switch (opc) {
				case OPC_ERROR: {
					
					// Read optional port name and message from payload
					String portName = reader.available() > 0 ? reader.readUTF() : null;
					String message = reader.available() > 0 ? reader.readUTF() : portName;
					if (message == portName) portName = null;
					
					if (portName != null) {
						
						// Notify pending open handshake about error
						if (this.pendingOpen.containsKey(portName)) {
							this.pendingOpen.remove(portName).complete(false);
							break;
						}
						
						// Notify already open port about error while STREAM phase
						if (this.openPorts.contains(portName)) {
							if (!this.streamErrors.containsKey(portName))
								this.streamErrors.put(portName, new ArrayList<IOException>());
							this.streamErrors.get(portName).add(new IOException(String.format("%s: %s", portName, message)));
							break;
						}
						
					}
					
					System.out.println(String.format("error: %s", message));
					break;
				}
				case OPC_OPENED: {
					
					// Read port name
					String portName = reader.readUTF();
					
					// Complete open port handshake
					if (this.pendingOpen.containsKey(portName)) {
						this.pendingOpen.remove(portName).complete(true);
						this.openPorts.add(portName);
						break;
					}
					
					System.out.println("received OPENED for unknown port: " + portName);
					break;
				}
				case OPC_CLOSED: {
					
					// Read port name
					String portName = reader.readUTF();
					
					// Complete close port handshake
					if (this.pendingClose.containsKey(portName)) {
						this.pendingClose.remove(portName).complete(null);
						this.openPorts.remove(portName);
						this.streamErrors.remove(portName);
						break;
					}
					
					System.out.println("received CLOSED for unknown port: " + portName);
					break;
				}
				case OPC_TX_CONFIRM: {
					
					String portName = reader.readUTF();
					int txid = reader.readInt();
					
					System.out.println("TX CONFIRM: " + portName +" " + txid);
					
					System.out.println("received invalid TX_CONFIRM!");
					break;
				}
				default: 
					System.out.println("received invalid opc");
					break rxl;
				}
				
			}
		} catch (IOException e) {
			
		} finally {
			
		}
	}
	
	protected void sendFrame(OpCode opc, byte[] payload) throws IOException {

		ByteArrayOutputStream buffer = new ByteArrayOutputStream();
		
		int len = payload.length;
		
		buffer.write(opc.opc() | ( len > 30 ? 31 << 3 : (len & 0x1F) << 3));
		if (len > 30) {
			buffer.write((len >> 24) & 0xFF);
			buffer.write((len >> 16) & 0xFF);
			buffer.write((len >> 8) & 0xFF);
			buffer.write((len >> 0) & 0xFF);
		}
		
		buffer.writeBytes(payload);
		
		socket.getOutputStream().write(buffer.toByteArray());
		
	}

	protected void sendFrameOpen(String port, int baud) throws IOException {

		ByteArrayOutputStream buffer = new ByteArrayOutputStream();
		DataOutputStream writer = new DataOutputStream(buffer);
		writer.writeInt(baud);
		writer.writeUTF(port);
		
		sendFrame(OpCode.OPC_OPEN, buffer.toByteArray());
		
	}

	protected void sendFrameClose(String port) throws IOException {

		ByteArrayOutputStream buffer = new ByteArrayOutputStream();
		DataOutputStream writer = new DataOutputStream(buffer);
		writer.writeUTF(port);
		
		sendFrame(OpCode.OPC_CLOSE, buffer.toByteArray());
		
	}
	
	protected void sendFrameStream(String port, int txid, byte[] payload) throws IOException {

		ByteArrayOutputStream buffer = new ByteArrayOutputStream();
		DataOutputStream writer = new DataOutputStream(buffer);
		writer.writeUTF(port);
		writer.writeInt(txid);
		writer.write(payload);
		
		sendFrame(OpCode.OPC_STREAM, buffer.toByteArray());
		
	}
	
	public CompletableFuture<Boolean> openPort(String name, int baud) throws IOException {
		if (isOpen(name)) return CompletableFuture.completedFuture(true);
		try {
			sendFrameOpen(name, baud);
			CompletableFuture<Boolean> completable = new CompletableFuture<Boolean>();
			this.pendingOpen.put(name, completable);
			return completable.orTimeout(this.timeout, this.timeoutUnit);
		} catch (IOException e) {
			throw new IOException("failed to send OPEN request!", e);
		}
	}
	
	public boolean isOpen(String port) {
		return this.openPorts.contains(port);
	}
	
	public List<String> getOpenPorts() {
		return openPorts;
	}
	
	public CompletableFuture<Void> closePort(String name) throws IOException {
		if (!isOpen(name)) return CompletableFuture.completedFuture(null);
		try {
			sendFrameClose(name);
			CompletableFuture<Void> completable = new CompletableFuture<Void>();
			this.pendingClose.put(name, completable);
			return completable.orTimeout(this.timeout, this.timeoutUnit);
		} catch (IOException e) {
			throw new IOException("failed to send CLOSE request!", e);
		}
	}
	
	
	
}
