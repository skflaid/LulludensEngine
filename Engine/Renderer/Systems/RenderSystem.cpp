// Renderer/Systems/RenderSystem.cpp
#include "RenderSystem.h"
#include "../Components/TransformComponent.h"
#include "../Components/MeshComponent.h"
#include "../Components/MaterialComponent.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

RenderSystem::RenderSystem(HWND hwnd, uint32_t width, uint32_t height)
    : m_Hwnd(hwnd), m_Width(width), m_Height(height), m_ConstantBufferSize(0) {
    for (int i = 0; i < FrameCount; ++i) {
        m_ConstantBufferDataBegin[i] = nullptr;
    }
}

RenderSystem::~RenderSystem() {
    Shutdown();
}

void RenderSystem::Initialize() {
    m_RendererCore = std::make_unique<RendererCore>();
    if (!m_RendererCore->Initialize(m_Hwnd, m_Width, m_Height)) {
        return;
    }

    // 카메라(뷰) 행렬 설정
    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -8.0f, 0.0f);  // 카메라 위치
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    // 바라보는 지점
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    // 상향 벡터
    XMStoreFloat4x4(&m_ViewMatrix, XMMatrixLookAtLH(eye, at, up));

    // 원근 투영 행렬 설정
    float fov = XM_PIDIV4; // 45도
    float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    XMStoreFloat4x4(&m_ProjMatrix, XMMatrixPerspectiveFovLH(fov, aspectRatio, 0.1f, 100.0f));

    CreateConstantBuffer();
    CreatePipelineState();
}

void RenderSystem::CreateConstantBuffer() {
    auto device = m_RendererCore->GetDevice();

    // 상수 버퍼는 256바이트 배수로 정렬되어야 함
    m_ConstantBufferSize = (sizeof(SceneConstants) + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = m_ConstantBufferSize * 100; // 100개 오브젝트까지 지원
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (int i = 0; i < FrameCount; ++i) {
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_ConstantBuffers[i])
        );

        D3D12_RANGE readRange = { 0, 0 }; // CPU에서 읽지 않음
        m_ConstantBuffers[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_ConstantBufferDataBegin[i]));
    }
}

void RenderSystem::CreatePipelineState() {
    auto device = m_RendererCore->GetDevice();

    // 루트 시그니처 생성
    D3D12_ROOT_PARAMETER rootParameters[2];

    // 상수 버퍼 뷰 (b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 두 번째 루트 파라미터는 사용 안 함 (색상을 상수 버퍼에 포함시킴)

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = 1; // 하나만 사용
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature));

    // 셰이더 코드
    const char* shaderCode = R"(
        cbuffer SceneConstants : register(b0) {
            float4x4 world;
            float4x4 view;
            float4x4 proj;
            float4 color;
        };

        struct VS_INPUT {
            float3 pos : POSITION;
            float3 normal : NORMAL;
            float2 texCoord : TEXCOORD;
        };

        struct PS_INPUT {
            float4 pos : SV_POSITION;
        };

        PS_INPUT VSMain(VS_INPUT input) {
            PS_INPUT output;
            float4 worldPos = mul(float4(input.pos, 1.0f), world);
            float4 viewPos = mul(worldPos, view);
            output.pos = mul(viewPos, proj);
            return output;
        }

        float4 PSMain(PS_INPUT input) : SV_TARGET {
            return color;
        }
    )";

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    D3DCompile(shaderCode, strlen(shaderCode), "BasicShader", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &error);
    D3DCompile(shaderCode, strlen(shaderCode), "BasicShader", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &error);

    // Input Layout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // PSO 생성
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_RootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState));
}

void RenderSystem::Update(float deltaTime) {
    // 렌더링 시스템은 Update에서 특별한 작업을 하지 않음
}

void RenderSystem::Shutdown() {
    for (int i = 0; i < FrameCount; ++i) {
        if (m_ConstantBuffers[i]) {
            m_ConstantBuffers[i]->Unmap(0, nullptr);
            m_ConstantBufferDataBegin[i] = nullptr;
        }
    }

    if (m_RendererCore) {
        m_RendererCore->Shutdown();
    }
}

void RenderSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<MeshComponent>() && entity->HasComponent<TransformComponent>()) {
        m_RenderableEntities.push_back(entity);

        // GPU에 메시 업로드
        auto* meshComp = entity->GetComponent<MeshComponent>();
        if (meshComp && meshComp->isLoaded && !meshComp->vertexBuffer) {
            auto device = m_RendererCore->GetDevice();

            // Vertex Buffer 생성
            D3D12_HEAP_PROPERTIES heapProps = {};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bufferDesc = {};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = sizeof(Vertex) * meshComp->vertices.size();
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&meshComp->vertexBuffer));

            UINT8* pVertexDataBegin;
            meshComp->vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin));
            memcpy(pVertexDataBegin, meshComp->vertices.data(), sizeof(Vertex) * meshComp->vertices.size());
            meshComp->vertexBuffer->Unmap(0, nullptr);

            meshComp->vertexBufferView.BufferLocation = meshComp->vertexBuffer->GetGPUVirtualAddress();
            meshComp->vertexBufferView.StrideInBytes = sizeof(Vertex);
            meshComp->vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * meshComp->vertices.size());

            // Index Buffer 생성
            bufferDesc.Width = sizeof(uint32_t) * meshComp->indices.size();
            device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&meshComp->indexBuffer));

            UINT8* pIndexDataBegin;
            meshComp->indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin));
            memcpy(pIndexDataBegin, meshComp->indices.data(), sizeof(uint32_t) * meshComp->indices.size());
            meshComp->indexBuffer->Unmap(0, nullptr);

            meshComp->indexBufferView.BufferLocation = meshComp->indexBuffer->GetGPUVirtualAddress();
            meshComp->indexBufferView.Format = DXGI_FORMAT_R32_UINT;
            meshComp->indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * meshComp->indices.size());
        }
    }
}

void RenderSystem::UnregisterEntity(Entity* entity) {
    m_RenderableEntities.erase(std::remove(m_RenderableEntities.begin(), m_RenderableEntities.end(), entity), m_RenderableEntities.end());
}

void RenderSystem::Render() {
    m_RendererCore->BeginFrame();

    auto commandList = m_RendererCore->GetCommandList();
    commandList->SetPipelineState(m_PipelineState.Get());
    commandList->SetGraphicsRootSignature(m_RootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int objectIndex = 0;
    for (Entity* entity : m_RenderableEntities) {
        if (entity && entity->IsActive()) {
            RenderEntity(entity);
            objectIndex++;
        }
    }

    m_RendererCore->EndFrame();
    m_RendererCore->Present();
}

void RenderSystem::RenderEntity(Entity* entity) {
    auto transform = entity->GetComponent<TransformComponent>();
    auto mesh = entity->GetComponent<MeshComponent>();
    auto material = entity->GetComponent<MaterialComponent>();

    if (!transform || !mesh || !material || !mesh->vertexBuffer) {
        return;
    }

    auto commandList = m_RendererCore->GetCommandList();
    UINT frameIndex = m_RendererCore->GetDevice() ? 0 : 0; // 간단하게 0번 프레임 사용

    // 상수 버퍼 데이터 준비
    SceneConstants constants;
    XMStoreFloat4x4(&constants.world, XMMatrixTranspose(transform->GetWorldMatrix()));
    XMStoreFloat4x4(&constants.view, XMMatrixTranspose(XMLoadFloat4x4(&m_ViewMatrix)));
    XMStoreFloat4x4(&constants.proj, XMMatrixTranspose(XMLoadFloat4x4(&m_ProjMatrix)));
    constants.color = material->albedo;

    // 상수 버퍼에 데이터 복사
    static int objIndex = 0;
    memcpy(m_ConstantBufferDataBegin[0] + (objIndex * m_ConstantBufferSize), &constants, sizeof(SceneConstants));

    // 상수 버퍼 바인딩
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = m_ConstantBuffers[0]->GetGPUVirtualAddress() + (objIndex * m_ConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(0, cbAddress);

    // 메시 렌더링
    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(mesh->indices.size()), 1, 0, 0, 0);

    objIndex = (objIndex + 1) % 100; // 100개까지 순환
}
