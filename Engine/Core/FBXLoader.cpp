#include "FBXLoader.h"
#include "Renderer/Model.h" // Model, Mesh, Vertex, Material 援ъ“泥?
//#include "Core/GameEngine.h"  // ?붿쭊???듭떖 湲곕뒫 ?묎렐??(媛?곸쓽 ?대옒??

// Assimp ?ㅻ뜑
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// DirectX ?ㅻ뜑
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

#include <filesystem>
#include <functional>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline void DebugPrint(const char* fmt, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringA(buffer);
}

static void MakeBindLocalInModel(Model& model)
{
    /*
    using namespace DirectX;
    auto& bones = model.Bones;
    size_t n = bones.size();
    if (!n) return;

    std::vector<XMFLOAT4X4> globalBind(n);

    // GlobalBind = inverse(Offset)
    for (size_t i = 0; i < n; ++i) {
        XMMATRIX off = XMLoadFloat4x4(&bones[i].Offset);    // Offset = inverse(bind)
        XMMATRIX g = XMMatrixInverse(nullptr, off);       // global bind
        XMStoreFloat4x4(&globalBind[i], g);
    }

    for (size_t i = 0; i < n; ++i) {
        int p = bones[i].ParentIndex;
        XMMATRIX g = XMLoadFloat4x4(&globalBind[i]);
        XMMATRIX loc;
        if (p >= 0) {
            XMMATRIX gp = XMLoadFloat4x4(&globalBind[p]);
            // ??row-vector: local = childGlobal * inverse(parentGlobal)
            loc = g * XMMatrixInverse(nullptr, gp);
        }
        else {
            loc = g; // 猷⑦듃: parent = I ??local = global
        }
        XMStoreFloat4x4(&bones[i].BindTransform, loc); // ?댁젣 '濡쒖뺄 諛붿씤??
    }
    */

}

namespace
{
    DirectX::XMFLOAT4X4 ToXMFLOAT4X4(const aiMatrix4x4& m)
    {
        return DirectX::XMFLOAT4X4(
            (float)m.a1, (float)m.b1, (float)m.c1, (float)m.d1,
            (float)m.a2, (float)m.b2, (float)m.c2, (float)m.d2,
            (float)m.a3, (float)m.b3, (float)m.c3, (float)m.d3,
            (float)m.a4, (float)m.b4, (float)m.c4, (float)m.d4
        );
    }


    int GetOrCreateBoneIndex(Model* model, const std::string& name, const aiBone* bone)
    {
        // ?뺢퇋?붾맂 ?대쫫?쇰줈 ?듭씪
        std::string normName = NormalizeBoneName(name);

        auto it = model->BoneNameToIndex.find(normName);
        if (it != model->BoneNameToIndex.end())
            return it->second;

        ModelBone newBone;
        newBone.Name = normName;      // ?붾쾭源낆슜?쇰줈???뺢퇋?붾맂 ?대쫫 ?ъ슜
        newBone.ParentIndex = -1;
        newBone.Offset = ToXMFLOAT4X4(bone->mOffsetMatrix);
        XMStoreFloat4x4(&newBone.BindTransform, XMMatrixIdentity());

        int index = (int)model->Bones.size();
        model->Bones.push_back(newBone);
        model->BoneNameToIndex[normName] = index;

        return index;
    }
}

Model* FBXLoader::Load(const std::string& filePath) {
    Assimp::Importer importer;

    // aiProcess_ConvertToLeftHanded: DirectX???쇱넀 醫뚰몴怨꾨? ?ъ슜?섎?濡?蹂??
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_ConvertToLeftHanded |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        // Log::Error("ASSIMP:: %s", importer.GetErrorString());
        return nullptr;
    }

    // ?붾젆?좊━ 寃쎈줈 異붿텧 (Windows? Unix 紐⑤몢 吏??
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        m_Directory = filePath.substr(0, lastSlash);
    }
    else {
        m_Directory = ".";
    }

    auto model = std::make_unique<Model>();

    ProcessMaterials(scene, model.get());
    ProcessNode(scene->mRootNode, scene, model.get());

    // 硫붿떆?ㅼ쓣 ?꾨? ?묒? ?ㅼ뿉 蹂?怨꾩링 + ?좊땲硫붿씠??泥섎━
    BuildSkeletonHierarchy(scene, model.get());

    std::vector<DirectX::XMMATRIX> globalBind(model->Bones.size());
    for (size_t i = 0; i < model->Bones.size(); ++i) {
        DirectX::XMMATRIX off = DirectX::XMLoadFloat4x4(&model->Bones[i].Offset);
        globalBind[i] = DirectX::XMMatrixInverse(nullptr, off);
    }
    // 媛?蹂몄쓽 濡쒖뺄 諛붿씤???됰젹 怨꾩궛 (?먯떇 湲濡쒕쾶 * 遺紐?湲濡쒕쾶 ??뻾??
    for (size_t i = 0; i < model->Bones.size(); ++i) {
        int parentIndex = model->Bones[i].ParentIndex;
        DirectX::XMMATRIX g = globalBind[i];
        DirectX::XMMATRIX localBind = (parentIndex >= 0)
            ? DirectX::XMMatrixMultiply(g, DirectX::XMMatrixInverse(nullptr, globalBind[parentIndex]))
            : g;
        DirectX::XMStoreFloat4x4(&model->Bones[i].BindTransform, localBind);
    }

    ProcessAnimations(scene, model.get());

    // ???⑥닔 ?섎굹媛 BindTransform(濡쒖뺄 諛붿씤????梨낆엫吏寃??붾떎
    MakeBindLocalInModel(*model);

    // ?붾쾭洹? 蹂?紐⑸줉 異쒕젰
    for (size_t i = 0; i < model->Bones.size(); ++i) {
        DebugPrint("[Bones] %zu : name='%s', parent=%d\n",
            i,
            model->Bones[i].Name.c_str(),
            model->Bones[i].ParentIndex);
    }

    /*for (auto& bone : model->Bones) {
        if (bone.ParentIndex < 0) {
            DirectX::XMMATRIX M = DirectX::XMLoadFloat4x4(&bone.BindTransform);
            DirectX::XMMATRIX rotY180 = DirectX::XMMatrixRotationY(DirectX::XM_PI);
            DirectX::XMStoreFloat4x4(&bone.BindTransform, DirectX::XMMatrixMultiply(M, rotY180));
        }
    }*/

    return model.release();
}

void FBXLoader::ProcessNode(aiNode* node, const aiScene* scene, Model* outModel) {
    // ?꾩옱 ?몃뱶???랁븳 紐⑤뱺 硫붿떆瑜?泥섎━
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        outModel->Meshes.push_back(std::unique_ptr<Mesh>(ProcessMesh(mesh, scene, outModel)));
    }
    // ?먯떇 ?몃뱶?ㅼ쓣 ?ш??곸쑝濡?諛⑸Ц
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, outModel);
    }
}

Mesh* FBXLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, Model* outModel) {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;

    // 1. ?뺤젏 ?곗씠??Vertex Data)瑜??쒗쉶?섎ŉ 異붿텧
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        ModelVertex v;
        v.Pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

        if (mesh->HasNormals()) {
            v.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
        }
        else {
            v.Normal = { 0.0f, 1.0f, 0.0f }; // 踰뺤꽑???놁쓣 寃쎌슦 ?꾩떆 媛?
        }

        // Assimp???щ윭 媛쒖쓽 ?띿뒪泥?醫뚰몴 梨꾨꼸??吏?? 蹂댄넻 泥?踰덉㎏(0) 梨꾨꼸???ъ슜.
        if (mesh->mTextureCoords[0]) {
            v.TexC = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
        }
        else {
            v.TexC = { 0.0f, 0.0f }; // ?띿뒪泥?醫뚰몴媛 ?놁쓣 寃쎌슦
        }
        vertices.push_back(v);
    }

    // --- 異붽?: ?ㅼ펷?덊깉 蹂??⑥씠??異붿텧 ---
    ExtractBoneWeights(mesh, outModel, vertices);

    // 2. ?몃뜳???곗씠??Index Data)瑜??쒗쉶?섎ŉ 異붿텧
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        // aiProcess_Triangulate ?뚮옒洹몃? ?ъ슜?덉쑝誘濡? ?대━怨ㅼ? ?쇨컖?뺤쑝濡?蹂?섎맖.
        // ?섏?留??쇱씤?대굹 ?ъ씤?멸? ?ы븿?????덉쑝誘濡?3媛쒖씤 寃쎌슦留?泥섎━.
        if (face.mNumIndices != 3) continue;

        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    // 3. 異붿텧???곗씠?곕줈 ?붿쭊??Mesh 媛앹껜 ?앹꽦
    Mesh* newMesh = new Mesh();
    newMesh->Name = mesh->mName.C_Str();
    newMesh->IndexCount = (UINT)indices.size();
    newMesh->MatIndex = mesh->mMaterialIndex;
    
    // CPU ?곗씠?????(Entity 蹂?????ъ슜)
    newMesh->Vertices = std::move(vertices);
    newMesh->Indices = std::move(indices);

    // TODO: ?꾨옒 遺遺꾩? ?붿쭊??D3D12 由ъ냼??愿由??쒖뒪?쒓낵 ?곕룞?댁빞 ?⑸땲??
    // -- 踰꾪띁 ?앹꽦 肄붾뱶 (D3D12 由ъ냼???앹꽦 ?⑥닔 ?ъ슜) --
    // ???⑥닔?ㅼ? CPU???곗씠?곕? GPU 硫붾え由щ줈 蹂듭궗?섎뒗 ??븷???⑸땲??
    // GPU 踰꾪띁??Entity濡?蹂?섑븷 ??RenderSystem?먯꽌 ?앹꽦?⑸땲??
    //
    // const UINT vbByteSize = (UINT)newMesh->Vertices.size() * sizeof(ModelVertex);
    // newMesh->VertexBuffer = GameEngine::GetRenderer()->CreateVertexBuffer(newMesh->Vertices.data(), vbByteSize);
    //
    // const UINT ibByteSize = (UINT)newMesh->Indices.size() * sizeof(uint32_t);
    // newMesh->IndexBuffer = GameEngine::GetRenderer()->CreateIndexBuffer(newMesh->Indices.data(), ibByteSize);
    //
    // newMesh->VertexBufferView = newMesh->VertexBuffer->GetVertexBufferView();
    // newMesh->IndexBufferView = newMesh->IndexBuffer->GetIndexBufferView();

    return newMesh;
}

void FBXLoader::ProcessMaterials(const aiScene* scene, Model* outModel) {
    outModel->Materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* mat = scene->mMaterials[i];
        auto material = std::make_unique<ModelMaterial>();

        material->Name = mat->GetName().C_Str();

        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            std::string fullTexPath = m_Directory + "/" + texPath.C_Str();
            material->DiffuseTextureName = std::filesystem::path(fullTexPath).stem().string();
        }

        if (mat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS ||
            mat->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS) {
            std::string fullTexPath = m_Directory + "/" + texPath.C_Str();
            material->NormalTextureName = std::filesystem::path(fullTexPath).stem().string();
        }

        outModel->Materials[i] = std::move(material);
    }
}

void FBXLoader::ExtractBoneWeights(aiMesh* mesh, Model* outModel, std::vector<ModelVertex>& vertices)
{
    if (!mesh->HasBones())
        return;

    for (auto& v : vertices) {
        for (int i = 0; i < 4; ++i) {
            v.BoneIndices[i] = 0;
            v.BoneWeights[i] = 0.0f;
        }
    }

    // aiMesh::mBones 瑜??뚮㈃??媛??뺤젏??蹂??몃뜳???⑥씠???좊떦
    for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
        aiBone* aiBonePtr = mesh->mBones[i];
        std::string boneName = aiBonePtr->mName.C_Str();

        int boneIndex = GetOrCreateBoneIndex(outModel, boneName, aiBonePtr);

        for (unsigned int j = 0; j < aiBonePtr->mNumWeights; ++j) {
            const aiVertexWeight& vw = aiBonePtr->mWeights[j];
            unsigned int vertexId = vw.mVertexId;
            float weight = vw.mWeight;

            if (vertexId >= vertices.size())
                continue;

            auto& v = vertices[vertexId];

            // 4媛??щ’ 以?鍮??먮━ ?먮뒗 媛???묒? ?⑥씠?몃? 援먯껜
            int slot = -1;
            float minWeight = weight;
            int minIndex = 0;

            for (int k = 0; k < 4; ++k) {
                if (v.BoneWeights[k] == 0.0f) {
                    slot = k;
                    break;
                }
                if (v.BoneWeights[k] < minWeight) {
                    minWeight = v.BoneWeights[k];
                    minIndex = k;
                }
            }

            if (slot == -1) {
                // ?대? 4媛?苑?李쇱쑝硫?媛???묒? ?⑥씠?몃? 援먯껜
                slot = minIndex;
            }

            v.BoneWeights[slot] = weight;
            v.BoneIndices[slot] = static_cast<uint32_t>(boneIndex);
        }
    }

    // ?뺢퇋??(珥앺빀??1???섎룄濡?
    for (auto& v : vertices) {
        float sum =
            v.BoneWeights[0] + v.BoneWeights[1] +
            v.BoneWeights[2] + v.BoneWeights[3];

        if (sum > 0.0f) {
            float inv = 1.0f / sum;
            for (int i = 0; i < 4; ++i) {
                v.BoneWeights[i] *= inv;
            }
        }
    }
}

void FBXLoader::BuildSkeletonHierarchy(const aiScene* /*scene*/, Model* outModel)
{
    if (!outModel || outModel->Bones.empty())
        return;

    // 1) ?쇰떒 ?꾨? 猷⑦듃(-1)濡?珥덇린??
    for (auto& b : outModel->Bones)
        b.ParentIndex = -1;

    auto findBone = [&](const std::string& name) -> int
        {
            auto it = outModel->BoneNameToIndex.find(name);
            if (it != outModel->BoneNameToIndex.end())
                return it->second;
            return -1;
        };

    for (size_t i = 0; i < outModel->Bones.size(); ++i)
    {
        auto& bone = outModel->Bones[i];
        const std::string& n = bone.Name;
        std::string parentName;

        // --- 猷⑦듃 / 紐명넻 ---
        if (n == "hips")
        {
            parentName.clear();  // 理쒖쥌 猷⑦듃
        }
        else if (n == "spine")
            parentName = "hips";
        else if (n == "spine1")
            parentName = "spine";
        else if (n == "spine2")
            parentName = "spine1";
        else if (n == "neck")
            parentName = "spine2";
        else if (n == "head")
            parentName = "neck";
        else if (n == "headtop_end")
            parentName = "head";

        // --- ?쇱そ ??---
        else if (n == "leftshoulder")
            parentName = "spine2";
        else if (n == "leftarm")
            parentName = "leftshoulder";
        else if (n == "leftforearm")
            parentName = "leftarm";
        else if (n == "lefthand")
            parentName = "leftforearm";

        // --- ?ㅻⅨ履???---
        else if (n == "rightshoulder")
            parentName = "spine2";
        else if (n == "rightarm")
            parentName = "rightshoulder";
        else if (n == "rightforearm")
            parentName = "rightarm";
        else if (n == "righthand")
            parentName = "rightforearm";

        // --- ?쇱そ ?ㅻ━ ---
        else if (n == "leftupleg")
            parentName = "hips";
        else if (n == "leftleg")
            parentName = "leftupleg";
        else if (n == "leftfoot")
            parentName = "leftleg";
        else if (n == "lefttoebase")
            parentName = "leftfoot";
        else if (n == "lefttoe_end")
            parentName = "lefttoebase";

        // --- ?ㅻⅨ履??ㅻ━ ---
        else if (n == "rightupleg")
            parentName = "hips";
        else if (n == "rightleg")
            parentName = "rightupleg";
        else if (n == "rightfoot")
            parentName = "rightleg";
        else if (n == "righttoebase")
            parentName = "rightfoot";
        else if (n == "righttoe_end")
            parentName = "righttoebase";

        // --- ?먭???(怨듯넻 洹쒖튃) ---
        else
        {
            // ?대쫫??xxx1, xxx2, xxx3, xxx4 ?⑦꽩??寃쎌슦
            if (!n.empty())
            {
                char last = n.back();
                if (last >= '2' && last <= '4')
                {
                    // xxx2 -> xxx1, xxx3 -> xxx2, xxx4 -> xxx3
                    std::string parentCandidate = n.substr(0, n.size() - 1);
                    parentCandidate.push_back(static_cast<char>(last - 1));
                    parentName = parentCandidate;
                }
                else if (last == '1')
                {
                    // *_1 ??遺紐⑤뒗 ?대떦 ??lefthand / righthand)
                    if (n.rfind("lefthand", 0) == 0)
                        parentName = "lefthand";
                    else if (n.rfind("righthand", 0) == 0)
                        parentName = "righthand";
                }
            }
        }

        if (!parentName.empty())
        {
            int p = findBone(parentName);
            if (p >= 0 && p != static_cast<int>(i))
            {
                bone.ParentIndex = p;
            }
        }
    }

    // (?붾쾭洹몄슜) ??踰???李띿뼱蹂닿퀬 hips / spine / ?ㅻ━ 怨꾩링???쒕?濡??섏삤?붿? ?뺤씤
}






void FBXLoader::ProcessAnimations(const aiScene* scene, Model* outModel)
{
    if (!scene || !scene->HasAnimations() || outModel->Bones.empty())
        return;

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        aiAnimation* anim = scene->mAnimations[a];
        ModelAnimationClip clip;

        clip.Name = anim->mName.C_Str();
        if (clip.Name.empty()) {
            clip.Name = "Anim" + std::to_string(a);
        }

        clip.Duration = anim->mDuration;
        clip.TicksPerSecond = (anim->mTicksPerSecond != 0.0)
            ? anim->mTicksPerSecond
            : 25.0; // 湲곕낯媛?

        clip.BoneAnimations.clear();
        clip.BoneAnimations.resize(outModel->Bones.size());

        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            aiNodeAnim* channel = anim->mChannels[c];
            std::string channelNameRaw = channel->mNodeName.C_Str();
            std::string channelName = NormalizeBoneName(channelNameRaw);

            int boneIndex = outModel->GetBoneIndex(channelName);
            if (boneIndex < 0) {
                // ?붾쾭源낆슜?쇰줈 蹂닿퀬 ?띠쑝硫??ш린??OutputDebugString ?⑤룄 ??
                DebugPrint("[Anim] channel '%s' (norm='%s') -> bone NOT FOUND\n",
                    channelNameRaw.c_str(), channelName.c_str());
                continue;
            }

            DebugPrint("[Anim] channel '%s' (norm='%s') -> boneIdx=%d, boneName='%s'\n",
                channelNameRaw.c_str(), channelName.c_str(),
                boneIndex, outModel->Bones[boneIndex].Name.c_str());

            ModelBoneAnimation& boneAnim = clip.BoneAnimations[boneIndex];

            // Position keys
            for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i) {
                const aiVectorKey& key = channel->mPositionKeys[i];
                ModelKeyframeVec3 kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT3(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z);
                boneAnim.Translations.push_back(kf);
            }

            // Rotation keys
            for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i) {
                const aiQuatKey& key = channel->mRotationKeys[i];
                ModelKeyframeQuat kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT4(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z,
                    (float)key.mValue.w);
                boneAnim.Rotations.push_back(kf);
            }

            // Scale keys
            for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i) {
                const aiVectorKey& key = channel->mScalingKeys[i];
                ModelKeyframeVec3 kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT3(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z);
                boneAnim.Scales.push_back(kf);
            }
        }

        outModel->Animations.push_back(std::move(clip));

        DebugPrint("=== Clip '%s' summary ===\n", clip.Name.c_str());
        for (size_t i = 0; i < clip.BoneAnimations.size(); ++i) {
            const auto& b = clip.BoneAnimations[i];
            if (!b.Translations.empty() || !b.Rotations.empty() || !b.Scales.empty()) {
                DebugPrint("[Anim] boneIdx=%zu name='%s' keys T:%zu R:%zu S:%zu\n",
                    i, outModel->Bones[i].Name.c_str(),
                    b.Translations.size(),
                    b.Rotations.size(),
                    b.Scales.size());
            }
        }
    }


}
