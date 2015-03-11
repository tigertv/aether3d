#include "Window.hpp"
#include <iostream>
#include <cassert>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
//#include <GL/glcorearb.h>
#include <GL/GL.h>
#include <GL/wglext.h>

namespace WindowGlobal
{
    bool isOpen = false;
    const int eventStackSize = 10;
    ae3d::WindowEvent eventStack[eventStackSize];
    int eventIndex = -1;
    HWND hwnd;
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_ACTIVATEAPP:
            //RendererImpl::Instance().SetFocus((wParam == WA_INACTIVE) ? false : true);
            break;
        case WM_SIZE:
        case WM_SETCURSOR:
        case WM_DESTROY:
        case WM_SYSKEYUP:
        case WM_KEYUP:
            break;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = ae3d::WindowEventType::KeyDown;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].keyCode = static_cast<int>( wParam );
        }
        break;
        case WM_SYSCOMMAND:
            if (wParam == SC_MINIMIZE)
            {
                // Handled by WM_SIZE.
            }
            else if (wParam == SC_RESTORE)
            {
            }
        case WM_LBUTTONDOWN:
            break;
        case WM_RBUTTONDOWN:
            break;
        case WM_MBUTTONDOWN:
            break;
        case WM_MOUSEWHEEL:
            break;
        case WM_MOUSEMOVE:
            //gMousePosition[0] = (short)LOWORD(lParam);
            //gMousePosition[1] = (short)HIWORD(lParam);
            
            // Check to see if the left button is held down:
            //bool leftButtonDown=wParam & MK_LBUTTON;
            
            // Check if right button down:
            //bool rightButtonDown=wParam & MK_RBUTTON;
            break;
        case WM_CLOSE:
            OutputDebugStringA("Window close message win32\n");
            ++WindowGlobal::eventIndex;
            WindowGlobal::eventStack[WindowGlobal::eventIndex].type = ae3d::WindowEventType::Close;
            break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

namespace ae3d
{
    void Window::Create(int width, int height, WindowCreateFlags flags)
    {
        const int finalWidth = width == 0 ? GetSystemMetrics(SM_CXSCREEN) : width;
        const int finalHeight = height == 0 ? GetSystemMetrics(SM_CYSCREEN) : height;

        const HINSTANCE hInstance = GetModuleHandle(nullptr);
        const bool fullscreen = flags & WindowCreateFlags::Fullscreen;

        WNDCLASSEX wc;
        ZeroMemory(&wc, sizeof(WNDCLASSEX));

        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; // OWNDC is needed for OpenGL.
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
        wc.lpszClassName = "WindowClass1";

        wc.hIcon = (HICON)LoadImage(nullptr,
            "glider.ico",
            IMAGE_ICON,
            32,
            32,
            LR_LOADFROMFILE);

        RegisterClassEx(&wc);

        WindowGlobal::hwnd = CreateWindowExA(fullscreen ? WS_EX_TOOLWINDOW | WS_EX_TOPMOST : 0,
            "WindowClass1",    // name of the window class
            "Window",   // title of the window
            fullscreen ? WS_POPUP : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU),    // window style
            CW_USEDEFAULT,    // x-position of the window
            CW_USEDEFAULT,    // y-position of the window
            finalWidth,    // width of the window
            finalHeight,    // height of the window
            nullptr,    // we have no parent window
            nullptr,    // we aren't using menus
            hInstance,    // application handle
            nullptr);    // used with multiple windows    
        
        ShowWindow(WindowGlobal::hwnd, SW_SHOW);
        CreateRenderer();
        WindowGlobal::isOpen = true;
    }

    bool Window::PollEvent(WindowEvent& outEvent)
    {
        if (WindowGlobal::eventIndex == -1)
        {
            return false;
        }

        outEvent = WindowGlobal::eventStack[WindowGlobal::eventIndex];
        --WindowGlobal::eventIndex;
        return true;
    }

    void Window::PumpEvents()
    {
        MSG msg;

        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    bool Window::IsOpen()
    {
        return WindowGlobal::isOpen;
    }

    // TODO: Separate from this source file.
    void Window::CreateRenderer()
    {
        // Choose pixel format
        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory(&pfd, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 32;
        pfd.iLayerType = PFD_MAIN_PLANE;

        const HDC hdc = GetDC(WindowGlobal::hwnd);
        const int pixelFormat = ChoosePixelFormat(hdc, &pfd);
        
        if (pixelFormat == 0)
        {
            assert(!"Failed to find suitable pixel format!");
        }

        if (!SetPixelFormat(hdc, pixelFormat, &pfd))
        {
            assert(!"Failed to set pixel format!");
        }

        // Create temporary context and make sure we have support
        HGLRC tempContext = wglCreateContext(hdc);
        if (!tempContext)
        {
            assert(!"Failed to create temporary context!");
        }

        if (!wglMakeCurrent(hdc, tempContext))
        {
            assert(!"Failed to activate temporary context!");
        }

        const int attribs[] =
        {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3, // Not using 4.5 yet because I'm developing also on non-supported GPUs.
            WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };

        PFNWGLCREATEBUFFERREGIONARBPROC wglCreateContextAttribsARB = (PFNWGLCREATEBUFFERREGIONARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
        if (!wglCreateContextAttribsARB)
        {
            assert(!"Failed to find pointer to wglCreateContextAttribsARB function!");
        }

        HANDLE context = wglCreateContextAttribsARB(hdc, 0, (UINT)attribs);

        if (context == 0)
        {
            assert(!"Failed to create OpenGL context!");
        }

        // Remove temporary context and activate forward compatible context
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempContext);
        if (!wglMakeCurrent(hdc, (HGLRC)context))
        {
            assert(!"Failed to activate forward compatible context!");
        }

        MessageBoxA(0, (char*)glGetString(GL_VERSION), "OPENGL VERSION", 0);
    }
}