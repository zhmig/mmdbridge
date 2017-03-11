#ifdef WITH_OPTIX

//#ifdef __APPLE__
//#  include <GLUT/glut.h>
//#else
//#  include <GL/glew.h>
//#  if defined( _WIN32 )
//#    include <GL/wglew.h>
//#    include <GL/freeglut.h>
//#  else
//#    include <GL/glut.h>
//#  endif
//#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYEXR_IMPLEMENTATION
#define _CRT_SECURE_NO_WARNINGS
#include "d3d9.h"
#include "d3dx9.h"

#include "bridge_parameter.h"
#include "UMStringUtil.h"
#include "UMPath.h"
#include "UMMath.h"
#include "UMBox.h"
#include "UMVector.h"

#include "stb_image.h"
#include "stb_image_write.h"
#include "tinyexr.h"

#include <boost/python/detail/wrap_python.hpp>
#include <boost/python.hpp>
#include <boost/python/make_constructor.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/map_indexing_suite.hpp>
#include <boost/python/copy_non_const_reference.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/python/args.hpp>
#include <boost/python/tuple.hpp>
#include <boost/python/class.hpp>
#include <boost/python/overloads.hpp>
#include <boost/format.hpp>

#include "optix.h"

#include <optixu/optixpp_namespace.h>
#include <optixu/optixu_matrix_namespace.h>
#include <optixu/optixu_math_stream_namespace.h>
#include <optixu/optixu_math_namespace.h>

using namespace optix;


namespace
{
	struct MMDMesh
	{
		optix::Buffer optix_tri_indices;
		optix::Buffer optix_mat_indices;
		optix::Buffer optix_positions;
		optix::Buffer optix_normals;
		optix::Buffer optix_texcoords;
	};


	Context        context = NULL;
	GeometryGroup geometry_group;
	std::string export_directory;
	Program        pgram_intersection = 0;
	Program        pgram_bounding_box = 0;
	optix::Aabb    aabb;

	std::vector< MMDMesh > mmd_mesh_list;

	// Camera state
	float3         camera_up;
	float3         camera_lookat;
	float3         camera_eye;
	Matrix4x4      camera_rotate;
	bool           camera_changed = true;

}

Buffer getOutputBuffer()
{
	return context["output_buffer"]->getBuffer();
}

std::string ptxPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "optixPathTracer.cu.ptx";
}

std::string pinholeCameraCuPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "pinhole_camera.cu.ptx";
}

std::string constantbgCuPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "constantbg.cu.ptx";
}

std::string phongCuPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "phong.cu.ptx";
}

std::string triangleMeshCuPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "triangle_mesh.cu.ptx";
}

std::string ptxParallelPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "optixPathTracer_generated_parallelogram.cu.ptx";
}

optix::Program createBoundingBoxProgram(optix::Context context)
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	std::string path = umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + "cuda_compile_ptx_generated_triangle_mesh.cu.ptx";
	return context->createProgramFromPTXFile(path, "mesh_bounds");
}

optix::Program createIntersectionProgram(optix::Context context)
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	std::string path = umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) +  "cuda_compile_ptx_generated_triangle_mesh.cu.ptx";
	return context->createProgramFromPTXFile(path, "mesh_intersect");
}

void createMaterialPrograms(
	optix::Context         context,
	bool                   use_textures,
	optix::Program&        closest_hit,
	optix::Program&        any_hit
	)
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	std::string path = phongCuPath();

	const std::string closest_name = use_textures ?
		"closest_hit_radiance_textured" :
		"closest_hit_radiance";

	if (!closest_hit)
		closest_hit = context->createProgramFromPTXFile(path, closest_name);
	if (!any_hit)
		any_hit = context->createProgramFromPTXFile(path, "any_hit_shadow");
}

void createMaterialProgramsPT(
	optix::Context         context,
	bool                   use_textures,
	optix::Program&        closest_hit,
	optix::Program&        any_hit
	)
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	std::string path = ptxPath();

	const std::string closest_name = use_textures ?
		"diffuse_textured" :
		"diffuse";

	if (!closest_hit)
		closest_hit = context->createProgramFromPTXFile(path, closest_name);
	if (!any_hit)
		any_hit = context->createProgramFromPTXFile(path, "shadow");
}

static TextureSampler loadTexture(IDirect3DTexture9* texture, const float3& default_color)
{
	optix::TextureSampler sampler = context->createTextureSampler();
	sampler->setWrapMode(0, RT_WRAP_REPEAT);
	sampler->setWrapMode(1, RT_WRAP_REPEAT);
	sampler->setWrapMode(2, RT_WRAP_REPEAT);
	sampler->setIndexingMode(RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
	sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
	sampler->setMaxAnisotropy(1.0f);
	sampler->setMipLevelCount(1u);
	sampler->setArraySize(1u);

	D3DLOCKED_RECT lockRect;
	if (texture && texture->lpVtbl->LockRect(texture, 0, &lockRect, NULL, D3DLOCK_READONLY) == D3D_OK)
	{
		IDirect3DSurface9 *surface;
		texture->lpVtbl->GetSurfaceLevel(texture, 0, &surface);
		D3DSURFACE_DESC desc;
		surface->lpVtbl->GetDesc(surface, &desc);
		surface->lpVtbl->Release(surface);
		const int width = desc.Width;
		const int height = desc.Height;

		optix::Buffer buffer = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_UNSIGNED_BYTE4, width, height);
		unsigned char* buffer_data = static_cast<unsigned char*>(buffer->map());

		unsigned char* src = reinterpret_cast<unsigned char*>(lockRect.pBits);
		for (int y = 0; y < height; ++y) {
			memcpy(&buffer_data[y * width * 4], src, width * 4);
			src += lockRect.Pitch;
		}
		texture->lpVtbl->UnlockRect(texture, 0);
		buffer->unmap();

		sampler->setBuffer(0u, 0u, buffer);
		sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
	}
	else
	{
		optix::Buffer buffer = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_UNSIGNED_BYTE4, 1u, 1u);
		unsigned char* buffer_data = static_cast<unsigned char*>(buffer->map());
		buffer_data[0] = (unsigned char)clamp((int)(default_color.x * 255.0f), 0, 255);
		buffer_data[1] = (unsigned char)clamp((int)(default_color.y * 255.0f), 0, 255);
		buffer_data[2] = (unsigned char)clamp((int)(default_color.z * 255.0f), 0, 255);
		buffer_data[3] = 255;
		buffer->unmap();

		sampler->setBuffer(0u, 0u, buffer);
		// Although it would be possible to use nearest filtering here, we chose linear
		// to be consistent with the textures that have been loaded from a file. This
		// allows OptiX to perform some optimizations.
		sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
	}

	return  sampler;
}

optix::Material createOptiXMaterial(
	optix::Context         context,
	optix::Program         closest_hit,
	optix::Program         any_hit,
	RenderedMaterial* material,
	bool                   use_textures
	)
{
	optix::Material mat = context->createMaterial();
	mat->setClosestHitProgram(0u, closest_hit);
	mat->setAnyHitProgram(1u, any_hit);

	if (use_textures) {
		mat["Kd_map"]->setTextureSampler(loadTexture(material->tex, optix::make_float3(0.7f, 0.7f, 0.7f)));
	} else {
		mat["Kd_map"]->setTextureSampler(loadTexture(NULL, optix::make_float3(0.7f, 0.7f, 0.7f)));
	}

	mat["Kd_mapped"]->setInt(use_textures);
	const float diffuse[] = {
		material->diffuse.z,
		material->diffuse.y,
		material->diffuse.x
	};
	const float specular[] = {
		material->specular.z,
		material->specular.y,
		material->specular.x
	};
	mat["Kd"]->set3fv(diffuse);
	mat["Ks"]->set3fv(specular);
	//mat["Kr"]->set3fv(mat_params.Kr);
	//mat["Ka"]->set3fv(mat_params.Ka);
	mat["phong_exp"]->setFloat(material->power);

	return mat;
}

static void createContext(int width, int height)
{
	// Set up context
	context = Context::create();
	context->setRayTypeCount(2);
	context->setEntryPointCount(1);

	context["radiance_ray_type"]->setUint(0u);
	context["shadow_ray_type"]->setUint(1u);
	context["scene_epsilon"]->setFloat(1.e-4f);

	Buffer buffer = context->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT4, width, height);
	context["output_buffer"]->set(buffer);

	// Ray generation program
	std::string ptx_path = pinholeCameraCuPath();
	Program ray_gen_program = context->createProgramFromPTXFile(ptx_path, "pinhole_camera");
	context->setRayGenerationProgram(0, ray_gen_program);

	// Exception program
	Program exception_program = context->createProgramFromPTXFile(ptx_path, "exception");
	context->setExceptionProgram(0, exception_program);
	context["bad_color"]->setFloat(1.0f, 0.0f, 1.0f);

	// Miss program
	ptx_path = constantbgCuPath();
	context->setMissProgram(0, context->createProgramFromPTXFile(ptx_path, "miss"));
	context["bg_color"]->setFloat(0.85f, 0.85f, 0.85f);
}

static void createContextPT(int width, int height)
{
	// Set up context
	context = Context::create();
	context->setRayTypeCount(2);
	context->setEntryPointCount(1);
	context->setStackSize(1040);

	context["scene_epsilon"]->setFloat(1.e-3f);
	context["pathtrace_ray_type"]->setUint(0u);
	context["pathtrace_shadow_ray_type"]->setUint(1u);
	context["rr_begin_depth"]->setUint(1);

	Buffer buffer = context->createBuffer(RT_BUFFER_OUTPUT, RT_FORMAT_FLOAT4, width, height);
	context["output_buffer"]->set(buffer);

	// Setup programs
	std::string ptx_path = ptxPath();
	context->setRayGenerationProgram(0, context->createProgramFromPTXFile(ptx_path, "pathtrace_camera"));
	context->setExceptionProgram(0, context->createProgramFromPTXFile(ptx_path, "exception"));
	context->setMissProgram(0, context->createProgramFromPTXFile(ptx_path, "miss"));

	context["sqrt_num_samples"]->setUint(2);
	context["bad_color"]->setFloat(1000000.0f, 0.0f, 1000000.0f); // Super magenta to make sure it doesn't get averaged out in the progressive rendering.
	context["bg_color"]->setFloat(make_float3(0.85f));
}

static void setupCamera()
{
	/*
	camera_eye = make_float3(278.0f, 273.0f, -900.0f);
	camera_lookat = make_float3(278.0f, 273.0f, 0.0f);
	camera_up = make_float3(0.0f, 1.0f, 0.0f);

	camera_rotate = Matrix4x4::identity();
	*/
	const float max_dim = fmaxf(aabb.extent(0), aabb.extent(1)); // max of x, y components

	camera_eye = aabb.center() + make_float3(0.0f, 0.0f, max_dim*1.5f);
	camera_lookat = aabb.center();
	camera_up = make_float3(0.0f, 1.0f, 0.0f);

	camera_rotate = Matrix4x4::identity();
}


struct MeshBuffers
{
	optix::Buffer tri_indices;
	optix::Buffer mat_indices;
	optix::Buffer positions;
	optix::Buffer normals;
	optix::Buffer texcoords;
};

struct ParallelogramLight
{
	optix::float3 corner;
	optix::float3 v1, v2;
	optix::float3 normal;
	optix::float3 emission;
};

struct BasicLight
{
#if defined(__cplusplus)
	typedef optix::float3 float3;
#endif
	float3 pos;
	float3 color;
	int    casts_shadow;
	int    padding;      // make this structure 32 bytes -- powers of two are your friend!
};


class OptixMesh
{
	// Input
	optix::Context               context;       // required
	optix::Material              material;      // optional single matl override

	// Output
	optix::GeometryInstance      geom_instance;
	optix::float3                bbox_min;
	optix::float3                bbox_max;
	int                          num_triangles;
};

static GeometryInstance createParallelogram(
	const float3& anchor,
	const float3& offset1,
	const float3& offset2)
{
	Geometry parallelogram = context->createGeometry();
	parallelogram->setPrimitiveCount(1u);
	parallelogram->setIntersectionProgram(pgram_intersection);
	parallelogram->setBoundingBoxProgram(pgram_bounding_box);

	float3 normal = normalize(cross(offset1, offset2));
	float d = dot(normal, anchor);
	float4 plane = make_float4(normal, d);

	float3 v1 = offset1 / dot(offset1, offset1);
	float3 v2 = offset2 / dot(offset2, offset2);

	parallelogram["plane"]->setFloat(plane);
	parallelogram["anchor"]->setFloat(anchor);
	parallelogram["v1"]->setFloat(v1);
	parallelogram["v2"]->setFloat(v2);

	GeometryInstance gi = context->createGeometryInstance();
	gi->setGeometry(parallelogram);
	return gi;
}

static void updateMMDMesh(MMDMesh& mmd_mesh, const RenderedBuffer & renderedBuffer, int renderedBufferIndex)
{
	const RenderedBuffer::UVList &uvList = renderedBuffer.uvs;
	const RenderedBuffer::VertexList &vertexList = renderedBuffer.vertecies;
	const RenderedBuffer::NormalList &normalList = renderedBuffer.normals;

	// indices
	UMVec3i* indices = reinterpret_cast<UMVec3i*>(mmd_mesh.optix_tri_indices->map());
	int32_t* mat_indices = reinterpret_cast<int32_t*>(mmd_mesh.optix_mat_indices->map());
	int index = 0;
	for (int k = 0, ksize = static_cast<int>(renderedBuffer.materials.size()); k < ksize; ++k)
	{
		RenderedMaterial* material = renderedBuffer.materials.at(k);

		for (int n = 0; n < material->surface.faces.size(); ++n)
		{
			mat_indices[index] = k;
			UMVec3i face = material->surface.faces[n];
			indices[index] = UMVec3i(face.x - 1, face.y - 1, face.z - 1);
			index++;
		};
	}

	UMBox box;
	UMVec3f* vertices = reinterpret_cast<UMVec3f*>  (mmd_mesh.optix_positions->map());
	for (int i = 0; i < vertexList.size(); ++i) {
		D3DXVECTOR3 v = vertexList[i];
		UMVec3d vv(v.x, v.y, v.z);
		box.extend(vv);
		memcpy(&vertices[i], &v, sizeof(UMVec3f));
	}

	float3 bmin = make_float3(box.minimum().x, box.minimum().y, box.minimum().z);
	float3 bmax = make_float3(box.maximum().x, box.maximum().y, box.maximum().z);
	aabb.set(bmin, bmax);

	UMVec3f* normals = reinterpret_cast<UMVec3f*>  (mmd_mesh.optix_normals->map());
	memcpy(normals, &(*normalList.begin()), normalList.size() * sizeof(UMVec3f));

	UMVec2f* uvs = NULL;
	if (!uvList.empty())
	{
		uvs = reinterpret_cast<UMVec2f*>  (mmd_mesh.optix_texcoords->map());
		memcpy(uvs, &(*uvList.begin()), uvList.size() * sizeof(UMVec2f));
	}

	mmd_mesh.optix_tri_indices->unmap();
	mmd_mesh.optix_positions->unmap();
	mmd_mesh.optix_normals->unmap();
	if (!uvList.empty())
	{
		mmd_mesh.optix_texcoords->unmap();
	}
	mmd_mesh.optix_mat_indices->unmap();

}

static GeometryInstance createMMDMesh(MMDMesh& mmd_mesh, const RenderedBuffer & renderedBuffer, int renderedBufferIndex)
{
	Geometry geometry = context->createGeometry();

	MeshBuffers buffers;

	const RenderedBuffer::UVList &uvList = renderedBuffer.uvs;
	const RenderedBuffer::VertexList &vertexList = renderedBuffer.vertecies;
	const RenderedBuffer::NormalList &normalList = renderedBuffer.normals;

	int num_triangles = 0;
	for (int k = 0, ksize = static_cast<int>(renderedBuffer.materials.size()); k < ksize; ++k)
	{
		RenderedMaterial* material = renderedBuffer.materials.at(k);
		num_triangles += material->surface.faces.size();
	}

	mmd_mesh.optix_tri_indices = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_INT3, num_triangles);
	mmd_mesh.optix_mat_indices = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_INT, num_triangles);
	mmd_mesh.optix_positions = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, vertexList.size());
	mmd_mesh.optix_normals = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, normalList.size());
	mmd_mesh.optix_texcoords = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT2, uvList.empty() ? 0 : uvList.size());
	std::vector<optix::Material> optix_materials;

	optix::Program tex_closest_hit;
	optix::Program tex_any_hit;
	optix::Program closest_hit;
	optix::Program any_hit;
	createMaterialProgramsPT(context, true, tex_closest_hit, tex_any_hit);
	createMaterialProgramsPT(context, false, closest_hit, any_hit);

	updateMMDMesh(mmd_mesh, renderedBuffer, renderedBufferIndex);
	for (int k = 0, ksize = static_cast<int>(renderedBuffer.materials.size()); k < ksize; ++k)
	{
		RenderedMaterial* material = renderedBuffer.materials.at(k);
		const bool use_texture = !material->memoryTexture.empty();
		optix_materials.push_back(createOptiXMaterial(
			context,
			use_texture ? tex_closest_hit : closest_hit,
			use_texture ? tex_any_hit : any_hit,
			material,
			use_texture));
	}

	geometry["vertex_buffer"]->setBuffer(mmd_mesh.optix_positions);
	geometry["normal_buffer"]->setBuffer(mmd_mesh.optix_normals);
	geometry["texcoord_buffer"]->setBuffer(mmd_mesh.optix_texcoords);
	geometry["material_buffer"]->setBuffer(mmd_mesh.optix_mat_indices);
	geometry["index_buffer"]->setBuffer(mmd_mesh.optix_tri_indices);
	geometry->setPrimitiveCount(num_triangles);
	geometry->setBoundingBoxProgram(pgram_bounding_box);
	geometry->setIntersectionProgram(pgram_intersection);

	GeometryInstance gi = context->createGeometryInstance(
		geometry,
		optix_materials.begin(),
		optix_materials.end()
		);

	return gi;
}

static void setMaterial(
	GeometryInstance& gi,
	Material material,
	const std::string& color_name,
	const float3& color)
{
	gi->addMaterial(material);
	gi[color_name]->setFloat(color);
}

static void setupLights()
{
	const float max_dim = fmaxf(aabb.extent(0), aabb.extent(1)); // max of x, y components

	BasicLight lights[] = {
		{ make_float3(-0.5f, 0.25f, -1.0f), make_float3(0.2f, 0.2f, 0.25f), 0, 0 },
		{ make_float3(-0.5f, 0.0f, 1.0f), make_float3(0.1f, 0.1f, 0.10f), 0, 0 },
		{ make_float3(0.5f, 0.5f, 0.5f), make_float3(0.7f, 0.7f, 0.65f), 1, 0 }
	};
	lights[0].pos *= max_dim * 10.0f;
	lights[1].pos *= max_dim * 10.0f;
	lights[2].pos *= -max_dim * 10.0f;

	Buffer light_buffer = context->createBuffer(RT_BUFFER_INPUT);
	light_buffer->setFormat(RT_FORMAT_USER);
	light_buffer->setElementSize(sizeof(BasicLight));
	light_buffer->setSize(sizeof(lights) / sizeof(lights[0]));
	memcpy(light_buffer->map(), lights, sizeof(lights));
	light_buffer->unmap();

	context["lights"]->set(light_buffer);
}

static void setupLightsPT()
{
	const float max_dim = fmaxf(aabb.extent(0), aabb.extent(1)); // max of x, y components

	ParallelogramLight light;
	light.corner = make_float3(343.0f, 548.6f, -227.0f);
	light.v1 = make_float3(-130.0f, 0.0f, 0.0f);
	light.v2 = make_float3(0.0f, 0.0f, 105.0f);
	light.normal = normalize(cross(light.v1, light.v2));
	light.emission = make_float3(15.0f, 15.0f, 5.0f) * 5;

	Buffer light_buffer = context->createBuffer(RT_BUFFER_INPUT);
	light_buffer->setFormat(RT_FORMAT_USER);
	light_buffer->setElementSize(sizeof(ParallelogramLight));
	light_buffer->setSize(1u);
	memcpy(light_buffer->map(), &light, sizeof(light));
	light_buffer->unmap();
	context["lights"]->setBuffer(light_buffer);
}

static void loadGeometry()
{
	std::vector<GeometryInstance> gis;

	// Set up parallelogram programs
	 std::string ptx_path = triangleMeshCuPath();
	pgram_bounding_box = context->createProgramFromPTXFile(ptx_path, "mesh_bounds");
	pgram_intersection = context->createProgramFromPTXFile(ptx_path, "mesh_intersect");

	// MMDメッシュの設定
	const BridgeParameter& parameter = BridgeParameter::instance();
	const VertexBufferList& finishBuffers = BridgeParameter::instance().finish_buffer_list;
	mmd_mesh_list.resize(finishBuffers.size());
	for (int i = 0, isize = static_cast<int>(finishBuffers.size()); i < isize; ++i)
	{
		const RenderedBuffer &renderedBuffer = parameter.render_buffer(i);
		gis.push_back(createMMDMesh(mmd_mesh_list[i], renderedBuffer, i));
	}

	// Light
	Material diffuse_light = context->createMaterial();
	Program diffuse_em = context->createProgramFromPTXFile(ptx_path, "diffuseEmitter");
	diffuse_light->setClosestHitProgram(0, diffuse_em);
	gis.push_back(createParallelogram(make_float3(343.0f, 548.6f, 227.0f),
		make_float3(-130.0f, 0.0f, 0.0f),
		make_float3(0.0f, 0.0f, 105.0f)));
	const float3 light_em = make_float3(15.0f, 15.0f, 5.0f);
	setMaterial(gis.back(), diffuse_light, "emission_color", light_em);

	// Create geometry group
	geometry_group = context->createGeometryGroup(gis.begin(), gis.end());
	geometry_group->setAcceleration(context->createAcceleration("Trbvh"));
	context["top_object"]->set(geometry_group);
	context["top_shadower"]->set(geometry_group);
}

static void loadGeometryPT()
{
	std::vector<GeometryInstance> gis;

	// Set up parallelogram programs
	std::string ptx_path = triangleMeshCuPath();
	pgram_bounding_box = context->createProgramFromPTXFile(ptx_path, "mesh_bounds");
	pgram_intersection = context->createProgramFromPTXFile(ptx_path, "mesh_intersect");

	// MMDメッシュの設定
	const BridgeParameter& parameter = BridgeParameter::instance();
	const VertexBufferList& finishBuffers = BridgeParameter::instance().finish_buffer_list;
	mmd_mesh_list.resize(finishBuffers.size());
	for (int i = 0, isize = static_cast<int>(finishBuffers.size()); i < isize; ++i)
	{
		const RenderedBuffer &renderedBuffer = parameter.render_buffer(i);
		gis.push_back(createMMDMesh(mmd_mesh_list[i], renderedBuffer, i));
	}

	// Create geometry group
	geometry_group = context->createGeometryGroup(gis.begin(), gis.end());
	geometry_group->setAcceleration(context->createAcceleration("Trbvh"));
	context["top_object"]->set(geometry_group);
	context["top_shadower"]->set(geometry_group);
}

static void calculateCameraVariables(float3 eye, float3 lookat, float3 up,
	float  fov, float  aspect_ratio,
	float3& U, float3& V, float3& W, bool fov_is_vertical)
{
	float ulen, vlen, wlen;
	W = -(lookat - eye); // Do not normalize W -- it implies focal length

	wlen = length(W);
	U = normalize(cross(W, up));
	V = normalize(cross(U, W));

	if (fov_is_vertical) {
		vlen = wlen * tanf(0.5f * fov * M_PIf / 180.0f);
		V *= vlen;
		ulen = vlen * aspect_ratio;
		U *= ulen;
	}
	else {
		ulen = wlen * tanf(0.5f * fov * M_PIf / 180.0f);
		U *= ulen;
		vlen = ulen / aspect_ratio;
		V *= vlen;
	}
}

static void d3d_vector3_transform(
	D3DXVECTOR3 &dst,
	const D3DXVECTOR3 &src,
	const D3DXMATRIX &matrix)
{
	const float tmp[] = {
		src.x*matrix.m[0][0] + src.y*matrix.m[1][0] + src.z*matrix.m[2][0] + 1.0f*matrix.m[3][0],
		src.x*matrix.m[0][1] + src.y*matrix.m[1][1] + src.z*matrix.m[2][1] + 1.0f*matrix.m[3][1],
		src.x*matrix.m[0][2] + src.y*matrix.m[1][2] + src.z*matrix.m[2][2] + 1.0f*matrix.m[3][2]
	};
	dst.x = tmp[0];
	dst.y = tmp[1];
	dst.z = tmp[2];
}

// 行列で3Dベクトルをトランスフォームする
// D3DXVec3Transformとほぼ同じ
static void d3d_vector3_dir_transform(
	D3DXVECTOR3 &dst,
	const D3DXVECTOR3 &src,
	const D3DXMATRIX &matrix)
{
	const float tmp[] = {
		src.x*matrix.m[0][0] + src.y*matrix.m[1][0] + src.z*matrix.m[2][0],
		src.x*matrix.m[0][1] + src.y*matrix.m[1][1] + src.z*matrix.m[2][1],
		src.x*matrix.m[0][2] + src.y*matrix.m[1][2] + src.z*matrix.m[2][2]
	};
	dst.x = tmp[0];
	dst.y = tmp[1];
	dst.z = tmp[2];
}

static void updateCamera(const RenderedBuffer & renderedBuffer, int currentframe)
{
	D3DXVECTOR4 v;
	UMGetCameraFovLH(&v);

	const float fov = umbase::um_to_degree(v.x);
	const float aspect_ratio = v.y;

	D3DXVECTOR3 eye;
	UMGetCameraEye(&eye);
	d3d_vector3_transform(eye, eye, renderedBuffer.world_inv);

	D3DXVECTOR3 lookat;
	UMGetCameraAt(&lookat);
	d3d_vector3_transform(lookat, lookat, renderedBuffer.world_inv);

	D3DXVECTOR3 up;
	UMGetCameraUp(&up);
	d3d_vector3_dir_transform(up, up, renderedBuffer.world_inv);
	::D3DXVec3Normalize(&up, &up);

	camera_eye = make_float3(eye.x, eye.y, eye.z);
	camera_lookat = make_float3(lookat.x, lookat.y, lookat.z);
	camera_up = make_float3(up.x, up.y, up.z);

	float3 camera_u, camera_v, camera_w;
	calculateCameraVariables(
		camera_eye, camera_lookat, camera_up, fov, aspect_ratio,
		camera_u, camera_v, camera_w, /*fov_is_vertical*/ true);

	context["frame_number"]->setUint(currentframe);
	context["eye"]->setFloat(camera_eye);
	context["U"]->setFloat(camera_u);
	context["V"]->setFloat(camera_v);
	context["W"]->setFloat(-camera_w);

	//if (camera_changed) // reset accumulation
	//	frame_number = 1;
	camera_changed = false;
}

static void updatePreview(RTbuffer buffer, float gamma)
{
	RTsize width, height;
	rtBufferGetSize2D(buffer, &width, &height);
	void* data;
	rtBufferMap(buffer, &data);

	std::vector<unsigned char> image_buffer(width * height * 4);
	// This buffer is upside down
	for (int y = height - 1; y >= 0; --y) {
		unsigned char *dst = &image_buffer[0] + (4 * width * (height - 1 - y));
		float* src = ((float*)data) + (4 * width * y);
		for (int i = 0; i < width; i++) {
			for (int elem = 0; elem < 4; ++elem) {
				int P = static_cast<int>(255.0f * powf((*src++), 1.0f / gamma) + 0.5f);
				unsigned int Clamped = P < 0 ? 0 : P > 0xff ? 0xff : P;
				*dst++ = static_cast<unsigned char>(Clamped);
			}
		}
	}

	BridgeParameter& parameter = BridgeParameter::mutable_instance();
	if (!parameter.preview_tex) {
		return;
	}

	if (parameter.preview_tex) {
		D3DLOCKED_RECT lockRect;
		parameter.preview_tex->lpVtbl->LockRect(parameter.preview_tex, 0, &lockRect, NULL, D3DLOCK_DISCARD);
		unsigned char* dst = reinterpret_cast<unsigned char*>(lockRect.pBits);
		for (int y = 0; y < parameter.viewport_height; ++y) {
			memcpy(dst, &image_buffer[y * width * 4], parameter.viewport_width * 4);
			dst += lockRect.Pitch;
		}
		parameter.preview_tex->lpVtbl->UnlockRect(parameter.preview_tex, 0);
	}

	rtBufferUnmap(buffer);
}
static void saveImage(const char* filename, RTbuffer buffer)
{
	RTsize width, height;
	rtBufferGetSize2D(buffer, &width, &height);
	void* data;
	rtBufferMap(buffer, &data);

	std::vector<unsigned char> image_buffer(width * height * 4);
	// This buffer is upside down
	for (int y = height - 1; y >= 0; --y) {
		unsigned char *dst = &image_buffer[0] + (4 * width * (height - 1 - y));
		float* src = ((float*)data) + (4 * width * y);
		for (int i = 0; i < width; i++) {
			for (int elem = 0; elem < 4; ++elem) {
				int P = static_cast<int>((*src++) * 255.0f);
				unsigned int Clamped = P < 0 ? 0 : P > 0xff ? 0xff : P;
				*dst++ = static_cast<unsigned char>(Clamped);
			}
		}
	}
	stbi_write_png(filename, width, height, STBI_rgb_alpha, &(*image_buffer.begin()), 0);

	rtBufferUnmap(buffer);
}

static bool loadEXRHDR(TextureSampler& sampler)
{
	const char* input = "D:\\IBL\\hdrmaps_com_free_072\\hdrmaps_com_free_072_Ref.exr";

	sampler->setWrapMode(0, RT_WRAP_REPEAT);
	sampler->setWrapMode(1, RT_WRAP_REPEAT);
	sampler->setWrapMode(2, RT_WRAP_REPEAT);
	sampler->setIndexingMode(RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
	sampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);

	// 1. Read EXR version.
	EXRVersion exr_version;
	int ret = ParseEXRVersionFromFile(&exr_version, input);
	if (ret != 0) {
		return false;
	}

	if (exr_version.multipart) {
		// must be multipart flag is false.
		return false;
	}

	// 2. Read EXR header
	EXRHeader exr_header;
	InitEXRHeader(&exr_header);

	const char* err;
	ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, input, &err);
	if (ret != 0) {
		//fprintf(stderr, "Parse EXR err: %s\n", err);
		return false;
	}
	if (exr_header.num_channels != 3 && exr_header.num_channels != 4) {
		return false;
	}

	// Read HALF channel as FLOAT.
	for (int i = 0; i < exr_header.num_channels; i++) {
		if (exr_header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
			exr_header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
		}
	}

	EXRImage exr_image;
	InitEXRImage(&exr_image);

	ret = LoadEXRImageFromFile(&exr_image, &exr_header, input, &err);
	if (ret != 0) {
		//fprintf(stderr, "Load EXR err: %s\n", err);
		return false;
	}

	optix::Buffer buffer = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT4, exr_image.width, exr_image.height);
	float* buffer_data = static_cast<float*>(buffer->map());
	if (exr_image.num_channels == 4) {
		float* a = reinterpret_cast<float*>(exr_image.images[0]);
		float* b = reinterpret_cast<float*>(exr_image.images[1]);
		float* g = reinterpret_cast<float*>(exr_image.images[2]);
		float* r = reinterpret_cast<float*>(exr_image.images[3]);
		for (int y = 0; y < exr_image.height; ++y)
		{
			for (int x = 0; x < exr_image.width; ++x)
			{
				const int srcpos = (y * exr_image.width + x);
				const int dstpos = ((exr_image.height - y - 1) * exr_image.width + x);
				buffer_data[dstpos * 4 + 0] = b[srcpos]; // b
				buffer_data[dstpos * 4 + 1] = g[srcpos]; // g
				buffer_data[dstpos * 4 + 2] = r[srcpos]; // r
				buffer_data[dstpos * 4 + 3] = a[srcpos]; // a
			}
		}
	}
	buffer->unmap();

	sampler->setBuffer(buffer);
	sampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);

	context["envmap"]->setTextureSampler(sampler);
	//updatePreview(buffer->get());

	FreeEXRImage(&exr_image);
	return true;
}

static void start_optix_export(
	const std::string& directory_path,
	int export_mode)
{
	const BridgeParameter& parameter = BridgeParameter::instance();

	export_directory = directory_path;
	createContextPT(parameter.viewport_width, parameter.viewport_height);
	optix::TextureSampler sampler = context->createTextureSampler();
	bool result = loadEXRHDR(sampler);
	loadGeometryPT();
	setupLightsPT();
	setupCamera();
	context->validate();
}

static void execute_optix_export(int currentframe)
{
	if (!context) return;

	const BridgeParameter& parameter = BridgeParameter::instance();
	const VertexBufferList& finishBuffers = BridgeParameter::instance().finish_buffer_list;
	const RenderBufferMap& renderBuffers = BridgeParameter::instance().render_buffer_map;

	bool exportedCamera = false;
	for (int i = static_cast<int>(finishBuffers.size()) - 1; i >= 0; --i)
	{
		const RenderedBuffer &renderedBuffer = parameter.render_buffer(i);

		if (!exportedCamera && !renderedBuffer.isAccessory)
		{
			updateCamera(renderedBuffer, currentframe);
			break;
		}
	}
	context->launch(0, parameter.viewport_width, parameter.viewport_height);

	//std::string file = umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + ("out\\frame_")
	//	+ umbase::UMStringUtil::number_to_string(currentframe) + ".png";
	//saveImage(file.c_str(), getOutputBuffer()->get());
	updatePreview(getOutputBuffer()->get(), 2.2f);
}

static void end_optix_export()
{
	if (context)
	{
		context->destroy();
		context = 0;
	}
}

void DisposeOptix()
{
	if (context)
	{
		try
		{
			context->destroy();
		}
		catch (optix::Exception& ex)
		{
			const std::string error = ex.getErrorString();
			::MessageBoxA(NULL, (LPCSTR)error.c_str(), "Dispose Error", MB_OK);
		}
		context = 0;
	}
}

void StartOptix(int currentframe)
{
	start_optix_export("", currentframe);
}

void UpdateOptix(int currentframe)
{
	execute_optix_export(currentframe);
}

void UpdateOptixGeometry()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	const VertexBufferList& finishBuffers = BridgeParameter::instance().finish_buffer_list;
	for (int i = 0, isize = static_cast<int>(finishBuffers.size()); i < isize; ++i)
	{
		const RenderedBuffer &renderedBuffer = parameter.render_buffer(i);
		updateMMDMesh(mmd_mesh_list[i], renderedBuffer, i);
	}
	geometry_group->getAcceleration()->markDirty();
}

// ---------------------------------------------------------------------------
BOOST_PYTHON_MODULE(mmdbridge_optix)
{
	using namespace boost::python;
	def("start_optix_export", start_optix_export);
	def("execute_optix_export", execute_optix_export);
	def("end_optix_export", end_optix_export);
}

#endif //WITH_OPTIX


// ---------------------------------------------------------------------------
#ifdef WITH_OPTIX
void InitOptix()
{
	PyImport_AppendInittab("mmdbridge_optix", PyInit_mmdbridge_optix);
}

#else
void InitOptix() {}
void DisposeOptix() {}
#endif //WITH_OPTIX

