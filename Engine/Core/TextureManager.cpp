#include "TextureManager.h"
#include "../Common/DDSTextureLoader.h"
#include "../Common/d3dUtil.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#ifdef _DEBUG
#include <Windows.h>
#endif

std::unique_ptr<TextureManager> TextureManager::s_Instance = nullptr;

TextureManager::TextureManager() 
    : m_NextSRVIndex(0) {
}

TextureManager* TextureManager::Get() {
    if (!s_Instance) {
        s_Instance = std::unique_ptr<TextureManager>(new TextureManager());
    }
    return s_Instance.get();
}

void TextureManager::SetSRVHeap(ID3D12DescriptorHeap* srvHeap, UINT descriptorSize) {
    m_SRVHeap = srvHeap;
    m_SRVDescriptorSize = descriptorSize;
    m_NextSRVIndex = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE TextureManager::AllocateSRVHandle() {
    if (!m_SRVHeap || m_NextSRVIndex >= MAX_TEXTURES) {
        return {};
    }
    
    // GBuffer SRV 힙의 인덱스 8부터 시작 (0-7은 G-Buffer, SSGI, Shadow용)
    const UINT TEXTURE_START_INDEX = 8;
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_SRVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (TEXTURE_START_INDEX + m_NextSRVIndex) * m_SRVDescriptorSize;
    m_NextSRVIndex++;
    
    return handle;
}

bool TextureManager::LoadTexture(const std::string& name, const std::wstring& filePath, 
                                 ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    // 이미 로드된 텍스처인지 확인
    if (m_TextureCache.find(name) != m_TextureCache.end()) {
        return true; // 이미 로드됨
    }
    
    auto textureInfo = std::make_unique<TextureInfo>();
    
    // DDS 텍스처 로드
    HRESULT hr = DirectX::CreateDDSTextureFromFile12(
        device,
        cmdList,
        filePath.c_str(),
        textureInfo->Resource,
        textureInfo->UploadHeap
    );
    
    if (FAILED(hr)) {
        #ifdef _DEBUG
        char errorMsg[512];
        sprintf_s(errorMsg, "Failed to load texture: %s (HRESULT: 0x%08X)\n", name.c_str(), hr);
        OutputDebugStringA(errorMsg);
        #endif
        return false;
    }
    
    // SRV 생성
    textureInfo->SRVHandle = AllocateSRVHandle();
    if (textureInfo->SRVHandle.ptr == 0) {
        #ifdef _DEBUG
        char errorMsg[512];
        sprintf_s(errorMsg, "Failed to allocate SRV handle for texture: %s\n", name.c_str());
        OutputDebugStringA(errorMsg);
        #endif
        return false; // SRV 핸들 할당 실패
    }
    
    // SRV 인덱스 저장 (AllocateSRVHandle에서 이미 증가시켰으므로 -1)
    textureInfo->SRVIndex = m_NextSRVIndex - 1;
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureInfo->Resource->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = textureInfo->Resource->GetDesc().MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    
    // D3D12의 CreateShaderResourceView는 void를 반환하므로 에러 체크는 디바이스 초기화 상태에 의존
    device->CreateShaderResourceView(textureInfo->Resource.Get(), &srvDesc, textureInfo->SRVHandle);
    
    textureInfo->IsValid = true;
    
    // SRV 인덱스 저장 (캐시에 저장하기 전에)
    UINT srvIndex = textureInfo->SRVIndex;
    
    // 캐시에 저장
    m_TextureCache[name] = std::move(textureInfo);
    
    #ifdef _DEBUG
    char successMsg[512];
    sprintf_s(successMsg, "Successfully loaded texture: %s (SRVIndex: %u)\n", name.c_str(), srvIndex);
    OutputDebugStringA(successMsg);
    #endif
    
    return true;
}

bool TextureManager::LoadDefaultTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    // white1x1.dds 로드
    // 실행 파일 경로를 기준으로 상대 경로 계산
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    
    // 프로젝트 루트 찾기 (Textures 폴더가 있는 디렉토리)
    std::filesystem::path currentPath = exeDir;
    std::vector<std::wstring> possiblePaths;
    
    // 현재 디렉토리부터 상위로 올라가며 Textures 폴더 찾기
    for (int i = 0; i < 5; ++i) {
        std::filesystem::path texturesPath = currentPath / "Textures" / "white1x1.dds";
        if (std::filesystem::exists(texturesPath)) {
            possiblePaths.push_back(texturesPath.wstring());
            break;
        }
        currentPath = currentPath.parent_path();
        if (currentPath == currentPath.root_path()) {
            break; // 루트에 도달
        }
    }
    
    // 찾지 못한 경우 기본 경로들 시도
    if (possiblePaths.empty()) {
        possiblePaths = {
            L"Textures/white1x1.dds",           // 프로젝트 루트 기준
            L"../Textures/white1x1.dds",        // x64/Debug에서 실행하는 경우
            L"../../Textures/white1x1.dds",     // 다른 하위 폴더에서 실행하는 경우
        };
    }
    
    for (const auto& path : possiblePaths) {
        if (LoadTexture("white1x1", path, device, cmdList)) {
            #ifdef _DEBUG
            char successMsg[512];
            sprintf_s(successMsg, "Default texture loaded from: %ws\n", path.c_str());
            OutputDebugStringA(successMsg);
            #endif
            return true;
        }
    }
    
    #ifdef _DEBUG
    OutputDebugStringA("Failed to load default texture from all possible paths\n");
    #endif
    return false;
}

TextureInfo* TextureManager::GetTexture(const std::string& name) {
    auto it = m_TextureCache.find(name);
    if (it != m_TextureCache.end() && it->second->IsValid) {
        return it->second.get();
    }
    
    // 기본 텍스처 반환
    auto defaultIt = m_TextureCache.find("white1x1");
    if (defaultIt != m_TextureCache.end() && defaultIt->second->IsValid) {
        return defaultIt->second.get();
    }
    
    return nullptr;
}

