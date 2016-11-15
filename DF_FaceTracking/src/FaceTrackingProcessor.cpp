#include "FaceTrackingProcessor.h"
#include <assert.h>
#include <string>
#include "pxcfaceconfiguration.h"
#include "pxcfacemodule.h"
#include "pxcsensemanager.h"
#include "FaceTrackingUtilities.h"
#include "FaceTrackingAlertHandler.h"
#include "FaceTrackingRendererManager.h"
#include "resource.h"

#include <direct.h>
#include <stdio.h>
#include <conio.h>
#include <errno.h>
#include "pxcsensemanager.h"
#include "PXCPhoto.h"
#include "util_render.h"

//#include <vld.h>
extern PXCSession* session;
extern FaceTrackingRendererManager* renderer;

extern volatile bool isStopped;
extern volatile bool isActiveApp;
extern pxcCHAR fileName[1024];
extern HANDLE ghMutex;

bool WriteDepthBuf(PXCImage *zImage, wchar_t filePath[]);


FaceTrackingProcessor::FaceTrackingProcessor(HWND window) : m_window(window), m_registerFlag(false), m_unregisterFlag(false) { }

void FaceTrackingProcessor::PerformRegistration()
{
	m_registerFlag = false;
	if(m_output->QueryFaceByIndex(0))
		m_output->QueryFaceByIndex(0)->QueryRecognition()->RegisterUser();
}

void FaceTrackingProcessor::PerformUnregistration()
{
	m_unregisterFlag = false;
	if(m_output->QueryFaceByIndex(0))
		m_output->QueryFaceByIndex(0)->QueryRecognition()->UnregisterUser();
}

void FaceTrackingProcessor::CheckForDepthStream(PXCSenseManager* pp, HWND hwndDlg)
{
	PXCFaceModule* faceModule = pp->QueryFace();
	if (faceModule == NULL) 
	{
		assert(faceModule);
		return;
	}
	PXCFaceConfiguration* config = faceModule->CreateActiveConfiguration();
	if (config == NULL)
	{
		assert(config);
		return;
	}

	PXCFaceConfiguration::TrackingModeType trackingMode = config->GetTrackingMode();
	config->Release();
	if (trackingMode == PXCFaceConfiguration::FACE_MODE_COLOR_PLUS_DEPTH)
	{
		PXCCapture::Device::StreamProfileSet profiles={};
		pp->QueryCaptureManager()->QueryDevice()->QueryStreamProfileSet(&profiles);
		if (!profiles.depth.imageInfo.format)
		{            
			std::wstring msg = L"Depth stream is not supported for device: ";
			msg.append(FaceTrackingUtilities::GetCheckedDevice(hwndDlg));           
			msg.append(L". \nUsing 2D tracking");
			MessageBox(hwndDlg, msg.c_str(), L"Face Tracking", MB_OK);            
		}
	}
}

void FaceTrackingProcessor::Process(HWND dialogWindow)
{
	// Return Codes
	const int ERROR_CMD = 1;
	const int ERROR_FILE = 2;
	const int ERROR_CAMERA = 3;

	PXCSenseManager* senseManager = session->CreateSenseManager();

	senseManager->EnableStream(PXCCapture::STREAM_TYPE_COLOR, 320, 240, 0);
	senseManager->EnableStream(PXCCapture::STREAM_TYPE_DEPTH, 320, 240, 0);

	// Initialize the PXCSenseManager
	pxcStatus camStatus;
	camStatus = senseManager->Init();
	if (camStatus != PXC_STATUS_NO_ERROR) {
		fprintf_s(stderr, "Unable to initizlize PXCSenseManager\n");
		senseManager->Release();

		return;
	}

	PXCImage *colorImage;
	PXCImage *depthImage;
	PXCPhoto *xdmPhoto = senseManager->QuerySession()->CreatePhoto();
	const char xdmExt[] = ".jpg";
	const char depBufExt[] = ".dep";
	const char colBufExt[] = ".col";

	// These two objects come from the Intel RealSense SDK helper for rendering
	// camera data. Any rendering technique could be used or omitted if no 
	// visual feedback is desired.

	UtilRender *renderColor = new UtilRender(L"COLOR STREAM");
	UtilRender *renderDepth = new UtilRender(L"DEPTH STREAM");


	camStatus = senseManager->AcquireFrame(true);
	if (camStatus < PXC_STATUS_NO_ERROR) {
		fprintf_s(stderr, "Error acquiring frame: %f\n", camStatus);
		return;
	}
	// Retrieve all available image samples
	PXCCapture::Sample *dsample = senseManager->QuerySample();
	if (dsample == NULL) {
		fprintf_s(stderr, "dSample unavailable\n");
	}
	else {
		colorImage = dsample->color;
		depthImage = dsample->depth;
		// Render the frames (not necessary)
		if (!renderColor->RenderFrame(colorImage)) return;
		if (!renderDepth->RenderFrame(depthImage)) return;

		// save xmd image
		wchar_t filePath1[512];
		wchar_t filePath2[512];
		char xmdFileName[18] = { 'r','5'};
		char depFileName[18] = { 'd','5' };

		swprintf_s(filePath1, 512, L"%S\\%S%S",
			"D:\\IntelRealSense\\VSProjects\\BehaviorDetection\\DF_FaceTracking\\rssdk-capture-xdm",
			xmdFileName,
			xdmExt);
		xdmPhoto->ImportFromPreviewSample(dsample);

		pxcStatus saveStatus = xdmPhoto->SaveXDM(filePath1);
		if (saveStatus != PXC_STATUS_NO_ERROR)
			fprintf_s(stderr, "Error: SaveXDM\n");

		// save depth buffer
		swprintf_s(filePath2, 512, L"%S\\%S%S",
			"D:\\IntelRealSense\\VSProjects\\BehaviorDetection\\DF_FaceTracking\\rssdk-capture-xdm",
			depFileName, ".bmp");
	
		if (!WriteDepthBuf(depthImage, filePath2)) {
			fprintf(stderr, "Error: WriteDepthBuf\n");
		}

		if (senseManager == NULL)
		{
			FaceTrackingUtilities::SetStatus(dialogWindow, L"Failed to create an SDK SenseManager", statusPart);
			return;
		}

		/* Set Mode & Source */
		PXCCaptureManager* captureManager = senseManager->QueryCaptureManager();
		if (!FaceTrackingUtilities::GetPlaybackState(dialogWindow))
		{
			captureManager->FilterByDeviceInfo(FaceTrackingUtilities::GetCheckedDeviceInfo(dialogWindow));
		}

		pxcStatus status = PXC_STATUS_NO_ERROR;
		if (FaceTrackingUtilities::GetRecordState(dialogWindow))
		{
			status = captureManager->SetFileName(fileName, true);
		}
		else if (FaceTrackingUtilities::GetPlaybackState(dialogWindow))
		{
			status = captureManager->SetFileName(fileName, false);
			senseManager->QueryCaptureManager()->SetRealtime(false);
		}
		if (status < PXC_STATUS_NO_ERROR)
		{
			FaceTrackingUtilities::SetStatus(dialogWindow, L"Failed to Set Record/Playback File", statusPart);
			return;
		}

		/* Set Module */
		senseManager->EnableFace();

		/* Initialize */
		FaceTrackingUtilities::SetStatus(dialogWindow, L"Init Started", statusPart);

		PXCFaceModule* faceModule = senseManager->QueryFace();
		if (faceModule == NULL)
		{
			assert(faceModule);
			return;
		}
		PXCFaceConfiguration* config = faceModule->CreateActiveConfiguration();
		if (config == NULL)
		{
			assert(config);
			return;
		}
		config->SetTrackingMode(FaceTrackingUtilities::GetCheckedProfile(dialogWindow));
		config->ApplyChanges();

		if (FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_PULSE) && !FaceTrackingUtilities::GetPlaybackState(dialogWindow))
		{
			PXCCapture::Device::StreamProfileSet set;
			memset(&set, 0, sizeof(set));
			set.color.imageInfo.height = 720;
			set.color.imageInfo.width = 1280;
			captureManager->FilterByStreamProfiles(&set);
		}

		if (senseManager->Init() < PXC_STATUS_NO_ERROR)
		{
			captureManager->FilterByStreamProfiles(NULL);
			if (senseManager->Init() < PXC_STATUS_NO_ERROR)
			{
				FaceTrackingUtilities::SetStatus(dialogWindow, L"Init Failed", statusPart);
				config->Release();
				senseManager->Close();
				senseManager->Release();
				return;
			}
		}

		PXCCapture::DeviceInfo info;
		senseManager->QueryCaptureManager()->QueryDevice()->QueryDeviceInfo(&info);

		CheckForDepthStream(senseManager, dialogWindow);
		FaceTrackingAlertHandler alertHandler(dialogWindow);
		if (FaceTrackingUtilities::GetCheckedModule(dialogWindow))
		{
			config->detection.isEnabled = FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_LOCATION);
			config->landmarks.isEnabled = FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_LANDMARK);
			config->pose.isEnabled = FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_POSE);
			FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_PULSE) ? config->QueryPulse()->Enable() : config->QueryPulse()->Disable();

			if (FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_EXPRESSIONS))
			{
				config->QueryExpressions()->Enable();
				config->QueryExpressions()->EnableAllExpressions();
			}
			else
			{
				config->QueryExpressions()->DisableAllExpressions();
				config->QueryExpressions()->Disable();
			}
			if (FaceTrackingUtilities::IsModuleSelected(dialogWindow, IDC_RECOGNITION))
			{
				config->QueryRecognition()->Enable();
			}

			config->EnableAllAlerts();
			config->SubscribeAlert(&alertHandler);

			config->ApplyChanges();
		}
		FaceTrackingUtilities::SetStatus(dialogWindow, L"Streaming", statusPart);
		m_output = faceModule->CreateOutput();

		bool isNotFirstFrame = false;
		bool isFinishedPlaying = false;

		ResetEvent(renderer->GetRenderingFinishedSignal());

		renderer->SetSenseManager(senseManager);
		renderer->SetNumberOfLandmarks(config->landmarks.numLandmarks);
		renderer->SetCallback(renderer->SignalProcessor);

		if (!isStopped)
		{
			while (true)
			{
				if (senseManager->AcquireFrame(true) < PXC_STATUS_NO_ERROR)
				{
					isFinishedPlaying = true;
				}

				if (isNotFirstFrame)
				{
					WaitForSingleObject(renderer->GetRenderingFinishedSignal(), INFINITE);
				}

				if (isFinishedPlaying || isStopped)
				{
					if (isStopped)
						senseManager->ReleaseFrame();

					if (isFinishedPlaying)
						PostMessage(dialogWindow, WM_COMMAND, ID_STOP, 0);

					break;
				}

				m_output->Update();
				PXCCapture::Sample* sample = senseManager->QueryFaceSample();

				isNotFirstFrame = true;

				if (sample != NULL)
				{
					DWORD dwWaitResult;
					dwWaitResult = WaitForSingleObject(ghMutex, INFINITE);
					if (dwWaitResult == WAIT_OBJECT_0)
					{
						renderer->DrawBitmap(sample, config->GetTrackingMode() == PXCFaceConfiguration::FACE_MODE_IR);
						renderer->SetOutput(m_output);
						renderer->SignalRenderer();
						if (!ReleaseMutex(ghMutex))
						{
							throw std::exception("Failed to release mutex");
							return;
						}
					}
				}

				if (config->QueryRecognition()->properties.isEnabled)
				{
					if (m_registerFlag)
						PerformRegistration();

					if (m_unregisterFlag)
						PerformUnregistration();
				}

				senseManager->ReleaseFrame();
			}

			m_output->Release();
			FaceTrackingUtilities::SetStatus(dialogWindow, L"Stopped", statusPart);
		}

		config->Release();
		senseManager->Close();
		senseManager->Release();
	}
}
void FaceTrackingProcessor::RegisterUser()
{
	m_registerFlag = true;
}

void FaceTrackingProcessor::UnregisterUser()
{
	m_unregisterFlag = true;
}
