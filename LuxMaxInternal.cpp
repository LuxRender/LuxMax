/***************************************************************************
* Copyright 1998-2015 by authors (see AUTHORS.txt)                        *
*                                                                         *
*   This file is part of LuxRender.                                       *
*                                                                         *
* Licensed under the Apache License, Version 2.0 (the "License");         *
* you may not use this file except in compliance with the License.        *
* You may obtain a copy of the License at                                 *
*                                                                         *
*     http://www.apache.org/licenses/LICENSE-2.0                          *
*                                                                         *
* Unless required by applicable law or agreed to in writing, software     *
* distributed under the License is distributed on an "AS IS" BASIS,       *
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*
* See the License for the specific language governing permissions and     *
* limitations under the License.                                          *
***************************************************************************/

#define LUXCORE_MATTE_CLASSID Class_ID(0x98265f22, 0x2cf529dd)
#define CAMERAHELPER_CLASSID Class_ID(4128,0)

#include "LuxMaxInternalpch.h"
#include "resource.h"
#include "LuxMaxInternal.h"
#include <maxscript\maxscript.h>
#include <render.h>
#include <point3.h>
#include <Path.h>
#include <bitmap.h>
#include <GraphicsWindow.h>
#include <IColorCorrectionMgr.h>
#include <IGame\IGame.h>

#include <string>
#include <string.h>
#include <iostream>
#include <boost/foreach.hpp>
#include <boost/assign.hpp>
#include <boost/format.hpp>

namespace luxcore
{
#include <luxcore/luxcore.h>
}
#include <boost/filesystem/operations.hpp>
#include <mesh.h>
#include <locale>
#include <sstream>

#pragma warning (push)
#pragma warning( disable:4002)
#pragma warning (pop)

using namespace std;
using namespace luxcore;
using namespace luxrays;

extern BOOL FileExists(const TCHAR *filename);
float* pixels;

std::string ToNarrow(const wchar_t *s, char dfault = '?',
	const std::locale& loc = std::locale())
{
	std::ostringstream stm;

	while (*s != L'\0') {
		stm << std::use_facet< std::ctype<wchar_t> >(loc).narrow(*s++, dfault);
	}
	return stm.str();
}

class LuxMaxInternalClassDesc :public ClassDesc {
public:
	int 			IsPublic() { return 1; }
	void *			Create(BOOL loading) { return new LuxMaxInternal; }
	const TCHAR *	ClassName() { return GetString(IDS_VRENDTITLE); }
	SClass_ID		SuperClassID() { return RENDERER_CLASS_ID; }
	Class_ID 		ClassID() { return REND_CLASS_ID; }
	const TCHAR* 	Category() { return _T(""); }
	void			ResetClassParams(BOOL fileReset) {}
};

static LuxMaxInternalClassDesc srendCD;

ClassDesc* GetRendDesc() {
	return &srendCD;
}

RefResult LuxMaxInternal::NotifyRefChanged(
	const Interval		&changeInt,
	RefTargetHandle		 hTarget,
	PartID				&partID,
	RefMessage			 message,
	BOOL				 propagate
	)
{
	return REF_SUCCEED;
}

::Matrix3 camPos;

int LuxMaxInternal::Open(
	INode *scene,     	// root node of scene to render
	INode *vnode,     	// view node (camera or light), or NULL
	ViewParams *viewPar,// view params for rendering ortho or user viewport
	RendParams &rp,  	// common renderer parameters
	HWND hwnd, 				// owner window, for messages
	DefaultLight* defaultLights, // Array of default lights if none in scene
	int numDefLights,	// number of lights in defaultLights array
	RendProgressCallback* prog
	)
{
	viewNode = vnode;
	camPos = viewPar->affineTM;

	return 1;
}

static void CreateBox(luxcore::Scene *scene, const string &objName, const string &meshName,
	const string &matName, const bool enableUV, const BBox &bbox) {
	Point *p = new Point[24];
	// Bottom face
	p[0] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMin.z);
	p[1] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMin.z);
	p[2] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMin.z);
	p[3] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMin.z);
	// Top face
	p[4] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMax.z);
	p[5] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMax.z);
	p[6] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMax.z);
	p[7] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMax.z);
	// Side left
	p[8] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMin.z);
	p[9] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMax.z);
	p[10] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMax.z);
	p[11] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMin.z);
	// Side right
	p[12] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMin.z);
	p[13] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMin.z);
	p[14] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMax.z);
	p[15] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMax.z);
	// Side back
	p[16] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMin.z);
	p[17] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMin.z);
	p[18] = Point(bbox.pMax.x, bbox.pMin.y, bbox.pMax.z);
	p[19] = Point(bbox.pMin.x, bbox.pMin.y, bbox.pMax.z);
	// Side front
	p[20] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMin.z);
	p[21] = Point(bbox.pMin.x, bbox.pMax.y, bbox.pMax.z);
	p[22] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMax.z);
	p[23] = Point(bbox.pMax.x, bbox.pMax.y, bbox.pMin.z);

	Triangle *vi = new Triangle[12];
	// Bottom face
	vi[0] = Triangle(0, 1, 2);
	vi[1] = Triangle(2, 3, 0);
	// Top face
	vi[2] = Triangle(4, 5, 6);
	vi[3] = Triangle(6, 7, 4);
	// Side left
	vi[4] = Triangle(8, 9, 10);
	vi[5] = Triangle(10, 11, 8);
	// Side right
	vi[6] = Triangle(12, 13, 14);
	vi[7] = Triangle(14, 15, 12);
	// Side back
	vi[8] = Triangle(16, 17, 18);
	vi[9] = Triangle(18, 19, 16);
	// Side back
	vi[10] = Triangle(20, 21, 22);
	vi[11] = Triangle(22, 23, 20);

	// Define the Mesh
	if (!enableUV) {
		// Define the object
		scene->DefineMesh(meshName, 24, 12, p, vi, NULL, NULL, NULL, NULL);
	}
	else {
		UV *uv = new UV[24];
		// Bottom face
		uv[0] = UV(0.f, 0.f);
		uv[1] = UV(1.f, 0.f);
		uv[2] = UV(1.f, 1.f);
		uv[3] = UV(0.f, 1.f);
		// Top face
		uv[4] = UV(0.f, 0.f);
		uv[5] = UV(1.f, 0.f);
		uv[6] = UV(1.f, 1.f);
		uv[7] = UV(0.f, 1.f);
		// Side left
		uv[8] = UV(0.f, 0.f);
		uv[9] = UV(1.f, 0.f);
		uv[10] = UV(1.f, 1.f);
		uv[11] = UV(0.f, 1.f);
		// Side right
		uv[12] = UV(0.f, 0.f);
		uv[13] = UV(1.f, 0.f);
		uv[14] = UV(1.f, 1.f);
		uv[15] = UV(0.f, 1.f);
		// Side back
		uv[16] = UV(0.f, 0.f);
		uv[17] = UV(1.f, 0.f);
		uv[18] = UV(1.f, 1.f);
		uv[19] = UV(0.f, 1.f);
		// Side front
		uv[20] = UV(0.f, 0.f);
		uv[21] = UV(1.f, 0.f);
		uv[22] = UV(1.f, 1.f);
		uv[23] = UV(0.f, 1.f);

		// Define the object
		scene->DefineMesh(meshName, 24, 12, p, vi, NULL, uv, NULL, NULL);
	}

	// Add the object to the scene
	Properties props;
	props.SetFromString(
		"scene.objects." + objName + ".ply = " + meshName + "\n"
		"scene.objects." + objName + ".material = " + matName + "\n"
		);
	scene->Parse(props);
}

static void DoRendering(RenderSession *session) {
	const u_int haltTime = session->GetRenderConfig().GetProperties().Get(Property("batch.halttime")(0)).Get<u_int>();
	const u_int haltSpp = session->GetRenderConfig().GetProperties().Get(Property("batch.haltspp")(0)).Get<u_int>();
	const float haltThreshold = session->GetRenderConfig().GetProperties().Get(Property("batch.haltthreshold")(-1.f)).Get<float>();

	char buf[512];
	const Properties &stats = session->GetStats();
	for (;;) {
		boost::this_thread::sleep(boost::posix_time::millisec(1000));

		session->UpdateStats();
		const double elapsedTime = stats.Get("stats.renderengine.time").Get<double>();
		if ((haltTime > 0) && (elapsedTime >= haltTime))
			break;

		const u_int pass = stats.Get("stats.renderengine.pass").Get<u_int>();
		if ((haltSpp > 0) && (pass >= haltSpp))
			break;

		// Convergence test is update inside UpdateFilm()
		const float convergence = stats.Get("stats.renderengine.convergence").Get<u_int>();
		if ((haltThreshold >= 0.f) && (1.f - convergence <= haltThreshold))
			break;

		// Print some information about the rendering progress
		sprintf(buf, "[Elapsed time: %3d/%dsec][Samples %4d/%d][Convergence %f%%][Avg. samples/sec % 3.2fM on %.1fK tris]",
			int(elapsedTime), int(haltTime), pass, haltSpp, 100.f * convergence,
			stats.Get("stats.renderengine.total.samplesec").Get<double>() / 1000000.0,
			stats.Get("stats.dataset.trianglecount").Get<double>() / 1000.0);
		mprintf(_T("Elapsed time %i\n"), int(elapsedTime));
		SLG_LOG(buf);
	}

	pixels = new float[921600]();

	session->GetFilm().GetOutput(session->GetFilm().OUTPUT_RGB_TONEMAPPED, pixels, 0);
	session->GetFilm().Save();
}

Point3 GetVertexNormal(::Mesh* mesh, int faceNo, RVertex* rv)
{
	Face* f = &mesh->faces[faceNo];
	DWORD smGroup = f->smGroup;
	int numNormals;
	Point3 vertexNormal;

	if (rv->rFlags & SPECIFIED_NORMAL)
	{
		vertexNormal = rv->rn.getNormal();
	}
	else if ((numNormals = rv->rFlags & NORCT_MASK))// && smGroup)
	{
		if (numNormals == 1)
		{
			vertexNormal = rv->rn.getNormal();
		}
		else
		{
			for (int i = 0; i < numNormals; i++)
			{
				if (rv->ern[i].getSmGroup() )//& smGroup)
				{
					vertexNormal = rv->ern[i].getNormal();
				}
			}
		}
//#pragma warning(pop)
	}
	else
	{
		vertexNormal = mesh->getFaceNormal(faceNo);
		//mprintf(_T("Got face normal instead of vertex normal for face %i\n"), faceNo);
	}

	return vertexNormal;
}


int LuxMaxInternal::Render(
	TimeValue t,   			// frame to render.
	Bitmap *tobm, 			// optional target bitmap
	FrameRendParams &frp,	// Time dependent parameters
	HWND hwnd, 				// owner window
	RendProgressCallback *prog,
	ViewParams *vp
	)
{
	using namespace std;
	using namespace luxrays;
	using namespace luxcore;

	mprintf(_T("\nRendering with Luxcore version: %s,%s \n"), LUXCORE_VERSION_MAJOR, LUXCORE_VERSION_MINOR);
	int frameNum = t / GetTicksPerFrame();
	mprintf(_T("\nRendering Frame: %i \n"), frameNum);

	Scene *scene = new Scene();
//	GraphicsWindow *gwd;

	// Define texture maps
	const u_int size = 500;
	float *img = new float[size * size * 3];
	float *ptr = img;
	for (u_int y = 0; y < size; ++y) {
		for (u_int x = 0; x < size; ++x) {
			if ((x % 50 < 25) ^ (y % 50 < 25)) {
				*ptr++ = 1.f;
				*ptr++ = 0.f;
				*ptr++ = 0.f;
			}
			else {
				*ptr++ = 1.f;
				*ptr++ = 1.f;
				*ptr++ = 0.f;
			}
		}
	}

	scene->DefineImageMap("check_texmap", img, 1.f, 3, size, size);
	scene->Parse(
		Property("scene.textures.map.type")("imagemap") <<
		Property("scene.textures.map.file")("check_texmap") <<
		Property("scene.textures.map.gamma")(1.f)
		);

	// Setup materials
	scene->Parse(
		Property("scene.materials.whitelight.type")("matte") <<
		Property("scene.materials.whitelight.emission")(1000000.f, 1000000.f, 1000000.f) <<
		Property("scene.materials.mat_white.type")("matte") <<
		Property("scene.materials.mat_white.kd")("map") <<
		Property("scene.materials.mat_red.type")("matte") <<
		Property("scene.materials.mat_red.kd")(0.75f, 0.f, 0.f) <<
		Property("scene.materials.mat_glass.type")("glass") <<
		Property("scene.materials.mat_glass.kr")(0.9f, 0.9f, 0.9f) <<
		Property("scene.materials.mat_glass.kt")(0.9f, 0.9f, 0.9f) <<
		Property("scene.materials.mat_glass.exteriorior")(1.f) <<
		Property("scene.materials.mat_glass.interiorior")(1.4f) <<
		Property("scene.materials.mat_gold.type")("metal2") <<
		Property("scene.materials.mat_gold.preset")("gold") //<<
		//		Property("scene.materials.tmpMat.type")("matte")
		);

	//Export all meshes
	INode* maxscene = GetCOREInterface7()->GetRootNode();
	for (int a = 0; maxscene->NumChildren() > a; a++)
	{
		INode* currNode = maxscene->GetChildNode(a);
		Object*	obj;
		ObjectState os = currNode->EvalWorldState(GetCOREInterface()->GetTime());
		obj = os.obj;
		bool doExport = true;
		switch (os.obj->SuperClassID())
		{
			//If we find a helper object - then we skip it (Sky\light helpers for example, target objects etc).
		case HELPER_CLASS_ID:
		{
								doExport = false;
								break;
		}
			//bool foundCamera = false;
		case CAMERA_CLASS_ID:
		{
								::Point3 camTrans = currNode->GetNodeTM(t).GetTrans();
								CameraObject*   cameraPtr = (CameraObject *)os.obj;
								INode* camNode = GetCOREInterface9()->GetActiveViewExp().GetViewCamera();

								if (camNode == NULL)
								{
									mprintf(L"ERROR: Set active view to a camera and try again.\n");
									return false;
									break;
								}
								else

									Interface* g_ip = GetCOREInterface();
								INode* NewCam = camNode;
								::Matrix3 targetPos;
								NewCam->GetTargetTM(t, targetPos);
								mprintf(L"Rendering with camera: : %s\n", camNode->GetName());
								scene->Parse(
									Property("scene.camera.lookat.orig")(camTrans.x, camTrans.y, camTrans.z) <<
									Property("scene.camera.lookat.target")(targetPos.GetTrans().x, targetPos.GetTrans().y, targetPos.GetTrans().z) <<
									Property("scene.camera.fieldofview")(45)

									);
								//mprintf(L"Setting FOV to: %f\n", cameraPtr->GetFOV(t,FOREVER));
								//	foundCamera = true;
								break;
		}
		

		case GEOMOBJECT_CLASS_ID:
			

			if (doExport)
			{

					Object *pObj = currNode->GetObjectRef();
					IDerivedObject *pDerObj;
					Modifier *Mod;
					Matrix3 nodeInitTM;
					Point4 nodeRotation;

					TriObject *p_triobj = NULL;

					BOOL fConvertedToTriObject = obj->CanConvertToType(triObjectClassID) && (p_triobj = (TriObject*)obj->ConvertToType(0, triObjectClassID)) != NULL;
					if (!fConvertedToTriObject)
					{
						mprintf(L"Error: Could not triangulate object : %s\n", currNode->GetName());
						break;
						//return false;
					}

					//use the ::Mesh to get the 'base class's' mesh class (3dsmax SDK)
					//If you do not do this then it conflicts with Luxrays's mesh class.

					::Mesh *p_trimesh = &p_triobj->mesh;
					int faceCount = p_trimesh->getNumFaces();
					int numUvs = p_trimesh->getNumTVerts();

					//Create buffers for holding the mesh data.
					Point *p = new Point[faceCount * 3];
					Triangle *vi = new Triangle[faceCount];
					Normal *n = new Normal[faceCount * 3];

					bool enableUV = false;
					if (enableUV)
					{
						UV *uv = NULL;
						if (numUvs > 0)
						{
							uv = new UV[numUvs];
						}
					}


					p_trimesh->checkNormals(true);
					//p_trimesh->buildNormals();
					
					Point3 normal;
					int counter = 0;

					for (int f = 0; f < p_trimesh->getNumFaces(); ++f)
					{
						Point3 normal;
						Face* face = &p_trimesh->faces[f];

						for (int v = 0; v < 3; ++v)
						{
							DWORD vi = face->v[v];
							Point3 normal;
							if (p_trimesh->getRVertPtr(vi))
								normal = GetVertexNormal(p_trimesh, f, p_trimesh->getRVertPtr(vi));
							else
								normal = Point3(0, 0, 1);
							
							n[counter].x = normal.x;
							n[counter].y = normal.y;
							n[counter].z = normal.z;
							
							counter++;
						}
					}

					int vindex = 0;
					for (int f = 0; f < p_trimesh->getNumFaces(); ++f)
					{
						Face* face = &p_trimesh->faces[f];
						for (int v = 0; v < 3; ++v)
						{
							DWORD vi = face->v[v];
							Point3 vPos = p_trimesh->verts[vi];
							p[vindex] = Point(vPos * currNode->GetObjectTM(GetCOREInterface()->GetTime()));
							vindex += 1;
						}
					}

					for (int f = 0; f < p_trimesh->getNumFaces(); ++f)
					{
						Face* face = &p_trimesh->faces[f];
						vi[f] = Triangle(f * 3 + 0, f * 3 + 1, f * 3 + 2);
					}

					if (!enableUV) {
						// Define the object - for now without UV and no normals.
						scene->DefineMesh(ToNarrow(currNode->GetName()), p_trimesh->getNumVerts(), p_trimesh->getNumFaces(), p, vi, n, NULL, NULL, NULL);
					}

					p = NULL;
					vi = NULL;
					n = NULL;

					Properties props;
					std::string objString;

					objString = "scene.objects.";
					objString.append(ToNarrow(currNode->GetName()));
					objString.append(".ply = ");
					objString.append(ToNarrow(currNode->GetName()));
					objString.append("\n");
					props.SetFromString(objString);
					objString = "";

					Mtl *objmat = NULL;
					objmat = currNode->GetMtl();
					if (objmat != NULL)
					{
						int numsubs = 0;
						numsubs = objmat->NumSubMtls();

						for (int f = 0; f < numsubs; ++f)
						{
							if (objmat->ClassID() == LUXCORE_MATTE_CLASSID)
							{
								objString.append("scene.materials.");
								objString.append(currNode->GetMtl()->GetName().ToCStr());
								objString.append(".type");

								scene->Parse(
									Property(objString)("matte") <<
									Property("")("")
									);
								objString = "";

								scene->Parse(
									Property("scene.materials.tmpMat.type")("matte") <<
									Property("")("")
									);

								mprintf(L"Exporting out material Luxcore matte,named: %s\n", currNode->GetMtl()->GetName());
								//mprintf(L"Num Param blocks in material: %i\n", objmat->NumParamBlocks());
								for (int i = 0, count = objmat->NumParamBlocks(); i < count; ++i)
								{
									IParamBlock2 *pBlock = objmat->GetParamBlock(i);

									::Point3 diffcol;
									diffcol = pBlock->GetPoint3(3, GetCOREInterface()->GetTime(), 0);

									mprintf(L"Setting material diffuse RGB: %f %f %f\n", diffcol.x, diffcol.y, diffcol.z);
									::std::string tmpMatStr;
									tmpMatStr.append("scene.materials.");
									tmpMatStr.append(ToNarrow(currNode->GetMtl()->GetName()));
									tmpMatStr.append(".kd");
									mprintf(L"Material kd string: %s\n", tmpMatStr.c_str());
									scene->Parse(
										Property(tmpMatStr)(float(diffcol.x), float(diffcol.y), float(diffcol.z)) <<
										Property("")("")

										);
									tmpMatStr = "";

									scene->Parse(
										Property("scene.materials.tmpMat.type")("matte") <<
										Property("scene.materials.tmpMat.kd")(float(diffcol.x), float(diffcol.y), float(diffcol.z))
										);
								}
							}
						}
					}

					objString = "";
					objString.append("scene.objects.");
					objString.append(ToNarrow(currNode->GetName()));
					objString.append(".material = ");
					objString.append(ToNarrow(currNode->GetMtl()->GetName()));
					props.SetFromString(objString);

					scene->Parse(props);
					//delete p, vi;// , uv;
			
			}
		}
	}

	// Create a SkyLight & SunLight
	scene->Parse(
		Property("scene.lights.skyl.type")("sky") <<
		Property("scene.lights.skyl.dir")(0.166974f, 0.59908f, 0.783085f) <<
		Property("scene.lights.skyl.turbidity")(2.2f) <<
		Property("scene.lights.skyl.gain")(0.8f, 0.8f, 0.8f) <<
		Property("scene.lights.sunl.type")("sun") <<
		Property("scene.lights.sunl.dir")(0.166974f, 0.59908f, 0.783085f) <<
		Property("scene.lights.sunl.turbidity")(2.2f) <<
		Property("scene.lights.sunl.gain")(0.8f, 0.8f, 0.8f)
		);

	std::string tmpFilename = FileName.ToCStr();
	int halttime = (int)_wtof(halttimewstr);
	if (tmpFilename != NULL)
	{
		mprintf(_T("\nRendering to: %s \n"), FileName.ToMSTR());
	}

	RenderConfig *config = new RenderConfig(
		Property("renderengine.type")("PATHCPU") <<
		Property("sampler.type")("RANDOM") <<
		Property("opencl.platform.index")(-1) <<
		Property("opencl.cpu.use")(false) <<
		Property("opencl.gpu.use")(true) <<
		Property("batch.halttime")(halttime) <<
		Property("film.outputs.1.type")("RGB_TONEMAPPED") <<
		Property("film.outputs.1.filename")(tmpFilename) <<
		Property("film.imagepipeline.0.type")("TONEMAP_AUTOLINEAR") <<
		Property("film.imagepipeline.1.type")("GAMMA_CORRECTION") <<
		Property("film.imagepipeline.1.value")(1.0f),
		scene);
	RenderSession *session = new RenderSession(config);

	session->Start();
	DoRendering(session);
	session->Stop();

	int i = 0;

	BMM_Color_64 col64;
	col64.r = 0;
	col64.g = 0;
	col64.b = 0;
	//fill in the pixels
	for (int w = 480; w > 0; w--)
	{
		for (int h = 0; h < 640; h++)
		{
			col64.r = (WORD)floorf(pixels[i] * 65535.f + .5f);
			col64.g = (WORD)floorf(pixels[i + 1] * 65535.f + .5f);
			col64.b = (WORD)floorf(pixels[i + 2] * 65535.f + .5f);

			tobm->PutPixels(h, w, 1, &col64);

			i += 3;
		}
	}
	tobm->RefreshWindow(NULL);

	pixels = NULL;
	delete session;
	delete config;
	delete scene;

	SLG_LOG("Done.");

	return 1;
}

void LuxMaxInternal::Close(HWND hwnd, RendProgressCallback* prog) {
	if (file)
		delete file;
	file = NULL;
}

RefTargetHandle LuxMaxInternal::Clone(RemapDir &remap) {
	LuxMaxInternal *newRend = new LuxMaxInternal;
	newRend->FileName = FileName;
	BaseClone(this, newRend, remap);
	return newRend;
}

void LuxMaxInternal::ResetParams(){
	FileName.Resize(0);
}

#define FILENAME_CHUNKID 001
#define HALTTIME_CHUNKID 002

IOResult LuxMaxInternal::Save(ISave *isave) {
	if (_tcslen(FileName) > 0) {
		isave->BeginChunk(FILENAME_CHUNKID);
		isave->WriteWString(FileName);
		isave->EndChunk();
	}

	isave->BeginChunk(HALTTIME_CHUNKID);
	isave->WriteWString(halttimewstr);
	isave->EndChunk();
	return IO_OK;
}

IOResult LuxMaxInternal::Load(ILoad *iload) {
	int id;
	TCHAR *buf;
	IOResult res;
	while (IO_OK == (res = iload->OpenChunk())) {
		switch (id = iload->CurChunkID())  {
		case FILENAME_CHUNKID:
			if (IO_OK == iload->ReadWStringChunk(&buf))
				FileName = buf;
			break;
		case HALTTIME_CHUNKID:
			if (IO_OK == iload->ReadWStringChunk(&buf))
				halttimewstr = buf;
			break;
		}
		iload->CloseChunk();
		if (res != IO_OK)
			return res;
	}
	return IO_OK;
}