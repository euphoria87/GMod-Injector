// GMod Injector - A simple DLL injector for Garry's Mod with an ImGui interface
// Created by Markus
// Github: https://github.com/euphoria87

#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <string>
#include "resource.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

DWORD GetProcessIdByName(const wchar_t* processName)
{
    DWORD pid = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe32{};
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(hSnapshot, &pe32))
    {
        do
        {
            if (_wcsicmp(pe32.szExeFile, processName) == 0)
            {
                pid = pe32.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return pid;
}

bool InjectDLL(DWORD pid, const std::wstring& dllPath)
{
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    SIZE_T pathLen = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID pRemote = VirtualAllocEx(hProcess, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemote)
    {
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemote, dllPath.c_str(), pathLen, NULL))
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel)
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC pFunc = GetProcAddress(hKernel, "LoadLibraryW");
    if (!pFunc)
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPTHREAD_START_ROUTINE pLoadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(pFunc);
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLib, pRemote, 0, NULL);
    if (!hThread)
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return (exitCode != 0);
}

int APIENTRY wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    
    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCE (IDI_ICON1));

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(NULL),
                       hIcon,
                       NULL, NULL, NULL,
                       L"GMod Injector", NULL };

    ::RegisterClassExW(&wc);

    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = { 0, 0, 450, 320 };
    AdjustWindowRect(&rect, dwStyle, FALSE);

    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"GMod Injector", dwStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, wc.hInstance, NULL);

    if (!g_hwnd)
    {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    io.Fonts->ClearFonts();
    const char* font_path = "C:\\Windows\\Fonts\\arial.ttf";
    ImFont* font = io.Fonts->AddFontFromFileTTF(font_path, 16.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if (!font)
        io.Fonts->AddFontDefault();
    io.Fonts->Build();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(20, 20);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 12);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.35f, 0.55f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 1.00f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

    std::wstring selectedDll = L"None selected";
    std::string status = "Ready";
    bool autoSearch = true;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT)
    {
        if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(450, 320), ImGuiCond_Always);

        ImGui::Begin("MainWindow", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushFont(font);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("GMod Injector").x) * 0.5f);
        ImGui::TextColored(ImVec4(0.50f, 0.75f, 1.00f, 1.00f), "GMod Injector");

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("v1.0").x) * 0.5f);
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.70f, 1.00f), "v1.0");
        ImGui::PopFont();

        ImGui::Separator();

        DWORD pid = 0;
        if (autoSearch)
        {
            pid = GetProcessIdByName(L"gmod.exe");
            if (pid == 0) pid = GetProcessIdByName(L"hl2.exe");
        }

        if (pid != 0)
        {
            char pid_text[128];
            snprintf(pid_text, sizeof(pid_text), "Garry's Mod detected (PID: %lu)", pid);
            ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.00f), "%s", pid_text);
        }
        else
        {
            ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.40f, 1.00f), "Garry's Mod not running");
        }

        ImGui::Spacing();

        if (ImGui::Button("SELECT DLL", ImVec2(100, 30)))
        {
            wchar_t szFile[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g_hwnd;
            ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle = L"Select DLL for injection";

            if (GetOpenFileNameW(&ofn))
                selectedDll = szFile;
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.80f, 0.90f, 1.00f));

        std::string displayPath = "None selected";
        if (selectedDll != L"None selected")
        {
            std::wstring ws = selectedDll;
            displayPath = std::string(ws.begin(), ws.end());
            if (displayPath.length() > 35)
                displayPath = "..." + displayPath.substr(displayPath.length() - 32);
        }
        ImGui::Text("%s", displayPath.c_str());

        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 140) * 0.5f);
        if (ImGui::Button("INJECT", ImVec2(140, 40)))
        {
            if (pid == 0)
                status = "Error: Garry's Mod not found";
            else if (selectedDll == L"None selected")
                status = "Error: No DLL selected";
            else
            {
                if (InjectDLL(pid, selectedDll))
                    status = "Success! DLL injected";
                else
                    status = "Injection failed";
            }
        }

        ImGui::Spacing();

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x) * 0.5f);

        if (status.find("Success") != std::string::npos)
            ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.00f), "%s", status.c_str());
        else if (status.find("Error") != std::string::npos || status.find("failed") != std::string::npos)
            ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.40f, 1.00f), "%s", status.c_str());
        else
            ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.70f, 1.00f), "%s", status.c_str());

        ImGui::End();

        ImGui::Render();

        const float clear_color_with_alpha[4] = { 0.08f, 0.08f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res == E_INVALIDARG)
    {
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
            &featureLevelArray[1], 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }

    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}