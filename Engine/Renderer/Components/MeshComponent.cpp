// MeshComponent.cpp
#include "MeshComponent.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

void MeshComponent::LoadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open skull file: " + filepath);
    }

    std::string line;
    size_t vertexCount = 0, triCount = 0;

    // 1) VertexCount, TriangleCount 읽기
    std::getline(file, line);
    {
        std::istringstream iss(line);
        std::string label;
        iss >> label >> vertexCount;
    }
    std::getline(file, line);
    {
        std::istringstream iss(line);
        std::string label;
        iss >> label >> triCount;
    }

    // 2) VertexList 블록 건너뛰기 (헤더, 중괄호)
    std::getline(file, line); // VertexList(...)
    std::getline(file, line); // {

    vertices.resize(vertexCount);
    // 3) 버텍스 읽기
    for (size_t i = 0; i < vertexCount; ++i) {
        std::getline(file, line);
        std::istringstream iss(line);
        iss >> vertices[i].position.x
            >> vertices[i].position.y
            >> vertices[i].position.z
            >> vertices[i].normal.x
            >> vertices[i].normal.y
            >> vertices[i].normal.z;
        vertices[i].texCoord = { 0,0 }; // 필요시 텍스처 좌표 처리
    }

    // 4) 블록 닫고 IndexList 블록 진입
    std::getline(file, line); // }
    std::getline(file, line); // IndexList
    std::getline(file, line); // {

    indices.resize(triCount * 3);
    // 5) 인덱스 읽기
    for (size_t i = 0; i < triCount; ++i) {
        uint32_t i0, i1, i2;
        file >> i0 >> i1 >> i2;
        indices[i * 3 + 0] = i0;
        indices[i * 3 + 1] = i1;
        indices[i * 3 + 2] = i2;
    }

    file.close();
    isLoaded = true;
}
