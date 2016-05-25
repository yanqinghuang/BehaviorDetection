/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2014 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <windows.h>
#include <vector>
#include "object_tracker.h"
#include "pxcsensemanager.h"
#include "pxctracker.h"
#include "pxcsession.h"
#include "pxcprojection.h"
#include "ui.h"

extern std::vector<Model> g_targets;
extern volatile bool g_stop;
extern pxcCHAR g_file[1024];
extern pxcCHAR g_calibrationFile[1024];
extern PXCRectI32 roi;

PXCTracker *pTracker;

// Creates a pipeline which begins tracking as soon as streaming starts
void ObjectTrackingPipeline(PXCSession *session)
{
	PXCProjection *projection = NULL;
	PXCSenseManager *senseMgr = session->CreateSenseManager();
	bool instantMode = false;	

	if (!senseMgr)
	{
		SetStatus(L"Failed to create an SDK SenseManager");
		return;
	}

	/* Set Mode & Source */
	pxcStatus sts = PXC_STATUS_NO_ERROR;
	PXCCaptureManager *captureMgr = senseMgr->QueryCaptureManager(); //no need to Release it is released with senseMgr
	pxcCHAR* device = NULL;

	if (IsRecordState())
	{
		sts = captureMgr->SetFileName(g_file,true);
		captureMgr->FilterByDeviceInfo(GetCheckedDevice(),0,0);		
	}
	else if (IsPlaybackState())
	{
		sts = captureMgr->SetFileName(g_file,false);
	}
	else
	{
		// Live streaming
		device = GetCheckedDevice();
		captureMgr->FilterByDeviceInfo(device,0,0);		
	}
	
	if (sts < PXC_STATUS_NO_ERROR)
	{
		SetStatus(L"Failed to Set Record/Playback File");
		return;
	}

	bool stsFlag = true;

	/* Set Module */
	sts = senseMgr->EnableTracker();
	if (sts < PXC_STATUS_NO_ERROR)
	{
		SetStatus(L"Failed to enable tracking module\n");
		MessageBoxW(0,L"Failed to enable tracking module",L"Object Tracker",MB_ICONEXCLAMATION | MB_OK);
        return;
	}

	// Init
	SetStatus(L"Init Started");

	pTracker = senseMgr->QueryTracker();
	if (pTracker==NULL)
	{
		SetStatus(L"Failed to Query tracking module\n");
		MessageBoxW(0,L"Failed to Query tracking module",L"Object Tracker",MB_ICONEXCLAMATION | MB_OK);
        return;
	}

	if (wcsnlen(g_calibrationFile,sizeof(g_calibrationFile)/sizeof(g_calibrationFile[0])) > 0)
	{
		if (pTracker->SetCameraParameters(g_calibrationFile) != PXC_STATUS_NO_ERROR)
		{
			MessageBoxW(0,L"Warning: failed to load camera calibration",L"Object Tracker",MB_ICONWARNING | MB_OK);
		}
	}

	if (senseMgr->Init() >= PXC_STATUS_NO_ERROR)
	{
		SetStatus(L"Streaming");

		projection = senseMgr->QueryCaptureManager()->QueryDevice()->CreateProjection();
		
		// Configure tracking now that the module has been initialized
		if (GetTrackingType() != TRACKING_INSTANT)
		{			
			pxcUID cosID;
			for (size_t i = 0; i < g_targets.size(); i++)
			{
				g_targets[i].cosIDs.clear();

				switch (GetTrackingType())
				{
				case TRACKING_2D:					
					sts = pTracker->Set2DTrackFromFile(g_targets[i].model_filename, cosID);
					g_targets[i].addCosID(cosID, L"2D Image");
					break;		

				case TRACKING_3D:
					pxcUID firstID, lastID;
					sts = pTracker->Set3DTrack(g_targets[i].model_filename, firstID, lastID);
					while (firstID <= lastID)
					{	
						PXCTracker::TrackingValues vals;
						pTracker->QueryTrackingValues(firstID, vals);

						g_targets[i].addCosID(firstID, vals.targetName);
						firstID++;
					}
					break;
				}
				
				if (sts<PXC_STATUS_NO_ERROR)
				{
					SetStatus(L"Failed to set tracking configuration!!\n");
					MessageBoxW(0,L"Please select a tracking configuration!",L"Object Tracker",MB_ICONEXCLAMATION|MB_OK);
					return;
				}
			}

			RefreshModelTree();
		}
		else
		{			
			instantMode = true;			

			// Skip 30 frames to avoid initial auto contrast, brightness, etc. adjustments
			pTracker->Set3DInstantTrack(false, 30);
		}
				
		while (!g_stop)
		{
			if (senseMgr->AcquireFrame(true) < PXC_STATUS_NO_ERROR)
				break;
		
			/* Display Results */
			PXCTracker::TrackingValues  trackData;
			int updatedTrackingCount = 0;
			const PXCCapture::Sample *sample = senseMgr->QueryTrackerSample();
			
			if (sample)
				DrawBitmap(sample->color);

			if (!instantMode)
			{				
				// Loop over all of the registered targets (COS IDs) and see if they are tracked
				for (TargetIterator targetIter = g_targets.begin(); targetIter != g_targets.end(); targetIter++)				
				{
					for (TrackingIterator iter = targetIter->cosIDs.begin(); iter != targetIter->cosIDs.end(); iter++)
					{
						pTracker->QueryTrackingValues(iter->cosID, trackData);

						if (PXCTracker::IsTracking(trackData.state))
						{
							updatedTrackingCount += (!iter->isTracking ) ? 1 : 0;
							iter->isTracking = true;
							DrawTrackingValues(&trackData, projection);
						}
						else
						{
							updatedTrackingCount += iter->isTracking ? 1 : 0;
							iter->isTracking = false;
						}
					}
				}			
			}
			else
			{
				// For instant tracking, query all detected objects (there is usually only one)
				pxcI32 numTracked = pTracker->QueryNumberTrackingValues();
				PXCTracker::TrackingValues *trackArr = new PXCTracker::TrackingValues[numTracked];

				pTracker->QueryAllTrackingValues(trackArr);

				for(pxcI32 i=0; i < numTracked; i++)
				{
					if (PXCTracker::IsTracking(trackArr[i].state))
					{						
						DrawTrackingValues(&trackArr[i], projection);
					}
				}
				
				delete[] trackArr;
			}
			
			UpdatePanel(updatedTrackingCount > 0);
			senseMgr->ReleaseFrame();			
		}

	}
	else
	{
		SetStatus(L"Init Failed");
		stsFlag=false;
	}
	
	if (projection)	projection->Release();
	senseMgr->Close();
	senseMgr->Release();
	if (stsFlag)
		SetStatus(L"Stopped");
}

void ObjectTracking_ROIUpdated()
{
	if (pTracker)
	{
		pTracker->SetRegionOfInterest(roi);
	}
}