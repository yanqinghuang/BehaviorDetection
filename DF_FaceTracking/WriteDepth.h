#pragma once
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


class WriteDepth
{
	public:
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
			switch (status) {
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
};

