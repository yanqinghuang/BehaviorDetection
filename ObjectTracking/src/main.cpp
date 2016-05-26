/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2014 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <stdio.h>
#include <Windows.h>
#include <WindowsX.h>
#include <gdiplus.h>
#include <commctrl.h>
#include "resource.h"
#include "pxctracker.h"
#include "pxcprojection.h"
#include "object_tracker.h"
#include "ui.h"
#include <vector>
#include <map>
#include "pxcmetadata.h"
#include "service/pxcsessionservice.h"
#include <iostream>
#include <fstream>

using namespace Gdiplus;
using namespace std;
using std::endl;

#define IDC_STATUS   10000
#define ID_DEVICEX   21000
#define ID_MODULEX   22000
#define ID_TRACKX	 23000

#define TEXT_HEIGHT 16


HINSTANCE   g_hInst=0;
PXCSession *g_session=0;
pxcCHAR     g_file[1024]={0};
pxcCHAR     g_calibrationFile[1024]={0};
pxcCHAR     g_buf[20];
std::vector<Model> g_targets(0);
pxcCHAR		g_status[512];

/* Panel Bitmap */
Bitmap     *g_bitmap = NULL;
Rect		drawnSize;
POINT		actualSize;
PXCRectI32	roi = {-1, -1, -1, -1 };
PXCPointI32 roiFirstClick = {-1, -1};
bool		drawingROI = false;
bool		validROI = false;

/* Threading control */
volatile bool g_running=false;
volatile bool g_stop=true;

static int fps, width, height;

HWND g_hWndDlg, g_hWndTrack, g_hWndMap;

bool toggleStartButton = false;

/** Layout Mangement **/

RECT initialDlgSize;
RECT oldDlgSize;

struct LayoutParam
{
	POINT delta;
	HWND  baseHWnd;
};

std::map<UINT, HWND> controlHWndMap;

BOOL CALLBACK ChildControlEnum(HWND hWndChild, LPARAM unused)
{
	UINT controlID = GetDlgCtrlID(hWndChild);

	if (controlID > 0)
	{
		controlHWndMap[controlID] = hWndChild;
	}

	return TRUE;
}

HWND __inline GetControl(UINT id)
{	
	HWND hwnd = controlHWndMap[id];

	if (hwnd == NULL)
	{
		if ((  (hwnd = GetDlgItem(g_hWndDlg, id))   != NULL)
		|| (   (hwnd = GetDlgItem(g_hWndTrack, id)) != NULL)
		|| (   (hwnd = GetDlgItem(g_hWndMap, id))   != NULL))
		{
			return hwnd;
		}
	}

	return hwnd;
}

BOOL CALLBACK ChildLayoutCallback(HWND hWndChild, LPARAM param)
{
	enum FixedEdges { LEFT = 0x1, RIGHT = 0x2, TOP = 0x4, BOTTOM = 0x8 };
	LayoutParam *layout = (LayoutParam*) param;

	RECT size;
	int edges = TOP | RIGHT | BOTTOM;	

	if ((GetParent(hWndChild) == g_hWndTrack || GetParent(hWndChild) == g_hWndMap))
	{
		if (layout->baseHWnd == g_hWndDlg)
		{
			return TRUE;
		}
		else if (GetParent(hWndChild) == g_hWndTrack)
		{
			// Redefine the default for the tracking pane
			edges = TOP | LEFT | BOTTOM;
		}
		else
		{
			edges = TOP | LEFT | BOTTOM | RIGHT;
		}
	}
				
	// Adjust sizes
	GetWindowRect(hWndChild, &size);

	UINT id = (hWndChild == g_hWndTrack ? IDD_TRACKINGVIEW : (hWndChild == g_hWndMap ? IDD_MAPVIEW : GetDlgCtrlID(hWndChild)));

	switch (id)
	{
	case IDC_PANEL:			
		edges = TOP | LEFT;
		break;		
	case IDD_TRACKINGVIEW:
	case IDD_MAPVIEW:
	case IDC_STATUS:
	case IDC_PANELSELECT:				
		edges = BOTTOM | LEFT;
		break;	

	case IDC_LOADMODEL:
	case IDC_CLEARMODELS:		
	case IDC_SAVEMAPAS:
	case IDC_TOOLBOX:
	case IDC_TRACKINGTYPE:
		edges = TOP | RIGHT | BOTTOM;
		break;
	}	
	
	POINT origin  = {size.left, size.top};
	POINT newSize = {size.right - size.left, size.bottom - size.top};

	POINT old = origin;
		
	origin.x  += (edges & LEFT) ? 0 : layout->delta.x;
	origin.y  += (edges & TOP)  ? 0 : layout->delta.y;			
	newSize.x += (edges & RIGHT) ? 0 : layout->delta.x;
	newSize.y += (edges & BOTTOM) ? 0 : layout->delta.y;
				
	ScreenToClient(GetParent(hWndChild), &origin);
		
	SetWindowPos(hWndChild, GetParent(hWndChild), origin.x, origin.y, newSize.x, newSize.y, SWP_NOZORDER);
	

	return TRUE;
}

void SaveLayout(RECT& storage = oldDlgSize)
{
	GetClientRect(g_hWndDlg, &storage);
	ClientToScreen(g_hWndDlg, (LPPOINT) &storage.left);  // Top left corner
	ClientToScreen(g_hWndDlg, (LPPOINT) &storage.right); // Bottom right corner
}

void RedoLayout(HWND hwnd)
{
	LayoutParam param;

	RECT curDlgSize;
	SaveLayout(curDlgSize);
		
	param.baseHWnd = hwnd;
	param.delta.x = (curDlgSize.right - oldDlgSize.right);
	param.delta.y = (curDlgSize.bottom - oldDlgSize.bottom);

	EnumChildWindows(hwnd, ChildLayoutCallback, (LPARAM) &param);

	if (hwnd == g_hWndDlg)
	{
		SaveLayout();
	}

	InvalidateRect(g_hWndDlg, NULL, false);
}

PXCPointI32 GetMouseCoords(WORD x, WORD y)
{
	// Get the top left corner of the render panel (in client coords relative to the main window)
	PXCPointI32 pt = {-1, -1};
	RECT panelRect;
	GetWindowRect(GetControl(IDC_PANEL), &panelRect);	
	RECT frameCoords = panelRect;	
	ScreenToClient(g_hWndDlg, (LPPOINT) &frameCoords.left);
	ScreenToClient(g_hWndDlg, (LPPOINT) &frameCoords.right);

	if (drawnSize.Height == 0 || drawnSize.Width == 0)
	{
		return pt;
	}

	if (x < frameCoords.left || x > frameCoords.right || y < frameCoords.top || y > frameCoords.bottom)
	{	
		return pt;
	}

	pt.x = (x - frameCoords.left - drawnSize.X) * actualSize.x / (drawnSize.Width);
	pt.y = (y - frameCoords.top  - drawnSize.Y) * actualSize.y / (drawnSize.Height);

	return pt;
}

/** Fill out menus based on detected devices, modules, etc **/

void PopulateDeviceMenu(HMENU menu)
{
	DeleteMenu(menu,0,MF_BYPOSITION);

	PXCSession::ImplDesc desc;
	memset(&desc,0,sizeof(desc));
	desc.group=PXCSession::IMPL_GROUP_SENSOR;
	desc.subgroup=PXCSession::IMPL_SUBGROUP_VIDEO_CAPTURE;
	HMENU menu1=CreatePopupMenu();
	for (int i=0,k=ID_DEVICEX;;i++)
	{
		PXCSession::ImplDesc desc1;
		if (g_session->QueryImpl(&desc,i,&desc1)<PXC_STATUS_NO_ERROR) break;
		PXCCapture *capture;
		if (g_session->CreateImpl<PXCCapture>(&desc1,&capture)<PXC_STATUS_NO_ERROR) continue;
		for (int j=0;;j++) {
			PXCCapture::DeviceInfo dinfo;
			if (capture->QueryDeviceInfo(j,&dinfo)<PXC_STATUS_NO_ERROR) break;
			if (dinfo.orientation == PXCCapture::DEVICE_ORIENTATION_REAR_FACING) break;
			AppendMenu(menu1,MF_STRING,k++,dinfo.name);
		}
	}
	CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),0,MF_BYPOSITION);
	InsertMenu(menu,0,MF_BYPOSITION|MF_POPUP,(UINT_PTR)menu1,L"Device");
}

void PopulateModuleMenu(HMENU menu)
{
	DeleteMenu(menu,1,MF_BYPOSITION);

	PXCSession::ImplDesc desc, desc1;
	memset(&desc,0,sizeof(desc));
	desc.cuids[0]=PXCTracker::CUID;
	HMENU menu1=CreatePopupMenu();
	int i;
	for (i=0;;i++) {
		if (g_session->QueryImpl(&desc,i,&desc1)<PXC_STATUS_NO_ERROR) break;
		AppendMenu(menu1,MF_STRING,ID_MODULEX+i,desc1.friendlyName);
	}
	CheckMenuRadioItem(menu1,0,i,0,MF_BYPOSITION);
	InsertMenu(menu,1,MF_BYPOSITION|MF_POPUP,(UINT_PTR)menu1,L"Module");
}

static int GetCheckedMenuIndex(HMENU menu) {
	for (int i=0;i<GetMenuItemCount(menu);i++)
		if (GetMenuState(menu,i,MF_BYPOSITION)&MF_CHECKED) return i;
	return 0;
}

pxcCHAR* GetCheckedDevice() {
	HMENU menu=GetSubMenu(GetMenu(g_hWndDlg),0);	// ID_DEVICE
	static pxcCHAR line[256];
	GetMenuString(menu,GetCheckedMenuIndex(menu),line,sizeof(line)/sizeof(pxcCHAR),MF_BYPOSITION);
	return line;
}


pxcCHAR *GetCheckedModule() {
	HMENU menu=GetSubMenu(GetMenu(g_hWndDlg),1);	// ID_MODULE
	static pxcCHAR line[256];
	GetMenuString(menu,GetCheckedMenuIndex(menu),line,sizeof(line)/sizeof(pxcCHAR),MF_BYPOSITION);
	return line;
}

static DWORD WINAPI PipelineProc(LPVOID arg)
{
	// Wait for any previous thread to stop
	if (g_running)
	{
		g_stop = true;

		while (g_running == true)
			Sleep(5);
	}

	g_stop=false;
	g_running=true;

	if (arg == 0)
	{
		ObjectTrackingPipeline(g_session);
	}
	else
	{
		MapCreationPipeline(g_session);
	}

	g_running=false;
	PostMessage(g_hWndDlg,WM_COMMAND,IDC_STOP,0);	
	return 0;
}

void SetStatus(pxcCHAR *line)
{	
	SetWindowText(GetControl(IDC_STATUS),line);
	g_status[0] = 0;

	if (wcscmp(line, L"Streaming") == 0 && toggleStartButton)
	{
		Button_Enable(GetControl(IDC_START), TRUE);
		toggleStartButton = false;
	}
}

void AppendStatus(pxcCHAR *text)
{
	wcscpy_s(g_status, text);
}

bool IsPlaybackState() {
	return (GetMenuState(GetMenu(g_hWndDlg),ID_MODE_PLAYBACK,MF_BYCOMMAND)&MF_CHECKED)!=0;
}

bool IsRecordState() {
	return (GetMenuState(GetMenu(g_hWndDlg),ID_MODE_RECORD,MF_BYCOMMAND)&MF_CHECKED)!=0;
}

void DrawBitmap(PXCImage *image) {
	if (!image) return;

	// Calcualte FPS
    static int g_fps_nframes;
    static double g_fps_first;
    if ((g_fps_nframes++)==0) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        g_fps_first=(double)now.QuadPart/(double)freq.QuadPart;
    }
    if (g_fps_nframes>30) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        fps=(int)((double)g_fps_nframes/((double)now.QuadPart/(double)freq.QuadPart-g_fps_first));

        pxcCHAR line[1024];
        swprintf_s<1024>(line,L"Rate (%d fps)  %s", fps, g_status);
        SetWindowText(GetControl(IDC_STATUS),line);
        g_fps_nframes=0;
    }

    PXCImage::ImageInfo info = image->QueryInfo();
    PXCImage::ImageData data;	
    if (image->AcquireAccess(PXCImage::ACCESS_READ,PXCImage::PIXEL_FORMAT_RGB32, &data)>=PXC_STATUS_NO_ERROR)
	{
		if (g_bitmap == NULL)
		{
			g_bitmap = new Bitmap(info.width, info.height, PixelFormat32bppRGB);
		}		

		if (g_bitmap->GetWidth() != info.width || g_bitmap->GetHeight() != info.height)
		{
			delete g_bitmap;
			g_bitmap = new Bitmap(info.width, info.height, PixelFormat32bppRGB);
		}
		
		Rect r(0, 0, g_bitmap->GetWidth(), g_bitmap->GetHeight());
		BitmapData bitmapData;
		bitmapData.Width = info.width;
		bitmapData.Height = info.height;
		bitmapData.Scan0 = data.planes[0];
		bitmapData.Stride = data.pitches[0];
		bitmapData.PixelFormat = PixelFormat32bppRGB;

		g_bitmap->LockBits(&r, ImageLockModeUserInputBuf | ImageLockModeWrite, PixelFormat32bppRGB, &bitmapData);
		g_bitmap->UnlockBits(&bitmapData);
		
		image->ReleaseAccess(&data);

		width  = info.width;//.bmiHeader.biWidth;
		height = info.height; //-binfo.bmiHeader.biHeight;
    }

	// Draw the ROI window (darken the rest of the image)
	if ((Button_GetCheck(GetControl(IDC_ROI)) == BST_CHECKED) && (drawingROI || validROI))
	{
		Graphics roiG(g_bitmap);

		SolidBrush dimGrey(Color(200, 20, 20, 20));
		roiG.FillRectangle(&dimGrey, 0, 0, width, roi.y);	// Top bar
		roiG.FillRectangle(&dimGrey, 0, roi.y, roi.x, roi.h); // Left side
		roiG.FillRectangle(&dimGrey, roi.x + roi.w, roi.y, width - roi.x - roi.w, roi.h); // Right side
		roiG.FillRectangle(&dimGrey, 0, roi.y + roi.h, width, height - roi.y - roi.h);	// Bottom bar
	}
}

static Rect GetResizeRect(RECT clientRect, int bmWidth, int bmHeight)
{ /* Keep the aspect ratio */
	Rect rc1;

	float sx   = (float) clientRect.right / (float)bmWidth;
	float sy   = (float) clientRect.bottom / (float)bmHeight;
	float sxy  = min(sx, sy);

	rc1.Width  = (int)(bmWidth * sxy);
	rc1.X      = (clientRect.right - rc1.Width) / 2 + clientRect.left;
	rc1.Height = (int)(bmHeight * sxy);
	rc1.Y      = (clientRect.bottom - rc1.Height) / 2 + clientRect.top;

	return rc1;
}

void UpdatePanel(bool StatusUpdated) {
	if (!g_bitmap) return;

	HWND panel=GetControl(IDC_PANEL);
	RECT rc;
	GetClientRect(panel,&rc);
	
	Bitmap buffer(rc.right, rc.bottom);
	Graphics back(&buffer);
	
	SolidBrush greyBrush(Color(255, 128, 128, 128));	
	back.FillRectangle(&greyBrush, 0, 0, rc.right, rc.bottom);
	
	if (Button_GetState(GetControl(IDC_SCALE)) & BST_CHECKED)
	{
		drawnSize = GetResizeRect(rc,g_bitmap->GetWidth(), g_bitmap->GetHeight());		
	}
	else
	{
		drawnSize = Rect(0, 0, g_bitmap->GetWidth(), g_bitmap->GetHeight());		
	}
	
	actualSize.x = g_bitmap->GetWidth();
	actualSize.y = g_bitmap->GetHeight();

	back.DrawImage(g_bitmap, drawnSize);

	Graphics front(panel);
	front.DrawImage(&buffer, 0, 0);

	if (StatusUpdated)
	{
		InvalidateRect(GetControl( IDC_MODELS), 0, TRUE);
	}
}

void WriteToFile(PXCPoint3DF32 eyePt) {
	HANDLE hFile;
	char dataBuffer[256];
	sprintf_s(dataBuffer, "%f, %f, %f;\r\n", eyePt.x, eyePt.y, eyePt.z);
	DWORD dwBytesToWrite = (DWORD)strlen(dataBuffer);
	DWORD dwBytesWritten = 0;
	BOOL bErrorFlag = FALSE;

	hFile = CreateFile(L"3Dpose.txt",  // name of write file
		FILE_APPEND_DATA,		  // write to the end
		FILE_SHARE_WRITE,		  // do not share
		NULL,					  // default security
		OPEN_EXISTING,			  // open existing file only
		FILE_ATTRIBUTE_NORMAL,    // normal file
		NULL);                    // no attr. template
	if (hFile == INVALID_HANDLE_VALUE) {
		char errBuffer[256];
		DWORD dwErr = GetLastError();
		sprintf_s(errBuffer, "last error 0x%x \n", dwErr);
		OutputDebugString(L"Open File Failed!\n");
		OutputDebugStringA(errBuffer);
	}
	//databuffer[strlen(databuffer)] = '\n';
	bErrorFlag = WriteFile(hFile,  // open file handle
		dataBuffer,				   // start of data to write
		dwBytesToWrite,            // number of bytes to write
		&dwBytesWritten,           // number of bytes that were written
		NULL);                     // no overlapped structure
	if (bErrorFlag == FALSE) {
		OutputDebugString(L"Write data to file failed!\n");
	}
	CloseHandle(hFile);
}

PXCPointF32 ProjectPoint(PXCPoint3DF32 pt, PXCPoint3DF32& translation, PXCPoint4DF32 &rotation, PXCProjection *projection)
{
	PXCPointF32 dst;		// Coordinates on the color plane
	PXCPoint3DF32 eyePt;	// Transformed coordinates in camera space
	float rotMat[3][3];
	PXCPoint4DF32 q = rotation;
		
	// Create the rotation matrix from the quaternion
	rotMat[0][0]=(1-(2*(q.y*q.y))-(2*(q.z*q.z))); rotMat[0][1]=((2*q.x*q.y)-(2*q.w*q.z));	rotMat[0][2]=((2*q.x*q.z)+(2*q.w*q.y));
	rotMat[1][0]=((2*q.x*q.y)+(2*q.w*q.z));		  rotMat[1][1]=(1-(2*q.x*q.x)-(2*q.z*q.z));	rotMat[1][2]=((2*q.y*q.z)-(2*q.w*q.x));
	rotMat[2][0]=((2*q.x*q.z)-(2*q.w*q.y));		  rotMat[2][1]=((2*q.y*q.z)+(2*q.w*q.x));	rotMat[2][2]=(1-(2*q.x*q.x)-(2*q.y*q.y));
	
	// Convert model coords into eye coords by multiplying by the translation/rotation ("model view") matrix
	eyePt.x = pt.x * rotMat[0][0] + pt.y * rotMat[0][1] + pt.z * rotMat[0][2] + translation.x;
	eyePt.y = pt.x * rotMat[1][0] + pt.y * rotMat[1][1] + pt.z * rotMat[1][2] + translation.y;
	eyePt.z = pt.x * rotMat[2][0] + pt.y * rotMat[2][1] + pt.z * rotMat[2][2] + translation.z;
	
	//Store 3D position in camera space 
	char dataBuffer[256];
	sprintf_s(dataBuffer, "%f, %f, %f;\r\n", eyePt.x, eyePt.y, eyePt.z);
	OutputDebugStringA(dataBuffer);
	WriteToFile(eyePt);

	if (projection)
	{
		projection->ProjectCameraToColor(1, &eyePt, &dst);
	}
	else
	{
		dst.x = eyePt.x + width / 2;
		dst.y = eyePt.y + height / 2;
	}
	
	return dst;
}

#define MOVE_TO(x,y) MoveToEx(dc2, (int) x, (int) y, NULL)
#define LINE_TO(x,y) LineTo(dc2, (int) x, (int) y)

void DrawTrackingValues(PXCTracker::TrackingValues *tv, PXCProjection *proj)
{
	if (!g_bitmap) return;

	Graphics g(g_bitmap);
		
	Pen red(Color(255,   255,   0,   0), 3);
	Pen blue(Color(255,    0,   0, 255), 3);
	Pen green(Color(255,   0, 255,   0), 3);
	Pen white(Color(255, 255, 255, 255), 3);
	if (Button_GetState(GetDlgItem(g_hWndDlg,IDC_LOCATION)) & BST_CHECKED)
	{		
		float objSize = 50;
		PXCPoint3DF32 xVec = { objSize,		  0,	   0};
		PXCPoint3DF32 yVec = {       0, objSize,	   0};
		PXCPoint3DF32 zVec = {       0,       0, objSize};

		PXCPointF32 xEnd = ProjectPoint(xVec, tv->translation, tv->rotation, proj);
		PXCPointF32 yEnd = ProjectPoint(yVec, tv->translation, tv->rotation, proj);
		PXCPointF32 zEnd = ProjectPoint(zVec, tv->translation, tv->rotation, proj);
		
		//only draws axis if button is checked
		if (Button_GetState(GetControl(IDC_XAXIS)) & BST_CHECKED)
		{
			//draw X (RED)			
			g.DrawLine(&red, tv->translationImage.x, tv->translationImage.y, xEnd.x, xEnd.y);
		}

		//only draws axis if button is checked
		if (Button_GetState(GetControl(IDC_YAXIS)) & BST_CHECKED)
		{
			//draw Y (GREEN)
			g.DrawLine(&green, tv->translationImage.x, tv->translationImage.y, yEnd.x, yEnd.y);
		}

		//only draws axis if button is checked
		if (Button_GetState(GetControl(IDC_ZAXIS)) & BST_CHECKED)
		{
			//draw Z (BLUE)
			g.DrawLine(&blue, tv->translationImage.x, tv->translationImage.y, zEnd.x, zEnd.y);
		}

		// Draw the target (model) number		
		pxcCHAR idText[4];
		int len = swprintf_s(idText, L"%d", tv->cosID);

		// Truncate the floating part of the coordinate to prevent jitter on-screen
		RectF textRect((float)((int) tv->translationImage.x), (float)((int) tv->translationImage.y), 12, 15);
		g.FillRectangle(&SolidBrush(Color(255, 255, 255, 255)), textRect);
		g.DrawString(idText, len, &Font(L"Arial", 10, FontStyleBold ), textRect, &StringFormat(), &SolidBrush(Color(255, 0, 0, 0)));		
	}		
}

/** gets the playback file **/
static void GetPlaybackFile(void)
{
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrFilter=L"RSSDK clip (*.rssdk)\0*.rssdk\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile=g_file; g_file[0]=0;
	ofn.nMaxFile=sizeof(g_file)/sizeof(pxcCHAR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetOpenFileName(&ofn)) g_file[0]=0;
}

/** opens the explorer to set record file **/
static void GetRecordFile(void)
{
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrFilter=L"RSSDK clip (*.rssdk)\0*.rssdk\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile=g_file; g_file[0]=0;
	ofn.nMaxFile=sizeof(g_file)/sizeof(pxcCHAR);
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetSaveFileName(&ofn)) g_file[0]=0;
	if (ofn.nFilterIndex==1 && ofn.nFileExtension==0) {
		size_t len = wcsnlen(g_file,sizeof(g_file)/sizeof(g_file[0]));
		if (len>1 && len<sizeof(g_file)/sizeof(pxcCHAR)-7) {
			wcscpy_s(&g_file[len], rsize_t(7), L".rssdk\0");
		}
	}
}

static void GetCalibrationFile(void)
{
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrFilter=L"Camera Calibration (*.xml)\0*.xml\0All Files (*.*)\0*.*\0\0";
	ofn.lpstrFile=g_calibrationFile; g_calibrationFile[0]=0;
	ofn.nMaxFile=sizeof(g_calibrationFile)/sizeof(pxcCHAR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (!GetOpenFileName(&ofn)) g_calibrationFile[0]=0;
}

void StartPipelineThread(bool tracking)
{	
	HANDLE thread = CreateThread(0,0,PipelineProc,(LPVOID) (tracking ? 0 : 1),0,0);
	CloseHandle(thread);	
}

INT_PTR CALLBACK TrackingUIProc(HWND hwndTrk, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MapCreationUIProc(HWND hwndTrk, UINT message, WPARAM wParam, LPARAM lParam);

void CreateTabPanel(HWND hWndDlg)
{
	HWND hwndTab = GetControl(IDC_PANELSELECT);
	HWND hWndTracking;
	HWND hWndMapView;	
	
	hWndTracking = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_TRACKINGVIEW), hWndDlg, TrackingUIProc);
	hWndMapView = CreateDialog(g_hInst, MAKEINTRESOURCE(IDD_MAPVIEW), hWndDlg, MapCreationUIProc);
	
	TCITEM tabItem;
	tabItem.mask = TCIF_TEXT | TCIF_PARAM;
	tabItem.pszText = L"Tracking";
	tabItem.lParam = (LPARAM) hWndTracking;

	TabCtrl_InsertItem(hwndTab, 0, &tabItem);
		
	tabItem.mask = TCIF_TEXT | TCIF_PARAM;
	tabItem.pszText = L"Map Creation";
	tabItem.lParam = (LPARAM) hWndMapView;

	TabCtrl_InsertItem(hwndTab, 1, &tabItem);
	
	g_hWndTrack = hWndTracking;
	g_hWndMap = hWndMapView;

	TabCtrl_SetCurFocus(hwndTab, 0);
	ShowWindow(hWndTracking, SW_SHOW);
		
	CheckDlgButton(hWndMapView, IDC_CREATEMAP, BST_CHECKED);
}

INT_PTR CALLBACK MainDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) { 
	HMENU	   menu			   = GetMenu(hwndDlg);
	HMENU	   menu1;	
	
	switch (message) { 
    case WM_INITDIALOG:
		{
			g_hWndDlg = hwndDlg;

		PopulateDeviceMenu(menu);
		PopulateModuleMenu(menu);

		CreateTabPanel(hwndDlg);
		
		SaveLayout();
		GetWindowRect(hwndDlg, &initialDlgSize);

		// Add all child controls to the HWND <-> ID map
		EnumChildWindows(hwndDlg, ChildControlEnum, NULL);

		Button_SetCheck(GetControl(IDC_LOCATION),BST_CHECKED);
			Button_SetCheck(GetControl(IDC_SCALE),BST_CHECKED);
			Button_SetCheck(GetControl(IDC_XAXIS),BST_CHECKED);
			Button_SetCheck(GetControl(IDC_YAXIS),BST_CHECKED);
			Button_SetCheck(GetControl(IDC_ZAXIS),BST_CHECKED);
        return TRUE; 
		}
    case WM_COMMAND: 
		menu1=GetSubMenu(menu,0);
		if (LOWORD(wParam)>=ID_DEVICEX && LOWORD(wParam)<ID_DEVICEX+GetMenuItemCount(menu1)) {
			CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),LOWORD(wParam)-ID_DEVICEX,MF_BYPOSITION);
			return TRUE;
		}
		menu1=GetSubMenu(menu,1);
		if (LOWORD(wParam)>=ID_MODULEX && LOWORD(wParam)<ID_MODULEX+GetMenuItemCount(menu1)) {
			CheckMenuRadioItem(menu1,0,GetMenuItemCount(menu1),LOWORD(wParam)-ID_MODULEX,MF_BYPOSITION);
			return TRUE;
		}
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
		{
			g_stop=true;
			if (g_running) {
				PostMessage(hwndDlg,WM_COMMAND,IDCANCEL,0);
			} else {
				DestroyWindow(hwndDlg); 
				PostQuitMessage(0);
			}
            return TRUE;
		}
		case IDC_START:
			Button_Enable(GetControl(IDC_START),false);
			Button_Enable(GetControl(IDC_STOP),true);

			if (TabCtrl_GetCurSel(GetControl(IDC_PANELSELECT)) == 0)
			{
				ComboBox_Enable(GetControl(IDC_TRACKINGTYPE), false);
				Button_Enable(GetControl(IDC_LOADMODEL), false);
				Button_Enable(GetControl(IDC_CLEARMODELS), false);
				for (int i=0;i<GetMenuItemCount(menu);i++)
					EnableMenuItem(menu,i,MF_BYPOSITION|MF_GRAYED);
				DrawMenuBar(hwndDlg);
				
				StartPipelineThread(true);
				Sleep(0);
			}
			else
			{
				bool StartMapCreationOperation();

				if (!g_running)
				{	
					StartPipelineThread(false);
				}

				if (!StartMapCreationOperation())
				{
					Button_Enable(GetControl(IDC_START),true);
					Button_Enable(GetControl(IDC_STOP),false);
				}
			}
			return TRUE;
		case IDC_STOP:			
			if (TabCtrl_GetCurSel(GetControl(IDC_PANELSELECT)) == 1)
			{
				// For map creation, keep streaming live video, but stop processing the frames
				void StopMapCreationOperation();
				StopMapCreationOperation();
				Button_Enable(GetControl(IDC_START),true);
				Button_Enable(GetControl(IDC_STOP),false);
			}
			else
			{
				// Stop the video stream (and tracking)
				g_stop=true;
				if (g_running) {
					PostMessage(hwndDlg,WM_COMMAND,IDC_STOP,0);
				} else {
					for (int i=0;i<GetMenuItemCount(menu);i++)
						EnableMenuItem(menu,i,MF_BYPOSITION|MF_ENABLED);
					DrawMenuBar(hwndDlg);
					Button_Enable(GetControl(IDC_START),true);
					Button_Enable(GetControl(IDC_STOP),false);
					ComboBox_Enable(GetControl(IDC_TRACKINGTYPE), true);					
					if (GetTrackingType() != TRACKING_INSTANT)
					{
						Button_Enable(GetControl(IDC_LOADMODEL), true);
						Button_Enable(GetControl(IDC_CLEARMODELS), true);
					}					
				}
			}
			return TRUE;
		case ID_MODE_LIVE:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_CHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_UNCHECKED);
			return TRUE;
		case ID_MODE_PLAYBACK:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_CHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_UNCHECKED);
			GetPlaybackFile();
			return TRUE;
		case ID_MODE_RECORD:
			CheckMenuItem(menu,ID_MODE_LIVE,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_PLAYBACK,MF_UNCHECKED);
			CheckMenuItem(menu,ID_MODE_RECORD,MF_CHECKED);
			GetRecordFile();
			return TRUE;
		case ID_CALIBRATION_LOAD:
			GetCalibrationFile();
			return TRUE;
		case IDC_ROI:
			if (Button_GetCheck(GetControl(IDC_ROI)) == BST_UNCHECKED)
			{
				drawingROI = false;
				validROI   = false;

				roi.x = roi.y = roi.w = roi.h = 0;

				if (TabCtrl_GetCurSel(GetControl(IDC_PANELSELECT)) == 0)
				{					
					ObjectTracking_ROIUpdated();
				}
				else
				{
					MapCreation_ROIUpdated();
				}
			}
			return TRUE;
		}
		break;
	case WM_LBUTTONDOWN:
		if (Button_GetCheck(GetControl(IDC_ROI)) == BST_CHECKED)
		{
			if (!drawingROI || validROI)
			{
				roiFirstClick = GetMouseCoords(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				drawingROI = true;
				validROI = false;
				SendMessage(hwndDlg, WM_MOUSEMOVE, wParam, lParam);
			}
			else if (drawingROI)
			{
				drawingROI = false;
				validROI = true;

				if (TabCtrl_GetCurSel(GetControl(IDC_PANELSELECT)) == 0)
				{					
					ObjectTracking_ROIUpdated();
				}
				else
				{
					MapCreation_ROIUpdated();
				}
			}
		}
		return TRUE;
	case WM_MOUSEMOVE:
		if (Button_GetCheck(GetControl(IDC_ROI)) == BST_CHECKED)
		{
			if (drawingROI)
			{
				PXCPointI32 mousePos = GetMouseCoords(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				roi.x = min(roiFirstClick.x, mousePos.x);
				roi.y = min(roiFirstClick.y, mousePos.y);
				roi.w = max(roiFirstClick.x, mousePos.x) - roi.x;
				roi.h = max(roiFirstClick.y, mousePos.y) - roi.y;
			}
		}
		return TRUE;
	case WM_GETMINMAXINFO:
		{
			MINMAXINFO *minMax = (MINMAXINFO*) lParam;
			minMax->ptMinTrackSize.x = initialDlgSize.right - initialDlgSize.left;
			minMax->ptMinTrackSize.y = initialDlgSize.bottom - initialDlgSize.top;
			break;
		}
	case WM_NOTIFY:		
		switch ( ((LPNMHDR) lParam)->code)
		{
			case TCN_SELCHANGING:
			{
				int selTab = TabCtrl_GetCurSel(GetControl( IDC_PANELSELECT));
				TCITEM item;
				item.mask = TCIF_PARAM;
				TabCtrl_GetItem(GetControl( IDC_PANELSELECT), selTab, &item);
				ShowWindow((HWND) item.lParam, SW_HIDE);
				break;
			}
			case TCN_SELCHANGE:
			{
				HWND hwndTab =GetControl(IDC_PANELSELECT);
				int selTab = TabCtrl_GetCurSel(hwndTab);
				TCITEM item;
				item.mask = TCIF_PARAM;
				TabCtrl_GetItem(hwndTab, selTab, &item);
				ShowWindow((HWND) item.lParam, SW_SHOW);

				if (selTab == 1)
				{
					Button_Enable(GetControl(IDC_START), FALSE);
					toggleStartButton = true;
					StartPipelineThread(false);					
					EnableMenuItem(menu, 2, MF_BYPOSITION | MF_GRAYED);
					DrawMenuBar(g_hWndDlg);
					Button_Enable(GetControl(IDC_ROI), true);					
				}
				else
				{
					EnableMenuItem(menu, 2, MF_BYPOSITION | MF_ENABLED);
					DrawMenuBar(g_hWndDlg);					
					if (ComboBox_GetItemData(GetControl(IDC_TRACKINGTYPE), ComboBox_GetCurSel(GetControl(IDC_TRACKINGTYPE))) == TRACKING_INSTANT)
					{
						Button_Enable(GetControl(IDC_ROI), true);
					}
					else
					{
						Button_SetCheck(GetControl(IDC_ROI), false);
						Button_Enable(GetControl(IDC_ROI), false);
					}

					g_stop = true;
				}
				break;
			}			
		}
		break;
	case WM_MOVE:
		SaveLayout();
		return TRUE;
	case WM_SIZE:	
		RedoLayout(hwndDlg);
		return TRUE;
    } 
    return FALSE; 
} 

#pragma warning(disable:4706) /* assignment within conditional */
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
	INITCOMMONCONTROLSEX init;
	init.dwSize = sizeof(init);
	init.dwICC = ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES;
	InitCommonControlsEx(&init);
	g_hInst=hInstance;

	g_session=PXCSession::CreateInstance();
	if (!g_session) {
		MessageBoxW(0,L"Failed to create an SDK session",L"Object Tracker",MB_ICONEXCLAMATION|MB_OK);
        return 1;
    }

    HWND hwnd=CreateDialogW(hInstance,MAKEINTRESOURCE(IDD_MAINFRAME),0,MainDialogProc);
    if (!hwnd)  {
		MessageBoxW(0,L"Failed to create a window",L"Object Tracker",MB_ICONEXCLAMATION|MB_OK);
        return 1;
    }

	HWND hWnd2=CreateStatusWindow(WS_CHILD|WS_VISIBLE,L"OK",hwnd,IDC_STATUS);
	if (!hWnd2) {
		MessageBoxW(0,L"Failed to create a status bar",L"Object Tracker",MB_ICONEXCLAMATION|MB_OK);
        return 1;
	}

	GdiplusStartupInput input;
	ULONG_PTR token;
	GdiplusStartup(&token, &input, NULL);
		
    MSG msg;
	//defaultfile = true;
	for (int sts;(sts=GetMessageW(&msg,NULL,0,0));) {
        if (sts == -1) return sts;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
	g_stop=true;
	while (g_running) Sleep(5);
	g_session->Release();

	GdiplusShutdown(token);
				
    return (int)msg.wParam;
}

