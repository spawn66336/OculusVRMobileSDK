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
	UsbInterface intf_ = null;
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
						(device.getVendorId() == 1155 && device.getProductId() == 22352) ||
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
			for (int i = 0; i < nInterface; i++) {
				UsbInterface intf = device.getInterface(i);
				String str = String.format("interface %d %d.%d.%d", intf.getId(), intf.getInterfaceClass(), 
						intf.getInterfaceSubclass(), intf.getInterfaceProtocol());
				Log.i(TAG, str);

				if (intf.getInterfaceClass() == 10 && intf.getInterfaceSubclass() == 0 && intf.getInterfaceProtocol() == 0) {
					intf_ = intf;
					Log.i(TAG, "found interface");
				}
				int numEndpoint = intf.getEndpointCount();
	            for (int j = 0; j < intf.getEndpointCount(); j++) {                    
	                UsbEndpoint endpoint = intf.getEndpoint(j);
	                String endpointInfo = String.format("endpoint %d addr 0x%x number %d attri %d", j, 
	                		endpoint.getAddress(), endpoint.getEndpointNumber(), endpoint.getAttributes());
	                Log.i(TAG, endpointInfo);
	            }
	            
			}

			connection = manager.openDevice(device);			
			boolean bRes = connection.claimInterface(intf_, forceClaim);			
			int fd = connection.getFileDescriptor();
			
			Log.i(TAG, String.format("%s %d", bRes ? "succeeded" : "failed", fd));
			
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
