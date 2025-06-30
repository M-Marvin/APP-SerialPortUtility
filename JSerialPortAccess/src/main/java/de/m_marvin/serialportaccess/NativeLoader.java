package de.m_marvin.serialportaccess;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.Map;

/* NativeLoader v1.0 */
public class NativeLoader {
	
	private static String libLoadConfig = "/libload.cfg";
	private static String tempLibFolder = System.getProperty("java.io.tmpdir");
	private static Map<String, File> libMap = new HashMap<>();
	
	private NativeLoader() {
		throw new UnsupportedOperationException("can not construct instance of NativeLoader!");
	}
	
	public static void setLibLoadConfig(String fileLocation) {
		libLoadConfig = fileLocation;
	}
	
	public static void setTempLibFolder(String libFolder) {
		tempLibFolder = libFolder;
	}
	
	public static String getArchitectureName() {
		
		// TODO: Check the correct values for os.arch
		String arch = System.getProperty("os.arch").toLowerCase();
		if (arch.contains("amd64")) {
			arch = "amd_64";
		} else if (arch.contains("x86")) {
			arch = "amd_32";
		} else if (arch.contains("arm")) {
			arch = "arm_32";
		} else if (arch.contains("aarch64")) {
			arch = "arm_64";
		} else {
			throw new UnsatisfiedLinkError("Unknown platform architecture: " + arch);
		}
		
		String os = System.getProperty("os.name").toLowerCase();
		if (os.contains("windows")) {
			os = "win";
		} else if (os.contains("linux") || os.contains("sunos") || os.contains("freebsd")) {
			os = "lin";
		} else {
			throw new UnsatisfiedLinkError("Unknown platform operating system: " + arch);
		}
		
		return os + "_" + arch;
	}
	
	private static String getNativeForArchitecture(String nativeName) throws IOException {
		String arch = getArchitectureName();
		String nativeFile = null;
		
		try {
			BufferedReader reader = new BufferedReader(new InputStreamReader(NativeLoader.class.getResourceAsStream(libLoadConfig)));
			String line;
			fileread: while ((line = reader.readLine()) != null) {
				if (line.equals("[" + nativeName + "]")) {
					while ((line = reader.readLine()) != null) {
						String[] entry = line.split("=");
						if (entry.length == 2 && entry[0].contains(arch)) {
							nativeFile = entry[1];
							break fileread;
						}
					}
				}
			}
			reader.close();
		} catch (IOException | NullPointerException e) {
			throw new IOException("Could not read native libload file: " + libLoadConfig, e);
		}
		
		if (nativeFile == null) {
			throw new IOException("Missing native definition for platform: " + arch);
		}
		
		return nativeFile;
	}
	
	public static File extractNative(String nativeLocation, File targetLocation) throws IOException {
		try {
			InputStream archiveStream = NativeLoader.class.getResourceAsStream(nativeLocation);
			if (archiveStream == null) {
				throw new IOException("Missing native library in archive: " + nativeLocation);
			}
			
			ByteArrayOutputStream bufferStream = new ByteArrayOutputStream();
			archiveStream.transferTo(bufferStream);
			archiveStream.close();
			
			File targetNativeFile;
			try {
				MessageDigest md = MessageDigest.getInstance("MD5");
				String hash = HexFormat.of().formatHex(md.digest(bufferStream.toByteArray()));
				targetNativeFile = new File(targetLocation, hash + "/" + new File(nativeLocation).getName());
			} catch (NoSuchAlgorithmException e) {
				targetNativeFile = new File(targetLocation, new File(nativeLocation).getName());
			}
			
			targetNativeFile.getParentFile().mkdirs();
			OutputStream fileStream = new FileOutputStream(targetNativeFile);
			fileStream.write(bufferStream.toByteArray());
			fileStream.close();
			return targetNativeFile;
		} catch (IOException e) {
			throw new IOException("Unable to extract native library: " + nativeLocation, e);
		}
	}
	
	public static String getNative(String nativeName) {
		
		if (!libMap.containsKey(nativeName)) {
			try {
				String nativeLocation = getNativeForArchitecture(nativeName);
				if (nativeLocation == null) {
					System.err.println("NativeLoader: Unable to extract native " + nativeLocation + "!");
					return null;
				}
				File tempFilePath = extractNative(nativeLocation, new File(tempLibFolder));
				libMap.put(nativeName, tempFilePath);
			} catch (IOException e) {
				throw new LinkageError("Unable to get native: " + nativeName, e);
			}
		}
		
		return libMap.get(nativeName).toString();
	}
	
	public static void loadNative(String nativeName) {
		String filePath = getNative(nativeName);
		if (filePath == null) {
			throw new UnsatisfiedLinkError("Unable to find native: " + nativeName);
		} else {
			System.load(filePath);
		}
	}
	
}
