#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <unistd.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>

#include "OVR_SensorDeviceImpl.h"
#include "Kernel/OVR_Array.h"
#include "Kernel/OVR_Timer.h"
#include "Android/LogUtils.h"
#include <jni.h>



struct usbfs_ctrltransfer {
	/* keep in sync with usbdevice_fs.h:usbdevfs_ctrltransfer */
	uint8_t  bmRequestType;
	uint8_t  bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;

	uint32_t timeout;	/* in milliseconds */

	/* pointer to data */
	void *data;
};

struct usbfs_bulktransfer {
	/* keep in sync with usbdevice_fs.h:usbdevfs_bulktransfer */
	unsigned int ep;
	unsigned int len;
	unsigned int timeout;	/* in milliseconds */

	/* pointer to data */
	void *data;
};



#define IOCTL_USBFS_CONTROL	_IOWR('U', 0, struct usbfs_ctrltransfer)
#define IOCTL_USBFS_BULK		_IOWR('U', 2, struct usbfs_bulktransfer)



namespace OVR {

	
	class MyThread
	{
	public:
		MyThread();
		virtual ~MyThread();

		bool	Create();

		virtual void*		Run() = 0;

	private:

		pthread_t	pid_;
	};

	

	//////////////////////////////////////////////////////////////////////////

	static void* ThreadFunc_s(void* arg)
	{
		MyThread* thread = (MyThread*)arg;
		void* r = thread->Run();
		return r;
	}


	MyThread::MyThread() {}

	MyThread::~MyThread() {}

	bool MyThread::Create()
	{
		int err = pthread_create(&pid_, NULL, ThreadFunc_s, this);
		if (err != 0){
			//GLog.LogError("Create thread failed!");
			return false;
		}

		return true;
	}

	

	static int devicefd = -1;
	static int deviceType = 0;

	static int count = 0;
	static double lastTime = 0.0;


	extern Array<SensorDeviceImpl*>  GSensorDevice;

	class DeviceThread : public MyThread
	{
	public:
		DeviceThread();
		~DeviceThread()	{}

		virtual void*		Run();

	};


	DeviceThread::DeviceThread() {	}


	int ReadSensorDataFromUsb()
	{
		if (devicefd < 0) {
			return 0;
		}
		
		char buffer[128];

		usbfs_bulktransfer bulk;
		bulk.ep = 0x81;			// endpoint 1 / read in
		bulk.data = buffer;
		bulk.len = 128;
		bulk.timeout = 0;

		int r = ioctl(devicefd, IOCTL_USBFS_BULK, &bulk);
		if (r < 0) {
			LogText("ioctl IOCTL_USBFS_BULK error r = %d errno %d", r, errno);
			LogText("thread done!");
			return -1;
		}

		double timeSeconds = Timer::GetSeconds();

		count++;
		if (count >= 500){
			double deltaTime = timeSeconds - lastTime;

			float freq = (float)(count / deltaTime);
			LogText("freq %.2f", freq);

			count = 0;
			lastTime = timeSeconds;
		}

		//LogText("bulk transfer size %d", r);

		if (GSensorDevice.GetSize() > 0)	{

			SensorDeviceImpl* device = GSensorDevice.Back();
			device->OnTicks(timeSeconds);

			if (deviceType == DEVICE_TYPE_DK1) {
				device->OnInputReport((UByte*)buffer, r);
			} else if (deviceType == DEVICE_TYPE_M3D) {
				device->OnInputReport2((UByte*)buffer, r);
			}
		}

		return r;
	}
	

	void* DeviceThread::Run()
	{		
		while (ReadSensorDataFromUsb() >= 0) {
		}

		return 0;
	}


	static DeviceThread deviceThread;

	bool GetM3DSerialNumber(UByte * serial, uint32_t size)
	{
		if (devicefd < 0) {
			return false;
		}

		unsigned char buffer[255];

		const uint8_t DT_STRING = 0x03;

		usbfs_ctrltransfer ctrl;
		ctrl.bmRequestType = 0x80;
		ctrl.bRequest = 0x06;		// request get descriptor
		ctrl.wValue = DT_STRING << 8 | 0;	// string descriptor | id
		ctrl.wIndex = 0;			// language id
		ctrl.data = buffer;
		ctrl.wLength = sizeof(buffer);
		ctrl.timeout = 1000;

		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl get language id error r = %d errno %d", r, errno);
			return false;
		}

		uint16_t langid = buffer[2] | (buffer[3] << 8);
		//GLog.LogInfo("langid = %hd", langid);

		ctrl.wValue = DT_STRING << 8 | 0x03;	// serial string
		ctrl.wIndex = langid;

		r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl get serial string error r = %d errno %d", r, errno);
			return false;
		}

		//GLog.LogInfo("read serial string length r = %d len = %d", r, (int)buffer[0]);

		if (buffer[0] > r || buffer[0] > ctrl.wLength) {
			LogText("ioctl get serial string size error %d", (int)buffer[0]);
			return false;
		}

		if (buffer[1] != DT_STRING) {
			LogText("ioctl get serial string error (buffer[1] != DT_STRING) buffer[1] = %d", (int)buffer[1]);
			return false;
		}

		// to ascii code
		int di = 0, si = 2, length = buffer[0];
		for (; si < length; si += 2) {
			if ((buffer[si] & 0x80) || (buffer[si + 1])) /* non-ASCII */
				buffer[di++] = '?';
			else
				buffer[di++] = buffer[si];
		}
		buffer[di] = 0;

		if (size >(uint32_t)di){
			LogText("get serial number error, buffer size error %d", size);
			return false;
		}

		memcpy(serial, buffer, size);

		LogText("serial string %s", buffer);

		return true;
	}

	extern "C"
	{

		JNIEXPORT void JNICALL Java_com_oculusvr_vrscene_MainActivity_setupUsbDevice(JNIEnv * env, jobject thiz, jint fd, jint _deviceType)
		{
			devicefd = fd;
			deviceType = _deviceType;

			LogText("deviceThread.Create()!!! %d", fd);
		}

		JNIEXPORT void JNICALL Java_com_oculusvr_vrscene_MainActivity_PushData
			(JNIEnv * env, jobject obj, jbyteArray data, jint size)
		{
			if (size < 0){
				LogText("data size error %d", size);
				return;
			}

			jbyte buffer[128];
			env->GetByteArrayRegion(data, 0, size, buffer);

			//LogText("read %d %c %c", size, buffer[0], buffer[1]);
			//LogText("Java_com_oculusvr_vrscene_MainActivity_PushData sensor %p", GSensorDevice);
			//LogText("Sensor create %d", GSensorDevice.GetSize());

			double timeSeconds = Timer::GetSeconds();

			if (GSensorDevice.GetSize() > 0)	{

				SensorDeviceImpl* device = GSensorDevice.Back();
				device->OnTicks(timeSeconds);
				device->OnInputReport((UByte*)buffer, size);
			}
		}


	} // extern c


	int GetSensorDeviceType()
	{
		return deviceType;
	}

	void Fancy3DKeepAlive()
	{
		UByte cmd = 0x31;

		usbfs_bulktransfer bulk;
		bulk.ep = 0x03;			// send out
		bulk.data = &cmd;
		bulk.len = 1;
		bulk.timeout = 0;

		int r = ioctl(devicefd, IOCTL_USBFS_BULK, &bulk);
		if (r < 0) {
			LogText("Fancy3DKeepAlive error r = %d errno %d", r, errno);
		}
	}


	bool UsbSetFeature(UByte* data, uint32_t size)
	{
		if (deviceType == DEVICE_TYPE_M3D){

			if (data[0] == 8) {
				Fancy3DKeepAlive();
				return true;
			}

			return false;
		}

		if (devicefd < 0) {
			return false;
		}

		UByte reportID = data[0];

		struct usbfs_ctrltransfer ctrl;

		ctrl.bmRequestType = 0x21;			// host to device / type : class / Recipient : interface
		ctrl.bRequest = 0x09;				// set report
		ctrl.wValue = 0x300 | reportID;		// HID_FEATURE | id
		ctrl.wIndex = 0;
		ctrl.wLength = size;
		ctrl.data = data;
		ctrl.timeout = 0;

		LogText("ioctl UsbSetFeature %d", reportID);

		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl UsbSetFeature error r = %d errno %d", r, errno);
			return false;
		}

		return true;
	}

	bool UsbGetFeature(UByte* data, uint32_t size)
	{
		if (deviceType == DEVICE_TYPE_M3D){
			return false;
		}

		if (devicefd < 0) {
			return false;
		}

		UByte reportID = data[0];

		struct usbfs_ctrltransfer ctrl;

		ctrl.bmRequestType = 0xa1;		// device to host / type : class / Recipient : interface
		ctrl.bRequest = 0x01;			// get report
		ctrl.wValue = 0x300 | reportID;		// HID_FEATURE | id
		ctrl.wIndex = 0;
		ctrl.wLength = size;
		ctrl.data = data;
		ctrl.timeout = 0;
		
		LogText("ioctl UsbGetFeature %d", reportID);

		int r = ioctl(devicefd, IOCTL_USBFS_CONTROL, &ctrl);
		if (r < 0) {
			LogText("ioctl UsbGetFeature error r = %d errno %d", r, errno);
			return false;
		}

		return true;
	}



}// namespace ovr