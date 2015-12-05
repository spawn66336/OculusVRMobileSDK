/************************************************************************************

Filename    :   OVR_SensorCalibration.cpp
Content     :   Calibration data implementation for the IMU messages 
Created     :   January 28, 2014
Authors     :   Max Katsev

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include "OVR_SensorCalibration.h"
#include "Kernel/OVR_Log.h"
#include "Kernel/OVR_Threads.h"
#include <time.h>

namespace OVR {

using namespace Alg;

const UByte VERSION = 2;
const UByte MAX_COMPAT_VERSION = 15;

SensorCalibration::SensorCalibration(SensorDevice* pSensor)
    : GyroFilter(6000), GyroAutoTemperature(0)
{
    this->pSensor = pSensor;
};

void SensorCalibration::Initialize(const String& deviceSerialNumber)
{

#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
	GyroCalibration.Initialize(deviceSerialNumber);
#endif

    // read factory calibration
    pSensor->GetFactoryCalibration(&AccelOffset, &GyroAutoOffset, &AccelMatrix, &GyroMatrix, &GyroAutoTemperature);

/**/
	LogText("LDC - SensorCalibration - Factory calibration GyroMatrix: %f %f %f %f, %f %f %f %f, %f %f %f %f, %f %f %f %f\n",	GyroMatrix.M[0][0], GyroMatrix.M[0][1], GyroMatrix.M[0][2], GyroMatrix.M[0][3],
																																GyroMatrix.M[1][0], GyroMatrix.M[1][1], GyroMatrix.M[1][2], GyroMatrix.M[1][3],
																																GyroMatrix.M[2][0], GyroMatrix.M[2][1], GyroMatrix.M[2][2], GyroMatrix.M[2][3],
																																GyroMatrix.M[3][0], GyroMatrix.M[3][1], GyroMatrix.M[3][2], GyroMatrix.M[3][3]);

	LogText("LDC - SensorCalibration - Factory calibration GyroAutoOffset: %f %f %f temp=%f \n", GyroAutoOffset.x, GyroAutoOffset.y, GyroAutoOffset.z, GyroAutoTemperature);


    // if the headset has an autocalibrated offset, prefer it over the factory defaults
    GyroOffsetReport gyroReport;
    bool result = pSensor->GetGyroOffsetReport(&gyroReport);
    if (result && gyroReport.Version != GyroOffsetReport::Version_NoOffset)
    {
        GyroAutoOffset = (Vector3f) gyroReport.Offset;
        GyroAutoTemperature = (float) gyroReport.Temperature;
    }
	else
	{
		GyroAutoOffset.Set(-0.137690f, 0.134313f, 0.017489f);
		GyroAutoTemperature = 25.f;
	}
	

    // read the temperature tables and prepare the interpolation structures
#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
	GyroCalibration.GetAllTemperatureReports(&TemperatureReports);
#else	
	result = pSensor->GetAllTemperatureReports(&TemperatureReports);
#endif	
	
    //OVR_ASSERT(result);
    for (int i = 0; i < 3; i++)
        Interpolators[i].Initialize(TemperatureReports, i);

}

void SensorCalibration::DebugPrintLocalTemperatureTable()
{
	LogText("TemperatureReports:\n");
	for (int i = 0; i < (int)TemperatureReports.GetSize(); i++)
	{
		for (int j = 0; j < (int)TemperatureReports[i].GetSize(); j++)
		{
			TemperatureReport& tr = TemperatureReports[i][j];

			LogText("SensorCalibration - [%d][%d]: Version=%3d, Bin=%d/%d, "
					"Sample=%d/%d, TargetTemp=%3.1lf, "
					"ActualTemp=%4.1lf, "
					"Offset=(%7.2lf, %7.2lf, %7.2lf), "
					"Time=%d\n",	i, j, tr.Version,
									tr.Bin, tr.NumBins,
									tr.Sample, tr.NumSamples,
									tr.TargetTemperature,
									tr.ActualTemperature,
									tr.Offset.x, tr.Offset.y, tr.Offset.z,
									tr.Time);
		}
	}
}

void SensorCalibration::DebugClearHeadsetTemperatureReports()
{
    OVR_ASSERT(pSensor != NULL);

	Array<Array<TemperatureReport> > temperatureReports;

#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
	GyroCalibration.GetAllTemperatureReports(&temperatureReports);
#else	
    bool result;
	pSensor->GetAllTemperatureReports(&temperatureReports);
#endif
	
	OVR_ASSERT(temperatureReports.GetSize() > 0);
	OVR_ASSERT(temperatureReports[0].GetSize() > 0);

	TemperatureReport& tr = TemperatureReports[0][0];

	tr.ActualTemperature = 0.0;
	tr.Time = 0;
	tr.Version = 0;
	tr.Offset.x = tr.Offset.y = tr.Offset.z = 0.0;

	for (UByte i = 0; i < tr.NumBins; i++)
	{
		tr.Bin = i;

		for (UByte j = 0; j < tr.NumSamples; j++)
		{
			tr.Sample = j;

#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
			GyroCalibration.SetTemperatureReport(tr);
#else			
			result = pSensor->SetTemperatureReport(tr);
			OVR_ASSERT(result);
#endif			
			
			// Need to wait for the tracker board to finish writing to eeprom.
			Thread::MSleep(50);
		}
	}
}

void SensorCalibration::Apply(MessageBodyFrame& msg)
{
	AutocalibrateGyro(msg);

    // compute the interpolated offset
    Vector3f gyroOffset;
    for (int i = 0; i < 3; i++)
        gyroOffset[i] = (float) Interpolators[i].GetOffset(msg.Temperature, GyroAutoTemperature, GyroAutoOffset[i]);

    // apply calibration
    msg.RotationRate = GyroMatrix.Transform(msg.RotationRate - gyroOffset);
    msg.Acceleration = AccelMatrix.Transform(msg.Acceleration - AccelOffset);
}

void SensorCalibration::AutocalibrateGyro(MessageBodyFrame const& msg)
{
    const float alpha = 0.4f;
    // 1.25f is a scaling factor related to conversion from per-axis comparison to length comparison
    const float absLimit = 1.25f * 0.349066f;
//  const float noiseLimit = 1.25f * 0.03f;		// This was the original threshold based on reported device noise characteristics.
	const float noiseLimit = 0.0175f;			// This is the updated threshold based on analyzing ten GearVR devices and determining a value that best
												// discriminates between stationary on a desk and almost stationary when on the user's head.

    Vector3f gyro = msg.RotationRate;
    // do a moving average to reject short term noise
    Vector3f avg = (GyroFilter.IsEmpty()) ? gyro : gyro * alpha + GyroFilter.PeekBack() * (1 - alpha);

    // Make sure the absolute value is below what is likely motion
    // Make sure it is close enough to the current average that it is probably noise and not motion
    if (avg.Length() >= absLimit || (avg - GyroFilter.Mean()).Length() >= noiseLimit)
        GyroFilter.Clear();
    GyroFilter.PushBack(avg);

    // if had a reasonable number of samples already use it for the current offset
    if (GyroFilter.GetSize() > GyroFilter.GetCapacity() / 2)
    {
        GyroAutoOffset = GyroFilter.Mean();
        GyroAutoTemperature = msg.Temperature;

        // After ~6 seconds of no motion, use the average as the new zero rate offset
        if (GyroFilter.IsFull())
		{
			StoreAutoOffset();
		}
    }
}

void SensorCalibration::StoreAutoOffset()
{
    const double maxDeltaT = 2.5;
    const double minExtraDeltaT = 0.5;
    //const UInt32 minDelay = 24 * 3600; // 1 day in seconds
	const UInt32 minDelay = 3600;

    // find the best bin
    UPInt binIdx = 0;
    for (UPInt i = 1; i < TemperatureReports.GetSize(); i++) 
        if (Abs(GyroAutoTemperature - TemperatureReports[i][0].TargetTemperature) < 
            Abs(GyroAutoTemperature - TemperatureReports[binIdx][0].TargetTemperature))
            binIdx = i;
		
    // find the oldest and newest samples
    // NB: uninitialized samples have Time == 0, so they will get picked as the oldest
    UPInt newestIdx = 0, oldestIdx = 0;
    for (UPInt i = 1; i < TemperatureReports[binIdx].GetSize(); i++)
    {
        // if the version is newer - do nothing
        if (TemperatureReports[binIdx][i].Version > VERSION)
            return;
        if (TemperatureReports[binIdx][i].Time > TemperatureReports[binIdx][newestIdx].Time)
            newestIdx = i;
        if (TemperatureReports[binIdx][i].Time < TemperatureReports[binIdx][oldestIdx].Time)
            oldestIdx = i;
    }
    TemperatureReport& oldestReport = TemperatureReports[binIdx][oldestIdx];
    TemperatureReport& newestReport = TemperatureReports[binIdx][newestIdx];
    OVR_ASSERT((oldestReport.Sample == 0 && newestReport.Sample == 0 && newestReport.Version == 0) || 
                oldestReport.Sample == (newestReport.Sample + 1) % newestReport.NumSamples);
	
#ifndef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
    bool writeSuccess = false;
#endif	
    UInt32 now = (UInt32) time(0);
	
    if (now - newestReport.Time > minDelay)
    {
        // only write a new sample if the temperature is close enough
        if (Abs(GyroAutoTemperature - oldestReport.TargetTemperature) < maxDeltaT)
        {
            oldestReport.Time = now;
            oldestReport.ActualTemperature = GyroAutoTemperature;
            oldestReport.Offset = (Vector3d) GyroAutoOffset;
            oldestReport.Version = VERSION;
			
#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
			LogText("GyroCalibration.SetTemperatureReport(oldestReport)");
			LogText("time %u actualTemp %.3f bin %d sample %d targetTemp %.3f", 
				oldestReport.Time, (float)oldestReport.ActualTemperature, (UInt32)oldestReport.Bin, 
				(UInt32)oldestReport.Sample, (float)oldestReport.TargetTemperature);

			GyroCalibration.SetTemperatureReport(oldestReport);
#else
            writeSuccess = pSensor->SetTemperatureReport(oldestReport);
            OVR_ASSERT(writeSuccess);
#endif
        }
    }
    else
    {
        // if the newest sample is too recent - _update_ it if significantly closer to the target temp
        if (Abs(GyroAutoTemperature - newestReport.TargetTemperature) + minExtraDeltaT
            < Abs(newestReport.ActualTemperature - newestReport.TargetTemperature))
        {
            // (do not update the time!)
            newestReport.ActualTemperature = GyroAutoTemperature;
            newestReport.Offset = (Vector3d) GyroAutoOffset;
            newestReport.Version = VERSION;

#ifdef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
			LogText("GyroCalibration.SetTemperatureReport(newestReport)");
			LogText("time %u actualTemp %.3f bin %d sample %d targetTemp %.3f",
				newestReport.Time, (float)newestReport.ActualTemperature, (UInt32)newestReport.Bin,
				(UInt32)newestReport.Sample, (float)newestReport.TargetTemperature);
			GyroCalibration.SetTemperatureReport(newestReport);
			//LogText("GyroCalibration.SetTemperatureReport offset ")
#else
            writeSuccess = pSensor->SetTemperatureReport(newestReport);
            OVR_ASSERT(writeSuccess);
#endif
        }
    }
	
#ifndef USE_LOCAL_TEMPERATURE_CALIBRATION_STORAGE
    if (!writeSuccess)
	{
		return;
	}
#endif
	
    // update the interpolators with the new data
    // this is not particularly expensive call and would only happen rarely
    // but if performance is a problem, it's possible to only recompute the data that has changed
	for (int i = 0; i < 3; i++)
		Interpolators[i].Initialize(TemperatureReports, i);
}

const TemperatureReport& median(const Array<TemperatureReport>& temperatureReportsBin, int coord)
{
    Array<double> values;
    values.Reserve(temperatureReportsBin.GetSize());
    for (unsigned i = 0; i < temperatureReportsBin.GetSize(); i++)
        if (temperatureReportsBin[i].ActualTemperature != 0)
            values.PushBack(temperatureReportsBin[i].Offset[coord]);
    if (values.GetSize() > 0)
    {
        double med = Median(values);
        // this is kind of a hack
        for (unsigned i = 0; i < temperatureReportsBin.GetSize(); i++)
            if (temperatureReportsBin[i].Offset[coord] == med)
                return temperatureReportsBin[i];
        // if we haven't found the median in the original array, something is wrong
        OVR_DEBUG_BREAK;
    }
    return temperatureReportsBin[0];
}

void OffsetInterpolator::Initialize(Array<Array<TemperatureReport> > const& temperatureReports, int coord)
{
    int bins = (int) temperatureReports.GetSize();
    Temperatures.Clear();
    Temperatures.Reserve(bins);
    Values.Clear();
    Values.Reserve(bins);

    for (int bin = 0; bin < bins; bin++)
    {
        OVR_ASSERT(temperatureReports[bin].GetSize() == temperatureReports[0].GetSize());
        const TemperatureReport& report = median(temperatureReports[bin], coord);
        if (report.Version > 0 && report.Version <= MAX_COMPAT_VERSION)
        {
            Temperatures.PushBack(report.ActualTemperature);
            Values.PushBack(report.Offset[coord]);
        }
    }
}

double OffsetInterpolator::GetOffset(double targetTemperature, double autoTemperature, double autoValue)
{

    const double autoRangeExtra = 1.0;
    const double minInterpolationDist = 0.5;

    // difference between current and autocalibrated temperature adjusted for preference over historical data
    const double adjustedDeltaT = Abs(autoTemperature - targetTemperature) - autoRangeExtra;

    int count = (int) Temperatures.GetSize();
    // handle special cases when we don't have enough data for proper interpolation
    if (count == 0)
        return autoValue;
    if (count == 1)
    {
        if (adjustedDeltaT < Abs(Temperatures[0] - targetTemperature))
            return autoValue;
        else
            return Values[0];
    }

    // first, find the interval that contains targetTemperature
    // if all points are on the same side of targetTemperature, find the adjacent interval
    int l;
    if (targetTemperature < Temperatures[1])
        l = 0;
    else if (targetTemperature >= Temperatures[count - 2])
        l = count - 2;
    else
        for (l = 1; l < count - 2; l++)
            if (Temperatures[l] <= targetTemperature && targetTemperature < Temperatures[l+1])
                break;
    int u = l + 1;

    // extend the interval if it's too small and the interpolation is unreliable
    if (Temperatures[u] - Temperatures[l] < minInterpolationDist)
    {
        if (l > 0 
            && (u == count - 1 || Temperatures[u] - Temperatures[l - 1] < Temperatures[u + 1] - Temperatures[l]))
            l--;
        else if (u < count - 1)
            u++;
    }

    // verify correctness
    OVR_ASSERT(l >= 0 && u < count);
    OVR_ASSERT(l == 0 || Temperatures[l] <= targetTemperature);
    OVR_ASSERT(u == count - 1 || targetTemperature < Temperatures[u]);
    OVR_ASSERT((l == 0 && u == count - 1) || Temperatures[u] - Temperatures[l] > minInterpolationDist);
    OVR_ASSERT(Temperatures[l] <= Temperatures[u]);

    // perform the interpolation
    double slope;
    if (Temperatures[u] - Temperatures[l] >= minInterpolationDist)
        slope = (Values[u] - Values[l]) / (Temperatures[u] - Temperatures[l]);
    else
        // avoid a badly conditioned problem
        slope = 0;
    if (adjustedDeltaT < Abs(Temperatures[u] - targetTemperature))
        // use the autocalibrated value, if it's close
        return autoValue + slope * (targetTemperature - autoTemperature);
    else
        return Values[u] + slope * (targetTemperature - Temperatures[u]);
}

} // namespace OVR
