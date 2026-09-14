#include "RawMouseDevice.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Harmonia {

RawMouseDevice* RawMouseDevice::instance_ = nullptr;

RawMouseDevice::RawMouseDevice() {
    instance_ = this;
}

RawMouseDevice::~RawMouseDevice() {
    shutdown();
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

#ifdef _WIN32
long long __stdcall RawMouseDevice::wndProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam) {
    if (msg == WM_INPUT && instance_) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        
        if (dwSize > 0) {
            BYTE* lpb = new BYTE[dwSize];
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)lpb;
                if (raw->header.dwType == RIM_TYPEMOUSE && instance_->isCaptured()) {
                    if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE) {
                        // Absolute device (e.g. RDP, tablet)
                        static LONG lastAbsX = raw->data.mouse.lLastX;
                        static LONG lastAbsY = raw->data.mouse.lLastY;
                        LONG dx = raw->data.mouse.lLastX - lastAbsX;
                        LONG dy = raw->data.mouse.lLastY - lastAbsY;
                        lastAbsX = raw->data.mouse.lLastX;
                        lastAbsY = raw->data.mouse.lLastY;
                        
                        // Ignore massive jumps (e.g. entering the window)
                        if (std::abs(dx) < 3000 && std::abs(dy) < 3000) {
                            // Scale down absolute coordinates (0-65535) to standard pixel-ish deltas
                            instance_->dx_.fetch_add(dx / 30, std::memory_order_relaxed);
                            instance_->dy_.fetch_add(dy / 30, std::memory_order_relaxed);
                        }
                    } else {
                        // Accumulate relative raw deltas
                        // Filter out impossibly large single-packet jumps (e.g. software cursor warping)
                        // A physical mouse won't move 400 pixels in a single 1ms-8ms hardware poll.
                        if (std::abs(raw->data.mouse.lLastX) < 400 && std::abs(raw->data.mouse.lLastY) < 400) {
                            instance_->dx_.fetch_add(raw->data.mouse.lLastX, std::memory_order_relaxed);
                            instance_->dy_.fetch_add(raw->data.mouse.lLastY, std::memory_order_relaxed);
                        }
                    }
                }
            }
            delete[] lpb;
        }
    }
    
    // Call the original window proc
    if (instance_ && instance_->originalWndProc_) {
        return CallWindowProc((WNDPROC)instance_->originalWndProc_, (HWND)hwnd, msg, wParam, lParam);
    }
    return DefWindowProc((HWND)hwnd, msg, wParam, lParam);
}
#else
long long __stdcall RawMouseDevice::wndProc(void*, unsigned int, unsigned long long, long long) { return 0; }
#endif

void RawMouseDevice::initialise(void* nativeWindowHandle) {
#ifdef _WIN32
    if (hwnd_) return; // already initialised
    hwnd_ = nativeWindowHandle;
    
    // Register for raw mouse input
    RAWINPUTDEVICE Rid[1];
    Rid[0].usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
    Rid[0].usUsage = 0x02;     // HID_USAGE_GENERIC_MOUSE
    Rid[0].dwFlags = RIDEV_INPUTSINK; // Receive input even if not focused (or we can use 0 for focus only)
    // Actually, we usually want input only when focused, so dwFlags = 0 is better.
    // But since we use it for capturing, RIDEV_INPUTSINK is fine. Let's use 0 to only get input when in foreground.
    Rid[0].dwFlags = 0;
    Rid[0].hwndTarget = (HWND)hwnd_;
    
    RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));
    
    // Subclass the window
    originalWndProc_ = (void*)SetWindowLongPtr((HWND)hwnd_, GWLP_WNDPROC, (LONG_PTR)&wndProc);
#endif
}

void RawMouseDevice::shutdown() {
#ifdef _WIN32
    if (hwnd_ && originalWndProc_) {
        SetWindowLongPtr((HWND)hwnd_, GWLP_WNDPROC, (LONG_PTR)originalWndProc_);
        originalWndProc_ = nullptr;
    }
    hwnd_ = nullptr;
#endif
}

void RawMouseDevice::setCaptured(bool captured) {
    if (captured_ == captured) return;
    
    captured_ = captured;
    
    if (captured) {
        // Discard any stale deltas on capture
        dx_ = 0;
        dy_ = 0;
        
#ifdef _WIN32
        // Confine the cursor to the window bounds
        if (hwnd_) {
            RECT rect;
            GetClientRect((HWND)hwnd_, &rect);
            ClientToScreen((HWND)hwnd_, (LPPOINT)&rect.left);
            ClientToScreen((HWND)hwnd_, (LPPOINT)&rect.right);
            ClipCursor(&rect);
        }
#endif
    } else {
#ifdef _WIN32
        // Release confinement
        ClipCursor(NULL);
#endif
    }
}

void RawMouseDevice::consumeDeltas(int& outX, int& outY) {
    outX = dx_.exchange(0, std::memory_order_relaxed);
    outY = dy_.exchange(0, std::memory_order_relaxed);
}

void RawMouseDevice::consumeGlobalDeltas(int& outX, int& outY) {
    if (instance_) {
        instance_->consumeDeltas(outX, outY);
    } else {
        outX = 0;
        outY = 0;
    }
}

} // namespace Harmonia
