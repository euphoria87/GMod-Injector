// GMod Injector - A simple DLL injector for Garry's Mod with a modern UI using Dear ImGui and DirectX 11.
// Version: 1.1
// Created by Markus
// Github: https://github.com/euphoria87


#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <tlhelp32.h>
#include <commdlg.h>
#include <algorithm>
#include <cstdint>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Drag & Drop сообщения
#define WM_DRAGENTER    0x0230
#define WM_DRAGOVER     0x0231
#define WM_DRAGLEAVE    0x0232
#define WM_DROP         0x0233

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_hwnd = nullptr;

ID3D11ShaderResourceView* g_texIcon = nullptr;
ID3D11ShaderResourceView* g_texClose = nullptr;
ID3D11ShaderResourceView* g_texMinimize = nullptr;

std::wstring selectedDllPath = L"";
std::string  selectedDllName = "None selected";

std::string status = "Ready";

bool isDraggingOver = false;

// Для автосброса статуса
float statusResetTimer = 0.0f;
const float STATUS_RESET_DELAY = 3.0f; // 3 секунды

// Для плавного появления интерфейса
float uiAlpha = 1.0f;
bool isAppStarting = true;

// Флаг для завершения приложения
bool shouldCloseApp = false;

// Анимации кнопок
float buttonMinimizeHoverAlpha = 0.0f;
float buttonCloseHoverAlpha = 0.0f;
float buttonInjectHoverAlpha = 0.0f;
float buttonSelectHoverAlpha = 0.0f;

// Анимация текста статуса
float statusTextAlpha = 1.0f;
float statusTextPulse = 0.0f;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::string GetFileNameFromPath(const std::wstring& fullPath);

// Функция для плавной интерполяции (lerp)
inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Функция для обновления анимации наведения
inline void UpdateHoverAnimation(bool isHovered, float& hoverAlpha, float speed = 0.08f)
{
    if (isHovered)
    {
        hoverAlpha += speed;
        if (hoverAlpha > 1.0f) hoverAlpha = 1.0f;
    }
    else
    {
        hoverAlpha -= speed;
        if (hoverAlpha < 0.0f) hoverAlpha = 0.0f;
    }
}

bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv)
{
    int w, h;
    unsigned char* data = stbi_load(filename, &w, &h, NULL, 4);
    if (!data) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* texture = nullptr;
    D3D11_SUBRESOURCE_DATA sub = { data, (UINT)(w * 4), 0 };

    if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, &sub, &texture))) {
        stbi_image_free(data);
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    HRESULT hr = g_pd3dDevice->CreateShaderResourceView(texture, &srvDesc, out_srv);
    texture->Release();
    stbi_image_free(data);
    return SUCCEEDED(hr);
}

DWORD GetProcessIdByName(const wchar_t* processName)
{
    DWORD pid = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe32 = { sizeof(pe32) };
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
    if (!pRemote) { CloseHandle(hProcess); return false; }

    if (!WriteProcessMemory(hProcess, pRemote, dllPath.c_str(), pathLen, NULL))
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC pFunc = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!pFunc)
    {
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pFunc, pRemote, 0, NULL);
    if (hThread)
    {
        WaitForSingleObject(hThread, 5000);
        CloseHandle(hThread);
    }

    VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

std::string GetFileNameFromPath(const std::wstring& fullPath)
{
    size_t pos = fullPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        return std::string(fullPath.begin() + pos + 1, fullPath.end());
    return std::string(fullPath.begin(), fullPath.end());
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(NULL), NULL, NULL, NULL, NULL,
                       L"GModInjector", NULL };
    RegisterClassExW(&wc);

    // Borderless окно без titlebar, minimize, close buttons
    DWORD dwStyle = WS_POPUP | WS_VISIBLE;
    DWORD dwExStyle = WS_EX_APPWINDOW;

    g_hwnd = CreateWindowExW(dwExStyle, wc.lpszClassName, L"GMod Injector",
        dwStyle, CW_USEDEFAULT, CW_USEDEFAULT, 380, 360, NULL, NULL, wc.hInstance, NULL);

    if (!g_hwnd) return 1;

    // Центрировать окно на экране
    RECT screen;
    GetClientRect(GetDesktopWindow(), &screen);
    int screenWidth = screen.right - screen.left;
    int screenHeight = screen.bottom - screen.top;
    int windowWidth = 380;
    int windowHeight = 360;
    int posX = (screenWidth - windowWidth) / 2;
    int posY = (screenHeight - windowHeight) / 2;
    SetWindowPos(g_hwnd, HWND_TOPMOST, posX, posY, windowWidth, windowHeight, SWP_NOZORDER);

    // Регистрация окна для Drag & Drop ПЕРЕД созданием устройства
    DragAcceptFiles(g_hwnd, TRUE);

    // Скругляем углы окна
    HRGN hRgn = CreateRoundRectRgn(0, 0, windowWidth, windowHeight, 16, 16);
    SetWindowRgn(g_hwnd, hRgn, FALSE);

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    // Сначала делаем окно прозрачным (альфа = 0), потом плавно появляется
    uiAlpha = 0.0f;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    LoadTextureFromFile("icon.png", &g_texIcon);
    LoadTextureFromFile("close.png", &g_texClose);
    LoadTextureFromFile("minimize.png", &g_texMinimize);

    io.Fonts->Clear();
    ImFont* fontBold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 25.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if (!fontBold) fontBold = io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;        // Округлые углы окна
    style.FrameRounding = 8.0f;          // Округлые углы кнопок
    style.PopupRounding = 8.0f;
    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;

    // Очень темные цвета (серо-черные)
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.48f, 0.88f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.58f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.35f, 0.75f, 1.00f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);

    bool windowDragging = false;
    ImVec2 dragStartPos;

    MSG msg = {};
    while (msg.message != WM_QUIT && !shouldCloseApp)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // Плавное появление интерфейса при запуске
        if (isAppStarting && uiAlpha < 1.0f)
        {
            uiAlpha += 0.05f;
            if (uiAlpha >= 1.0f)
                isAppStarting = false;
        }

        // Обновляем таймер сброса статуса
        if (status != "Ready")
        {
            statusResetTimer += 0.016f; // ~60 FPS
            if (statusResetTimer >= STATUS_RESET_DELAY)
            {
                status = "Ready";
                statusResetTimer = 0.0f;
            }
            // Пульс для текста статуса
            statusTextPulse = std::sin(statusResetTimer * 3.14159f) * 0.2f + 0.8f;
        }
        else
        {
            statusResetTimer = 0.0f;
            statusTextPulse = 1.0f;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(380, 360), ImGuiCond_Always);

        ImGui::Begin("##Main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        // Получаем drawlist для скругления окна БЕЗ обводки
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 wmin = ImGui::GetWindowPos();
        ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowWidth(), wmin.y + ImGui::GetWindowHeight());
        drawList->AddRectFilled(wmin, wmax, IM_COL32(31, 31, 31, 255), 12.0f);

        // Применяем прозрачность всему окну
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, uiAlpha);

        ImGui::PushFont(fontBold);

        // Кастомный titlebar с иконкой и кнопками
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##drag", ImVec2(280, 65));

        // Логика перетаскивания окна
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (!windowDragging)
            {
                windowDragging = true;
                POINT cp; GetCursorPos(&cp);
                RECT wr; GetWindowRect(g_hwnd, &wr);
                dragStartPos.x = (float)(cp.x - wr.left);
                dragStartPos.y = (float)(cp.y - wr.top);
            }
            POINT p; GetCursorPos(&p);
            SetWindowPos(g_hwnd, NULL, p.x - (int)dragStartPos.x, p.y - (int)dragStartPos.y, 0, 0, SWP_NOSIZE);
        }
        else if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) windowDragging = false;

        // Иконка
        ImGui::SetCursorPos(ImVec2(12, 8));
        if (g_texIcon) ImGui::Image((ImTextureID)(intptr_t)g_texIcon, ImVec2(48, 48));

        // Текст "GMod Injector v1.1" - красиво на одной линии
        ImGui::SetCursorPos(ImVec2(68, 18));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 textPos = ImGui::GetCursorScreenPos();

        // Градиент для текста (голубой -> светлый голубой)
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(textPos.x, textPos.y),
            ImGui::GetColorU32(ImVec4(0.45f, 0.78f, 1.0f, 1.0f)), "GMod Injector");

        // Версия рядом
        float textWidth = ImGui::CalcTextSize("GMod Injector").x;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f,
            ImVec2(textPos.x + textWidth + 6, textPos.y + 5),
            ImGui::GetColorU32(ImVec4(0.58f, 0.58f, 0.68f, 1.0f)), "v1.1");

        ImGui::PopFont();

        // Кнопка сворачивания с иконкой
        ImGui::SetCursorPos(ImVec2(290, 8));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (g_texMinimize && ImGui::ImageButton("##minimize", (ImTextureID)(intptr_t)g_texMinimize, ImVec2(40, 40), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0)))
        {
            ShowWindow(g_hwnd, SW_MINIMIZE);
        }
        UpdateHoverAnimation(ImGui::IsItemHovered(), buttonMinimizeHoverAlpha);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        // Кнопка закрытия с иконкой
        ImGui::SetCursorPos(ImVec2(333, 8));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 0.6f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (g_texClose && ImGui::ImageButton("##close", (ImTextureID)(intptr_t)g_texClose, ImVec2(40, 40), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0)))
        {
            PostMessage(g_hwnd, WM_DESTROY, 0, 0);
        }
        UpdateHoverAnimation(ImGui::IsItemHovered(), buttonCloseHoverAlpha);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::SetCursorPosY(73);
        ImGui::Separator();

        DWORD pid = GetProcessIdByName(L"gmod.exe");
        if (pid == 0) pid = GetProcessIdByName(L"hl2.exe");

        if (pid != 0)
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "Garry's Mod detected (PID: %lu)", pid);
        else
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Garry's Mod not running");

        ImGui::Spacing(); ImGui::Spacing();

        if (ImGui::Button("SELECT DLL", ImVec2(140, 36)))
        {
            wchar_t szFile[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g_hwnd;
            ofn.lpstrFilter = L"DLL files (*.dll)\0*.dll\0All files\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameW(&ofn))
            {
                selectedDllPath = szFile;
                selectedDllName = GetFileNameFromPath(selectedDllPath);

                if (selectedDllPath.size() >= 4 &&
                    _wcsicmp(selectedDllPath.c_str() + selectedDllPath.size() - 4, L".dll") != 0)
                {
                    status = "Error: File must be .dll";
                    selectedDllName = "None selected";
                    selectedDllPath = L"";
                }
                else
                {
                    status = "DLL selected";
                }
            }
        }

        // Анимация наведения для SELECT DLL
        UpdateHoverAnimation(ImGui::IsItemHovered(), buttonSelectHoverAlpha);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.82f, 0.92f, 1.0f));
        ImGui::Text("%s", selectedDllName.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImGui::SetCursorPosX((380 - 140) * 0.5f);
        if (ImGui::Button("INJECT", ImVec2(140, 44)))
        {
            if (pid == 0)
                status = "Error: Garry's Mod not found";
            else if (selectedDllPath.empty() || selectedDllName == "None selected")
                status = "Error: No DLL selected";
            else
                status = InjectDLL(pid, selectedDllPath) ? "Success! DLL injected" : "Injection failed";
        }

        // Анимация наведения для INJECT
        UpdateHoverAnimation(ImGui::IsItemHovered(), buttonInjectHoverAlpha);

        ImGui::Spacing();

        ImGui::SetCursorPosX((380 - ImGui::CalcTextSize(status.c_str()).x) * 0.5f);

        // Анимированный цвет статуса с пульсом
        float statusAlpha = statusTextPulse;
        if (status.find("Success") != std::string::npos)
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, statusAlpha), "%s", status.c_str());
        else if (status.find("Error") != std::string::npos || status.find("failed") != std::string::npos)
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, statusAlpha), "%s", status.c_str());
        else
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.75f, 1.0f), "%s", status.c_str());

        ImGui::PopStyleVar(); // Закрываем Alpha
        ImGui::End();

        ImGui::Render();
        const float clear_color[4] = { 0.12f, 0.12f, 0.12f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    if (g_texIcon) g_texIcon->Release();
    if (g_texClose) g_texClose->Release();
    if (g_texMinimize) g_texMinimize->Release();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// ==================== Device Functions ====================

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (res == E_INVALIDARG)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            &levels[1], 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (FAILED(res)) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    // Обработка WM_DRAGENTER для включения оверлея
    if (msg == WM_DRAGENTER)
    {
        isDraggingOver = true;
        return TRUE;
    }

    // Обработка WM_DRAGOVER для поддержания оверлея
    if (msg == WM_DRAGOVER)
    {
        isDraggingOver = true;
        return TRUE;
    }

    // Обработка WM_DRAGLEAVE для выключения оверлея
    if (msg == WM_DRAGLEAVE)
    {
        isDraggingOver = false;
        return TRUE;
    }

    // Обработка WM_DROPFILES для получения файла
    if (msg == WM_DROPFILES)
    {
        HDROP hDrop = (HDROP)wParam;
        wchar_t szFile[MAX_PATH] = {};

        if (DragQueryFileW(hDrop, 0, szFile, MAX_PATH))
        {
            std::wstring path = szFile;

            if (path.size() >= 4 && _wcsicmp(path.c_str() + path.size() - 4, L".dll") == 0)
            {
                selectedDllPath = path;
                selectedDllName = GetFileNameFromPath(path);
                status = "DLL loaded via Drag & Drop";
            }
            else
            {
                status = "Error: File must be .dll format";
            }
        }
        DragFinish(hDrop);
        isDraggingOver = false;
        return 0;
    }

    if (msg == WM_DESTROY)
    {
        shouldCloseApp = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
