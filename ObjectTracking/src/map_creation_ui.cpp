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
#include <commctrl.h>
#include "resource.h"
#include "pxctracker.h"
#include "pxctrackerutils.h"
#include "object_tracker.h"
#include "ui.h"

extern HINSTANCE g_hInst;
extern HWND g_hWndDlg, g_hWndTrack, g_hWndMap;

pxcCHAR g_mapFile[1024] = {0};

void GetMapFile()
{	
	OPENFILENAME ofn;

	// Disable the start button until a valid file is loaded
	EnableWindow(GetControl(IDC_START), FALSE);

	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrInitialDir = L"";
	ofn.lpstrFilter=L"3D Map file (.slam)\0*.slam\0All Files\0*.*\0";
	ofn.lpstrFile=g_mapFile; g_mapFile[0] = 0;
	ofn.nMaxFile=sizeof(g_mapFile)/sizeof(pxcCHAR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileName(&ofn))
	{
		EnableWindow(GetControl(IDC_START), TRUE);
	}
	else
	{
		g_mapFile[0]=0;
	}	
}

void SaveMapFile()
{	
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrInitialDir = L"";
	ofn.lpstrFilter=L"3D Map file (.slam)\0*.slam\0";
	ofn.lpstrFile=g_mapFile; g_mapFile[0] = 0;
	ofn.nMaxFile=sizeof(g_mapFile)/sizeof(pxcCHAR);
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetSaveFileName(&ofn))
	{
		// Make sure the file has a .slam extension (necessary for loading the files)
		size_t fileLen = wcsnlen(g_mapFile,sizeof(g_mapFile)/sizeof(g_mapFile[0]));		
		if (fileLen < 5 || _wcsnicmp(&g_mapFile[fileLen - 5], L".slam", 4) != 0)
		{
			wcscat_s(g_mapFile, L".slam");
		}
	}
	else
	{
		g_mapFile[0]=0;
	}
}

bool GetMarkerInfo(int& id, int& size)
{
	pxcCHAR buf[10], *endPtr;
	Edit_GetLine(GetControl(IDC_MARKERID), 0, buf, 10);
	id = wcstoul(buf, &endPtr, 10);

	if (endPtr == buf || id == 0)	
		return false;
	
	Edit_GetLine(GetControl(IDC_MARKERSIZE), 0, buf, 10);
	size = wcstoul(buf, &endPtr, 10);
	if (endPtr == buf || id == 0)	
		return false;

	return true;
}

PXCTrackerUtils::ObjectSize GetObjectSize()
{
	int curSel = ComboBox_GetCurSel(GetControl(IDC_OBJECTSIZE));
	return (PXCTrackerUtils::ObjectSize) ComboBox_GetItemData(GetControl(IDC_OBJECTSIZE), curSel);	
}

MapOperation GetMapOperation()
{
	if (Button_GetCheck(GetControl(IDC_CREATEMAP)) == BST_CHECKED)
	{
		return MAP_CREATE;
	}
	else if (Button_GetCheck(GetControl(IDC_EXTENDMAP)) == BST_CHECKED)
	{
		return MAP_EXTEND;
	}
	else if (Button_GetCheck(GetControl(IDC_ALIGNMAP)) == BST_CHECKED)
	{
		return MAP_ALIGN;
	}

	return MAP_CREATE;
}

void SetFeatureCount(int count, bool initial)
{
	static int initialFeatures = 0;
	pxcCHAR buf[100];

	if (initial)
	{
		swprintf_s(buf, L"Initial Features: %d", count);
		Static_SetText(GetControl(IDC_INITIALCOUNT), buf);

		initialFeatures = count;
	}
	else
	{
		swprintf_s(buf, L"Added Features: %d", count - initialFeatures);
		Static_SetText(GetControl(IDC_ADDEDCOUNT), buf);
	}
}

INT_PTR CALLBACK MapCreationUIProc(HWND hwndMap, UINT message, WPARAM wParam, LPARAM lParam)
{ 
	int temp;

	switch (message)
	{   
	case WM_INITDIALOG:
		g_hWndMap = hwndMap;
		ComboBox_AddString(GetControl(IDC_OBJECTSIZE), L"Very Small");
		ComboBox_SetItemData(GetControl(IDC_OBJECTSIZE), 0, PXCTrackerUtils::ObjectSize::VERY_SMALL);
		ComboBox_AddString(GetControl(IDC_OBJECTSIZE), L"Small");
		ComboBox_SetItemData(GetControl(IDC_OBJECTSIZE), 0, PXCTrackerUtils::ObjectSize::SMALL);
		ComboBox_AddString(GetControl(IDC_OBJECTSIZE), L"Medium");
		ComboBox_SetItemData(GetControl(IDC_OBJECTSIZE), 0, PXCTrackerUtils::ObjectSize::MEDIUM);
		ComboBox_AddString(GetControl(IDC_OBJECTSIZE), L"Large");
		ComboBox_SetItemData(GetControl(IDC_OBJECTSIZE), 0, PXCTrackerUtils::ObjectSize::LARGE);

		ComboBox_SetCurSel(GetControl(IDC_OBJECTSIZE), 0);
		
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{		
		case IDC_CREATEMAP:
			EnableWindow(GetControl(IDC_OBJECTSIZE), TRUE);
			EnableWindow(GetControl(IDC_MARKERID),   FALSE);
			EnableWindow(GetControl(IDC_MARKERSIZE), FALSE);
			EnableWindow(GetControl(IDC_START),		 TRUE);
			return TRUE;
		case IDC_EXTENDMAP:
			EnableWindow(GetControl(IDC_OBJECTSIZE), FALSE);
			EnableWindow(GetControl(IDC_MARKERID),   FALSE);
			EnableWindow(GetControl(IDC_MARKERSIZE), FALSE);
			GetMapFile();
			return TRUE;
		case IDC_ALIGNMAP:
			EnableWindow(GetControl(IDC_OBJECTSIZE), FALSE);
			EnableWindow(GetControl(IDC_MARKERID),   TRUE);
			EnableWindow(GetControl(IDC_MARKERSIZE), TRUE);
			GetMapFile();
			// Disable start button if the ID and size are not valid
			if (!GetMarkerInfo(temp, temp))
			{
				EnableWindow(GetControl(IDC_START), FALSE);
			}			
			return TRUE;
		case IDC_SAVEMAPAS:
			void SaveMap(pxcCHAR*);
			SaveMapFile();
			if (g_mapFile[0] != 0)
			{
				SaveMap(g_mapFile);
			}
			return TRUE;		
		case IDC_TOOLBOX:
			{				
				pxcCHAR *toolboxPath = L"contrib\\Metaio\\MetaioTrackerToolbox\\";
				pxcCHAR *toolboxName = L"MetaioTrackerToolbox.exe";
				size_t dirSize = GetEnvironmentVariable(L"RSSDK_DIR", NULL, 0);
				dirSize += wcsnlen(toolboxPath,1024) + wcsnlen(toolboxName,1024) + 2;
				pxcCHAR *fullPath = new pxcCHAR[dirSize];
				pxcCHAR *fullExec = new pxcCHAR[dirSize];				
				GetEnvironmentVariable(L"RSSDK_DIR", fullPath, (DWORD) dirSize);
				GetEnvironmentVariable(L"RSSDK_DIR", fullExec, (DWORD) dirSize);				
				wcscat_s(fullExec, dirSize, toolboxPath);
				wcscat_s(fullExec, dirSize, toolboxName);				
				wcscat_s(fullPath, dirSize, toolboxPath);				
				PROCESS_INFORMATION info = {0};
				STARTUPINFO si = {0};
				if (CreateProcess(fullExec, NULL, NULL, NULL, FALSE, 0, NULL, fullPath, &si, &info) == 0)
				{
					MessageBoxW(hwndMap, L"Failed to start toolbox process", L"Error", MB_ICONERROR);
				};

				delete[] fullExec;
				delete[] fullPath;
				CloseHandle(info.hProcess);
				CloseHandle(info.hThread);
				return TRUE;
			}
		}

		switch (HIWORD(wParam))
		{
		case EN_CHANGE:
			if (GetMarkerInfo(temp, temp))
			{
				EnableWindow(GetControl(IDC_START), TRUE);
			}
			else
			{
				EnableWindow(GetControl(IDC_START), FALSE);
			}
			return TRUE;
		}
		break;
	}

	return FALSE;
}
