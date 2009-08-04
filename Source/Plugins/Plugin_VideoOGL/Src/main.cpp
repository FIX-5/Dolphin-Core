// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/



// OpenGL Plugin Documentation
/* 

1.1 Display settings


Internal and fullscreen resolution: Since the only internal resolutions allowed are also
fullscreen resolution allowed by the system there is only need for one resolution setting
that applies to both the internal resolution and the fullscreen resolution.

Todo: Make the internal resolution option apply instantly, currently only the native and 2x option
applies instantly. To do this we need to enumerate all avaliable display resolutions before
Renderer:Init().

1.2 Screenshots


The screenshots should be taken from the internal representation of the picture regardless of
what the current window size is. Since AA and wireframe is applied together with the picture resizing
this rule is not currently applied to AA or wireframe pictures, they are instead taken from whatever
the window size is.

Todo: Render AA and wireframe to a separate picture used for the screenshot in addition to the one
for display.

1.3 AA


Make AA apply instantly during gameplay if possible

*/



#include "Globals.h"
#include "LogManager.h"
#include "Thread.h"
#include "Atomic.h"

#include <cstdarg>

#ifdef _WIN32
#include "OS/Win32.h"
#endif

#if defined(HAVE_WX) && HAVE_WX
#include "GUI/ConfigDlg.h"
GFXConfigDialogOGL *m_ConfigFrame = NULL;
#include "Debugger/Debugger.h"
GFXDebuggerOGL *m_DebuggerFrame = NULL;
#endif // HAVE_WX

#include "Config.h"
#include "LookUpTables.h"
#include "ImageWrite.h"
#include "Render.h"
#include "GLUtil.h"
#include "Fifo.h"
#include "OpcodeDecoding.h"
#include "TextureMngr.h"
#include "BPStructs.h"
#include "VertexLoader.h"
#include "VertexLoaderManager.h"
#include "VertexManager.h"
#include "PixelShaderCache.h"
#include "PixelShaderManager.h"
#include "VertexShaderCache.h"
#include "VertexShaderManager.h"
#include "XFB.h"
#include "XFBConvert.h"
#include "TextureConverter.h"
#include "PostProcessing.h"
#include "OnScreenDisplay.h"
#include "Setup.h"

#include "VideoState.h"

SVideoInitialize g_VideoInitialize;
PLUGIN_GLOBALS* globals = NULL;

bool allowConfigShow = true;

// Logging
int GLScissorX, GLScissorY, GLScissorW, GLScissorH;

static bool s_PluginInitialized = false;

static u32 s_swapRequested = FALSE;
static u32 s_efbAccessRequested = FALSE;

void GetDllInfo (PLUGIN_INFO* _PluginInfo)
{
    _PluginInfo->Version = 0x0100;
    _PluginInfo->Type = PLUGIN_TYPE_VIDEO;
#ifdef DEBUGFAST
    sprintf(_PluginInfo->Name, "Dolphin OpenGL (DebugFast)");
#else
#ifndef _DEBUG
    sprintf(_PluginInfo->Name, "Dolphin OpenGL");
#else
    sprintf(_PluginInfo->Name, "Dolphin OpenGL (Debug)");
#endif
#endif
}

void SetDllGlobals(PLUGIN_GLOBALS* _pPluginGlobals)
{
	globals = _pPluginGlobals;
	LogManager::SetInstance((LogManager *)globals->logManager);
}

// This is used for the functions right below here which use wxwidgets
#if defined(HAVE_WX) && HAVE_WX
#ifdef _WIN32
	WXDLLIMPEXP_BASE void wxSetInstance(HINSTANCE hInst);
	extern HINSTANCE g_hInstance;
#endif

wxWindow* GetParentedWxWindow(HWND Parent)
{
#ifdef _WIN32
	wxSetInstance((HINSTANCE)g_hInstance);
#endif
	wxWindow *win = new wxWindow();
#ifdef _WIN32
	win->SetHWND((WXHWND)Parent);
	win->AdoptAttributesFromHWND();
#endif
	return win;
}
#endif

#if defined(HAVE_WX) && HAVE_WX
void DllDebugger(HWND _hParent, bool Show)
{
	if (!m_DebuggerFrame)
		m_DebuggerFrame = new GFXDebuggerOGL(GetParentedWxWindow(_hParent));

	if (Show)
		m_DebuggerFrame->ShowModal();
	else
		m_DebuggerFrame->Hide();
}
#else
void DllDebugger(HWND _hParent, bool Show) { }
#endif

void DllConfig(HWND _hParent)
{
#if defined(HAVE_WX) && HAVE_WX
	//if (!m_ConfigFrame)
if (allowConfigShow) // Prevent user to show more than 1 config window at same time
{
	m_ConfigFrame = new GFXConfigDialogOGL(GetParentedWxWindow(_hParent));

	//else if (!m_ConfigFrame->GetParent()->IsShown())
	//	m_ConfigFrame->Close(true);

#if defined(_WIN32)


	// Search for avaliable resolutions
	
	DWORD iModeNum = 0;
	DEVMODE dmi;
	ZeroMemory(&dmi, sizeof(dmi));
	dmi.dmSize = sizeof(dmi);
	std::vector<std::string> resos;
	resos.reserve(20);
	int i = 0;

	while (EnumDisplaySettings(NULL, iModeNum++, &dmi) != 0)
	{
		char szBuffer[100];
		sprintf(szBuffer, "%dx%d", dmi.dmPelsWidth, dmi.dmPelsHeight);
		std::string strBuffer(szBuffer);
		// Create a check loop to check every pointer of resolutions to see if the res is added or not
		int b = 0;
		bool resFound = false;
		while (b < i && !resFound)
		{
			// Is the res already added?
			resFound = (resos[b] == strBuffer);
			b++;
		}
		// Add the resolution
		if (!resFound && i < 100)  // don't want to overflow resos array. not likely to happen, but you never know.
		{
			resos.push_back(strBuffer);
			i++;
			m_ConfigFrame->AddFSReso(szBuffer);
			m_ConfigFrame->AddWindowReso(szBuffer);
		}
        ZeroMemory(&dmi, sizeof(dmi));
	}

#elif defined(HAVE_X11) && HAVE_X11 && defined(HAVE_XXF86VM) && HAVE_XXF86VM

    int glxMajorVersion, glxMinorVersion;
    int vidModeMajorVersion, vidModeMinorVersion;
    GLWin.dpy = XOpenDisplay(0);
    glXQueryVersion(GLWin.dpy, &glxMajorVersion, &glxMinorVersion);
    XF86VidModeQueryVersion(GLWin.dpy, &vidModeMajorVersion, &vidModeMinorVersion);
	//Get all full screen resos for the config dialog
	XF86VidModeModeInfo **modes = NULL;
	int modeNum = 0;
	int bestMode = 0;

	//set best mode to current
	bestMode = 0;
	XF86VidModeGetAllModeLines(GLWin.dpy, GLWin.screen, &modeNum, &modes);
	int px = 0, py = 0;
	if (modeNum > 0 && modes != NULL)
	{
		for (int i = 0; i < modeNum; i++)
		{
			if (px != modes[i]->hdisplay && py != modes[i]->vdisplay)
			{
				char temp[32];
				sprintf(temp,"%dx%d", modes[i]->hdisplay, modes[i]->vdisplay);
				m_ConfigFrame->AddFSReso(temp);
				m_ConfigFrame->AddWindowReso(temp);//Add same to Window ones, since they should be nearly all that's needed
				px = modes[i]->hdisplay;//Used to remove repeating from different screen depths
				py = modes[i]->vdisplay;
			}
		}
	}    
	XFree(modes);

#elif defined(HAVE_COCOA) && HAVE_COCOA
	
	CFArrayRef modes;
	CFRange range;
	CFDictionaryRef modesDict;
	CFNumberRef modeValue;

	int modeWidth;
	int modeHeight;
	int modeBpp;
	int modeIndex;
	int px = 0, py = 0;


	modes = CGDisplayAvailableModes(CGMainDisplayID());

	range.location = 0;
	range.length = CFArrayGetCount(modes);

	for (modeIndex=0; modeIndex<range.length; modeIndex++)
	{
		modesDict = (CFDictionaryRef)CFArrayGetValueAtIndex(modes, modeIndex);
		modeValue = (CFNumberRef) CFDictionaryGetValue(modesDict, kCGDisplayWidth);
    		CFNumberGetValue(modeValue, kCFNumberLongType, &modeWidth);
		modeValue = (CFNumberRef) CFDictionaryGetValue(modesDict, kCGDisplayHeight);
    		CFNumberGetValue(modeValue, kCFNumberLongType, &modeHeight);
		modeValue = (CFNumberRef) CFDictionaryGetValue(modesDict, kCGDisplayBitsPerPixel);
    		CFNumberGetValue(modeValue, kCFNumberLongType, &modeBpp);

		if (px != modeWidth && py != modeHeight)
		{
			char temp[32];
			sprintf(temp,"%dx%d", modeWidth, modeHeight);
			m_ConfigFrame->AddFSReso(temp);
			m_ConfigFrame->AddWindowReso(temp);//Add same to Window ones, since they should be nearly all that's needed
			px = modeWidth;
			py = modeHeight;
		}
	}
#endif

	// Check if at least one resolution was found. If we don't and the resolution array is empty
	// CreateGUIControls() will crash because the array is empty.
	if (m_ConfigFrame->arrayStringFor_FullscreenCB.size() == 0)
	{
		m_ConfigFrame->AddFSReso("<No resolutions found>");
		m_ConfigFrame->AddWindowReso("<No resolutions found>");
	}

	// Only allow one open at a time
	//if (!m_ConfigFrame->IsShown())
	//{

		allowConfigShow = false;
		m_ConfigFrame->CreateGUIControls();
		allowConfigShow = m_ConfigFrame->ShowModal() == 1 ? true : false;
}
	//}
	//else
	//	m_ConfigFrame->Hide();
#endif
}

void Initialize(void *init)
{
    frameCount = 0;
    SVideoInitialize *_pVideoInitialize = (SVideoInitialize*)init;
    g_VideoInitialize = *(_pVideoInitialize); // Create a shortcut to _pVideoInitialize that can also update it
	InitXFBConvTables();
    g_Config.Load();
    
	g_Config.GameIniLoad();

#if defined(HAVE_WX) && HAVE_WX
	g_Config.UpdateProjectionHack();
	//Enable support for PNG screenshots.
	wxImage::AddHandler( new wxPNGHandler );
#endif

    if (!OpenGL_Create(g_VideoInitialize, 640, 480)) // 640x480 will be the default if all else fails
	{
        g_VideoInitialize.pLog("Renderer::Create failed\n", TRUE);
        return;
    }

	_pVideoInitialize->pPeekMessages = g_VideoInitialize.pPeekMessages;
    _pVideoInitialize->pUpdateFPSDisplay = g_VideoInitialize.pUpdateFPSDisplay;

	// Now the window handle is written
    _pVideoInitialize->pWindowHandle = g_VideoInitialize.pWindowHandle;

	OSD::AddMessage("Dolphin OpenGL Video Plugin" ,5000);
}

void DoState(unsigned char **ptr, int mode) {
#ifndef _WIN32
	// WHY is this here??
	OpenGL_MakeCurrent();
#endif
    // Clear all caches that touch RAM
    TextureMngr::Invalidate(false);
    VertexLoaderManager::MarkAllDirty();
    
    PointerWrap p(ptr, mode);
    VideoCommon_DoState(p);
    
    // Refresh state.
	if (mode == PointerWrap::MODE_READ)
	{
        BPReload();
		RecomputeCachedArraybases();
	}
}

// This is called after Video_Initialize() from the Core
void Video_Prepare(void)
{
    OpenGL_MakeCurrent();
    if (!Renderer::Init()) {
        g_VideoInitialize.pLog("Renderer::Create failed\n", TRUE);
        PanicAlert("Can't create opengl renderer. You might be missing some required opengl extensions, check the logs for more info");
        exit(1);
    }

    TextureMngr::Init();

    BPInit();
    VertexManager::Init();
    Fifo_Init(); // must be done before OpcodeDecoder_Init()
    OpcodeDecoder_Init();
    VertexShaderCache::Init();
    VertexShaderManager::Init();
    PixelShaderCache::Init();
    PixelShaderManager::Init();
	PostProcessing::Init();
    GL_REPORT_ERRORD();
    VertexLoaderManager::Init();
    TextureConverter::Init();

	s_swapRequested = FALSE;
	s_efbAccessRequested = FALSE;

	s_PluginInitialized = true;
	INFO_LOG(VIDEO, "Video plugin initialized.");
}

void Shutdown(void)
{
	s_PluginInitialized = false;

	s_efbAccessRequested = FALSE;
	s_swapRequested = FALSE;

	Fifo_Shutdown();
	PostProcessing::Shutdown();

	// The following calls are NOT Thread Safe
	// And need to be called from the video thread
	TextureConverter::Shutdown();
	VertexLoaderManager::Shutdown();
	VertexShaderCache::Shutdown();
	VertexShaderManager::Shutdown();
	PixelShaderManager::Shutdown();
	PixelShaderCache::Shutdown();
	VertexManager::Shutdown();
	TextureMngr::Shutdown();
	OpcodeDecoder_Shutdown();
	Renderer::Shutdown();
	OpenGL_Shutdown();
}


void Video_SendFifoData(u8* _uData, u32 len)
{
	Fifo_SendFifoData(_uData, len);
}

// Enter and exit the video loop
void Video_EnterLoop()
{
	Fifo_EnterLoop(g_VideoInitialize);
}

void Video_ExitLoop()
{
	Fifo_ExitLoop();
}



// Screenshot and screen message

void Video_Screenshot(const char *_szFilename)
{
	Renderer::SetScreenshot(_szFilename);
}

void Video_AddMessage(const char* pstr, u32 milliseconds)
{
	OSD::AddMessage(pstr, milliseconds);
}

static volatile struct
{
	u32 xfbAddr;
	FieldType field;
	u32 fbWidth;
	u32 fbHeight;
} s_beginFieldArgs;

// Run from the graphics thread (from Fifo.cpp)
void VideoFifo_CheckSwapRequest()
{
	if (Common::AtomicLoadAcquire(s_swapRequested))
	{
#ifdef XXX_ENABLE_CPU_CONTROLLED_SWAPPING
		Renderer::Swap(s_beginFieldArgs.xfbAddr, s_beginFieldArgs.field, s_beginFieldArgs.fbWidth, s_beginFieldArgs.fbHeight);
		
		// TODO: Find better name for this because I don't know if it means what it says.
		g_VideoInitialize.pCopiedToXFB(true);
#endif

		Common::AtomicStoreRelease(s_swapRequested, FALSE);
	}
}

inline bool addrRangesOverlap(u32 aLower, u32 aUpper, u32 bLower, u32 bUpper)
{
	return !((aLower >= bUpper) || (bLower >= aUpper));
}

// Run from the graphics thread (from Fifo.cpp)
void VideoFifo_CheckSwapRequestAt(u32 xfbAddr, u32 fbWidth, u32 fbHeight)
{
	if (Common::AtomicLoadAcquire(s_swapRequested))
	{
		u32 aLower = xfbAddr;
		u32 aUpper = xfbAddr + 2 * fbWidth * fbHeight;
		u32 bLower = s_beginFieldArgs.xfbAddr;
		u32 bUpper = s_beginFieldArgs.xfbAddr + 2 * s_beginFieldArgs.fbWidth * s_beginFieldArgs.fbHeight;

		if (addrRangesOverlap(aLower, aUpper, bLower, bUpper))
			VideoFifo_CheckSwapRequest();
	}
}

// Run from the CPU thread (from VideoInterface.cpp)
void Video_BeginField(u32 xfbAddr, FieldType field, u32 fbWidth, u32 fbHeight)
{
	if (s_PluginInitialized)
	{
		// Make sure previous swap request has made it to the screen
		if (g_VideoInitialize.bUseDualCore)
		{
			while (Common::AtomicLoadAcquire(s_swapRequested))
				Common::YieldCPU();
		}
		else
			VideoFifo_CheckSwapRequest();

		s_beginFieldArgs.xfbAddr = xfbAddr;
		s_beginFieldArgs.field = field;
		s_beginFieldArgs.fbWidth = fbWidth;
		s_beginFieldArgs.fbHeight = fbHeight;
	}
}

// Run from the CPU thread (from VideoInterface.cpp)
void Video_EndField()
{
	if (s_PluginInitialized)
	{
		Common::AtomicStoreRelease(s_swapRequested, TRUE);
	}
}

static struct
{
	EFBAccessType type;
	u32 x;
	u32 y;
} s_accessEFBArgs;

static u32 s_AccessEFBResult = 0;

void VideoFifo_CheckEFBAccess()
{
	if (Common::AtomicLoadAcquire(s_efbAccessRequested))
	{
		s_AccessEFBResult = Renderer::AccessEFB(s_accessEFBArgs.type, s_accessEFBArgs.x, s_accessEFBArgs.y);

		Common::AtomicStoreRelease(s_efbAccessRequested, FALSE);
	}
}

u32 Video_AccessEFB(EFBAccessType type, u32 x, u32 y)
{
	if (s_PluginInitialized)
	{
		s_accessEFBArgs.type = type;
		s_accessEFBArgs.x = x;
		s_accessEFBArgs.y = y;

		Common::AtomicStoreRelease(s_efbAccessRequested, TRUE);

		if (g_VideoInitialize.bUseDualCore)
		{
			while (Common::AtomicLoadAcquire(s_efbAccessRequested))
				Common::YieldCPU();
		}
		else
			VideoFifo_CheckEFBAccess();

		return s_AccessEFBResult;
	}

	return 0;
}

