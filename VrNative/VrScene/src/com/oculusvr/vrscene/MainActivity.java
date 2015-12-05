/************************************************************************************

Filename    :   MainActivity.java
Content     :   
Created     :   
Authors     :   

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/
package com.oculusvr.vrscene;

import android.os.Bundle;
import android.util.Log;
import android.content.Intent;
import com.oculusvr.vrlib.VrActivity;
import com.oculusvr.vrlib.VrLib;
import android.hardware.usb.*;
import java.util.*;
import android.content.Context;
import java.util.HashMap;
import java.util.HashSet;

public class MainActivity extends VrActivity {

	public static final String TAG = "VrScene";
	
	/** Load jni .so on initialization */
	static {
		Log.d( TAG, "LoadLibrary" );
		System.loadLibrary( "vrscene" );
	}

	public static native long nativeSetAppInterface( VrActivity act, String fromPackageNameString, String commandString, String uriString );

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		Log.d( TAG, "onCreate" );
		super.onCreate(savedInstanceState);

		Intent intent = getIntent();
		String commandString = VrLib.getCommandStringFromIntent( intent );
		String fromPackageNameString = VrLib.getPackageStringFromIntent( intent );
		String uriString = VrLib.getUriStringFromIntent( intent );

		appPtr = nativeSetAppInterface( this, fromPackageNameString, commandString, uriString );

		InitUsbDevice();
	}

	private byte[] bytes;
	private boolean forceClaim = true; 
	UsbDeviceConnection connection = null;
	UsbInterface intf = null;
	UsbEndpoint endpoint = null;

	protected void InitUsbDevice() {

		UsbManager manager = (UsbManager) getSystemService(Context.USB_SERVICE);
		HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
		
		
		Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
		 
		while(deviceIterator.hasNext()){    
			UsbDevice device = deviceIterator.next();

			int deviceType = 0;
			
			if (device.getVendorId() == 10291 && device.getProductId() == 1) {
				deviceType = 1;
			} else if (	(device.getVendorId() == 1155 && device.getProductId() == 22336) ||
						(device.getVendorId() == 949 && device.getProductId() == 1)
			) {
				deviceType = 0;
			} else {
				continue;
			}
						
			int nInterface = device.getInterfaceCount();
			if (nInterface < 1)	{
				continue;
			}
			
			Log.d(TAG, "usb device found interface count " + nInterface);

			connection = manager.openDevice(device);
			intf = device.getInterface(0);
			int endpointCount = intf.getEndpointCount();
			endpoint = intf.getEndpoint(0);	
			
			connection.claimInterface(intf, forceClaim);
			
			int fd = connection.getFileDescriptor();
			setupUsbDevice(fd, deviceType);	
			
			// use native thread
			//StartUsbIOThread();					
			
			// 0xa1 : device to host / Type : class / Recipien : interface
			// request : 1 (get report)
			// value : 0x300 HID_FEATURE  0x3 FEATURE_CALIBRATE
			
			//size = connection.controlTransfer(0xa1, 1, 0x303, 0, bytes, 256, 0);
			//addText("control read " + size);					
			
			//size = connection.controlTransfer(0xa1, 1, 0x304, 0, bytes, 256, 0);
			//addText("control read " + size);
								
		}					

	}
	
	void StartUsbIOThread() {
		bytes = new byte[256];
		
		new Thread(new Runnable() {
			public void run() {
							
				while (true) {	
					int size = connection.bulkTransfer(endpoint, bytes, bytes.length, 0);	
					PushData(bytes, size);
				}				
			}
		}).start();
	}
	
		
    public native void PushData(byte[] buffer, int length);	
    public native void setupUsbDevice(int fd, int deviceType);
	
}
