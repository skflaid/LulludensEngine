#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <memory>

using Microsoft::WRL::ComPtr;

struct TextureInfo {
    ComPtr<ID3D12Resource> Resource;
    ComPtr<ID3D12Resource> UploadHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE SRVHandle = {};
    UINT SRVIndex = 0; // SRV 힙 내 인덱스
    bool IsValid = false;
};

class TextureManager {
public:
    static TextureManager* Get();
    
    // 텍스처 로드 (경로와 이름을 받음)
    bool LoadTexture(const std::string& name, const std::wstring& filePath, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    
    // 이름으로 텍스처 가져오기
    TextureInfo* GetTexture(const std::string& name);
    
    // 기본 텍스처 로드 (white1x1.dds)
    bool LoadDefaultTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    
    // SRV 힙 설정 (텍스처 SRV를 저장할 힙)
    void SetSRVHeap(ID3D12DescriptorHeap* srvHeap, UINT descriptorSize);
    
    // SRV 핸들 할당 (다음 사용 가능한 SRV 핸들 반환)
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateSRVHandle();

private:
    TextureManager();
    static std::unique_ptr<TextureManager> s_Instance;
    
    std::unordered_map<std::string, std::unique_ptr<TextureInfo>> m_TextureCache;
    
    ID3D12DescriptorHeap* m_SRVHeap = nullptr;
    UINT m_SRVDescriptorSize = 0;
    UINT m_NextSRVIndex = 0;
    static const UINT MAX_TEXTURES = 256; // 최대 텍스처 개수
};

