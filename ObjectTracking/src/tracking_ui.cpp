/*******************************************************************************

INTEL CORPORATION PROPRIETARY INFORMATION
This software is supplied under the terms of a license agreement or nondisclosure
agreement with Intel Corporation and may not be copied or disclosed except in
accordance with the terms of that agreement
Copyright(c) 2014 Intel Corporation. All Rights Reserved.

*******************************************************************************/
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include "resource.h"
#include "object_tracker.h"
#include <vector>
#include "ui.h"

extern HINSTANCE g_hInst;
extern HWND g_hWndDlg, g_hWndTrack, g_hWndMap;
extern std::vector<Model> g_targets;

bool showedMessage = false;
pxcCHAR *fileFilters[] = 
{
	L"Supported Files(png,jpg)\0*.png;*.jpg\0All Files\0*.*\0\0",	
	L"Supported Files(xml,slam)\0*.xml;*.slam\0All Files\0*.*\0",
	L"\0\0"
};

TrackingType GetTrackingType()
{
	HWND comboBox = GetControl(IDC_TRACKINGTYPE);
	return (TrackingType) ComboBox_GetItemData(comboBox, ComboBox_GetCurSel(comboBox));
}

/** opens the file explorer and lets user add a tracking file **/
void AddTargetFile()
{
	pxcCHAR	file[1024] = {0};
	OPENFILENAME ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.lpstrInitialDir = L"";
	ofn.lpstrFilter=fileFilters[GetTrackingType()];
	ofn.lpstrFile=file;
	ofn.nMaxFile=sizeof(file)/sizeof(pxcCHAR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileName(&ofn))
	{	
		g_targets.push_back(Model(file));
		Button_Enable(GetControl(IDC_START), true);
		Button_Enable(GetControl(IDC_CLEARMODELS), true);

		RefreshModelTree();
	}
}

void RefreshModelTree()
{	
	TVITEM item;	
	HWND hwndTree = GetControl(IDC_MODELS);

	TreeView_DeleteAllItems(hwndTree);
	
	// Loop over all of the registered targets (COS IDs) and add them to the view
	for (TargetIterator targetIter = g_targets.begin(); targetIter != g_targets.end(); targetIter++)		
	{
		item.mask = TVIF_TEXT | TVIF_IMAGE;
		item.cChildren = targetIter->cosIDs.size() > 0 ? 1 : 0;
		item.pszText = targetIter->model_filename;
		item.cchTextMax = (int)wcsnlen(item.pszText, sizeof(targetIter->model_filename)/sizeof(targetIter->model_filename[0]));
		item.iImage = 3;	// Invalid image ID to appear white

		TVINSERTSTRUCT	tvinsert;
		tvinsert.hParent = TVI_ROOT;
		tvinsert.item = item;
		tvinsert.hInsertAfter = TVI_LAST;
		HTREEITEM parent = TreeView_InsertItem(hwndTree, &tvinsert);

		for (TrackingIterator iter = targetIter->cosIDs.begin(); iter != targetIter->cosIDs.end(); iter++)
		{
			pxcCHAR buf[260] = {'\0'};

			item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_PARAM;
			item.cChildren = 0;
			_snwprintf_s(buf, 260, L"ID: %2d    Name: %ws", iter->cosID, iter->friendlyName);		
			item.pszText = buf;
			item.cchTextMax = (int) wcsnlen(item.pszText,sizeof(buf)/sizeof(buf[0]));
			item.iImage = I_IMAGECALLBACK;
			item.lParam = (iter - targetIter->cosIDs.begin() << 8) | (targetIter - g_targets.begin());

			TVINSERTSTRUCT	tvinsert;
			tvinsert.hParent = parent;
			tvinsert.item = item;
			tvinsert.hInsertAfter = TVI_LAST;
			TreeView_InsertItem(hwndTree, &tvinsert);			
		}

		TreeView_Expand(hwndTree, parent, TVE_EXPAND);
	}
}

void ClearModelTree()
{
	Button_Enable(GetControl(IDC_START),false);
	Button_Enable(GetControl(IDC_CLEARMODELS),false);
	g_targets.clear();
	TreeView_DeleteAllItems(GetControl(IDC_MODELS));
}

INT_PTR CALLBACK TrackingUIProc(HWND hwndTrk, UINT message, WPARAM wParam, LPARAM lParam)
{ 	
	switch (message)
	{ 
    case WM_INITDIALOG:
		{
			g_hWndTrack = hwndTrk;
		
			LVCOLUMN column;
			column.mask = LVCF_TEXT | LVCF_SUBITEM | LVCF_WIDTH;
			column.cx = 50;
			column.iSubItem = 0;
			column.pszText = L"ID";
			ListView_InsertColumn(GetControl(IDC_MODELS), 0, &column);
			column.cx = 400;
			column.iSubItem = 1;
			column.pszText = L"File";
			ListView_InsertColumn(GetControl(IDC_MODELS), 1, &column);
				
			HIMAGELIST imgList = ImageList_Create(16, 16, 0, 2, 0);
		
			HBITMAP trackingBmp	   = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_TRACKING_OK));
			HBITMAP notTrackingBmp = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_NOT_TRACKING));
				
			ImageList_Add(imgList, notTrackingBmp, NULL);
			ImageList_Add(imgList, trackingBmp, NULL);

			TreeView_SetImageList(GetControl(IDC_MODELS), imgList, TVSIL_NORMAL);

			DeleteObject(trackingBmp);
			DeleteObject(notTrackingBmp);
		
			// Initialize combo tracking box
			ComboBox_AddString(GetControl(IDC_TRACKINGTYPE), L"2D Tracking");
			ComboBox_SetItemData(GetControl(IDC_TRACKINGTYPE), 0, TRACKING_2D);
			ComboBox_AddString(GetControl(IDC_TRACKINGTYPE), L"3D Tracking");
			ComboBox_SetItemData(GetControl(IDC_TRACKINGTYPE), 1, TRACKING_3D);
			ComboBox_AddString(GetControl(IDC_TRACKINGTYPE), L"3D Instant");
			ComboBox_SetItemData(GetControl(IDC_TRACKINGTYPE), 2, TRACKING_INSTANT);
			ComboBox_SetCurSel(GetControl(IDC_TRACKINGTYPE), 0);
		
			return TRUE; 
		}

	case WM_COMMAND:
        switch (LOWORD(wParam))
		{		
		case IDC_LOADMODEL:
			AddTargetFile();
			return TRUE;
		case IDC_CLEARMODELS:
			ClearModelTree();
			return TRUE;
        }
		
		if (HIWORD(wParam) == CBN_SELCHANGE)
		{
			HWND comboBox = (HWND) lParam;
			int selIndex = ComboBox_GetCurSel(comboBox);
			TrackingType type = (TrackingType) ComboBox_GetItemData(comboBox, selIndex);

			switch (type)
			{
			case TRACKING_2D:
				Button_Enable(GetControl(IDC_START), false);
				Button_Enable(GetControl(IDC_LOADMODEL), true);
				Button_Enable(GetControl(IDC_CLEARMODELS), false);
				Button_Enable(GetControl(IDC_ROI), false);
				Button_SetCheck(GetControl(IDC_ROI), false);
				ClearModelTree();
				return TRUE;		
			case TRACKING_3D:				
				Button_Enable(GetControl(IDC_START), false);
				Button_Enable(GetControl(IDC_LOADMODEL), true);
				Button_Enable(GetControl(IDC_CLEARMODELS), false);
				Button_Enable(GetControl(IDC_ROI), false);
				Button_SetCheck(GetControl(IDC_ROI), false);
				ClearModelTree();			
				return TRUE;
			case TRACKING_INSTANT:				
				ClearModelTree();
				Button_Enable(GetControl(IDC_START),true);
				Button_Enable(GetControl(IDC_LOADMODEL),false);
				Button_Enable(GetControl(IDC_CLEARMODELS), false);
				Button_Enable(GetControl(IDC_ROI), true);
				Button_SetCheck(GetControl(IDC_ROI), false);

				if (!showedMessage)
				{
					showedMessage = true;
					MessageBoxExW(GetParent(hwndTrk), L"Please be aware that instant tracking needs special scene/background setup. Refer to the reference manual for details.", L"Note", MB_OK | MB_ICONINFORMATION, 0);
				}
				return TRUE;
			}
		}
		break;
	case WM_NOTIFY:		
		switch ( ((LPNMHDR) lParam)->code)
		{
			case TVN_GETDISPINFO:
			{
				NMTVDISPINFO *dispInfo = (NMTVDISPINFO*) lParam;			
		
				if (dispInfo->item.mask & LVIF_IMAGE)
				{				
					int targetIndex = dispInfo->item.lParam & 0xFF;
					int cosIDIndex  = (dispInfo->item.lParam & 0xFF00) >> 8;

					if (g_targets[targetIndex].cosIDs[cosIDIndex].isTracking)
					{
						dispInfo->item.iImage = 1;
					}
					else
					{
						dispInfo->item.iImage = 0;
					}				
				}
				return TRUE;
			}			
		}
		break;	
    } 
    return FALSE; 
} 

