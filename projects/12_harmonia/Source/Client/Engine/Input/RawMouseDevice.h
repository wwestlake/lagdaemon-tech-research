#pragma once
#include <atomic>

namespace Harmonia {

class RawMouseDevice {
public:
    RawMouseDevice();
    ~RawMouseDevice();

    void initialise(void* nativeWindowHandle);
    void shutdown();

    void setCaptured(bool captured);
    bool isCaptured() const { return captured_; }

    // Atomically reads and zeroes the accumulated deltas
    void consumeDeltas(int& outX, int& outY);
    static void consumeGlobalDeltas(int& outX, int& outY);

private:
    void* hwnd_ = nullptr;
    void* originalWndProc_ = nullptr;
    bool captured_ = false;

    std::atomic<int> dx_{0};
    std::atomic<int> dy_{0};

    static RawMouseDevice* instance_;
    
    // Windows API hook
    static long long __stdcall wndProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);
};

} // namespace Harmonia
