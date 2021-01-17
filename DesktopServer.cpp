// DesktopServer.cpp : Defines the entry point for the application.
//
#include "stdafx.h"
#include <stdio.h>
#include <process.h>    /* _beginthread, _endthread */


#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")


#include "DesktopServer.h"

#define MAX_LOADSTRING 100


// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_DESKTOPSERVER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DESKTOPSERVER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_DESKTOPSERVER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW +1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_DESKTOPSERVER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
char m_szText[256];
#define LOGS 32
typedef char LOG[256];
LOG m_szLog[LOGS];
int m_logi = 0;
HDC m_screenMemoryDC = NULL;
HBITMAP m_screenBitmap = NULL;
HANDLE m_screenDIB = NULL;
UINT* m_screenDIBBuffer = NULL;
UCHAR* m_screenSendBuffer = NULL;
int m_screenSendBufferSize = 0;
int m_screenWidth = 0;
int m_screenHeight = 0;
int m_screenSize = m_screenWidth * m_screenHeight;
UINT m_bufferSize = m_screenWidth * m_screenHeight * 4;
LONGLONG m_startTime = 0;
DWORD m_sentFrames = 0;
LONGLONG m_updatedPixels = 0;
LONGLONG m_sentBytes = 0;
LONGLONG m_receivedBytes = 0;
bool m_running = true;

LOG& Log()
{
	if (++m_logi == LOGS) m_logi = 0;
	return m_szLog[m_logi];
}

void InitLog()
{
	for (int i = 0; i < LOGS; i++)
		m_szLog[i][0] = 0;
}


void InitScreenMem()
{
	RECT rc;
	GetWindowRect(GetDesktopWindow(), &rc);
	if (m_screenWidth == rc.right && m_screenHeight == rc.bottom) {
		return;
	}

	if (m_screenMemoryDC) DeleteDC(m_screenMemoryDC);
	if (m_screenBitmap) DeleteObject(m_screenBitmap);
	m_screenWidth = rc.right;
	m_screenHeight = rc.bottom;
	HDC hdcScreen = GetDC(NULL);
	m_screenMemoryDC = CreateCompatibleDC(hdcScreen);
	m_screenBitmap = CreateCompatibleBitmap(hdcScreen, m_screenWidth, m_screenHeight);
	SelectObject(m_screenMemoryDC, m_screenBitmap);
	m_screenSize = m_screenWidth * m_screenHeight;
	m_bufferSize = m_screenWidth * m_screenHeight * 4;
	ReleaseDC(NULL, hdcScreen);

	if (m_screenDIB) GlobalFree(m_screenDIB);
	if (m_screenDIBBuffer) free(m_screenDIBBuffer);
	if (m_screenSendBuffer) free(m_screenSendBuffer);

	size_t size = m_screenSize * 4;
	m_screenDIB = GlobalAlloc(GHND, size);
	m_screenDIBBuffer = (UINT*)malloc(size);
	m_screenSendBuffer = (UCHAR*)malloc(size);
}

SOCKET m_clientScreenSocket = INVALID_SOCKET;
SOCKET m_listenScreenSocket = INVALID_SOCKET;

inline bool IsDisconnected(const SOCKET& socket)
{
	return socket == INVALID_SOCKET;
}

inline bool IsConnected(const SOCKET& socket)
{
	return socket != INVALID_SOCKET;
}

void Close(SOCKET& socket, int how)
{
	shutdown(socket, how);
	socket = INVALID_SOCKET;
}


HRESULT InitSocketSystem()
{
	HRESULT hr = S_OK;
	// initialize the socket system
	WSADATA wsaData;
	int result = WSAStartup(0x202, &wsaData);
	if (result != 0)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		return hr;
	}
	return hr;
}

HRESULT InitSocket(SOCKET& listenSocket, UINT port)
{
	HRESULT hr = S_OK;
	addrinfo* resultAddress = NULL;
	addrinfo hints;
	char portStr[8] = { '\0' };

	do
	{
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_IP;
		hints.ai_flags = AI_PASSIVE;

		sprintf_s(portStr, 8, "%d", port);

		// Resolve the server address and port
		int result = getaddrinfo(NULL, portStr, &hints, &resultAddress);
		if (result != 0)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// Create a SOCKET for connecting to server
		//listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == INVALID_SOCKET)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// configure the socket
		//int nZero = 0;
		//result = setsockopt(listenSocket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero));
		if (result == SOCKET_ERROR)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// Setup the TCP listening socket
		result = bind(listenSocket, resultAddress->ai_addr,
			(int)(resultAddress->ai_addrlen));
		if (result == SOCKET_ERROR)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		freeaddrinfo(resultAddress);
		resultAddress = NULL;

		// start listening on the input socket
		result = listen(listenSocket, SOMAXCONN);
		if (result == SOCKET_ERROR)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

	} while (FALSE);

	if (FAILED(hr))
	{
		if (resultAddress != NULL)
		{
			freeaddrinfo(resultAddress);
		}
	}

	return hr;
}


HRESULT SendBytes(SOCKET& socket, const void* bytes, DWORD size)
{
	HRESULT hr = S_OK;
	int sendResult = 0;

	sendResult = send(socket, (const char*)bytes, size, 0);
	if (sendResult == SOCKET_ERROR)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		Close(socket, SD_SEND);
		return hr;
	}
	m_sentBytes += size;
	return hr;
}

template<typename T>
bool send(SOCKET& socket, T& t)
{
	return SendBytes(socket, &t, sizeof(t));
}


bool ReadBytes(SOCKET& socket, char * bytes, int size)
{
	int result = recv(socket, bytes, size, MSG_WAITALL);
	if (result == SOCKET_ERROR)
	{
		//HRESULT hr = HRESULT_FROM_WIN32(WSAGetLastError());
		Close(socket, SD_BOTH);
		return false;
	}
	m_receivedBytes += result;
	return result;
}

template<typename T>
bool read(SOCKET& socket, T& t)
{
	return ReadBytes(socket, (char*)&t, sizeof(t));
}

#define MOUSE_LEFTDOWN	1	
#define MOUSE_LEFTUP	2
#define MOUSE_RCLICK	3
#define MOUSE_WHEEL		4
#define MOUSE_MOVE		5

#define KEY_DOWN	11
#define KEY_UP		12

struct CODEMAP
{
	WORD c1;
	WORD c2;
};

CODEMAP scm[]
{
	{105,	VK_LEFT},
	{103,	VK_UP},
	{106,	VK_RIGHT},
	{108,	VK_DOWN},
	{110,	VK_INSERT},
	{102,	VK_HOME},
	{111,	VK_DELETE},
	{107,	VK_END},
	{104,	VK_PRIOR},
	{109,	VK_NEXT},
	//{1,		VK_ESCAPE},
	{NULL,	NULL},
};

bool map(WORD& c1, WORD& c2, CODEMAP m[])
{
	for (int i = 0; m[i].c1 ; i++){
		if (m[i].c1 == c1) {
			c2 = m[i].c2;
			return true;
		}			
	}
	return false;
}

void MouseWheel(DWORD data)
{
	INPUT input;
	input.type = INPUT_MOUSE;
	input.mi.dx = input.mi.dy = 0;
	input.mi.dwFlags = input.mi.mouseData = input.mi.time = 0;
	input.mi.dwExtraInfo = 0;
	input.mi.mouseData = data * 120;
	input.mi.dwFlags = MOUSEEVENTF_WHEEL;
	SendInput(1, &input, sizeof(input));
}

void ReadDesktopControl()
{

	BYTE type;
	if (!read(m_clientScreenSocket, type))
		return;
	
	if (type >= MOUSE_LEFTDOWN && type <= MOUSE_MOVE) {
		DWORD data = 0;
		INPUT input;
		input.type = INPUT_MOUSE;
		input.mi.dx = input.mi.dy = 0;
		input.mi.dwFlags = input.mi.mouseData = input.mi.time = 0;
		input.mi.dwExtraInfo = 0;

		switch (type)
		{
		case MOUSE_LEFTDOWN:
			input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
			break;
		case MOUSE_LEFTUP:
			input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
			break;
		case MOUSE_RCLICK:
			input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
			SendInput(1, &input, sizeof(input));
			input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
			break;
		case MOUSE_WHEEL:
			if (read(m_clientScreenSocket, data)) {
				input.mi.mouseData = data * 120;
				input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			}
			break;
		case MOUSE_MOVE:
		{
			DWORD dx = 0;
			DWORD dy = 0;
			if (read(m_clientScreenSocket, dx) && read(m_clientScreenSocket, dy)) {
				input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
				input.mi.dx = dx * 65536 / m_screenWidth;
				input.mi.dy = dy * 65536 / m_screenHeight;
			}
		}
			break;
		}
		if (input.mi.dwFlags) {
			SendInput(1, &input, sizeof(input));
			//sprintf_s(Log(), "mouse %X %d %d %d", input.mi.dwFlags, input.mi.dx * m_screenWidth / 65536, input.mi.dy * m_screenWidth / 65536, input.mi.mouseData);
		}
	}
	else if (type >= KEY_DOWN && type <= KEY_UP)  {
		WORD code;
		if (read(m_clientScreenSocket, code))
		{
			INPUT input;
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = 0;
			input.ki.wScan = code;
			input.ki.dwFlags = 0;
			input.ki.time = 0;
			input.ki.dwExtraInfo = 0;

			if (map(code, input.ki.wVk, scm))
				input.ki.wScan = 0;
			else
				input.ki.dwFlags = KEYEVENTF_SCANCODE;

			if (type == KEY_UP)
				input.ki.dwFlags |= KEYEVENTF_KEYUP;

			SendInput(1, &input, sizeof(input));

			//sprintf_s(Log(), "key %X  %d  %c %d", code, code, code, type);
		}
	}
}

HRESULT Accept(SOCKET& clientSocket,const SOCKET& listenSocket)
{
	HRESULT hr = S_OK;

	if (clientSocket != INVALID_SOCKET)
		return S_FALSE;

	// Accept a client socket connection
	clientSocket = accept(listenSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		return hr;
	}
	return hr;
}

bool m_sendAudio = false;
bool m_captureScreen = true;

void GetScreenDIB(UINT* screenDIB)
{
	BITMAPINFOHEADER   bi;

	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = m_screenWidth;
	bi.biHeight = -m_screenHeight;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = 0;
	bi.biXPelsPerMeter = 0;
	bi.biYPelsPerMeter = 0;
	bi.biClrUsed = 0;
	bi.biClrImportant = 0;

	GetDIBits(m_screenMemoryDC, m_screenBitmap, 0, (UINT)m_screenHeight, screenDIB, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
}

void InitScreenDC()
{
	RECT rc = { 0, 0 , m_screenWidth, m_screenHeight };
	LPCSTR info = "Screen Capture Stopped";
	SelectObject(m_screenMemoryDC, GetStockObject(BLACK_BRUSH));
	SetBkColor(m_screenMemoryDC, 0);
	SetTextColor(m_screenMemoryDC, 0xFFFFF);
	Rectangle(m_screenMemoryDC, rc.left, rc.top, rc.right, rc.bottom);
	DrawTextA(m_screenMemoryDC, info, (int)strlen(info), &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}


bool GetScreenBuffer()
{
	UINT* screenDIB = (UINT*)GlobalLock(m_screenDIB);
	UINT* screenBuffer = m_screenDIBBuffer;

	GetScreenDIB(screenDIB);

	const UINT skip = 0xFFFF;
	UCHAR* buffer = m_screenSendBuffer;
	UINT index = 0;
	UINT n = 0;
	UINT size = 0;
	UINT color = skip;
	HRESULT hr = S_OK;
	int updatedPixels = 0;
	
	for (int i = 0; i < m_screenSize; i++)
	{
		UINT c = screenDIB[i];
		c = (c & 0xF80000) >> 9 | (c & 0xF800) >> 6 | (c & 0xF8) >> 3;
		if (screenBuffer[i] != c) {
			screenBuffer[i] = c;
		}
		else {
			c = skip;
		}
		if ((c != color || i == m_screenSize - 1) && i > 0) {
			if (n > 1 || color == skip) {
				UINT t = n;
				UINT bytes = 0;
				BYTE b[4];
				while (t > 0x1F) {
					b[bytes++] = t;
					t >>= 8;
				};
				buffer[index++] = (0x80 | (bytes << 5)) | t;
				for (int j = bytes-1; j >= 0; j--)
					buffer[index++] = b[j];
			}
			else n = 1;
			size += n;
			
			if (color != skip)
			{
				updatedPixels += n;
				buffer[index++] = color >> 8;
			}
			buffer[index++] = color;
			n = 0;
		}
		color = c;
		n++;
	}

	GlobalUnlock(m_screenDIB);

	m_screenSendBufferSize = index;
	m_updatedPixels += updatedPixels;

	return updatedPixels > 0;
}

void SendScreenBuffer()
{		
	SendBytes(m_clientScreenSocket, m_screenSendBuffer, m_screenSendBufferSize);
	m_sentFrames++;
}

void InitScreenCapture() 
{
	InitScreenMem();

	int dim = m_screenWidth << 16 | m_screenHeight;
	send(m_clientScreenSocket, dim);
	char sendAudio = (char)(m_sendAudio ? 1 : 0);
	send(m_clientScreenSocket, sendAudio);

	memset(m_screenDIBBuffer, 0, m_screenSize * 4);

	m_updatedPixels = m_sentBytes = m_receivedBytes = m_sentFrames = 0;
	m_startTime = GetTickCount64();

	if (!m_captureScreen)
	{
		InitScreenDC();
		GetScreenBuffer();
		SendScreenBuffer();
		GetScreenBuffer();
	}
}

void CaptureScreen()
{	
	bool screenUpdated = false;
	if (m_captureScreen)
	{
		HDC screenDC = GetDC(NULL);
		m_captureScreen = BitBlt(m_screenMemoryDC, 0, 0, m_screenWidth, m_screenHeight, screenDC, 0, 0, SRCCOPY);
		ReleaseDC(NULL, screenDC);
		if (m_captureScreen)
		{
			screenUpdated = GetScreenBuffer();
		}
		else
		{
			InitScreenDC();
			GetScreenBuffer();
			SendScreenBuffer();
			GetScreenBuffer();
		}

	}

	static int s_count = 0;
	if (screenUpdated || ++s_count == 10)
	{
		SendScreenBuffer();
		s_count = 0;
	}
}

void SendScreen(LPVOID p)
{
	HWND hWnd = (HWND)p;
	static LONGLONG s_tick = 0;
	while (m_running) {
		if (IsDisconnected(m_clientScreenSocket))
		{
			if (m_szText[0]) {
				m_szText[0] = 0;
				InvalidateRect(hWnd, NULL, TRUE);
			}
			if (m_szLog[0]) {
				InitLog();
				InvalidateRect(hWnd, NULL, TRUE);
			}

			if (s_tick >= 149)
				s_tick = 0;
			else
				s_tick++;

			if (s_tick % 30 == 0)
				MouseWheel(-2);
			else if (s_tick == 130)
				MouseWheel(4);
			
			Sleep(200);
		}
		else if (m_startTime > 0)
		{
			static LONGLONG s_sentBytes = 0;
			static DWORD s_sentFrames = 0;
			LONGLONG tick = GetTickCount64();
			CaptureScreen();
			DWORD bps = (DWORD)(m_sentBytes - s_sentBytes);
			if (tick - s_tick >= 1000)
			{
				DWORD fps = m_sentFrames - s_sentFrames;
				DWORD bpf = fps ? bps / fps : 0;
				DWORD time = (DWORD)((tick - m_startTime) / 1000);
				sprintf_s(m_szText, "pixels %5.3fM sent %5.3fMB received %5.3fMB frames %d time %02d:%02d bpf %d bps %d fps %d",
					(float)m_updatedPixels/(1000*1000), (float)m_sentBytes/ (float)(1024 *1024), (float)m_receivedBytes/(float)(1024*1024), m_sentFrames, time/60, time%60, bpf, bps, fps);
				s_tick = tick;
				s_sentBytes = m_sentBytes;
				s_sentFrames = m_sentFrames;
				InvalidateRect(hWnd, NULL, TRUE);
			}
			DWORD frameTicks = bps >> 12;
			if (frameTicks < 125)
				frameTicks = 125;
			DWORD ticks = (DWORD)(GetTickCount64() - tick);
			if (ticks < frameTicks) {
				Sleep(frameTicks - ticks);
			}
		}
	}
}

void ReceiveDesktopControl(LPVOID p)
{
	while (m_running) {
		if (IsDisconnected(m_clientScreenSocket)) {
			if (Accept(m_clientScreenSocket, m_listenScreenSocket) == S_OK) {
				//system("tscon console");
				InitScreenCapture();
			}
		}
		else
			ReadDesktopControl();
	}
}

#define REMOTE_AUDIO
#ifdef REMOTE_AUDIO

#include <audioclient.h>
#include <mmdeviceapi.h>

SOCKET m_clientAudioSocket = INVALID_SOCKET;
SOCKET m_listenAudioSocket = INVALID_SOCKET;

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000


#define EXIT_ON_ERROR(hres) if (FAILED(hres)) goto Exit;
#define SAFE_RELEASE(punk)  if ((punk) != NULL) (punk)->Release();

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

//typedef char SAMPLE;
typedef short SAMPLE;
const char sampleBits = sizeof(SAMPLE) * 8;
const short sampleRate = 16000;
const float sampleMax = (float)((1 << (sampleBits - 1)) - 1);
bool m_playAudio = false;

void LogFormat(WAVEFORMATEX* pwfx, HWND hWnd)
{
	WAVEFORMATEXTENSIBLE* pwfe = (WAVEFORMATEXTENSIBLE*)pwfx;

	sprintf_s(Log(), "format %X, channels %d, samples %d, bits %d, block %d, abps %d, cbs %d",
		pwfx->wFormatTag, pwfx->nChannels, pwfx->nSamplesPerSec, pwfx->wBitsPerSample, pwfx->nBlockAlign, pwfx->nAvgBytesPerSec, pwfx->cbSize);
	const char * format = "";
	if (KSDATAFORMAT_SUBTYPE_PCM == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_PCM";
	else if (KSDATAFORMAT_SUBTYPE_IEEE_FLOAT == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_IEEE_FLOAT";
	else if (KSDATAFORMAT_SUBTYPE_DRM == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_DRM";
	else if (KSDATAFORMAT_SUBTYPE_ALAW == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_ALAW";
	else if (KSDATAFORMAT_SUBTYPE_MULAW == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_MULAW";
	else if (KSDATAFORMAT_SUBTYPE_ADPCM == pwfe->SubFormat)
		format = "KSDATAFORMAT_SUBTYPE_ADPCM";

	sprintf_s(Log(), "format %s, bits %d, samples %d, mask %d", format, pwfe->Samples.wValidBitsPerSample, pwfe->Samples.wSamplesPerBlock, pwfe->dwChannelMask);
	InvalidateRect(hWnd, NULL, TRUE);
}

void SendAudio(LPVOID p)
{
	HWND hWnd = (HWND)p;
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	UINT32 bufferFrameCount;
	UINT32 numFramesAvailable;
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	IAudioClient *pAudioClient = NULL;
	IAudioCaptureClient *pCaptureClient = NULL;
	UINT32 packetLength = 0;
	BOOL bDone = FALSE;
	BYTE *pData;
	DWORD flags;

	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, m_playAudio ? eCommunications : eConsole, &pDevice);
	EXIT_ON_ERROR(hr)

	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,	NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr)

	WAVEFORMATEX* pwfx;
	hr = pAudioClient->GetMixFormat(&pwfx);

	sprintf_s(Log(), "sending at %d Hz", sampleRate);
	LogFormat(pwfx, hWnd);
	InvalidateRect(hWnd, NULL, TRUE);

	hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, hnsRequestedDuration,	0, pwfx, NULL);
	EXIT_ON_ERROR(hr)

	// Get the size of the allocated buffer.
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
	EXIT_ON_ERROR(hr)

	// Calculate the actual duration of the allocated buffer.
	hnsActualDuration = ((LONGLONG)REFTIMES_PER_SEC * bufferFrameCount) / pwfx->nSamplesPerSec;

	hr = pAudioClient->Start();  // Start recording.
	EXIT_ON_ERROR(hr)

	// Each loop fills about half of the shared buffer.
	DWORD wait = (DWORD) (hnsActualDuration / REFTIMES_PER_MILLISEC / 8);
	DWORD tick = GetTickCount();
	char channels = m_playAudio ? 1 : 2; 
	SAMPLE samples[sampleRate];
	send(m_clientAudioSocket, channels);
	send(m_clientAudioSocket, sampleBits);
	send(m_clientAudioSocket, sampleRate);
	char sendAudio = (char)(m_playAudio ? 1 : 0);
	send(m_clientAudioSocket, sendAudio);

	while (IsConnected(m_clientAudioSocket))
	{
		// Sleep for half the buffer duration.
		Sleep(wait - (GetTickCount() - tick));
		tick = GetTickCount();

		hr = pCaptureClient->GetNextPacketSize(&packetLength);
		EXIT_ON_ERROR(hr)

		while (packetLength != 0)
		{
			// Get the available data in the shared buffer.
			hr = pCaptureClient->GetBuffer(
				&pData,
				&numFramesAvailable,
				&flags, NULL, NULL);

			EXIT_ON_ERROR(hr)

			if (flags)
			{
				pData = NULL;  // Tell CopyData to write silence.
			}
			else
			{
				int deflate = pwfx->nSamplesPerSec / sampleRate;
				int size = numFramesAvailable / deflate * channels;
				deflate *= pwfx->nChannels;
				bool silence = true;
				float* frame = (float*)pData;
				for (int i = 0; i < size; i += channels) {
					SAMPLE sample = (SAMPLE)((*frame) * sampleMax);
					samples[i] = sample;
					if (silence && sample >> 2) silence = false;
					sample = (SAMPLE)((*(frame + 1)) * sampleMax);
					if (silence && sample >> 2) silence = false;
					if (channels == 2)
						samples[i + 1] = sample;
					else if (sample)
						samples[i] = (SAMPLE)(((int)samples[i] + (int)sample) >> 1);
					frame += deflate;
				}
				if (!silence) {
					hr = SendBytes(m_clientAudioSocket, samples, size * sizeof(SAMPLE));
					EXIT_ON_ERROR(hr)
					//sprintf_s(m_szText, "sent %dKB", (UINT)(m_sentBytes / 1024));
					//InvalidateRect(hWnd, NULL, TRUE);
				}
			}

			hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
			EXIT_ON_ERROR(hr)

			hr = pCaptureClient->GetNextPacketSize(&packetLength);
			EXIT_ON_ERROR(hr)
		}
	}

	hr = pAudioClient->Stop();  // Stop recording.
	EXIT_ON_ERROR(hr)

Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pCaptureClient)
	if (hr) {
		sprintf_s(Log(), "send audio error %X", hr);
		InvalidateRect(hWnd, NULL, TRUE);
		if (IsConnected(m_clientAudioSocket))
			Close(m_clientAudioSocket, SD_SEND);
	}
}

HRESULT LoadAudioData(UINT nFrames, BYTE* pData, int inflate)
{
	float* frame = (float*)pData;
	static float value = 0;
	for (UINT32 i = 0; i < nFrames; i+= inflate)
	{
		SAMPLE sample;// = (SAMPLE)(0.5 * sampleMax * sin(i *3.14));
		//if (true)
		if (read(m_clientAudioSocket, sample)) 
		{
			float dv = (sample / sampleMax - value) / inflate;
			for (int j = 0; j < inflate && i+j < nFrames; j++)
			{
				value += dv;
				*frame++ = value;
				*frame++ = value;
			}
		}
		else
			return -1;
	}
	return S_OK;
}

void PlayAudio(LPVOID p)
{
	HWND hWnd = (HWND)p;
	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	REFERENCE_TIME hnsActualDuration;
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	IAudioClient *pAudioClient = NULL;
	IAudioRenderClient *pRenderClient = NULL;
	WAVEFORMATEX *pwfx = NULL;
	UINT32 bufferFrameCount;
	UINT32 numFramesAvailable;
	UINT32 numFramesPadding;
	BYTE *pData;
	DWORD flags = 0;

	short sampleRate;
	if (!read(m_clientAudioSocket, sampleRate))
		return;

	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	EXIT_ON_ERROR(hr)

	hr = pDevice->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			NULL, (void**)&pAudioClient);

	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetMixFormat(&pwfx);
	EXIT_ON_ERROR(hr)

	sprintf_s(Log(), "receivng at %d Hz", sampleRate);
	LogFormat(pwfx, hWnd);

	int inflate = pwfx->nSamplesPerSec / sampleRate;

	hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, pwfx, NULL);
	EXIT_ON_ERROR(hr)

	// Tell the audio source which format to use.
	//hr = pMySource->SetFormat(pwfx);
	EXIT_ON_ERROR(hr)

	// Get the actual size of the allocated buffer.
	hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
	EXIT_ON_ERROR(hr)

	// Grab the entire buffer for the initial fill operation.
	//hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
	//EXIT_ON_ERROR(hr)

	// Load the initial data into the shared buffer.
	//hr = LoadAudioData(bufferFrameCount, pData);
	//EXIT_ON_ERROR(hr)

	//hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
	//EXIT_ON_ERROR(hr)

		// Calculate the actual duration of the allocated buffer.
	hnsActualDuration = REFTIMES_PER_SEC *	bufferFrameCount / pwfx->nSamplesPerSec;

	hr = pAudioClient->Start();  // Start playing.
	EXIT_ON_ERROR(hr)

	// Each loop fills about half of the shared buffer.
	while (IsConnected(m_clientAudioSocket))
	{
		// Sleep for half the buffer duration.
		Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 8));

		// See how much buffer space is available.
		hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
		EXIT_ON_ERROR(hr)

		numFramesAvailable = bufferFrameCount - numFramesPadding;

		// Grab all the available space in the shared buffer.
		hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
		EXIT_ON_ERROR(hr)

		hr = LoadAudioData(numFramesAvailable, pData, inflate);
		EXIT_ON_ERROR(hr)

		hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
		EXIT_ON_ERROR(hr)
	}

	// Wait for last data in buffer to play before stopping.
	Sleep((DWORD)(hnsActualDuration / REFTIMES_PER_MILLISEC / 2));

	hr = pAudioClient->Stop();  // Stop playing.
	EXIT_ON_ERROR(hr)

Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pRenderClient)
	if (hr) {
		sprintf_s(Log(), "play audio error %X", hr);
		InvalidateRect(hWnd, NULL, TRUE);
		if (IsConnected(m_clientAudioSocket))
			Close(m_clientAudioSocket, SD_RECEIVE);
	}
}

void ReceiveAudio(LPVOID p)
{
	while (m_running) {
		if (IsDisconnected(m_clientAudioSocket)) {
			if (m_sendAudio && Accept(m_clientAudioSocket, m_listenAudioSocket) == S_OK)
				_beginthread(SendAudio, 0, p);
		}
		else {
			if (m_playAudio)
				PlayAudio(p);
		}			
		Sleep(50);
	}
}
#endif
//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 200, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);// SW_MINIMIZE);
	UpdateWindow(hWnd);

	InitLog();

	if (InitSocketSystem() != S_OK)
		return FALSE;


#ifdef REMOTE_AUDIO
	if (CoInitializeEx(0, COINIT_MULTITHREADED) != S_OK)
		return FALSE;

	if (InitSocket(m_listenAudioSocket, 18081) != S_OK)
		return FALSE;

	_beginthread(ReceiveAudio, 0, hWnd);
#endif

	if (InitSocket(m_listenScreenSocket, 18080) != S_OK)
		return FALSE;

	_beginthread(ReceiveDesktopControl, 0, hWnd);
	_beginthread(SendScreen, 0, hWnd);

	return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
			case IDM_SCREEN_CAPTURE:
				m_captureScreen = true;
				break;
			case IDM_SCREEN_STOP:
				m_captureScreen = false;
				break;
			case IDM_AUDIO_SEND:
				m_sendAudio = true;
				m_playAudio = false;
				break;
			case IDM_AUDIO_RECEIVE:
				m_sendAudio = true;
				m_playAudio = true;				
				break;
			case IDM_AUDIO_STOP:
				m_sendAudio = false;
				m_playAudio = false;
				break;
			case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			DrawTextA(hdc, m_szText, (int)strlen(m_szText), &rcClient, DT_CENTER);
			for (int i = 0; i < LOGS; i++) {
				rcClient.top += 16;
				LOG& log = m_szLog[(m_logi + i + 1) % LOGS];
				if (log[0])
					DrawTextA(hdc, log, (int)strlen(log), &rcClient, DT_LEFT);
			}
			EndPaint(hWnd, &ps);
		}
        break;
	case WM_TIMER:
		break;
#if 0
	case WM_KEYDOWN:
		sprintf_s(Log(), "key down %d %d", wParam, VkKeyScan(wParam));
		break;
	case WM_CHAR:
		sprintf_s(Log(), "char %d %d", wParam, VkKeyScan(wParam));
		break;
#endif
	case WM_DESTROY:
		m_running = false;
		if (IsConnected(m_clientScreenSocket))
			Close(m_clientScreenSocket, SD_SEND);
		if (IsConnected(m_listenScreenSocket))
			Close(m_listenScreenSocket, SD_RECEIVE);
#ifdef REMOTE_AUDIO
		if (IsConnected(m_clientAudioSocket)) 
			Close(m_clientAudioSocket, SD_SEND);
		if (IsConnected(m_listenAudioSocket))
			Close(m_listenAudioSocket, SD_RECEIVE);
#endif
		Sleep(100);
		if (m_screenDIB) GlobalFree(m_screenDIB);
		if (m_screenDIBBuffer) free(m_screenDIBBuffer);
		if (m_screenSendBuffer) free(m_screenSendBuffer);
		PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
