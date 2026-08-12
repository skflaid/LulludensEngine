#include "EditorUI.h"

#include "GameEngine.h"
#include "Core/Entity.h"
#include "Physics/Components/ColliderComponent.h"
#include "Physics/Components/RigidbodyComponent.h"
#include "Renderer/Components/MaterialComponent.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/TransformComponent.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
constexpr float kLeftPanelWidth = 260.0f;
constexpr float kRightPanelWidth = 340.0f;
constexpr float kBottomPanelHeight = 180.0f;

Entity* FindEntity(GameEngine* engine, uint32_t id)
{
    if (!engine)
        return nullptr;

    for (uint64_t index = 0; index < engine->GetEntityCount(); ++index)
    {
        Entity* entity = engine->GetEntityByIndex(index);
        if (entity && entity->GetID() == id)
            return entity;
    }
    return nullptr;
}
}

bool EditorUI::Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
    if (!hwnd || !device || !commandQueue)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = DescriptorCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_SrvHeap))))
        return false;

    m_Hwnd = hwnd;
    m_DescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(m_Hwnd);

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device = device;
    initInfo.CommandQueue = commandQueue;
    initInfo.NumFramesInFlight = 2;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = m_SrvHeap.Get();
    initInfo.SrvDescriptorAllocFn = AllocateDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeDescriptor;

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_SrvHeap.Reset();
        return false;
    }

    m_Initialized = true;
    return true;
}

void EditorUI::Shutdown()
{
    if (!m_Initialized)
        return;

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_SrvHeap.Reset();
    m_Initialized = false;
}

void EditorUI::Render(GameEngine* engine, ID3D12GraphicsCommandList* commandList, float frameDeltaTime)
{
    if (!m_Initialized || !commandList)
        return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (frameDeltaTime > 0.0f) {
        m_SmoothedFrameTime += (frameDeltaTime - m_SmoothedFrameTime) * 0.1f;
    }

    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    if (!m_LayoutInitialized)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        // The scene is still rendered directly to the swap chain.  Keep the
        // central docking node transparent so that the 3D backbuffer remains
        // visible behind the Scene Viewport editor chrome.
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

        ImGuiID mainNode = dockspaceId;
        ImGuiID leftNode = 0;
        ImGuiID rightNode = 0;
        ImGuiID bottomNode = 0;
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.20f, &leftNode, &mainNode);
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.27f, &rightNode, &mainNode);
        ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Down, 0.25f, &bottomNode, &mainNode);
        ImGui::DockBuilderDockWindow("Scene Outliner", leftNode);
        ImGui::DockBuilderDockWindow("Inspector", rightNode);
        ImGui::DockBuilderDockWindow("Content / Instances", bottomNode);
        ImGui::DockBuilderDockWindow("Scene Viewport", mainNode);
        ImGui::DockBuilderFinish(dockspaceId);
        m_LayoutInitialized = true;
    }

    DrawMainMenu();
    DrawSceneOutliner(engine);
    DrawViewportOverlay();
    DrawInspector(engine);
    DrawContentDrawer();
    DrawStatusBar(engine);

    ImGui::Render();
    ID3D12DescriptorHeap* heaps[] = { m_SrvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool EditorUI::HandleWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) const
{
    return m_Initialized && ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0;
}

bool EditorUI::WantsMouse() const
{
    return m_Initialized && ImGui::GetIO().WantCaptureMouse;
}

bool EditorUI::WantsKeyboard() const
{
    return m_Initialized && ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorUI::WantsViewportInput() const
{
    return m_Initialized && m_ViewportAcceptsInput;
}

bool EditorUI::IsViewportInputArea(int clientX, int clientY) const
{
    return m_Initialized &&
        clientX >= m_ViewportInputLeft && clientX <= m_ViewportInputRight &&
        clientY >= m_ViewportInputTop && clientY <= m_ViewportInputBottom;
}

void EditorUI::AllocateDescriptor(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
    auto* editor = initInfo ? static_cast<EditorUI*>(initInfo->UserData) : nullptr;
    if (!editor || !editor->m_SrvHeap)
        return;

    for (UINT index = 0; index < DescriptorCount; ++index)
    {
        if (editor->m_DescriptorInUse[index])
            continue;

        editor->m_DescriptorInUse[index] = true;
        *cpuHandle = editor->m_SrvHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle->ptr += static_cast<SIZE_T>(index) * editor->m_DescriptorSize;
        *gpuHandle = editor->m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
        gpuHandle->ptr += static_cast<UINT64>(index) * editor->m_DescriptorSize;
        return;
    }
}

void EditorUI::FreeDescriptor(ImGui_ImplDX12_InitInfo* initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* editor = initInfo ? static_cast<EditorUI*>(initInfo->UserData) : nullptr;
    if (!editor || !editor->m_SrvHeap || editor->m_DescriptorSize == 0)
        return;

    const SIZE_T first = editor->m_SrvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    if (cpuHandle.ptr < first)
        return;

    const SIZE_T offset = cpuHandle.ptr - first;
    if (offset % editor->m_DescriptorSize != 0)
        return;

    const UINT index = static_cast<UINT>(offset / editor->m_DescriptorSize);
    if (index < DescriptorCount)
        editor->m_DescriptorInUse[index] = false;
}

void EditorUI::DrawMainMenu()
{
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File"))
    {
        ImGui::MenuItem("New Scene", "Ctrl+N", false, false);
        ImGui::MenuItem("Save Scene", "Ctrl+S", false, false);
        ImGui::Separator();
        ImGui::MenuItem("Exit", "Alt+F4");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
        ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window"))
    {
        ImGui::MenuItem("Reset Layout", nullptr, &m_LayoutInitialized);
        ImGui::EndMenu();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 190.0f);
    ImGui::TextDisabled("Lulludens Engine Editor");
    ImGui::EndMainMenuBar();
}

void EditorUI::DrawSceneOutliner(GameEngine* engine)
{
    ImGui::Begin("Scene Outliner");
    ImGui::TextUnformatted("Main Scene");
    ImGui::SameLine();
    ImGui::Button("+", ImVec2(24.0f, 0.0f));
    ImGui::Separator();

    char filter[128] = {};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##scene-filter", "Search entities...", filter, IM_ARRAYSIZE(filter));
    ImGui::Separator();

    if (engine && ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (uint64_t index = 0; index < engine->GetEntityCount(); ++index)
        {
            Entity* entity = engine->GetEntityByIndex(index);
            if (!entity)
                continue;

            char label[64];
            std::snprintf(label, sizeof(label), "Entity %u", entity->GetID());
            if (filter[0] != '\0' && std::strstr(label, filter) == nullptr)
                continue;

            const bool selected = entity->GetID() == m_SelectedEntityId;
            if (ImGui::Selectable(label, selected))
                m_SelectedEntityId = entity->GetID();
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

void EditorUI::DrawViewportOverlay()
{
    ImGui::Begin("Scene Viewport", nullptr, ImGuiWindowFlags_NoBackground);
    ImGui::TextDisabled("Scene");
    ImGui::SameLine();
    ImGui::Button("Select");
    ImGui::SameLine();
    ImGui::Button("Move");
    ImGui::SameLine();
    ImGui::Button("Rotate");
    ImGui::SameLine();
    ImGui::Button("Scale");
    ImGui::SameLine();
    ImGui::TextDisabled("Lit");

    const ImVec2 toolbarBottomRight = ImGui::GetItemRectMax();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    m_ViewportInputLeft = windowPos.x;
    m_ViewportInputTop = toolbarBottomRight.y + ImGui::GetStyle().ItemSpacing.y;
    m_ViewportInputRight = windowPos.x + windowSize.x;
    m_ViewportInputBottom = windowPos.y + windowSize.y;

    const ImVec2 mousePosition = ImGui::GetMousePos();
    m_ViewportAcceptsInput = ImGui::IsWindowHovered() &&
        mousePosition.x >= m_ViewportInputLeft && mousePosition.x <= m_ViewportInputRight &&
        mousePosition.y >= m_ViewportInputTop && mousePosition.y <= m_ViewportInputBottom &&
        !ImGui::IsAnyItemActive();
    ImGui::End();
}

void EditorUI::DrawInspector(GameEngine* engine)
{
    ImGui::Begin("Inspector");
    Entity* entity = FindEntity(engine, m_SelectedEntityId);
    if (!entity)
    {
        ImGui::TextDisabled("Select an entity to inspect its components.");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity %u", entity->GetID());
    bool active = entity->IsActive();
    if (ImGui::Checkbox("Active", &active))
        entity->SetActive(active);
    ImGui::Separator();

    if (auto* transform = entity->GetComponent<TransformComponent>(); transform && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        float position[3] = { transform->position.x, transform->position.y, transform->position.z };
        if (ImGui::DragFloat3("Position", position, 0.05f))
            transform->SetPosition(position[0], position[1], position[2]);

        float rotation[3] = {
            DirectX::XMConvertToDegrees(transform->rotation.x),
            DirectX::XMConvertToDegrees(transform->rotation.y),
            DirectX::XMConvertToDegrees(transform->rotation.z)
        };
        if (ImGui::DragFloat3("Rotation", rotation, 0.5f))
            transform->SetRotationDegrees(rotation[0], rotation[1], rotation[2]);

        float scale[3] = { transform->scale.x, transform->scale.y, transform->scale.z };
        if (ImGui::DragFloat3("Scale", scale, 0.01f, 0.001f, 1000.0f))
            transform->SetScale(scale[0], scale[1], scale[2]);
    }

    if (auto* mesh = entity->GetComponent<MeshComponent>(); mesh && ImGui::CollapsingHeader("Mesh"))
    {
        ImGui::Text("Vertices: %zu", mesh->vertices.size());
        ImGui::Text("Indices: %zu", mesh->indices.size());
    }

    if (auto* material = entity->GetComponent<MaterialComponent>(); material && ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Albedo", &material->albedo.x);
        ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
        ImGui::Text("Albedo texture: %s", material->albedoTextureName.c_str());
    }

    if (auto* body = entity->GetComponent<RigidbodyComponent>(); body && ImGui::CollapsingHeader("Rigidbody"))
    {
        ImGui::DragFloat("Mass", &body->mass, 0.05f, 0.001f, 10000.0f);
        ImGui::Checkbox("Use Gravity", &body->useGravity);
        ImGui::Checkbox("Kinematic", &body->isKinematic);
    }

    if (ColliderComponent* collider = entity->GetCollider(); collider && ImGui::CollapsingHeader("Collider"))
    {
        ImGui::Checkbox("Trigger", &collider->isTrigger);
    }

    ImGui::Separator();
    ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f));
    ImGui::End();
}

void EditorUI::DrawContentDrawer()
{
    ImGui::Begin("Content / Instances");
    if (ImGui::BeginTabBar("ContentTabs"))
    {
        if (ImGui::BeginTabItem("Assets"))
        {
            ImGui::TextDisabled("Asset browser will be connected to ResourceManager.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Instances"))
        {
            ImGui::TextDisabled("Prefab and instance creation will appear here.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorUI::DrawStatusBar(GameEngine* engine)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 22.0f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 22.0f));
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##StatusBar", nullptr, flags);
    const float fps = m_SmoothedFrameTime > 0.0f ? 1.0f / m_SmoothedFrameTime : 0.0f;
    ImGui::TextDisabled("FPS: %.1f | Frame: %.2f ms", fps, m_SmoothedFrameTime * 1000.0f);
    ImGui::SameLine();
    ImGui::TextDisabled(" | ");
    ImGui::SameLine();
    ImGui::TextDisabled("Entities: %llu", engine ? static_cast<unsigned long long>(engine->GetEntityCount()) : 0ull);
    ImGui::SameLine();
    ImGui::TextDisabled(" | ");
    ImGui::SameLine();
    if (m_SelectedEntityId != UINT32_MAX)
        ImGui::Text("Selected: Entity %u", m_SelectedEntityId);
    else
        ImGui::TextDisabled("No selection");
    ImGui::End();
}
