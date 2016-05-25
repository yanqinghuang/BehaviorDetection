/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2014 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <windows.h>
#include <stdio.h>
#include "object_tracker.h"
#include "pxcsensemanager.h"
#include "pxctracker.h"
#include "pxctrackerutils.h"
#include "pxcsession.h"
#include "pxcprojection.h"
#include "ui.h"

extern volatile bool g_stop;
extern pxcCHAR g_mapFile[1024];
extern pxcCHAR g_calibrationFile[1024];
extern PXCRectI32 roi;

PXCSenseManager *senseMgr;
PXCTracker *tracker = NULL;
PXCTrackerUtils *trackerUtils;

MapOperation op;
bool active = false;
bool initialized = false;

// Creates a pipeline which begins streaming immediately.  Only live streaming is supported
// Map creation functions are started any time after streaming has begun
void MapCreationPipeline(PXCSession *session)
{
	PXCProjection *projection = NULL;
	senseMgr = session->CreateSenseManager();
	
	if (!senseMgr)
	{
		SetStatus(L"Failed to create an SDK SenseManager");
		return;
	}

	/* Set Mode & Source */	
	PXCCaptureManager *captureMgr = senseMgr->QueryCaptureManager(); //no need to Release it is released with senseMgr
	pxcCHAR* device = NULL;

	// Live streaming
	device = GetCheckedDevice();
	captureMgr->FilterByDeviceInfo(device,0,0);		

	// Enable a color stream so video starts before the tracking module is unpaused
	senseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR);

	// The pipeline is static, so tracking must be enabled before it is created. However, pause the module
	// so frames are not processed until map creation is started
	senseMgr->EnableTracker();
	senseMgr->PauseTracker(TRUE);

	tracker = senseMgr->QueryTracker();
	if (tracker==NULL)
	{
		SetStatus(L"Failed to Query tracking module\n");
		MessageBoxW(0,L"Failed to Query tracking module",L"Object Tracker",MB_ICONEXCLAMATION | MB_OK);
        return;
	}

	trackerUtils = tracker->QueryTrackerUtils();
	if (trackerUtils==NULL)
	{
		SetStatus(L"Failed to Query tracking utility module\n");
		MessageBoxW(0,L"Failed to Query tracking utility module",L"Object Tracker",MB_ICONEXCLAMATION | MB_OK);
        return;
	}
	
	if (wcsnlen(g_calibrationFile,sizeof(g_calibrationFile)/sizeof(g_calibrationFile[0])) > 0)
	{
		if (senseMgr->QueryTracker()->SetCameraParameters(g_calibrationFile) != PXC_STATUS_NO_ERROR)
		{
			MessageBoxW(0,L"Warning: failed to load camera calibration",L"Object Tracker",MB_ICONWARNING | MB_OK);
		}
	}
		
	bool stsFlag = true;

	if (senseMgr->Init() >= PXC_STATUS_NO_ERROR)
	{
		SetStatus(L"Streaming");
		
		projection = senseMgr->QueryCaptureManager()->QueryDevice()->CreateProjection();
				
		// Constant streaming loop, only displays tracking results when an operation is active
		while (!g_stop)
		{
			if (senseMgr->AcquireFrame(true) < PXC_STATUS_NO_ERROR)
				break;			
					
			const PXCCapture::Sample *sample = senseMgr->QuerySample();
			
			if (sample)
				DrawBitmap(sample->color);

			if (active)
			{	
				int numFeatures = trackerUtils->QueryNumberFeaturePoints();
				pxcUID cosID = 0;
				pxcCHAR buf[100];

				switch (op)
				{
				case MAP_EXTEND:					
					SetFeatureCount(numFeatures, !initialized);

					// Feature count isn't valid until the first frame has been processed,
					// it will return 0 until then
					if (numFeatures > 0)
					{
						initialized = true;
					}
				case MAP_CREATE:
					cosID = PXCTrackerUtils::IN_PROGRESS_MAP;
					swprintf_s<100>(buf, L"Number of features: %d", numFeatures);
					AppendStatus(buf);
					break;
				case MAP_ALIGN:
					cosID = PXCTrackerUtils::ALIGNMENT_MARKER;
					if (trackerUtils->Is3DMapAlignmentComplete())
					{
						swprintf_s(buf, L"Alignment Complete");
						AppendStatus(buf);
					}
					break;
				}
				
				// Draw the current object axes
				PXCTracker::TrackingValues trackingVals;
				tracker->QueryTrackingValues(cosID, trackingVals);
				
				if (PXCTracker::IsTracking(trackingVals.state))
				{					
					DrawTrackingValues(&trackingVals, projection);
				}
			}

			UpdatePanel(false);
			senseMgr->ReleaseFrame();			
		}
	}
	else
	{
		SetStatus(L"Init Failed");		
		stsFlag=false;
	}

	active = false;
		
	if (projection) projection->Release();
	senseMgr->Close();
	senseMgr->Release();
	senseMgr = NULL;
	if (stsFlag)
		SetStatus(L"Stopped");
}

#define CHECK_STATUS(msg) if (sts != PXC_STATUS_NO_ERROR){ MessageBoxW(0,msg,L"Object Tracker",MB_ICONEXCLAMATION | MB_OK); break; }

// Start a map operation (creation, extension, alignment).  Results will be updated and rendered in the pipeline thread function
bool StartMapCreationOperation()
{	
	pxcStatus sts;
	int markerID, markerSize;

	senseMgr->PauseTracker(FALSE);	
			
	// Reset any operations which were in progress before
	trackerUtils->Cancel3DMapCreation();
	trackerUtils->Cancel3DMapExtension();
	trackerUtils->Cancel3DMapAlignment();

	// Set the new calibration file which was specified (if any)
	if (wcsnlen(g_calibrationFile,sizeof(g_calibrationFile)/sizeof(g_calibrationFile[0])) > 0)
	{
		if (tracker->SetCameraParameters(g_calibrationFile) != PXC_STATUS_NO_ERROR)
		{
			MessageBoxW(0,L"Warning: failed to load camera calibration",L"Object Tracker",MB_ICONWARNING | MB_OK);
		}
	}

	tracker->SetRegionOfInterest(roi);

	op = GetMapOperation();
	
	switch (op)
	{
	case MAP_CREATE:
		sts = trackerUtils->Start3DMapCreation(GetObjectSize());
		CHECK_STATUS(L"Failed to start map creation");
		break;
	case MAP_EXTEND:
		sts = trackerUtils->Load3DMap(g_mapFile);
		CHECK_STATUS(L"Failed to load map file");
		sts = trackerUtils->Start3DMapExtension();
		CHECK_STATUS(L"Failed to begin map extension");
		initialized = false;
		break;
	case MAP_ALIGN:
		GetMarkerInfo(markerID, markerSize);
		sts = trackerUtils->Load3DMap(g_mapFile);
		CHECK_STATUS(L"Failed to load map file");
		sts = trackerUtils->Start3DMapAlignment(markerID, markerSize);		
		CHECK_STATUS(L"Failed to start alignment");				
		AppendStatus(L"Alignment in progress...");
		break;		
	}

	if (sts == PXC_STATUS_NO_ERROR)
	{
		active = true;
	}

	return sts == PXC_STATUS_NO_ERROR ? true : false;
}

// Stop a map creation operation by pausing the tracker (which pauses frame processing)
void StopMapCreationOperation()
{	
	if (!senseMgr)
		return;
	
	senseMgr->PauseTracker(TRUE);	
	
	AppendStatus(L"Processing paused, the map has not been automatically saved");
	active = false;

	// Since Tracker is paused no new frames will be sent to the module (so no new features will
	// be identified)
}

// Save a map file from the latest operation
void SaveMap(pxcCHAR *filename)
{
	if (trackerUtils->Save3DMap(filename) != PXC_STATUS_NO_ERROR)
	{
		MessageBoxW(0,L"Failed to save map",L"Object Tracker",MB_ICONEXCLAMATION | MB_OK);
	}
	else
	{
		AppendStatus(L"Map file saved successfully");
	}
}

void MapCreation_ROIUpdated()
{
	// If currently performing an operation, will update the ROI
	// Otherwise "Start" will set the ROI
	if (tracker)
	{
		tracker->SetRegionOfInterest(roi);
	}
}