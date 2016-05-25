#include <windows.h>
#include <pxctracker.h>
#include <pxctrackerutils.h>
#include <pxcprojection.h>
#include "object_tracker.h"

// Returns the HWND for a control given the ID
HWND GetControl(UINT);

// Status bar control
void SetStatus(pxcCHAR *line);
void AppendStatus(pxcCHAR *text);

// Get the state of menu options
pxcCHAR* GetCheckedDevice();
pxcCHAR* GetCheckedModule();
pxcCHAR* GetCheckedTrack();
bool IsPlaybackState();
bool IsRecordState();

// Render the stream/tracking results
void DrawBitmap(PXCImage*);
void DrawTrackingValues(PXCTracker::TrackingValues *arrData, PXCProjection *projection);
void UpdatePanel(bool);

// Get the state of the tracking UI
TrackingType GetTrackingType();
void RefreshModelTree();

// Get the state of the map creation UI
MapOperation GetMapOperation();
PXCTrackerUtils::ObjectSize GetObjectSize();
void SetFeatureCount(int numFeatures, bool initial);
bool GetMarkerInfo(int &id, int &size);

// Pipeline thread functions
void ObjectTrackingPipeline(PXCSession *session);
void ObjectTracking_ROIUpdated();
void MapCreationPipeline(PXCSession *session);
void MapCreation_ROIUpdated();