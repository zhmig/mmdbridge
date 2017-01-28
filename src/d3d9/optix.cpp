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
#define _CRT_SECURE_NO_WARNINGS
#include "d3d9.h"
#include "d3dx9.h"

#include "bridge_parameter.h"
#include "UMStringUtil.h"
#include "UMPath.h"
#include "UMMath.h"
#include "UMVector.h"

#include "stb_image.h"
#include "stb_image_write.h"

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

	Context        context;
	std::string export_directory;
	Program        pgram_intersection = 0;
	Program        pgram_bounding_box = 0;

	// Camera state
	float3         camera_up;
	float3         camera_lookat;
	float3         camera_eye;
	Matrix4x4      camera_rotate;
	bool           camera_changed = true;

	int width = 800;
	int height = 600;

}

Buffer getOutputBuffer()
{
	return context["output_buffer"]->getBuffer();
}

std::string ptxPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + ("optixPathTracer_generated_optixPathTracer.cu.ptx");
}

std::string ptxParallelPath()
{
	const BridgeParameter& parameter = BridgeParameter::instance();
	return umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + ("optixPathTracer_generated_parallelogram.cu.ptx");
}

static void createContext(int width, int height)
{
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
	const std::string ptx_path = ptxPath();
	context->setRayGenerationProgram(0, context->createProgramFromPTXFile(ptx_path, "pathtrace_camera"));
	context->setExceptionProgram(0, context->createProgramFromPTXFile(ptx_path, "exception"));
	context->setMissProgram(0, context->createProgramFromPTXFile(ptx_path, "miss"));

	context["sqrt_num_samples"]->setUint(2);
	// Super magenta to make sure it doesn't get averaged out in the progressive rendering.
	context["bad_color"]->setFloat(1000000.0f, 0.0f, 1000000.0f);
	context["bg_color"]->setFloat(make_float3(0.0f));
}

static void setupCamera()
{
	camera_eye = make_float3(278.0f, 273.0f, -900.0f);
	camera_lookat = make_float3(278.0f, 273.0f, 0.0f);
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
//
//void setupMeshLoaderInputs(
//	optix::Context            context,
//	MeshBuffers&              buffers,
//	Mesh&                     mesh
//	)
//{
//	buffers.tri_indices = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_INT3, mesh.num_triangles);
//	buffers.mat_indices = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_INT, mesh.num_triangles);
//	buffers.positions = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, mesh.num_vertices);
//	buffers.normals = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT3,
//		mesh.has_normals ? mesh.num_vertices : 0);
//	buffers.texcoords = context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT2,
//		mesh.has_texcoords ? mesh.num_vertices : 0);
//
//	mesh.tri_indices = reinterpret_cast<int32_t*>(buffers.tri_indices->map());
//	mesh.mat_indices = reinterpret_cast<int32_t*>(buffers.mat_indices->map());
//	mesh.positions = reinterpret_cast<float*>  (buffers.positions->map());
//	mesh.normals = reinterpret_cast<float*>  (mesh.has_normals ? buffers.normals->map() : 0);
//	mesh.texcoords = reinterpret_cast<float*>  (mesh.has_texcoords ? buffers.texcoords->map() : 0);
//
//	mesh.mat_params = new MaterialParams[mesh.num_materials];
//}
//
//
//static bool execute_optix_export(int currentframe)
//{
//	GeometryGroup group;
//	std::vector<MeshBuffers buffers;
//	setupMeshLoaderInputs(context, buffers, mesh);
//}

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

static void setMaterial(
	GeometryInstance& gi,
	Material material,
	const std::string& color_name,
	const float3& color)
{
	gi->addMaterial(material);
	gi[color_name]->setFloat(color);
}

static  void loadGeometry()
{
	// Light buffer
	ParallelogramLight light;
	light.corner = make_float3(343.0f, 548.6f, 227.0f);
	light.v1 = make_float3(-130.0f, 0.0f, 0.0f);
	light.v2 = make_float3(0.0f, 0.0f, 105.0f);
	light.normal = normalize(cross(light.v1, light.v2));
	light.emission = make_float3(15.0f, 15.0f, 5.0f);

	Buffer light_buffer = context->createBuffer(RT_BUFFER_INPUT);
	light_buffer->setFormat(RT_FORMAT_USER);
	light_buffer->setElementSize(sizeof(ParallelogramLight));
	light_buffer->setSize(1u);
	memcpy(light_buffer->map(), &light, sizeof(light));
	light_buffer->unmap();
	context["lights"]->setBuffer(light_buffer);


	// Set up material
	std::string ptx_path = ptxPath();
	Material diffuse = context->createMaterial();
	Program diffuse_ch = context->createProgramFromPTXFile(ptx_path, "diffuse");
	Program diffuse_ah = context->createProgramFromPTXFile(ptx_path, "shadow");
	diffuse->setClosestHitProgram(0, diffuse_ch);
	diffuse->setAnyHitProgram(1, diffuse_ah);

	Material diffuse_light = context->createMaterial();
	Program diffuse_em = context->createProgramFromPTXFile(ptx_path, "diffuseEmitter");
	diffuse_light->setClosestHitProgram(0, diffuse_em);

	// Set up parallelogram programs
	ptx_path = ptxParallelPath();
	pgram_bounding_box = context->createProgramFromPTXFile(ptx_path, "bounds");
	pgram_intersection = context->createProgramFromPTXFile(ptx_path, "intersect");

	// create geometry instances
	std::vector<GeometryInstance> gis;

	const float3 white = make_float3(0.8f, 0.8f, 0.8f);
	const float3 green = make_float3(0.05f, 0.8f, 0.05f);
	const float3 red = make_float3(0.8f, 0.05f, 0.05f);
	const float3 light_em = make_float3(15.0f, 15.0f, 5.0f);

	// Floor
	gis.push_back(createParallelogram(make_float3(0.0f, 0.0f, 0.0f),
		make_float3(0.0f, 0.0f, 559.2f),
		make_float3(556.0f, 0.0f, 0.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);

	// Ceiling
	gis.push_back(createParallelogram(make_float3(0.0f, 548.8f, 0.0f),
		make_float3(556.0f, 0.0f, 0.0f),
		make_float3(0.0f, 0.0f, 559.2f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);

	// Back wall
	gis.push_back(createParallelogram(make_float3(0.0f, 0.0f, 559.2f),
		make_float3(0.0f, 548.8f, 0.0f),
		make_float3(556.0f, 0.0f, 0.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);

	// Right wall
	gis.push_back(createParallelogram(make_float3(0.0f, 0.0f, 0.0f),
		make_float3(0.0f, 548.8f, 0.0f),
		make_float3(0.0f, 0.0f, 559.2f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", green);

	// Left wall
	gis.push_back(createParallelogram(make_float3(556.0f, 0.0f, 0.0f),
		make_float3(0.0f, 0.0f, 559.2f),
		make_float3(0.0f, 548.8f, 0.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", red);

	// Short block
	gis.push_back(createParallelogram(make_float3(130.0f, 165.0f, 65.0f),
		make_float3(-48.0f, 0.0f, 160.0f),
		make_float3(160.0f, 0.0f, 49.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(290.0f, 0.0f, 114.0f),
		make_float3(0.0f, 165.0f, 0.0f),
		make_float3(-50.0f, 0.0f, 158.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(130.0f, 0.0f, 65.0f),
		make_float3(0.0f, 165.0f, 0.0f),
		make_float3(160.0f, 0.0f, 49.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(82.0f, 0.0f, 225.0f),
		make_float3(0.0f, 165.0f, 0.0f),
		make_float3(48.0f, 0.0f, -160.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(240.0f, 0.0f, 272.0f),
		make_float3(0.0f, 165.0f, 0.0f),
		make_float3(-158.0f, 0.0f, -47.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);

	// Tall block
	gis.push_back(createParallelogram(make_float3(423.0f, 330.0f, 247.0f),
		make_float3(-158.0f, 0.0f, 49.0f),
		make_float3(49.0f, 0.0f, 159.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(423.0f, 0.0f, 247.0f),
		make_float3(0.0f, 330.0f, 0.0f),
		make_float3(49.0f, 0.0f, 159.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(472.0f, 0.0f, 406.0f),
		make_float3(0.0f, 330.0f, 0.0f),
		make_float3(-158.0f, 0.0f, 50.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(314.0f, 0.0f, 456.0f),
		make_float3(0.0f, 330.0f, 0.0f),
		make_float3(-49.0f, 0.0f, -160.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);
	gis.push_back(createParallelogram(make_float3(265.0f, 0.0f, 296.0f),
		make_float3(0.0f, 330.0f, 0.0f),
		make_float3(158.0f, 0.0f, -49.0f)));
	setMaterial(gis.back(), diffuse, "diffuse_color", white);

	// Create shadow group (no light)
	GeometryGroup shadow_group = context->createGeometryGroup(gis.begin(), gis.end());
	shadow_group->setAcceleration(context->createAcceleration("Trbvh"));
	context["top_shadower"]->set(shadow_group);

	// Light
	gis.push_back(createParallelogram(make_float3(343.0f, 548.6f, 227.0f),
		make_float3(-130.0f, 0.0f, 0.0f),
		make_float3(0.0f, 0.0f, 105.0f)));
	setMaterial(gis.back(), diffuse_light, "emission_color", light_em);

	// Create geometry group
	GeometryGroup geometry_group = context->createGeometryGroup(gis.begin(), gis.end());
	geometry_group->setAcceleration(context->createAcceleration("Trbvh"));
	context["top_object"]->set(geometry_group);
}

static void calculateCameraVariables(float3 eye, float3 lookat, float3 up,
	float  fov, float  aspect_ratio,
	float3& U, float3& V, float3& W, bool fov_is_vertical)
{
	float ulen, vlen, wlen;
	W = lookat - eye; // Do not normalize W -- it implies focal length

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

static void updateCamera(int currentframe)
{
	const float fov = 35.0f;
	const float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);

	float3 camera_u, camera_v, camera_w;
	calculateCameraVariables(
		camera_eye, camera_lookat, camera_up, fov, aspect_ratio,
		camera_u, camera_v, camera_w, /*fov_is_vertical*/ true);

	const Matrix4x4 frame = Matrix4x4::fromBasis(
		normalize(camera_u),
		normalize(camera_v),
		normalize(-camera_w),
		camera_lookat);
	const Matrix4x4 frame_inv = frame.inverse();
	// Apply camera rotation twice to match old SDK behavior
	const Matrix4x4 trans = frame*camera_rotate*camera_rotate*frame_inv;

	camera_eye = make_float3(trans*make_float4(camera_eye, 1.0f));
	camera_lookat = make_float3(trans*make_float4(camera_lookat, 1.0f));
	camera_up = make_float3(trans*make_float4(camera_up, 0.0f));

	calculateCameraVariables(
		camera_eye, camera_lookat, camera_up, fov, aspect_ratio,
		camera_u, camera_v, camera_w, true);

	camera_rotate = Matrix4x4::identity();

	context["frame_number"]->setUint(currentframe);
	context["eye"]->setFloat(camera_eye);
	context["U"]->setFloat(camera_u);
	context["V"]->setFloat(camera_v);
	context["W"]->setFloat(camera_w);

	//if (camera_changed) // reset accumulation
	//	frame_number = 1;
	camera_changed = false;
}


static void start_optix_export(
	const std::string& directory_path,
	int export_mode)
{
	export_directory = directory_path;
	createContext(800, 600);
	setupCamera();
	loadGeometry();
	context->validate();
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

static void execute_optix_export(int currentframe)
{
	if (!context) return;

	updateCamera(currentframe);
	context->launch(0, width, height);

	const BridgeParameter& parameter = BridgeParameter::instance();
	std::string file = umbase::UMStringUtil::wstring_to_utf8(parameter.base_path) + ("out\\frame_")
		+ umbase::UMStringUtil::number_to_string(currentframe) + ".png";
	saveImage(file.c_str(), getOutputBuffer()->get());
}

static void end_optix_export()
{
}

void DisposeOptix()
{
	if (context)
	{
		context->destroy();
		context = 0;
	}
}

// ---------------------------------------------------------------------------
BOOST_PYTHON_MODULE(mmdbridge_optix)
{
	using namespace boost::python;
	def("start_optix_export", start_optix_export);
	def("execute_optix_export", execute_optix_export);
	def("end_optix_export", DisposeOptix);
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

