#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>

class GameEngine;
struct ImGui_ImplDX12_InitInfo;

// Owns the Dear ImGui context used by the editor.  Keeping this outside of the
// game model makes it possible for every editor panel to share one selection.
class EditorUI
{
public:
    bool Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    void Shutdown();

    void Render(GameEngine* engine, ID3D12GraphicsCommandList* commandList, float frameDeltaTime);
    bool HandleWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) const;

    bool WantsMouse() const;
    bool WantsKeyboard() const;
    bool WantsViewportInput() const;
    bool IsViewportInputArea(int clientX, int clientY) const;

private:
    static void AllocateDescriptor(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
    static void FreeDescriptor(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    void DrawMainMenu();
    void DrawSceneOutliner(GameEngine* engine);
    void DrawViewportOverlay(GameEngine* engine);
    void DrawInspector(GameEngine* engine);
    void DrawContentDrawer();
    void DrawStatusBar(GameEngine* engine);

    static constexpr UINT DescriptorCount = 64;

    HWND m_Hwnd = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
    UINT m_DescriptorSize = 0;
    std::array<bool, DescriptorCount> m_DescriptorInUse{};
    uint32_t m_SelectedEntityId = UINT32_MAX;
    bool m_Initialized = false;
    bool m_LayoutInitialized = false;
    bool m_ViewportAcceptsInput = false;
    float m_ViewportInputLeft = 0.0f;
    float m_ViewportInputTop = 0.0f;
    float m_ViewportInputRight = 0.0f;
    float m_ViewportInputBottom = 0.0f;
    float m_SmoothedFrameTime = 1.0f / 60.0f;
};
