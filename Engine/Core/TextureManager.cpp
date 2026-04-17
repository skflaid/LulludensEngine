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
    
    D3D12_RESOURCE_DESC resourceDesc = textureInfo->Resource->GetDesc();
    textureInfo->IsCubeMap =
        resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        resourceDesc.DepthOrArraySize == 6;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = resourceDesc.Format;

    if (textureInfo->IsCubeMap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = resourceDesc.MipLevels;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = resourceDesc.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }
    
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

bool TextureManager::LoadAllDDSFromDirectory(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    // 실행 파일 경로를 기준으로 상대 경로 계산
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    
    // 프로젝트 루트 찾기 (Textures 폴더가 있는 디렉토리)
    std::filesystem::path texturesDir;
    std::filesystem::path currentPath = exeDir;
    bool found = false;
    
    // 현재 디렉토리부터 상위로 올라가며 Textures 폴더 찾기
    for (int i = 0; i < 5; ++i) {
        std::filesystem::path testPath = currentPath / "Textures";
        if (std::filesystem::exists(testPath) && std::filesystem::is_directory(testPath)) {
            texturesDir = testPath;
            found = true;
            break;
        }
        currentPath = currentPath.parent_path();
        if (currentPath == currentPath.root_path()) {
            break; // 루트에 도달
        }
    }
    
    // 찾지 못한 경우 기본 경로들 시도
    if (!found) {
        std::vector<std::filesystem::path> possiblePaths = {
            std::filesystem::path("Textures"),
            std::filesystem::path("../Textures"),
            std::filesystem::path("../../Textures"),
        };
        
        for (const auto& path : possiblePaths) {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                texturesDir = path;
                found = true;
                break;
            }
        }
    }
    
    if (!found || !std::filesystem::exists(texturesDir)) {
        #ifdef _DEBUG
        OutputDebugStringA("Failed to find Textures directory\n");
        #endif
        return false;
    }
    
    // 디렉토리 내의 모든 .dds 파일 찾기
    int loadedCount = 0;
    int failedCount = 0;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(texturesDir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path filePath = entry.path();
                if (filePath.extension() == ".dds" || filePath.extension() == ".DDS") {
                    // 파일명을 텍스처 이름으로 사용 (확장자 제외)
                    std::string textureName = filePath.stem().string();
                    std::wstring wFilePath = filePath.wstring();
                    
                    if (LoadTexture(textureName, wFilePath, device, cmdList)) {
                        loadedCount++;
                        #ifdef _DEBUG
                        char successMsg[512];
                        sprintf_s(successMsg, "Loaded texture: %s\n", textureName.c_str());
                        OutputDebugStringA(successMsg);
                        #endif
                    } else {
                        failedCount++;
                        #ifdef _DEBUG
                        char errorMsg[512];
                        sprintf_s(errorMsg, "Failed to load texture: %s\n", textureName.c_str());
                        OutputDebugStringA(errorMsg);
                        #endif
                    }
                }
            }
        }
    }
    catch (const std::filesystem::filesystem_error& e) {
        #ifdef _DEBUG
        char errorMsg[512];
        sprintf_s(errorMsg, "Filesystem error while loading textures: %s\n", e.what());
        OutputDebugStringA(errorMsg);
        #endif
        return false;
    }
    
    #ifdef _DEBUG
    char summaryMsg[512];
    sprintf_s(summaryMsg, "Texture loading complete: %d loaded, %d failed\n", loadedCount, failedCount);
    OutputDebugStringA(summaryMsg);
    #endif
    
    return loadedCount > 0; // 최소 하나라도 로드되면 성공
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

