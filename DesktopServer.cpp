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
HANDLE m_screenBuffer = NULL;
int m_screenWidth = 0;
int m_screenHeight = 0;
int m_screenSize = m_screenWidth * m_screenHeight;
UINT m_bufferSize = m_screenWidth * m_screenHeight * 4;
LONGLONG m_startTime = 0;
LONGLONG m_sentFrames = 0;
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


void InitMem()
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
	if (m_screenBuffer) GlobalFree(m_screenBuffer);

	m_screenDIB = GlobalAlloc(GHND, m_screenSize * 4);
	m_screenBuffer = GlobalAlloc(GHND, m_screenSize * 4);
}

SOCKET m_clientSocket = INVALID_SOCKET;
SOCKET m_listenSocket = INVALID_SOCKET;
#define WM_SOCKET WM_USER + 1


HRESULT InitSocket(HWND hWnd)
{
	HRESULT hr = S_OK;
	addrinfo* resultAddress = NULL;
	addrinfo hints;
	char portStr[8] = { '\0' };

	// initialize the socket system
	WSADATA wsaData;
	int result = WSAStartup(0x202, &wsaData);
	if (result != 0)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		return hr;
	}

	do
	{
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_IP;
		hints.ai_flags = AI_PASSIVE;

		sprintf_s(portStr, 8, "%d", 18080);

		// Resolve the server address and port
		result = getaddrinfo(NULL, portStr, &hints, &resultAddress);
		if (result != 0)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// Create a SOCKET for connecting to server
		//m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == INVALID_SOCKET)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// configure the socket
		//int nZero = 0;
		//result = setsockopt(m_listenSocket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero));
		if (result == SOCKET_ERROR)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		// Setup the TCP listening socket
		result = bind(m_listenSocket, resultAddress->ai_addr,
			(int)(resultAddress->ai_addrlen));
		if (result == SOCKET_ERROR)
		{
			hr = HRESULT_FROM_WIN32(WSAGetLastError());
			break;
		}

		freeaddrinfo(resultAddress);
		resultAddress = NULL;

		// start listening on the input socket
		result = listen(m_listenSocket, SOMAXCONN);
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


HRESULT SendData(const void* data, DWORD dataLength)
{
	HRESULT hr = S_OK;
	int sendResult = 0;

	sendResult = send(m_clientSocket, (const char*)data, dataLength, 0);
	if (sendResult == SOCKET_ERROR)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		shutdown(m_clientSocket, SD_SEND);
		m_clientSocket = INVALID_SOCKET;
		return hr;
	}
	return hr;
}

bool ReadBytes(char * buf, int size)
{
	int result = recv(m_clientSocket, buf, size, MSG_WAITALL);
	if (result == SOCKET_ERROR)
	{
		HRESULT hr = HRESULT_FROM_WIN32(WSAGetLastError());
		shutdown(m_clientSocket, SD_SEND);
		m_clientSocket = INVALID_SOCKET;
		return false;
	}
	m_receivedBytes += result;
	return true;
}

template<typename T>
bool read(T& t)
{
	return ReadBytes((char*)&t, sizeof(t));
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

void Read()
{

	BYTE type;
	if (!read(type))
		return;
	
	if (type >= MOUSE_LEFTDOWN && type <= MOUSE_MOVE) {
		DWORD data = 0;
		INPUT input;
		input.type = INPUT_MOUSE;
		input.mi.dx = 0;
		input.mi.dy = 0;
		input.mi.dwFlags = 0;
		input.mi.mouseData = 0;
		input.mi.time = 0;
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
			if (read(data)) {
				input.mi.mouseData = data * 120;
				input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			}
			break;
		case MOUSE_MOVE:
		{
			DWORD dx = 0;
			DWORD dy = 0;
			if (read(dx) && read(dy)) {
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
		if (read(code))
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

HRESULT Accept()
{
	HRESULT hr = S_OK;

	if (m_clientSocket != INVALID_SOCKET)
		return S_FALSE;

	// Accept a client socket connection
	m_clientSocket = accept(m_listenSocket, NULL, NULL);
	if (m_clientSocket == INVALID_SOCKET)
	{
		hr = HRESULT_FROM_WIN32(WSAGetLastError());
		return hr;
	}

	InitMem();

	int dim = m_screenWidth << 16 | m_screenHeight;
	SendData(&dim, sizeof(dim));

	UINT* buffer = (UINT*)GlobalLock(m_screenBuffer);
	memset(buffer, 0, m_screenSize * 4);
	GlobalUnlock(m_screenBuffer);

	m_startTime = GetTickCount64();

	return hr;
}

bool IsDisconnected()
{
	return m_clientSocket == INVALID_SOCKET;
}

void Capture()
{
	HDC screenDC = GetDC(NULL);
	BitBlt(m_screenMemoryDC, 0, 0, m_screenWidth, m_screenHeight, screenDC, 0, 0, SRCCOPY);
	ReleaseDC(NULL, screenDC);

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

	UINT* screenDIB = (UINT*)GlobalLock(m_screenDIB);
	UINT* sceenBuffer = (UINT*)GlobalLock(m_screenBuffer);

	GetDIBits(m_screenMemoryDC, m_screenBitmap, 0, (UINT)m_screenHeight, screenDIB, (BITMAPINFO *)&bi, DIB_RGB_COLORS);

	const UINT skip = 0xFFFF;
	UCHAR buffer[512];
	UINT index = 0;
	UINT n = 0;
	UINT size = 0;
	UINT color = skip;
	HRESULT hr = S_OK; 
	int updatedPixels = 0;

	for (int i = 0; i < m_screenSize; i++)
	{
		UINT c;
		if (sceenBuffer[i] != screenDIB[i]) {
			c = sceenBuffer[i] = screenDIB[i];
			c = (c & 0xF80000) >> 9 | (c & 0xF800) >> 6 | (c & 0xF8) >> 3;
		}
		else {
			c = skip;
		}
		if ((c != color || i == m_screenSize - 1) && i > 0) {
			if (n > 1 || color == skip) {
				UINT t = n;
				while (t) {
					buffer[index++] = (t | 0x80) & 0xBF;
					t >>= 6;
				};
			}
			else n = 1;
			size += n;
			if (color != skip)
				updatedPixels += n;
			n = 0;
			if (color != skip)
				buffer[index++] = color >> 8;
			buffer[index++] = color;
			if (index >= sizeof(buffer) - 8) {
				hr = SendData(buffer, index);
				m_sentBytes += index;
				index = 0;
				if (hr != S_OK)
					break;
			}
		}
		color = c;
		n++;
	}

	if (updatedPixels) {
		if (index) {
			SendData(buffer, index);
			m_sentBytes += index;
		}
		m_updatedPixels += updatedPixels;
		m_sentFrames++;
	}

	GlobalUnlock(m_screenDIB);
	GlobalUnlock(m_screenBuffer);

}

void Send(LPVOID p)
{
	HWND hWnd = (HWND)p;
	while (m_running) {
		if (IsDisconnected())
		{
			m_startTime = m_updatedPixels = m_sentFrames = m_sentBytes = m_receivedBytes = 0;
			if (m_szText[0]) {
				m_szText[0] = 0;
				InvalidateRect(hWnd, NULL, TRUE);
			}
			if (m_szLog[0]) {
				InitLog();
				InvalidateRect(hWnd, NULL, TRUE);
			}
			Sleep(200);
		}
		else if (m_startTime > 0)
		{
			static LONGLONG s_tick = GetTickCount64();
			static LONGLONG s_sentBytes = 0;
			static LONGLONG s_sentFrames = 0;
			LONGLONG tick = GetTickCount64();
			Capture();
			DWORD bps = (DWORD)(m_sentBytes - s_sentBytes);
			DWORD fps = (DWORD)(m_sentFrames - s_sentFrames);
			DWORD bpf = fps ? bps / fps : 0;
			if (GetTickCount64() - s_tick >= 1000)
			{
				LONGLONG time = tick - m_startTime;
				sprintf_s(m_szText, "pixels %llu bytes %llu/%llu frames %llu time %llu bpf %d  bps %d fps %d",
					m_updatedPixels, m_sentBytes, m_receivedBytes, m_sentFrames, time, bpf, bps, fps);
				s_tick = tick;
				s_sentBytes = m_sentBytes;
				s_sentFrames = m_sentFrames;
				InvalidateRect(hWnd, NULL, TRUE);
			}
			if (bpf > 0) {
				DWORD frameTicks = bpf / 500;
				if (frameTicks < 125)
					frameTicks = 125;
				DWORD ticks = (DWORD)(GetTickCount64() - tick);
				if (ticks < frameTicks) {
					Sleep(frameTicks - ticks);
				}
			}
			else
				Sleep(50);
		}
	}
}

void Receive(LPVOID p)
{
	while (m_running) {
		if (IsDisconnected())
			Accept();
		else
			Read();
	}
}

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

	if (InitSocket(hWnd) != S_OK)
		return TRUE;

	_beginthread(Receive, 0, hWnd);
	_beginthread(Send, 0, hWnd);

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
		if (m_clientSocket != INVALID_SOCKET) {
			shutdown(m_clientSocket, SD_SEND);
			m_clientSocket = INVALID_SOCKET;
		}
		if (m_listenSocket != INVALID_SOCKET) {
			shutdown(m_listenSocket, SD_RECEIVE);
			m_listenSocket = INVALID_SOCKET;
		}
		Sleep(100);
		GlobalFree(m_screenDIB);
		GlobalFree(m_screenBuffer);
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
