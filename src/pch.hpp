#pragma warning(push)
#pragma warning(disable : 4201)

// Windows.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <comdef.h>
#include <wrl/client.h>

// Windows Sockets 2.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

// Windows Media Foundation.

#include <codecapi.h>
#include <d3d11.h>
#include <d3d9.h>
#include <d3d9caps.h>
#include <d3d9types.h>
#include <dxva.h>
#include <dxva2api.h>
#include <dxvahd.h>
#include <evr.h>
#include <evr9.h>
#include <mfapi.h>
#include <mfcaptureengine.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfmediacapture.h>
#include <mfmediaengine.h>
#include <mfmp2dlna.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <opmapi.h>
#include <wmcodecdsp.h>
#include <wmcontainer.h>

#include <dvdmedia.h>

// DirectX.

#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d11on12.h>

#pragma warning(pop)

#include <print>
#include <fstream>
#include <vector>
#include <span>
#include <cassert>
#include <chrono>
#include <algorithm>
#include <ranges>
#include <filesystem>
#include <thread>
#include <condition_variable>

using namespace std::literals;