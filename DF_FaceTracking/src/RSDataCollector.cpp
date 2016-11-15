/******************************************************************************
Copyright 2015, Intel Corporation All Rights Reserved.

Permission is granted to use, copy, distribute and prepare derivative works of
this software for any purpose and without fee, provided, that the above 
copyright notice and this statement appear in all copies. Intel makes no 
representations about the suitability of this software for any purpose. 
THIS SOFTWARE IS PROVIDED "AS IS." INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES, 
EXPRESS OR IMPLIED, AND ALL LIABILITY, INCLUDING CONSEQUENTIAL AND OTHER 
INDIRECT DAMAGES, FOR THE USE OF THIS SOFTWARE, INCLUDING LIABILITY FOR 
INFRINGEMENT OF ANY PROPRIETARY RIGHTS, AND INCLUDING THE WARRANTIES OF 
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Intel does not assume any 
responsibility for any errors which may appear in this software nor any 
responsibility to update it.
******************************************************************************/

/*******************************************************************************
Compiling: This source depends on the Intel(R) RealSense(tm) SDK. It can be
obtained from https://software.intel.com/realsense. To easily include the SDK
in your Microsoft Visual Studio project, add the property sheet included with
the SDK located at <RSSDK_Path>\props\. There is a README.txt file at that
location with instructions.
*******************************************************************************/

#include <direct.h>
#include <stdio.h>
#include <conio.h>
#include <errno.h>
#include "pxcsensemanager.h"
#include "PXCPhoto.h"
#include "util_render.h"

const unsigned int MAXPATH = 512;

// Defaults
bool verbose = false;
unsigned long framesPerTest = 100;
char *bufPath = nullptr;
const char depBufExt[] = ".dep";
const char colBufExt[] = ".col";
char *xdmPath = nullptr;
const char xdmExt[] = ".jpg";

// Return Codes
const int ERROR_CMD = 1; 
const int ERROR_FILE = 2;
const int ERROR_CAMERA = 3;

void PrintUsage(char* cmd) {
	const char usageString[] =
		"Usage:\n"
		"  %s [options]\n"
		"\n"
		"Options:\n"
		"  -h          Print this message and quit.\n"
		"  -v          Verbose Output.\n"
		"              Default: %s\n"
		"  -f <val>    Frames. The number of frames processed at each test\n"
		"              iteration.\n"
		"              Default: %u\n"
		"  -b <path>   Buffer Path. The path to the directory where raw\n"
		"              buffer files should be written. Each file will be\n"
		"              time stamped and have the \"%s\" extension for depth\n"
		"              and \"%s\" for color.\n"
		"              Default: None\n"
		"  -x <path>   XDM Path. The path to the directory where XDM images\n"
		"              will be written. Each file will be time stamped and\n"
		"              have the \"%s\" extension.\n"
		"              Note: Saving XDM files will result in a low frame\n"
		"                    rate.\n"
		"              Default: None\n"
		"\n";
	fprintf_s(stdout, usageString
		// Usage
		,cmd 
		// -v (Verbose)
		, verbose ? "on" : "off"
		// -f (Frames)
		,framesPerTest 
		// -b (Buffer Path)
		,depBufExt, colBufExt 
		// -x (XDM Path)
		,xdmExt
		);
	return;
}

bool ProcessCmdArgs(int argc, char *argv[]) {
	if (argc > 1) {
		for (int i = 1; i < argc; i++){
			if (strcmp(argv[i], ("-h")) == 0) {
				PrintUsage(argv[0]);
				return false;
			}
			else if (strcmp(argv[i], "-v") == 0) {
				verbose = true;
			}
			else if (strcmp(argv[i], "-f") == 0) {
				i++;
				if (i <= argc)
					framesPerTest = strtoul(argv[i], NULL, 10);
			}
			else if (strcmp(argv[i], "-b") == 0){
				i++;
				if (i <= argc)
					bufPath = argv[i];
			}
			else if (strcmp(argv[i], "-x") == 0){
				i++;
				if (i <= argc)
					xdmPath = argv[i];
			}
			else {
				fprintf_s(stderr, "Unrecognized option: %s\n\n", argv[i]);
				PrintUsage(argv[0]);
				return false;
			}
		}
	}
	if(verbose) {
		if (argc < 2)
			fprintf_s(stdout, "Using defaults.\n");
		fprintf_s(stdout, "Parameters:\n");
		fprintf_s(stdout, "  Verbose Output: %s\n",
			verbose ? "on" : "off");
		fprintf_s(stdout, "  Frames        : %lu\n", framesPerTest);
		fprintf_s(stdout, "  Buffer Path   : %s\n",
			bufPath == nullptr ? "None" : bufPath);
		fprintf_s(stdout, "  XDM Path      : %s\n",
			xdmPath == nullptr ? "None" : xdmPath);
	}
	return true;
}

void TimeStampString(char *timeStampString) {
	SYSTEMTIME time;
	GetLocalTime(&time);
	WCHAR fileName[18];
	swprintf_s(fileName, 18,
		L"%04d%02d%02d%02d%02d%02d%03d",
		time.wYear,          // +4 char = 4
		time.wMonth,         // +2 char = 6
		time.wDay,           // +2 char = 8
		time.wHour,          // +2 char = 10
		time.wMinute,        // +2 char = 12
		time.wSecond,        // +2 char = 14
		time.wMilliseconds); // +3 char = 17
	                         // +null   = 18
	sprintf_s(timeStampString, 18, "%S",fileName);
	return;
}

/*******************************************************************************
WriteDepthBuf contains the logic for extracting raw depth data from the 
RealSense image object. This function demonstrates how to acquire the frame, 
populate the data object, and navigate the 1-dimensional data buffer.
wchar_t is used here for the path to be consistent with the Intel RealSense
SDK SaveXDM function but char is used everywhere else. 
*******************************************************************************/
bool WriteDepthBuf(PXCImage *zImage, wchar_t filePath[]) {
	if (!zImage) return false;

	FILE *dumpFile;
	_wfopen_s(&dumpFile, filePath, L"w");
	if (dumpFile == NULL)
	{
		fprintf(stderr, "Error opening dump file.\n");
		return false;
	}

	PXCImage::ImageInfo imageInfo = zImage->QueryInfo();
	/* You can examine the image format. We're expecting "...DEPTH".
	printf("Depth format: ");
	switch (imageInfo.format) {
	case PXCImage::PIXEL_FORMAT_DEPTH:
		printf("PIXEL_FORMAT_DEPTH\n");
		break;
	case PXCImage::PIXEL_FORMAT_DEPTH_RAW:
		printf("PIXEL_FORMAT_DEPTH_RAW\n");
		break;
	case PXCImage::PIXEL_FORMAT_DEPTH_F32:
		printf("PIXEL_FORMAT_DEPTH_F32\n");
		break;
	default:
		printf("Unknown\n");
	}
	*/

	pxcI32 sizeX = imageInfo.width;  // Number of pixels in X
	pxcI32 sizeY = imageInfo.height;  // Number of pixels in Y
	// The total image buffer will be sizeX * sizeY * 2 for PIXEL_FORMAT_DEPTH
	// Each pixel is represented as a 16 bit unsigned integer (2 bytes)
	// There may be additional data that should be indicated by the magnitute
	// of the pitch value. Currently, the pitch value for PIXEL_FORMAT_DEPTH is
	// 640 for x = 320 indicating that there is no additional data.
	
	// Initialize our imageData with info from the provided image sample
	PXCImage::ImageData imageData;
	imageData.format = imageInfo.format;
	// Initialize the planes and pitches arrays
	imageData.planes[0] = { 0 };
	imageData.pitches[0] = { 0 };

	// Below is a common AcquireAccess - Process - ReleaseAccess pattern
	// for RealSense images.
	pxcStatus status;
	status = zImage->AcquireAccess(PXCImage::ACCESS_READ, 
		PXCImage::PIXEL_FORMAT_DEPTH, &imageData);
	
	if (imageData.pitches[0] != 2 * sizeX) {
		fprintf_s(stderr, "Error: Unexpected data in buffer.\n");
		return false;
	}
	const pxcI32 pixelPitch = 2;

	if (status == PXC_STATUS_NO_ERROR) {
		pxcBYTE* imageArray = imageData.planes[0];
		for (pxcI32 y = 0; y < sizeY; y++) {
			for (pxcI32 x = 0; x < sizeX; x++) {
				// Each pixel is currently 2 bytes (pixelPitch)
				// 2 bytes * the x pixel + the y offset = the current pixel
				// y offset = y * image width in bytes (pitch)
				UINT16 byte1 = (UINT16)imageArray[pixelPitch * x + 
					                              y*imageData.pitches[0]];
				UINT16 byte2 = (UINT16)imageArray[pixelPitch * x + 
					                              y*imageData.pitches[0] + 1] 
												  << 8;
				// Writing out CSV format matrix
				fprintf(dumpFile, "%d", byte1 + byte2);
				if (x < sizeX - 1)
					fprintf(dumpFile, ",");
			}
			fprintf(dumpFile, "\n");
		}
	}
	else {
		fprintf(stderr, "Error acquiring data: ");
		switch (status){
		case PXC_STATUS_DEVICE_BUSY:
			fprintf(stderr, "PXC_STATUS_DEVICE_BUSY\n");
			break;
		case PXC_STATUS_PARAM_UNSUPPORTED:
			fprintf(stderr, "PXC_STATUS_PARAM_UNSUPPORTED\n");
			break;
		default:
			fprintf(stderr, "Unknown\n");
		}
		zImage->ReleaseAccess(&imageData);
		fclose(dumpFile);
		return false;
	}
	zImage->ReleaseAccess(&imageData);
	fclose(dumpFile);
	return true;
}

/*******************************************************************************
Main entry point. Here we demonstrate, after some initial housekeeping, how to
initialize the camera, start streaming, grab samples, and process them.
*******************************************************************************/
int main(int argc, char *argv[]) {
	if (!ProcessCmdArgs(argc, argv)) {
		return 1;
	}

	// Check / create file directories
	if (xdmPath != nullptr) {
		if (_mkdir(xdmPath) != 0 && errno != EEXIST){
			fprintf(stderr, "Error: Invalid XDM path. Error %d\n", errno);
			fprintf(stderr, "Terminate? [Y/n]\n");
			char choice = _getch();
			if (choice != 'n' && choice != 'N') {
				return ERROR_FILE;
			}
			xdmPath = nullptr;
		}
		else {
			// Remove any trailing '\' in the path since we'll add it later.
			if (xdmPath[strlen(xdmPath) - 1] == '\\')
				xdmPath[strlen(xdmPath) - 1] = '0';
		}
	}
	if (bufPath != nullptr) {
		if (_mkdir(bufPath) != 0 && errno != EEXIST){
			fprintf(stderr, "Error: Invalid Buffer path. Error %d\n", errno);
			fprintf(stderr, "Terminate? [Y/n]\n");
			char choice = _getch();
			if (choice != 'n' && choice != 'N') {
				return ERROR_FILE;
			}
			bufPath = nullptr;
		}
		else {
			// Remove any trailing '\' in the path since we'll add it later.
			if (bufPath[strlen(bufPath) - 1] == '\\')
				bufPath[strlen(bufPath) - 1] = '0';
		}
	}

	// Start timer for total test execution
	unsigned long long programStart = GetTickCount64();
	
	// Initialize camera and streams
	if (verbose) fprintf_s(stdout, "Initializing camera...\n");
	// The Sense Manager is the root object for interacting with the camera.
	PXCSenseManager *senseManager = nullptr;
	senseManager = PXCSenseManager::CreateInstance();
	if (!senseManager) {
		fprintf_s(stderr, "Unable to create the PXCSenseManager\n");
		return ERROR_CAMERA;
	}

	// When enabling the streams (color and depth), the parameters must match
	// the capabilities of the camera. For example, 60fps for color will fail
	// on the DS4 / R200.
	// Here we're hard-coding the resolution and frame rate
	senseManager->EnableStream(PXCCapture::STREAM_TYPE_COLOR, 320, 240, 30);
	senseManager->EnableStream(PXCCapture::STREAM_TYPE_DEPTH, 320, 240, 30);

	// Initialize the PXCSenseManager
	pxcStatus camStatus;
	camStatus = senseManager->Init();
	if (camStatus != PXC_STATUS_NO_ERROR) {
		fprintf_s(stderr, "Unable to initizlize PXCSenseManager\n");
		senseManager->Release();
		return ERROR_CAMERA;
	}
	
	PXCImage *colorImage;
	PXCImage *depthImage;
	PXCPhoto *xdmPhoto = senseManager->QuerySession()->CreatePhoto();

	// These two objects come from the Intel RealSense SDK helper for rendering
	// camera data. Any rendering technique could be used or omitted if no 
	// visual feedback is desired.
	UtilRender *renderColor = new UtilRender(L"COLOR STREAM");
	UtilRender *renderDepth = new UtilRender(L"DEPTH STREAM");
	
	// Start test
	if (verbose) fprintf_s(stdout, "Running...\n");
	// This section may be wrapped in additional code to automate 
	// repetitive tests. Closure provided just for convenience.
	{ // Beginning of single test block
		unsigned long totalFrames = 0;
		unsigned long long streamStart = GetTickCount64();
		for (unsigned int i = 0; i < framesPerTest; i++) {
			// Passing 'true' to AcquireFrame blocks until all streams are 
			// ready (depth and color).  Passing 'false' will result in 
			// frames unaligned in time.
			camStatus = senseManager->AcquireFrame(true);
			if (camStatus < PXC_STATUS_NO_ERROR) {
				fprintf_s(stderr, "Error acquiring frame: %f\n", camStatus);
				break;
			}
			// Retrieve all available image samples
			PXCCapture::Sample *sample = senseManager->QuerySample();
			if (sample == NULL) {
				fprintf_s(stderr, "Sample unavailable\n");
			}
			else {
				totalFrames++;

				colorImage = sample->color;
				depthImage = sample->depth;
				// Render the frames (not necessary)
				if (!renderColor->RenderFrame(colorImage)) break;
				if (!renderDepth->RenderFrame(depthImage)) break;

				// Save data if requested
				wchar_t filePath[MAXPATH];
				char fileName[18];
				TimeStampString(fileName);
				if (xdmPath != nullptr) { // Save XDM image
					swprintf_s(filePath, MAXPATH, L"%S\\%S%S", 
						xdmPath, 
						fileName,
						xdmExt);
					xdmPhoto->ImportFromPreviewSample(sample);
					pxcStatus saveStatus = xdmPhoto->SaveXDM(filePath);
					if (saveStatus != PXC_STATUS_NO_ERROR)
						fprintf_s(stderr, "Error: SaveXDM\n");
				}
				if (bufPath != NULL) { // Save depth buffer
					swprintf_s(filePath, MAXPATH, L"%S\\%S%S", 
						bufPath, fileName, depBufExt);
					// The WriteDepthBuf function has a lot of detail about
					// accessing image data and should be examined.
					if (!WriteDepthBuf(depthImage, filePath)) {
						fprintf(stderr, "Error: WriteDepthBuf\n");
					}
				}
			}

			// Unlock the current frame to fetch the next one
			senseManager->ReleaseFrame();

		}

		float frameRate = (float)totalFrames /
			((float)(GetTickCount64() - streamStart) / 1000);
		if (verbose) fprintf_s(stdout, "Frame Rate: %.2f\n", frameRate);
	} // End of single test block

	// Wrap up
	if(verbose) fprintf_s(stdout, "Finished in %llu seconds.\n",
		(GetTickCount64() - programStart) / 1000);

	delete renderColor;
	delete renderDepth;
	xdmPhoto->Release();
	if(senseManager != nullptr) senseManager->Release();

	return 0;
}
