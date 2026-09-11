#include "MeshLoader.h"

#include "Core/DMemory.hpp"
#include "Core/EngineLogger.hpp"

#include "Platform/File/File.hpp"
#include "Systems/ResourceSystem.h"
#include "Systems/GeometrySystem.h"
#include "Math/GeometryUtils.hpp"

#include <vector>
#include <stdio.h>	//sscanf

// Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

MeshLoader::MeshLoader() {
	Type = EAssetType::StaticMesh;
	TypePath = "Models";
}

bool MeshLoader::Load(const FString& name, void* params, UAsset* resource) {
	if (name.Length() == 0 || resource == nullptr) {
		return false;
	}

	const char* FormatStr = "%s/%s/%s%s";

#define SUPPORTED_FILETYPE_COUNT 7  // 增加支持的文件类型数量
	FSupportedMeshFileType SupportedFileTypes[SUPPORTED_FILETYPE_COUNT];
	SupportedFileTypes[0] = FSupportedMeshFileType{ ".dsm", MeshFileType::eMesh_File_Type_DSM, true };
	SupportedFileTypes[1] = FSupportedMeshFileType{ ".obj", MeshFileType::eMesh_File_Type_3D_Model, false };
	SupportedFileTypes[2] = FSupportedMeshFileType{ ".gltf", MeshFileType::eMesh_File_Type_3D_Model, false };
	SupportedFileTypes[3] = FSupportedMeshFileType{ ".glb", MeshFileType::eMesh_File_Type_3D_Model, true };  // 二进制GLTF
	SupportedFileTypes[4] = FSupportedMeshFileType{ ".fbx", MeshFileType::eMesh_File_Type_3D_Model, true };  // FBX
	SupportedFileTypes[5] = FSupportedMeshFileType{ ".dae", MeshFileType::eMesh_File_Type_3D_Model, false }; // Collada
	SupportedFileTypes[6] = FSupportedMeshFileType{ ".3ds", MeshFileType::eMesh_File_Type_3D_Model, true };  // 3DS Max

	FString FullFilePath;
	MeshFileType MeshFileType = MeshFileType::eMesh_File_Type_Not_Found;
	// 尝试每种支持的扩展名
	for (uint32_t i = 0; i < SUPPORTED_FILETYPE_COUNT; ++i) {
		FullFilePath = FString::Format(FormatStr, ResourceSystem::Get().GetRootPath(), TypePath.CStr(), name.CStr(), SupportedFileTypes[i].extension.c_str());
		File TestExist(FullFilePath.CStr());
		// 如果文件存在
		if (TestExist.IsExist()) {
			MeshFileType = SupportedFileTypes[i].type;
			break;
		}
	}

	if (MeshFileType == MeshFileType::eMesh_File_Type_Not_Found) {
		GLOG(Log::eError, "Unable to find mesh of supported type called '%s'.", name.CStr());
		return false;
	}

	resource->FullPath = FullFilePath;
	resource->Name = name;

	// 资源数据是几何体配置的数组
	std::vector<FGeometryConfig> ResourceDatas;
	ResourceDatas.reserve(25968);
	bool Result = false;

	switch (MeshFileType) {
	case MeshFileType::eMesh_File_Type_3D_Model:
	{
		// 生成DSM文件名
		FString DsmFileName;
		DsmFileName = FString::Format("%s/%s/%s%s", ResourceSystem::Get().GetRootPath(), TypePath.CStr(), name.CStr(), ".dsm");

		// 使用统一的Assimp加载器处理所有3D模型格式
		Result = Import3DModelFile(FullFilePath, DsmFileName, ResourceDatas);

		GLOG(Log::eInfo, "Loaded 3D model '%s' with %zu geometries using Assimp",
			FullFilePath.CStr(), ResourceDatas.size());
	}break;

	case MeshFileType::eMesh_File_Type_DSM:
		Result = LoadDsmFile(FullFilePath, ResourceDatas);
		GLOG(Log::eDebug, "Loaded DSM file '%s' with %zu geometries", FullFilePath.CStr(), ResourceDatas.size());
		break;

	case MeshFileType::eMesh_File_Type_Not_Found:
		GLOG(Log::eError, "Unable to find mesh of supported type called '%s'.", name.CStr());
		Result = false;
		break;
	}

	if (!Result) {
		GLOG(Log::eError, "Failed to process mesh file '%s'.", FullFilePath.CStr());
		ResourceDatas.clear();
		resource->Data = nullptr;
		resource->DataSize = 0;
		return false;
	}

	if (ResourceDatas.empty()) {
		GLOG(Log::eWarn, "No geometries found in mesh file '%s'.", FullFilePath.CStr());
		resource->Data = nullptr;
		resource->DataSize = 0;
		resource->DataCount = 0;
		return true;  // 不算错误，只是空文件
	}

	// 分配内存并复制数据
	resource->Data = Memory::Allocate(sizeof(FGeometryConfig) * ResourceDatas.size(), MemoryType::eMemory_Type_Array);
	for (size_t i = 0; i < ResourceDatas.size(); ++i) {
		new (static_cast<FGeometryConfig*>(resource->Data) + i) FGeometryConfig(ResourceDatas[i]); // 使用移动构造
	}
	resource->DataSize = sizeof(FGeometryConfig);
	resource->DataCount = ResourceDatas.size();

	std::vector<FGeometryConfig>().swap(ResourceDatas);

	GLOG(Log::eInfo, "Successfully loaded mesh resource '%s' with %u geometries", name.CStr(), resource->DataCount);
	return true;
}

void MeshLoader::Unload(UAsset* resource) {
	for (uint32_t i = 0; i < resource->DataCount; ++i) {
		FGeometryConfig* Config = &((FGeometryConfig*)resource->Data)[i];
		GeometrySystem::Get().ConfigDispose(Config);
		//static_cast<SGeometryConfig*>(resource->Data)[i].~SGeometryConfig();
	}

	if (resource->Data) {
		Memory::Free(resource->Data, MemoryType::eMemory_Type_Array);
		resource->Data = nullptr;
		resource->DataSize = 0;
		resource->DataCount = 0;
		resource->LoaderID = INVALID_ID;
	}

	resource = nullptr;
}

bool MeshLoader::Import3DModelFile(const FString& model_file, const FString& out_dsm_filename, std::vector<FGeometryConfig>& out_geometries) {
	Assimp::Importer importer;

	// 设置后处理标志 - 适用于所有3D格式
	unsigned int postProcessFlags =
		aiProcess_Triangulate |           // 三角化所有面
		aiProcess_GenNormals |           // 为没有法线的顶点生成法线
		aiProcess_CalcTangentSpace |     // 计算切线和副切线
		aiProcess_JoinIdenticalVertices | // 合并相同的顶点
		aiProcess_ValidateDataStructure | // 验证数据结构
		aiProcess_ImproveCacheLocality |  // 优化顶点缓存局部性
		aiProcess_RemoveRedundantMaterials | // 移除冗余材质
		aiProcess_SortByPType |          // 按图元类型排序
		aiProcess_FindDegenerates |      // 查找退化的多边形
		aiProcess_FindInvalidData |      // 查找无效数据
		aiProcess_GenUVCoords |          // 为缺少UV坐标的顶点生成UV
		aiProcess_TransformUVCoords |    // 处理UV变换
		aiProcess_OptimizeMeshes |       // 优化网格数量
		aiProcess_OptimizeGraph;         // 优化场景图

	// 加载3D模型文件
	const aiScene* scene = importer.ReadFile(model_file.CStr(), postProcessFlags);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		GLOG(Log::eError, "Failed to load 3D model with Assimp: %s", importer.GetErrorString());
		return false;
	}

	GLOG(Log::eInfo, "Successfully loaded 3D model: %s", model_file.CStr());
	GLOG(Log::eDebug, "Model contains %u meshes, %u materials", scene->mNumMeshes, scene->mNumMaterials);

	// 材质配置
	std::vector<FMaterialConfig> MaterialConfigs;

	// 处理材质
	if (!ProcessAssimpMaterials(scene, out_dsm_filename, MaterialConfigs)) {
		GLOG(Log::eWarn, "Failed to process some materials");
	}

	// 处理场景节点和网格
	ProcessAssimpNode(scene->mRootNode, scene, MaterialConfigs, Matrix4::Identity(), out_geometries);

	MaterialConfigs.clear();

	// 去重几何体
	DeduplicateGeometry(out_geometries);

	// 顶点合并/共享之后再生成切线：
	// 此时相邻面共用的顶点已被 DeduplicateVertices 合并为同一顶点，切线可跨面累积，
	// 避免逐面独立切线导致法线贴图出现面状接缝。
	for (FGeometryConfig& geo : out_geometries) {
		if (geo.vertices == nullptr || geo.indices == nullptr) continue;
		if (geo.vertex_count == 0 || geo.index_count == 0) continue;

		GeometryUtils::GenerateTangents(
			geo.vertex_count,
			(Vertex*)geo.vertices,
			geo.index_count,
			(uint32_t*)geo.indices);
	}

	// 输出DSM文件
	return WriteDsmFile(out_dsm_filename, model_file, out_geometries);
}

bool MeshLoader::ProcessAssimpMaterials(const aiScene* scene, const FString& out_dsm_filename, std::vector<FMaterialConfig>& materialConfigs) {
	for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
		const aiMaterial* mat = scene->mMaterials[i];
		FMaterialConfig config;

		// 材质名称
		aiString name;
		if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS && strlen(name.C_Str()) > 0) {
			config.name = name.C_Str();
		}
		else {
			FString file = FString::FilenameNoExtensionFromPath(out_dsm_filename);
			config.name = file + "_auto_material_" + FString::FromInt(i);
		}

		// 基础颜色/漫反射颜色
		aiColor4D baseColor;
		if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
			config.diffuse_color = Vector4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
		}
		else if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
			config.diffuse_color = Vector4(baseColor.r, baseColor.g, baseColor.b, 1.0f);
		}
		else {
			config.diffuse_color = Vector4(0.8f, 0.8f, 0.8f, 1.0f); // 默认颜色
		}

		// PBR属性 - 金属度
		float metallic = 0.0f;
		if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
			config.Metallic = metallic;
		}

		// PBR属性 - 粗糙度
		float roughness = 0.5f;
		if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
			config.Roughness = roughness;
		}

		// 自发光颜色
		aiColor3D emissive;
		if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
			config.EmissiveColor = Vector4(emissive.r, emissive.g, emissive.b, 1.0f);
		}

		// 反光度 (用于传统的Blinn-Phong着色)
		float shininess = 32.0f;
		if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
			config.shininess = shininess;
		}
		if (config.shininess == 0.0f) {
			config.shininess = 8.0f; // 避免除零错误
		}

		// 透明度/不透明度
		float opacity = 1.0f;
		if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
			config.diffuse_color.a = opacity;
		}

		// 环境光颜色（如果需要的话）
		aiColor3D ambient;
		if (mat->Get(AI_MATKEY_COLOR_AMBIENT, ambient) == AI_SUCCESS) {
			// 可以选择如何处理环境光，比如存储到额外的字段或者影响漫反射颜色
			// 这里我们暂时不处理，因为现代渲染通常在着色器中处理环境光
		}

		// 折射率（如果需要的话）
		float refractiveIndex = 1.0f;
		if (mat->Get(AI_MATKEY_REFRACTI, refractiveIndex) == AI_SUCCESS) {
			// 可以添加到SMaterialConfig中，如果需要支持折射
		}

		// 处理贴图
		ProcessAssimpTextures(mat, config);

		config.shader_name = DEFAULT_SHADER;

		// 环境光遮蔽默认值（通常通过贴图处理，而不是材质属性）
		config.AmbientOcclusion = 1.0f;

		materialConfigs.push_back(config);

		// 写入材质文件
		if (!WriteDmtFile(out_dsm_filename, &config)) {
			GLOG(Log::eWarn, "Failed to write material file for: %s", config.name.CStr());
		}
	}

	// 如果没有材质，创建一个默认材质
	if (materialConfigs.empty()) {
		FMaterialConfig defaultConfig;
		defaultConfig.name = "DefaultMaterial";
		defaultConfig.diffuse_color = Vector4(0.8f, 0.8f, 0.8f, 1.0f);
		defaultConfig.shininess = 32.0f;
		defaultConfig.Metallic = 0.0f;
		defaultConfig.Roughness = 0.5f;
		defaultConfig.AmbientOcclusion = 1.0f;
		defaultConfig.EmissiveColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
		defaultConfig.shader_name = DEFAULT_SHADER;
		materialConfigs.push_back(defaultConfig);
	}

	return true;
}

void MeshLoader::ProcessAssimpTextures(const aiMaterial* mat, FMaterialConfig& config) {
	aiString texPath;

	// 漫反射/基础颜色贴图
	if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
		config.diffuse_map_name = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}
	else if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
		config.diffuse_map_name = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}

	// 法线贴图
	if (mat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
		config.normal_map_name = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}
	else if (mat->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS) {
		config.normal_map_name = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}

	// 镜面反射贴图
	if (mat->GetTexture(aiTextureType_SPECULAR, 0, &texPath) == AI_SUCCESS) {
		config.specular_map_name = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}

	// 金属度贴图
	if (mat->GetTexture(aiTextureType_METALNESS, 0, &texPath) == AI_SUCCESS) {
		FString TexName = FString::FilenameNoExtensionFromPath(texPath.C_Str());
		config.MetallicRoughnessTexName = TexName;
	}

	// 粗糙度贴图
	if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath) == AI_SUCCESS) {
		FString TexName = FString::FilenameNoExtensionFromPath(texPath.C_Str());
		if (config.MetallicRoughnessTexName.IsEmpty()) {
			config.MetallicRoughnessTexName = TexName;
		}
	}

	// 自发光贴图
	if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &texPath) == AI_SUCCESS) {
		FString TexName = FString::FilenameNoExtensionFromPath(texPath.C_Str());
		config.EmissiveFactorTexName = TexName;
	}

	// 环境光遮蔽贴图 - 使用正确的aiTextureType
	if (mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texPath) == AI_SUCCESS) {
		FString texName = FString::FilenameNoExtensionFromPath(texPath.C_Str());
	}

	// 不透明度贴图
	if (mat->GetTexture(aiTextureType_OPACITY, 0, &texPath) == AI_SUCCESS) {
		// 可以处理透明度贴图
		FString texName = FString::FilenameNoExtensionFromPath(texPath.C_Str());
		// config.OpacityTexName = std::string(texName); // 如果需要的话
	}
}

void MeshLoader::ProcessAssimpNode(aiNode* node, const aiScene* scene, const std::vector<FMaterialConfig>& materialConfigs, const Matrix4& parentTransform, std::vector<FGeometryConfig>& out_geometries) {
	// 计算当前节点的变换矩阵
	aiMatrix4x4 aiTrans = node->mTransformation;

	// 转换Assimp矩阵到引擎矩阵格式
	// Assimp使用行主序，需要转换为列主序（如果你的引擎使用列主序）
	Matrix4 nodeTransform = Matrix4(
		aiTrans.a1, aiTrans.b1, aiTrans.c1, aiTrans.d1,
		aiTrans.a2, aiTrans.b2, aiTrans.c2, aiTrans.d2,
		aiTrans.a3, aiTrans.b3, aiTrans.c3, aiTrans.d3,
		aiTrans.a4, aiTrans.b4, aiTrans.c4, aiTrans.d4
	);

	Matrix4 worldTransform = parentTransform.Multiply(nodeTransform);

	// 处理当前节点的网格
	for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		ProcessAssimpMesh(mesh, scene, materialConfigs, worldTransform, out_geometries);
	}

	// 递归处理子节点
	for (unsigned int i = 0; i < node->mNumChildren; ++i) {
		ProcessAssimpNode(node->mChildren[i], scene, materialConfigs, worldTransform, out_geometries);
	}
}

void MeshLoader::ProcessAssimpMesh(aiMesh* mesh, const aiScene* scene, const std::vector<FMaterialConfig>& materialConfigs, const Matrix4& transform, std::vector<FGeometryConfig>& out_geometries) {
	if (!mesh->HasFaces()) {
		GLOG(Log::eWarn, "Mesh has no faces, skipping: %s", mesh->mName.C_Str());
		return;
	}

	if (mesh->mNumVertices == 0) {
		GLOG(Log::eWarn, "Mesh has no vertices, skipping: %s", mesh->mName.C_Str());
		return;
	}

	if (mesh->mNumFaces == 0) {
		GLOG(Log::eWarn, "Mesh has no faces, skipping: %s", mesh->mName.C_Str());
		return;
	}

	std::vector<Vector3> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2f> texcoords;

	positions.reserve(mesh->mNumVertices);
	normals.reserve(mesh->mNumVertices);
	texcoords.reserve(mesh->mNumVertices);

	// 计算法线变换矩阵（逆转置）
	Matrix4 normalMatrix = transform.Inverse().Transpose();

	// 处理顶点
	for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
		// 位置
		Vector3 pos(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
		pos = transform * pos;  // 应用变换
		positions.push_back(pos);

		// 法线
		if (mesh->mNormals) {
			Vector3 normal(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
			normal = normalMatrix * normal;  // 用法线变换矩阵
			normals.push_back(normal.Normalized());
		}
		else {
			normals.push_back(Vector3(0, 0, 1));
		}

		// 纹理坐标（使用第一套UV）
		if (mesh->mTextureCoords[0]) {
			Vector2f texcoord(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
			texcoords.push_back(texcoord);
		}
		else {
			texcoords.push_back(Vector2f(0, 0));
		}
	}

	// 转换为MeshFaceData格式以复用现有逻辑
	std::vector<FMeshFaceData> faces;
	faces.reserve(mesh->mNumFaces);

	for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
		aiFace face = mesh->mFaces[i];

		// 确保是三角形
		if (face.mNumIndices != 3) {
			GLOG(Log::eWarn, "Non-triangular face found, skipping");
			continue;
		}

		// 检查索引是否有效
		bool validIndices = true;
		for (unsigned int j = 0; j < 3; ++j) {
			if (face.mIndices[j] >= mesh->mNumVertices) {
				GLOG(Log::eWarn, "Invalid vertex index %u in mesh '%s' (max: %u), skipping face",
					face.mIndices[j], mesh->mName.C_Str(), mesh->mNumVertices - 1);
				validIndices = false;
				break;
			}
		}

		if (!validIndices) {
			continue;
		}

		FMeshFaceData meshFace;
		for (unsigned int j = 0; j < 3; ++j) {
			uint32_t idx = face.mIndices[j];
			meshFace.vertices[j].position_index = idx;
			meshFace.vertices[j].normal_index = idx;
			meshFace.vertices[j].texcoord_index = idx;
		}
		faces.push_back(meshFace);
	}

	// 检查是否有有效的面数据
	if (faces.empty()) {
		GLOG(Log::eWarn, "No valid triangular faces found in mesh: %s", mesh->mName.C_Str());
		return;
	}

	// 检查是否有有效的顶点数据
	if (positions.empty()) {
		GLOG(Log::eWarn, "No valid vertex positions found in mesh: %s", mesh->mName.C_Str());
		return;
	}

	// 创建几何体配置
	FGeometryConfig newData;

	// 设置名称
	if (strlen(mesh->mName.C_Str()) > 0) {
		newData.name = mesh->mName.C_Str();
	}
	else {
		newData.name = "UnnamedMesh_" + FString::FromInt64(out_geometries.size());
	}

	// 设置材质
	if (mesh->mMaterialIndex < materialConfigs.size()) {
		newData.material_name = materialConfigs[mesh->mMaterialIndex].name;
	}
	else {
		newData.material_name = DEFAULT_MATERIAL_NAME;
	}

	// 复用现有的处理逻辑
	ProcessSubobject(positions, normals, texcoords, faces, &newData);
	out_geometries.push_back(newData);

	GLOG(Log::eDebug, "Processed mesh: %s with %u vertices, %u faces",
		newData.name.CStr(), newData.vertex_count, (uint32_t)faces.size());
}

void MeshLoader::ProcessSubobject(std::vector<Vector3>& positions, std::vector<Vector3>& normals, std::vector<Vector2f>& texcoords, std::vector<FMeshFaceData>& faces, FGeometryConfig* out_data) {
	std::vector<uint32_t> Indices;
	std::vector<Vertex> Vertices;
	Indices.reserve(65535);
	Vertices.reserve(65535);
	
	bool ExtentSet = false;
	Memory::Zero(&out_data->min_extents, sizeof(Vector3));
	Memory::Zero(&out_data->max_extents, sizeof(Vector3));
	
	size_t FaceCount = faces.size();
	size_t NormalCount = normals.size();
	size_t TexcoordCount = texcoords.size();

	bool SkipNormal = false;
	bool SkipTexcoord = false;

	if (NormalCount == 0) {
		GLOG(Log::eWarn, "No normals are present in this model. Generating normals...");
		SkipNormal = true;
	}

	if (TexcoordCount == 0) {
		GLOG(Log::eWarn, "No tex-coord are present in this model.");
		SkipTexcoord = true;
	}

	for (size_t f = 0; f < FaceCount; f++) {
		// 确保法线存在
		Vector3 DefaultNormal = Vector3(0, 0, 1);
		if (SkipNormal) {
			FMeshVertexIndexData IndexData1 = faces[f].vertices[0];
			FMeshVertexIndexData IndexData2 = faces[f].vertices[1];
			FMeshVertexIndexData IndexData3 = faces[f].vertices[2];

			Vector3 Pos1 = positions[IndexData1.position_index];
			Vector3 Pos2 = positions[IndexData2.position_index];
			Vector3 Pos3 = positions[IndexData3.position_index];

			Vector3 Edge1 = Pos2 - Pos1;
			Vector3 Edge2 = Pos3 - Pos2;

			DefaultNormal = (Edge1.Cross(Edge2)).Normalized();
		}

		// Each vertex
		for (size_t i = 0; i < 3; ++i) {
			FMeshVertexIndexData IndexData = faces[f].vertices[i];
			Indices.push_back((uint32_t)(i + (f * 3)));

			Vertex Vert;
			Vector3 Pos = positions[IndexData.position_index];
			Vert.position = Pos;

			// Check extents - min
			if (Pos.x < out_data->min_extents.x || !ExtentSet) {
				out_data->min_extents.x = Pos.x;
			}
			if (Pos.y < out_data->min_extents.y || !ExtentSet) {
				out_data->min_extents.y = Pos.y;
			}
			if (Pos.z < out_data->min_extents.z || !ExtentSet) {
				out_data->min_extents.z = Pos.z;
			}

			// Check extents - max
			if (Pos.x > out_data->max_extents.x || !ExtentSet) {
				out_data->max_extents.x = Pos.x;
			}
			if (Pos.y > out_data->max_extents.y || !ExtentSet) {
				out_data->max_extents.y = Pos.y;
			}
			if (Pos.z > out_data->max_extents.z || !ExtentSet) {
				out_data->max_extents.z = Pos.z;
			}

			ExtentSet = true;

			if (SkipNormal) {
				Vert.normal = DefaultNormal;
			}
			else {
				Vert.normal = normals[IndexData.normal_index];
			}

			if (SkipTexcoord) {
				Vert.texcoord = Vector2f(0, 0);
			}
			else {
				Vert.texcoord = texcoords[IndexData.texcoord_index];
			}

			// TODO: Color
			Vert.color = Vector4(1, 1, 1, 1);
			Vertices.push_back(Vert);
		}
	}

	// Calculate the center based on the extents.
	for (unsigned short i = 0; i < 3; ++i) {
		out_data->center.elements[i] = (out_data->min_extents.elements[i] + out_data->max_extents.elements[i]) / 2.0f;
	}

	out_data->vertex_count = (uint32_t)Vertices.size();
	out_data->vertex_size = sizeof(Vertex);
	out_data->vertices = Memory::Allocate(out_data->vertex_count * out_data->vertex_size, MemoryType::eMemory_Type_Array);
	Memory::Copy(out_data->vertices, Vertices.data(), out_data->vertex_count * out_data->vertex_size);

	out_data->index_count = (uint32_t)Indices.size();
	out_data->index_size = sizeof(uint32_t);
	out_data->indices = Memory::Allocate(out_data->index_count * out_data->index_size, MemoryType::eMemory_Type_Array);
	Memory::Copy(out_data->indices, Indices.data(), out_data->index_count * out_data->index_size);

	std::vector<uint32_t>().swap(Indices);
	std::vector<Vertex>().swap(Vertices);
}	
bool MeshLoader::WriteDmtFile(const FString& mtl_file_path, FMaterialConfig* config) {
	// 从 mtl 文件路径提取目录，拼接目标路径
	FString Directory = FString::DirectoryFromPath(mtl_file_path);
	FString FullFilePath = FString::Format("%s../Materials/%s%s", Directory.CStr(), config->name.CStr(), ".dmt");

	File f(FullFilePath.CStr());
	if (!f.Open(eFileMode::Write)) {
		GLOG(Log::eError, "Error opening material file for writing: '%s'.", FullFilePath.CStr());
		return false;
	}

	GLOG(Log::eDebug, "Writing .dmt file '%s'.", FullFilePath.CStr());

	FString LineBuf;

	f.WriteLine("#material file");
	f.WriteLine("");
	f.WriteLine("version=0.1");

	LineBuf = FString::Format("name=%s", config->name.CStr());
	f.WriteLine(LineBuf);

	// Base
	LineBuf = FString::Format("light_intensity=%.6f %.6f %.6f %.6f",
		config->light_intensity.r, config->light_intensity.g,
		config->light_intensity.b, config->light_intensity.a);
	f.WriteLine(LineBuf);

	// BlinnPhong
	LineBuf = FString::Format("diffuse_color=%.6f %.6f %.6f %.6f",
		config->diffuse_color.r, config->diffuse_color.g,
		config->diffuse_color.b, config->diffuse_color.a);
	f.WriteLine(LineBuf);

	LineBuf = FString::Format("shininess=%.6f", config->shininess);
	f.WriteLine(LineBuf);

	// PBR
	LineBuf = FString::Format("metallic=%.6f", config->Metallic);
	f.WriteLine(LineBuf);

	LineBuf = FString::Format("roughness=%.6f", config->Roughness);
	f.WriteLine(LineBuf);

	LineBuf = FString::Format("ambient_occlusion=%.6f", config->AmbientOcclusion);
	f.WriteLine(LineBuf);

	// Textures
	if (!config->diffuse_map_name.IsEmpty()) {
		LineBuf = FString::Format("diffuse_texture=%s", config->diffuse_map_name.CStr());
		f.WriteLine(LineBuf);
	}
	if (!config->specular_map_name.IsEmpty()) {
		LineBuf = FString::Format("specular_texture=%s", config->specular_map_name.CStr());
		f.WriteLine(LineBuf);
	}
	if (!config->normal_map_name.IsEmpty()) {
		LineBuf = FString::Format("normal_texture=%s", config->normal_map_name.CStr());
		f.WriteLine(LineBuf);
	}
	if (!config->MetallicRoughnessTexName.IsEmpty()) {
		LineBuf = FString::Format("roughness_metallic_texture=%s", config->MetallicRoughnessTexName.CStr());
		f.WriteLine(LineBuf);
	}

	LineBuf = FString::Format("shader=%s", config->shader_name.CStr());
	f.WriteLine(LineBuf);

	// File::~File 会自动 Close，这里显式调用让意图更清晰
	f.Close();
	return true;
}

bool MeshLoader::LoadDsmFile(const FString& path, std::vector<FGeometryConfig>& out_geometries) {
	File f(path.CStr());
	if (!f.IsExist()) {
		GLOG(Log::eError, "DSM file '%s' does not exist.", path.CStr());
		return false;
	}

	if (!f.Open(eFileMode::Read, true)) {  // true = 二进制模式
		GLOG(Log::eError, "Failed to open DSM file '%s'.", path.CStr());
		return false;
	}

	// Version
	unsigned short Version = 0;
	if (!f.Read(&Version)) return false;

	// Name
	uint32_t NameLength = 0;
	if (!f.Read(&NameLength)) return false;
	char name[256] = {};
	if (!f.ReadBuffer(name, sizeof(char) * NameLength)) return false;

	// Geometry count
	uint32_t GeometryCount = 0;
	if (!f.Read(&GeometryCount)) return false;

	for (uint32_t i = 0; i < GeometryCount; ++i) {
		FGeometryConfig g;

		// Vertices (size / count / array)
		if (!f.Read(&g.vertex_size))  return false;
		if (!f.Read(&g.vertex_count)) return false;
		g.vertices = Memory::Allocate(g.vertex_count * g.vertex_size, MemoryType::eMemory_Type_Array);
		if (!f.ReadBuffer(g.vertices, g.vertex_count * g.vertex_size)) return false;

		// Indices (size / count / array)
		if (!f.Read(&g.index_size))  return false;
		if (!f.Read(&g.index_count)) return false;
		g.indices = Memory::Allocate(g.index_count * g.index_size, MemoryType::eMemory_Type_Array);
		if (!f.ReadBuffer(g.indices, g.index_count * g.index_size)) return false;

		// Geometry name
		uint32_t GNameLength = 0;
		if (!f.Read(&GNameLength)) return false;
		char* gn = (char*)Memory::Allocate(sizeof(char) * GNameLength, MemoryType::eMemory_Type_String);
		if (!f.ReadBuffer(gn, sizeof(char) * GNameLength)) {
			Memory::Free(gn, MemoryType::eMemory_Type_String);
			return false;
		}
		g.name = gn;
		Memory::Free(gn, MemoryType::eMemory_Type_String);

		// Material name
		uint32_t MNameLength = 0;
		if (!f.Read(&MNameLength)) return false;
		char* mn = (char*)Memory::Allocate(sizeof(char) * MNameLength, MemoryType::eMemory_Type_String);
		if (!f.ReadBuffer(mn, sizeof(char) * MNameLength)) {
			Memory::Free(mn, MemoryType::eMemory_Type_String);
			return false;
		}
		g.material_name = mn;
		Memory::Free(mn, MemoryType::eMemory_Type_String);

		// Center
		if (!f.Read(&g.center)) return false;

		// Extents (min / max)
		if (!f.Read(&g.min_extents)) return false;
		if (!f.Read(&g.max_extents)) return false;

		out_geometries.push_back(g);
	}

	f.Close();
	return true;
}

bool MeshLoader::WriteDsmFile(const FString& path, const FString& name, std::vector<FGeometryConfig>& geometries) {
	File f(path.CStr());

	if (f.IsExist()) {
		GLOG(Log::eInfo, "File '%s' already exists and will be overwritten.", path.CStr());
	}

	if (!f.Open(eFileMode::Write, true)) {  // true = 二进制模式
		GLOG(Log::eError, "Unable to open file '%s'. Dsm file write failed.", path.CStr());
		return false;
	}

	uint32_t geometry_count = (uint32_t)geometries.size();

	// Version
	unsigned short Version = 0x0001U;
	if (!f.Write(&Version)) return false;

	// Name length + name
	uint32_t NameLength = (uint32_t)name.Length() + 1;
	if (!f.Write(&NameLength)) return false;
	if (!f.WriteBuffer(name.CStr(), sizeof(char) * NameLength)) return false;

	// Geometry count
	if (!f.Write(&geometry_count)) return false;

	for (uint32_t i = 0; i < geometry_count; ++i) {
		FGeometryConfig* g = &geometries[i];

		// Vertices (size / count / array)
		if (!f.Write(&g->vertex_size))  return false;
		if (!f.Write(&g->vertex_count)) return false;
		if (!f.WriteBuffer(g->vertices, g->vertex_size * g->vertex_count)) return false;

		// Indices (size / count / array)
		if (!f.Write(&g->index_size))  return false;
		if (!f.Write(&g->index_count)) return false;
		if (!f.WriteBuffer(g->indices, g->index_size * g->index_count)) return false;

		// Geometry name
		uint32_t GNameLength = (uint32_t)g->name.Length() + 1;
		if (!f.Write(&GNameLength)) return false;
		if (!f.WriteBuffer(g->name.CStr(), sizeof(char) * GNameLength)) return false;

		// Material name
		uint32_t MNameLength = (uint32_t)g->material_name.Length() + 1;
		if (!f.Write(&MNameLength)) return false;
		if (!f.WriteBuffer(g->material_name.CStr(), sizeof(char) * MNameLength)) return false;

		// Center
		if (!f.Write(&g->center)) return false;

		// Extents (min / max)
		if (!f.Write(&g->min_extents)) return false;
		if (!f.Write(&g->max_extents)) return false;
	}

	f.Close();
	return true;
}
bool MeshLoader::DeduplicateGeometry(std::vector<FGeometryConfig>& outGeometries) {
	size_t Count = outGeometries.size();
	uint32_t NewVertCount = 0;
	Vertex* UniqueVerts = nullptr;
	for (size_t i = 0; i < Count; ++i) {
		FGeometryConfig* g = &outGeometries[i];
		GLOG(Log::eDebug, "Geometry de-duplication process starting on geometry object named '%s'.", g->name.CStr());
		GeometryUtils::DeduplicateVertices(g->vertex_count, (Vertex*)g->vertices, g->index_count, (uint32_t*)g->indices, &NewVertCount, &UniqueVerts);

		// Destroy the old, large array.
		Memory::Free(g->vertices, MemoryType::eMemory_Type_Array);

		// And replace with the de-duplicated one.k
		g->vertex_count = NewVertCount;
		g->vertices = UniqueVerts;

		// Take a copy of the indices as a normal.
		uint32_t* Indices = (uint32_t*)Memory::Allocate(sizeof(uint32_t) * g->index_count, MemoryType::eMemory_Type_Array);
		Memory::Copy(Indices, g->indices, sizeof(uint32_t) * g->index_count);
		// Destroy.
		Memory::Free(g->indices, MemoryType::eMemory_Type_Array);
		g->indices = Indices;
	}

	return true;
}