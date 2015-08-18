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

#define CAMERAHELPER_CLASSID Class_ID(4128,0)
#define OMNI_CLASSID Class_ID(4113, 0)
#define SPOTLIGHT_CLASSID Class_ID(4114,0)

#define SKYLIGHT_CLASSID Class_ID(2079724664, 1378764549)
#define DIRLIGHT_CLASSID Class_ID(4115, 0) // free directional light and sun light classid


#include "LuxMaxInternalpch.h"
#include "resource.h"
#include "LuxMaxInternal.h"
//#include "luxlight.h"
#include "LuxMaxUtils.h"
#include "LuxMaxMaterials.h"
#include "LuxMaxLights.h"

#include <maxscript\maxscript.h>
#include <render.h>
#include <point3.h>
#include <MeshNormalSpec.h>
#include <Path.h>
#include <bitmap.h>
#include <GraphicsWindow.h>
#include <IColorCorrectionMgr.h>
#include <IGame\IGame.h>
#include <VertexNormal.h>
#include <string>
#include <string.h>
#include <iostream>


namespace luxcore
{
#include <luxcore/luxcore.h>
}
#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#include <boost/assign.hpp>
#include <boost/format.hpp>
#include <mesh.h>
#include <locale>
#include <sstream>



LuxMaxLights lxmLights;
LuxMaxMaterials lxmMaterials;
LuxMaxUtils lxmUtils;

#pragma warning (push)
#pragma warning( disable:4002)
#pragma warning (pop)

using namespace std;
using namespace luxcore;
using namespace luxrays;

extern BOOL FileExists(const TCHAR *filename);
float* pixels;

bool defaultlightset = true;
int rendertype = 4;
int renderWidth = 0;
int renderHeight = 0;

Scene *scene;


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


static void DoRendering(RenderSession *session, RendProgressCallback *prog, Bitmap *tobm) {
	const u_int haltTime = session->GetRenderConfig().GetProperties().Get(Property("batch.halttime")(0)).Get<u_int>();
	const u_int haltSpp = session->GetRenderConfig().GetProperties().Get(Property("batch.haltspp")(0)).Get<u_int>();
	const float haltThreshold = session->GetRenderConfig().GetProperties().Get(Property("batch.haltthreshold")(-1.f)).Get<float>();
	const wchar_t *state = NULL;

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

		state = (L"Rendering ....");
		prog->SetTitle(state);
		bool renderabort = prog->Progress(elapsedTime+1, haltTime);
		if (renderabort == false)
			break;

		int pixelArraySize = renderWidth * renderHeight * 3;

		pixels = new float[pixelArraySize]();

		session->GetFilm().GetOutput(session->GetFilm().OUTPUT_RGB_TONEMAPPED, pixels, 0);
		session->GetFilm().Save();

		int i = 0;

		BMM_Color_64 col64;
		col64.r = 0;
		col64.g = 0;
		col64.b = 0;
		//fill in the pixels
		for (int w = renderHeight; w > 0; w--)
		{
			for (int h = 0; h < renderWidth; h++)
			{
				col64.r = (WORD)floorf(pixels[i] * 65535.f + .5f);
				col64.g = (WORD)floorf(pixels[i + 1] * 65535.f + .5f);
				col64.b = (WORD)floorf(pixels[i + 2] * 65535.f + .5f);

				tobm->PutPixels(h, w, 1, &col64);

				i += 3;
			}
		}
		tobm->RefreshWindow(NULL);

		SLG_LOG(buf);
	}

	int pixelArraySize = renderWidth * renderHeight * 3;

	pixels = new float[pixelArraySize]();

	session->GetFilm().GetOutput(session->GetFilm().OUTPUT_RGB_TONEMAPPED, pixels, 0);
	session->GetFilm().Save();
}


//New code for meshes
static int hashPoint3(Point3& p) { return (*(int*)&p.x * 73856093) ^ (*(int*)&p.y * 19349663) ^ (*(int*)&p.z * 83492791); }

struct vertex : public MaxHeapOperators
{
	static const int hashTable[];

	Point3 p;
	Point3 n;
	Point3 uv;
	int	   mid;

	unsigned int index;
	int			 hash;

	vertex() : index(0), hash(0) {}

	bool operator==(const vertex& v) const { return hash == v.hash; }
	bool operator!=(const vertex& v) const { return hash != v.hash; }
	bool operator>(const vertex& v) const  { return hash > v.hash; }
	bool operator<(const vertex& v) const  { return hash < v.hash; }

	void hashit()
	{
		hash ^= hashPoint3(p) * hashTable[0];
		hash ^= hashPoint3(n) * hashTable[1];
		hash ^= hashPoint3(uv) * hashTable[2];
		hash ^= mid * hashTable[3];
		hash ^= index * hashTable[4];
	}
};

const int vertex::hashTable[] = { 93944371, 36311839, 82895123, 10033109, 59882063, 42133979, 24823181 };

typedef vertex* vertexPtr;

int CompareVertexFn(const void* i, const void* j)
{
	if (**(vertexPtr*)i < **(vertexPtr*)j)
		return -1;
	if (**(vertexPtr*)i > **(vertexPtr*)j)
		return 1;
	return 0;
}

void GetFaceRNormals(::Mesh& mesh, int fi, Point3* normals)
{
	Face& face = mesh.faces[fi];
	DWORD fsmg = face.getSmGroup();
	if (fsmg == 0)
	{
		normals[0] = normals[1] = normals[2] = mesh.getFaceNormal(fi);
		return;
	}
	MtlID fmtl = face.getMatID();
	DWORD* fverts = face.getAllVerts();
	for (int v = 0; v < 3; ++v)
	{
		RVertex& rvert = mesh.getRVert(fverts[v]);
		int numNormals = (int)(rvert.rFlags & NORCT_MASK);

		if (numNormals == 1)
			normals[v] = rvert.rn.getNormal();
		else
		{
			for (int n = 0; n < numNormals; ++n)
			{
				RNormal& rn = rvert.ern[n];
				if ((fsmg & rn.getSmGroup()) && fmtl == rn.getMtlIndex())
				{
					normals[v] = rn.getNormal();
					break;
				}
			}
		}
	}
}

vertexPtr CollectRawVerts(::Mesh& mesh, int rawcount)
{
	int numfaces = mesh.numFaces;
	vertexPtr rawverts = new vertex[rawcount];
	if (!rawverts) return NULL;

	mesh.checkNormals(TRUE);
	Face* faces = mesh.faces;
	Point3* verts = mesh.verts;
	TVFace* tvfaces = mesh.tvFace;
	Point3* uvverts = mesh.tVerts;

	for (int f = 0, i = 0; f < numfaces; ++f, ++faces, ++tvfaces)
	{
		Point3 fnormals[3];
		GetFaceRNormals(mesh, f, fnormals);
		short mid = faces->getMatID();
		Point2 tmpUv = Point2(0, 0);
		bool hasUvs = true;
		if (mesh.getNumTVerts() < 1)
		{
			hasUvs = false;
		}

		for (int v = 0; v < 3; ++v)
		{
			vertex& rv = rawverts[i++];
			rv.index = faces->v[v];
			rv.p = verts[faces->v[v]];
			rv.n = fnormals[v];
			if (hasUvs)
			{
				rv.uv = uvverts[tvfaces->t[v]];
			}
			else
			{
				rv.uv.x = 0;
				rv.uv.y = 0;
			}
			
			rv.mid = mid;
			rv.hashit();
		}
	}
	return rawverts;
}

vertexPtr CreateOptimizeVertexList(vertexPtr rawverts, int numverts, int& numoutverts)
{
	vertexPtr* vptrs = new vertexPtr[numverts];

	vertexPtr vptr = rawverts;
	for (int i = 0; i < numverts; ++i, ++vptr)
		vptrs[i] = vptr;

	qsort(vptrs, numverts, sizeof(vertexPtr), CompareVertexFn);

	int* copylist = new int[numverts];
	unsigned int cc = 0, ri = 0;
	copylist[cc] = vptrs[ri] - rawverts;
	while (++ri < numverts)
	{
		int index = vptrs[ri] - rawverts;
		if (rawverts[copylist[cc]] != rawverts[index])
			copylist[++cc] = index;
	}
	numoutverts = cc + 1;
	vertexPtr optverts = new vertex[numoutverts];

	for (int i = 0; i < numoutverts; ++i)
		optverts[i] = rawverts[copylist[i]];

	delete[] copylist;
	delete[] vptrs;
	return optverts;
}

unsigned int* CreateOptimizeFaceIndices(vertexPtr raw, int rawcount, vertexPtr opt, int optcount)
{
	vertexPtr* vptrs = new vertexPtr[optcount];
	unsigned int* faces = new unsigned int[rawcount];

	vertexPtr vptr = opt;
	for (int i = 0; i < optcount; ++i, ++vptr)
		vptrs[i] = vptr;

	qsort(vptrs, optcount, sizeof(vertexPtr), CompareVertexFn);

	for (int i = 0; i < rawcount; ++i)
	{
		vertexPtr key = &raw[i];

		// find the correct index of a raw vert in the optimized array

		vertexPtr* result = (vertexPtr*)bsearch(&key, vptrs, optcount, sizeof(vertexPtr), CompareVertexFn);
		if (result)
		{
			faces[i] = *result - opt; // why derefence vertexPtr to get index?
		}
		else
		{
			mprintf(_T("\nError getting the face index for index: %i \n"), i);
		}
	}

	delete[] vptrs;
	return faces;
}

void BuildMesh(::Mesh& mesh, vertexPtr verts, int nverts, unsigned int* faces, int nfaces)
{
	mesh.setNumVerts(nverts);
	mesh.setNumFaces(nfaces);
	mesh.setNumTVerts(nverts);
	mesh.setNumTVFaces(nfaces);

	::MeshNormalSpec* nspec = new ::MeshNormalSpec;
	nspec->SetNumFaces(nfaces);
	nspec->SetNumNormals(nverts);

	for (int v = 0; v < nverts; ++v)
	{
		mesh.setVert(v, verts[v].p);
		mesh.setTVert(v, verts[v].uv);
		nspec->Normal(v) = verts[v].n;
	}
	int fi = 0;
	for (int f = 0; f < nfaces * 3; f += 3)
	{
		int a = faces[f];
		int b = faces[f + 1];
		int c = faces[f + 2];
		mesh.faces[fi].setVerts(a, b, c);
		mesh.tvFace[fi].setTVerts(a, b, c);
		mesh.faces[fi].setEdgeVisFlags(1, 1, 1);
		mesh.faces[fi].setMatID(verts[a].mid);
		nspec->Face(fi).SetNormalID(0, a);
		nspec->Face(fi).SetNormalID(1, b);
		nspec->Face(fi).SetNormalID(2, c);
		fi++;
	}
	nspec->MakeNormalsExplicit(false);

	MeshNormalSpec *meshNormals = (MeshNormalSpec *)mesh.GetInterface(MESH_NORMAL_SPEC_INTERFACE);
	if (meshNormals)
	{
		*meshNormals = *nspec;
		meshNormals->SetParent(&mesh);
	}
	delete nspec;
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

	const wchar_t *renderProgTitle = NULL;
	defaultlightset = true;

	mprintf(_T("\nRendering with Luxcore version: %s,%s \n"), LUXCORE_VERSION_MAJOR, LUXCORE_VERSION_MINOR);
	int frameNum = t / GetTicksPerFrame();
	mprintf(_T("\nRendering Frame: %i \n"), frameNum);

	//Scene *scene = new Scene();
	scene = new Scene();

	//Export all meshes
	INode* maxscene = GetCOREInterface7()->GetRootNode();

	for (int a = 0; maxscene->NumChildren() > a; a++)
	{
		INode* currNode = maxscene->GetChildNode(a);

		//prog->SetCurField(1);
		renderProgTitle = (L"Translating object: %s", currNode->GetName());
		prog->SetTitle(renderProgTitle);
		mprintf(_T("\n Total Rendering elements number: %i"), maxscene->NumChildren());
		mprintf(_T("   ::   Current elements number: %i \n"), a + 1);
		prog->Progress(a+1, maxscene->NumChildren());

		Object*	obj;
		ObjectState os = currNode->EvalWorldState(GetCOREInterface()->GetTime());
		obj = os.obj;
		bool doExport = true;

		switch (os.obj->SuperClassID())
		{
		case HELPER_CLASS_ID:
		{
			doExport = false;
			break;
		}

		case LIGHT_CLASS_ID:
		{
			Properties props;
			std::string objString;
			bool lightsupport = false;

			if (defaultlightchk == true)
			{
				if (defaultlightauto == true)
				{
					defaultlightset = false;
				}
			}
			else
			{
				defaultlightset = false;
				mprintf(_T("\n Default Light Deactive Automaticlly %i \n"));
			}

			if (os.obj->ClassID() == OMNI_CLASSID)
			{
				scene->Parse(lxmLights.exportOmni(currNode));
				lightsupport = true;
			}
			if (os.obj->ClassID() == SPOTLIGHT_CLASSID)
			{
				scene->Parse(lxmLights.exportSpotLight(currNode));
				lightsupport = true;
			}
			if (os.obj->ClassID() == SKYLIGHT_CLASSID)
			{
				scene->Parse(lxmLights.exportSkyLight(currNode));
				lightsupport = true;
			}
			if (os.obj->ClassID() == DIRLIGHT_CLASSID)
			{
				scene->Parse(lxmLights.exportDiright(currNode));
				lightsupport = true;
			}
			if (lightsupport = false)
			{
				if (defaultlightchk == true)
				{
					mprintf(_T("\n There is No Suported light in scene %i \n"));
					defaultlightset = true;
				}
			}

			break;
		}

		case CAMERA_CLASS_ID:
		{
			//::Point3 camTrans = currNode->GetNodeTM(t).GetTrans();
			CameraObject*   cameraPtr = (CameraObject *)os.obj;
			INode* camNode = GetCOREInterface9()->GetActiveViewExp().GetViewCamera();

			if (camNode == NULL)
			{
				MessageBox(0, L"Set active view to a target camera and render again.", L"Error!", MB_OK);
				return false;
				break;
			}
			else
			{
				::Point3 camTrans = camNode->GetNodeTM(GetCOREInterface()->GetTime()).GetTrans();
				Interface* g_ip = GetCOREInterface();
				INode* NewCam = camNode;
				::Matrix3 targetPos;
				NewCam->GetTargetTM(t, targetPos);

				float FOV = cameraPtr->GetFOV(t, FOREVER) * 180 / PI;
				float aspectratio = GetCOREInterface11()->GetImageAspRatio();
				if (aspectratio < 1)
					FOV = 2.0f * ((180 / PI) *(atan(tan((PI / 180)*(FOV / 2.0f)) / aspectratio)));

				float LensRadius = (float)_wtof(LensRadiusstr);
				//float blur = camNode->GetImageBlurMultiplier(t);
				mprintf(L"Rendering with camera: : %s\n", camNode->GetName());
				scene->Parse(
					Property("scene.camera.lookat.orig")(camTrans.x, camTrans.y, camTrans.z) <<
					Property("scene.camera.lookat.target")(targetPos.GetTrans().x, targetPos.GetTrans().y, targetPos.GetTrans().z) <<
					Property("scene.camera.fieldofview")(FOV) <<
					Property("scene.camera.lensradius")(LensRadius) <<
					Property("scene.camera.focaldistance")(cameraPtr->GetTDist(t, FOREVER)) <<
					Property("scene.camera.shutteropen")(0.0f) <<
					Property("scene.camera.shutterclose")(1.615f)
					);
				break;
			}
			break;
		}

		case GEOMOBJECT_CLASS_ID:

			if (doExport)
			{
				Object *pObj = currNode->GetObjectRef();
				Matrix3 nodeInitTM;
				Point4 nodeRotation;
				TriObject *p_triobj = NULL;

				BOOL fConvertedToTriObject = obj->CanConvertToType(triObjectClassID) && (p_triobj = (TriObject*)obj->ConvertToType(0, triObjectClassID)) != NULL;

				if (!fConvertedToTriObject)
				{
					mprintf(L"Debug: Did not triangulate object : %s\n", currNode->GetName());
					break;
				}
				else
				{
					mprintf(L"Info: Creating mesh for object : %s\n", currNode->GetName());
					const wchar_t *objName = L"";
					std::string tmpName = lxmUtils.ToNarrow(currNode->GetName());
					lxmUtils.removeUnwatedChars(tmpName);
					std::wstring replacedObjName = std::wstring(tmpName.begin(), tmpName.end());
					objName = replacedObjName.c_str();

					::Mesh *p_trimesh = &p_triobj->mesh;

					if (p_trimesh->getNumFaces() < 1)
					{
						mprintf(L"Debug: Did not triangulate object : %s, numfaces < 1\n", currNode->GetName());
						break;
					}
					p_trimesh->checkNormals(true);
					p_trimesh->buildNormals();

					int numUvs = p_trimesh->getNumTVerts();
					int rawcount = p_trimesh->numFaces * 3;
					int optcount = 0;

					vertexPtr rawverts = CollectRawVerts(*p_trimesh, rawcount);
					vertexPtr optverts = CreateOptimizeVertexList(rawverts, rawcount, optcount);
					unsigned int* indices = CreateOptimizeFaceIndices(rawverts, rawcount, optverts, optcount);
					int numTriangles = p_trimesh->getNumFaces();

					Point *p = Scene::AllocVerticesBuffer(optcount);
					Triangle *vi = Scene::AllocTrianglesBuffer(numTriangles);
					Normal *n = new Normal[optcount];
					UV *uv = new UV[optcount];

					for (int vert = 0; vert < optcount; vert++)
					{
						p[vert] = Point(optverts[vert].p);
					}

					for (int norm = 0; norm < optcount; norm++)
					{
						::Point3 tmpNorm = optverts[norm].n;
						n[norm].x = tmpNorm.x;
						n[norm].y = tmpNorm.y;
						n[norm].z = tmpNorm.z;
					}

					for (int i = 0, fi = 0; fi < numTriangles; i += 3, ++fi)
					{
						vi[fi] = Triangle(
							int(indices[i]),
							int(indices[i + 1]),
							int(indices[i + 2]));
					}

					if (numUvs > 0)
					{
						//uv = new UV[numUvs];
						for (int u = 0; u < optcount; u++)
						{
							uv[u].u = optverts[u].uv.x;
							uv[u].v = optverts[u].uv.y;
						}
					}

					if (numUvs < 1) {
						// Define the object - without UV
						scene->DefineMesh(lxmUtils.ToNarrow(objName), optcount, numTriangles, p, vi, n, NULL, NULL, NULL);
					}
					else
					{
						// Define the object - with UV
						scene->DefineMesh(lxmUtils.ToNarrow(objName), optcount, numTriangles, p, vi, n, NULL, NULL, NULL);
					}

					delete[] rawverts;
					delete[] optverts;
					delete[] indices;
					delete[] uv;

					p = NULL;
					vi = NULL;
					n = NULL;
					uv = NULL;

					Properties props;
					std::string objString;

					objString = "scene.objects.";
					objString.append(lxmUtils.ToNarrow(objName));
					objString.append(".ply = ");
					objString.append(lxmUtils.ToNarrow(objName));
					objString.append("\n");
					props.SetFromString(objString);
					objString = "";

					Mtl *objmat = NULL;

					if (currNode->GetMtl() == NULL)
					{
						objString.append("scene.materials.undefined");
						objString.append(".type");

						scene->Parse(
							Property(objString)("matte") <<
							Property("")("")
							);
						objString = "";

						::std::string tmpMatStr;
						tmpMatStr.append("scene.materials.undefined.kd");
						mprintf(L"Creating fallback material for undefined material.\n");
						scene->Parse(
							Property(tmpMatStr)(float(0.5), float(0.5), float(0.5)) <<
							Property("")("")
							);
						tmpMatStr = "";

						objString = "";
						objString.append("scene.objects.");
						objString.append(lxmUtils.ToNarrow(objName));
						objString.append(".material = ");
						objString.append("undefined");
						objString.append("\n");
						props.SetFromString(objString);
						scene->Parse(props);
						objString = "";

					}
					else
					{
						const wchar_t *matName = L"";
						matName = currNode->GetMtl()->GetName();
						std::string tmpMatName = lxmUtils.ToNarrow(matName);
						lxmUtils.removeUnwatedChars(tmpMatName);
						std::wstring replacedMaterialName = std::wstring(tmpMatName.begin(), tmpMatName.end());
						matName = replacedMaterialName.c_str();

						objmat = currNode->GetMtl();
						int numsubs = 0;
						numsubs = objmat->NumSubMtls();
						if (numsubs < 1)
						{
							numsubs = 1;
						}
						for (int f = 0; f < numsubs; ++f)
						{
							if (lxmMaterials.isSupportedMaterial(objmat))
							{
								lxmMaterials.exportMaterial(objmat, scene);
							}
							else
							{
								objString.append("scene.materials.");
								objString.append(lxmUtils.ToNarrow(matName));
								objString.append(".type");

								scene->Parse(
									Property(objString)("matte") <<
									Property("")("")
									);
								objString = "";

								::std::string tmpMatStr;
								tmpMatStr.append("scene.materials.");
								tmpMatStr.append(lxmUtils.ToNarrow(matName));
								tmpMatStr.append(".kd");
								mprintf(L"Creating fallback material for unsupported material: %s\n", matName);
								scene->Parse(
									Property(tmpMatStr)(float(0.5), float(0.5), float(0.5)) <<
									Property("")("")
									);
								tmpMatStr = "";
							}
						}

						objString = "";
						objString.append("scene.objects.");
						objString.append(lxmUtils.ToNarrow(objName));
						objString.append(".material = ");
						objString.append(lxmUtils.ToNarrow(matName));
						objString.append("\n");
						props.SetFromString(objString);
						scene->Parse(props);
						objString = "";
					}

					//Set the transformation matrix for the current mesh object.
					//the getMaxNodeTransform function returns the numbers for the matrix.
					//that is why we append it here.
						objString.append("scene.objects.");
						objString.append(lxmUtils.ToNarrow(objName));
						objString.append(".transformation = ");
						objString.append(lxmUtils.getMaxNodeTransform(currNode));
						props.SetFromString(objString);
						scene->Parse(props);
				}
			}
		}
	}

	if (defaultlightchk == true)
	{
		if (defaultlightset == true)
		{
			scene->Parse(
				//Property("scene.lights.infinitelight.type")("infinite") <<
				//Property("scene.lights.infinitelight.file")("C:/Temp/glacier.exr") <<
				//Property("scene.lights.infinitelight.gain")(1.0f, 1.0f, 1.0f)
				Property("scene.lights.skyl.type")("sky") <<
				Property("scene.lights.skyl.dir")(0.166974f, 0.59908f, 0.783085f) <<
				Property("scene.lights.skyl.turbidity")(2.2f) <<
				Property("scene.lights.skyl.gain")(1.0f, 1.0f, 1.0f)
				);
		}
	}

	std::string tmpFilename = FileName.ToCStr();
	int halttime = (int)_wtof(halttimewstr);

	if (tmpFilename != NULL)
	{
		mprintf(_T("\nRendering to: %s \n"), FileName.ToMSTR());
	}

	renderWidth = GetCOREInterface11()->GetRendWidth();
	renderHeight = GetCOREInterface11()->GetRendHeight();

	string tmprendtype = "PATHCPU";
	rendertype = renderType;

	switch (rendertype)
	{
		case 0:
			tmprendtype = "BIASPATHCPU";
			break;
		case 1:
			tmprendtype = "BIASPATHOCL";
			break;
		case 2:
			tmprendtype = "BIDIRCPU";
			break;
		case 3:
			tmprendtype = "BIDIRVMCPU";
			break;
		case 4:
			tmprendtype = "PATHCPU";
			break;
		case 5:
			tmprendtype = "PATHOCL";
			break;
		case 6:
			tmprendtype = "RTBIASPATHOCL";
			break;
		case 7:
			tmprendtype = "RTPATHOCL";
			break;
	}
	mprintf(_T("\n Renderengine type is %i \n"), rendertype);

	RenderConfig *config = new RenderConfig(
		//filesaver
		//Property("renderengine.type")("FILESAVER") <<
		//Property("filesaver.directory")("C:/tmp/filesaveroutput/") <<
		//Property("filesaver.renderengine.type")("engine") <<
		//Filesaver

		Property("renderengine.type")(tmprendtype) <<
		Property("sampler.type")("SOBOL") <<
		//Property("sampler.type")("METROPOLIS") <<
		Property("opencl.platform.index")(-1) <<
		Property("opencl.cpu.use")(false) <<
		Property("opencl.gpu.use")(true) <<
		Property("batch.halttime")(halttime) <<
		Property("film.outputs.1.type")("RGBA_TONEMAPPED") <<
		Property("film.outputs.1.filename")(tmpFilename) <<
		Property("film.imagepipeline.0.type")("TONEMAP_AUTOLINEAR") <<
		Property("film.imagepipeline.1.type")("GAMMA_CORRECTION") <<
		Property("film.height")(renderHeight) <<
		Property("film.width")(renderWidth) <<
		Property("film.imagepipeline.1.value")(1.0f),
		scene);
	RenderSession *session = new RenderSession(config);

	session->Start();

	//We need to stop the rendering immidiately if debug output is selsected.

	DoRendering(session, prog, tobm);
	session->Stop();

	int i = 0;

	BMM_Color_64 col64;
	col64.r = 0;
	col64.g = 0;
	col64.b = 0;
	//fill in the pixels
	for (int w = renderHeight; w > 0; w--)
	{
		for (int h = 0; h < renderWidth; h++)
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
#define LENSRADIUS_CHUNKID 003

IOResult LuxMaxInternal::Save(ISave *isave) {
	if (_tcslen(FileName) > 0) {
		isave->BeginChunk(FILENAME_CHUNKID);
		isave->WriteWString(FileName);
		isave->EndChunk();
	}

	isave->BeginChunk(HALTTIME_CHUNKID);
	isave->WriteWString(halttimewstr);
	isave->EndChunk();
	isave->BeginChunk(LENSRADIUS_CHUNKID);
	isave->WriteWString(LensRadiusstr);
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
		case LENSRADIUS_CHUNKID:
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