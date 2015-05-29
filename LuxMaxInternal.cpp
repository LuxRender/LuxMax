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
#define LR_INTERNAL_MATTE_CLASSID Class_ID(334255,416532)
#define OMNI_CLASSID Class_ID(4113, 0)
#define SPOTLIGHT_CLASSID Class_ID(4114,0)
#define STANDARDMATERIAL_CLASSID Class_ID(2,0)

#include "LuxMaxInternalpch.h"
#include "resource.h"
#include "LuxMaxInternal.h"
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

int renderWidth = 0;
int renderHeight = 0;

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

::std::string getstring(const wchar_t* wstr)
{
	std::wstring ws(wstr);
	std::string str(ws.begin(), ws.end());
	return str;
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

	int pixelArraySize = renderWidth * renderHeight * 3;

	pixels = new float[pixelArraySize]();

	session->GetFilm().GetOutput(session->GetFilm().OUTPUT_RGB_TONEMAPPED, pixels, 0);
	session->GetFilm().Save();
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

bool replace(std::string& str, const std::string& from, const std::string& to) {
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, from.length(), to);
	return true;
}

std::string removeUnwatedChars(std::string& str)
{
	str.erase(remove_if(str.begin(), str.end(), isspace), str.end());
	replace(str, "#", "_");
	replace(str, ".", "_");
	replace(str, ",", "_");
	replace(str, """", "_");
	replace(str, "�", "_");
	replace(str, "%25", "_");
	replace(str, "&", "_");
	replace(str, "/", "_");
	replace(str, "(", "_");
	replace(str, ")", "_");
	replace(str, "=", "_");
	replace(str, "?", "_");
	replace(str, "+", "_");
	replace(str, "\\", "_");
	replace(str, "`", "_");
	replace(str, "^", "_");
	replace(str, "�", "_");
	replace(str, "|", "_");
	replace(str, "*", "_");
	replace(str, "'", "_");
	replace(str, ";", "_");
	replace(str, ":", "_");

	return str;
}

Properties exportOmni(INode* Omni)
{
	::Point3 trans = Omni->GetNodeTM(GetCOREInterface11()->GetTime()).GetTrans();
	::Point3 color;

	Properties props;
	std::string objString;

	objString.append("scene.lights.");
	objString.append(ToNarrow(Omni->GetName()));
	objString.append(".type = point");
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(Omni->GetName()));
	objString.append(".position = ");
	objString.append(::to_string(trans.x) + " " + ::to_string(trans.y) + " " + ::to_string(trans.z));
	objString.append("\n");

	ObjectState ostate = Omni->EvalWorldState(0);
	LightObject *light = (LightObject*)ostate.obj;
	color = light->GetRGBColor(GetCOREInterface()->GetTime(), FOREVER);

	objString.append("scene.lights.");
	objString.append(ToNarrow(Omni->GetName()));
	objString.append(".color = ");
	objString.append(::to_string(color.x / 255) + " " + ::to_string(color.y / 255) + " " + ::to_string(color.z / 255));
	objString.append("\n");
	props.SetFromString(objString);

	objString = "";
	return props;
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
		for (int v = 0; v < 3; ++v)
		{
			vertex& rv = rawverts[i++];
			rv.index = faces->v[v];
			rv.p = verts[faces->v[v]];
			rv.n = fnormals[v];
			rv.uv = uvverts[tvfaces->t[v]];
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

bool isSupportedMaterial(::Mtl* mat)
{
	if (mat->ClassID() == LR_INTERNAL_MATTE_CLASSID)
	{
		return true;
	}
	if (mat->ClassID() == STANDARDMATERIAL_CLASSID)
	{
		return true;
	}
	
	return false;
}

::Point3 getMaterialDiffuseColor(::Mtl* mat)
{
	std::string objString;
	::Point3 diffcolor;

	if (mat->ClassID() == LR_INTERNAL_MATTE_CLASSID)
	{
		for (int i = 0, count = mat->NumParamBlocks(); i < count; ++i)
		{
			IParamBlock2 *pBlock = mat->GetParamBlock(i);
			diffcolor = pBlock->GetPoint3(0, GetCOREInterface()->GetTime(), 0);
		}
	}
	if (mat->ClassID() == STANDARDMATERIAL_CLASSID)
	{
		diffcolor = mat->GetDiffuse(0);
	}

	return diffcolor;
}

//new code for meshes
Properties exportSpotLight(INode* SpotLight)
{
	Properties props;
	std::string objString;
	::Point3 trans = SpotLight->GetNodeTM(GetCOREInterface11()->GetTime()).GetTrans();
	::Matrix3 targetPos;

	ObjectState os = SpotLight->EvalWorldState(GetCOREInterface()->GetTime());
	LightObject *light = (LightObject*)os.obj;

	::Point3 color;
	color = light->GetRGBColor(GetCOREInterface()->GetTime(), FOREVER);

	SpotLight->GetTargetTM(GetCOREInterface11()->GetTime(), targetPos);
	trans = SpotLight->GetNodeTM(GetCOREInterface11()->GetTime(), 0).GetTrans();

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".type = spot");
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".position = ");
	objString.append(::to_string(trans.x) + " " + ::to_string(trans.y) + " " + ::to_string(trans.z));
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".target = ");
	objString.append(::to_string(targetPos.GetTrans().x) + " " + ::to_string(targetPos.GetTrans().y) + " " + ::to_string(targetPos.GetTrans().z));
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".coneangle = ");
	objString.append(::to_string(light->GetHotspot(GetCOREInterface11()->GetTime(), FOREVER)));
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".conedeltaangle = ");
	objString.append(to_string(light->GetFallsize(GetCOREInterface11()->GetTime(), FOREVER))); //* 180 / 3.14159265));
	objString.append("\n");

	objString.append("scene.lights.");
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".gain = ");
	float gainval = light->GetIntensity(GetCOREInterface()->GetTime(), FOREVER);
	objString.append(::to_string(gainval * color.x) + " " + ::to_string(gainval * color.y) + " " + ::to_string(gainval * color.z));
	objString.append("\n");

	props.SetFromString(objString);
	return props;
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

	mprintf(_T("\nRendering with Luxcore version: %s,%s \n"), LUXCORE_VERSION_MAJOR, LUXCORE_VERSION_MINOR);
	int frameNum = t / GetTicksPerFrame();
	mprintf(_T("\nRendering Frame: %i \n"), frameNum);

	Scene *scene = new Scene();

	//Export all meshes
	INode* maxscene = GetCOREInterface7()->GetRootNode();
	for (int a = 0; maxscene->NumChildren() > a; a++)
	{
		INode* currNode = maxscene->GetChildNode(a);

		renderProgTitle = (L"Translating object: %s", currNode->GetName());
		prog->SetTitle(renderProgTitle);
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

			if (os.obj->ClassID() == OMNI_CLASSID)
			{
				scene->Parse(exportOmni(currNode));
			}

			if (os.obj->ClassID() == SPOTLIGHT_CLASSID)
			{
				scene->Parse(exportSpotLight(currNode));
			}

			break;
		}

		case CAMERA_CLASS_ID:
		{
			::Point3 camTrans = currNode->GetNodeTM(t).GetTrans();
			CameraObject*   cameraPtr = (CameraObject *)os.obj;
			INode* camNode = GetCOREInterface9()->GetActiveViewExp().GetViewCamera();

			if (camNode == NULL)
			{
				MessageBox(0, L"Set active view to a target camera and render again.", L"Error!", MB_OK);
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
				Property("scene.camera.fieldofview")(cameraPtr->GetFOV(t, FOREVER) * 180 / pi)
				);
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
					std::string tmpName = ToNarrow(currNode->GetName());
					removeUnwatedChars(tmpName);
					std::wstring replacedObjName = std::wstring(tmpName.begin(), tmpName.end());
					objName = replacedObjName.c_str();

					::Mesh *p_trimesh = &p_triobj->mesh;
					p_trimesh->checkNormals(true);
					p_trimesh->buildNormals();

					const wchar_t *matName = L"";
					matName = currNode->GetMtl()->GetName();
					std::string tmpMatName = ToNarrow(matName);
					removeUnwatedChars(tmpMatName);
					std::wstring replacedMaterialName = std::wstring(tmpMatName.begin(), tmpMatName.end());
					matName = replacedMaterialName.c_str();

					int numUvs = p_trimesh->getNumTVerts();

					UV *uv = NULL;

					int rawcount = p_trimesh->numFaces * 3;
					int optcount = 0;
					vertexPtr rawverts = CollectRawVerts(*p_trimesh, rawcount);
					vertexPtr optverts = CreateOptimizeVertexList(rawverts, rawcount, optcount);
					unsigned int* indices = CreateOptimizeFaceIndices(rawverts, rawcount, optverts, optcount);
					int numTriangles = p_trimesh->getNumFaces();

					Point *p = Scene::AllocVerticesBuffer(optcount);
					Triangle *vi = Scene::AllocTrianglesBuffer(numTriangles);
					Normal *n = new Normal[optcount];

					for (int vert = 0; vert < optcount; vert++)
					{
						p[vert] = Point(optverts[vert].p * currNode->GetObjectTM(GetCOREInterface()->GetTime()));
					}

					for (int norm = 0; norm < optcount; norm++)
					{
						::Point3 tmpNorm = optverts[norm].n * currNode->GetObjectTM(GetCOREInterface()->GetTime());
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
						uv = new UV[numUvs];
						for (int u = 0; u < numUvs; u++)
						{
							uv[u].u = p_trimesh->getTVert(u).x;
							uv[u].v = p_trimesh->getTVert(u).y;
						}
					}

					if (numUvs < 1) {
						// Define the object - without UV
						scene->DefineMesh(ToNarrow(objName), optcount, numTriangles, p, vi, n, NULL, NULL, NULL);
					}
					else
					{
						// Define the object - with UV
						scene->DefineMesh(ToNarrow(objName), optcount, numTriangles, p, vi, n, NULL, NULL, NULL);
					}

					delete[] rawverts;
					delete[] optverts;
					delete[] indices;

					p = NULL;
					vi = NULL;
					n = NULL;
					uv = NULL;

					Properties props;
					std::string objString;

					objString = "scene.objects.";
					objString.append(ToNarrow(objName));
					objString.append(".ply = ");
					objString.append(ToNarrow(objName));
					objString.append("\n");
					props.SetFromString(objString);
					objString = "";

					Mtl *objmat = NULL;
					objmat = currNode->GetMtl();
					//if (!scene->IsMaterialDefined(ToNarrow(matName)))
					//{
					if (objmat != NULL)
					{
						int numsubs = 0;
						numsubs = objmat->NumSubMtls();
						if (numsubs < 1)
						{
							numsubs = 1;
						}
						for (int f = 0; f < numsubs; ++f)
						{
							//OutputDebugStringW(objmat->GetFullName());
							if (isSupportedMaterial(objmat))
							{ 
								//if ((objmat->ClassID() == LR_INTERNAL_MATTE_CLASSID))
								//{
									objString.append("scene.materials.");
									objString.append(ToNarrow(matName));
									objString.append(".type");

									scene->Parse(
										Property(objString)("matte") <<
										Property("")("")
										);
									objString = "";

									::Point3 diffcol;
									diffcol = getMaterialDiffuseColor(objmat);

									::std::string tmpMatStr;
									tmpMatStr.append("scene.materials.");
									tmpMatStr.append(ToNarrow(matName));
									tmpMatStr.append(".kd");
									//mprintf(L"Material kd string: %s\n", tmpMatStr.c_str());
									scene->Parse(
										Property(tmpMatStr)(float(diffcol.x), float(diffcol.y), float(diffcol.z)) <<
										Property("")("")
										);
									tmpMatStr = "";
								//}
							}
							//else
							//{
							//	objString.append("scene.materials.");
							//	objString.append(ToNarrow(matName));
							//	objString.append(".type");

							//	scene->Parse(
							//		Property(objString)("matte") <<
							//		Property("")("")
							//		);
							//	objString = "";

							//	::std::string tmpMatStr;
							//	tmpMatStr.append("scene.materials.");
							//	tmpMatStr.append(ToNarrow(matName));
							//	tmpMatStr.append(".kd");
							//	mprintf(L"Creating fallback material for unsupported material: %s\n", matName);
							//	scene->Parse(
							//		Property(tmpMatStr)(float(125), float(125), float(125)) <<
							//		Property("")("")
							//		);
							//	tmpMatStr = "";

							//}
						}
						//	}

						objString = "";
						objString.append("scene.objects.");
						objString.append(ToNarrow(objName));
						objString.append(".material = ");
						objString.append(ToNarrow(matName));
						props.SetFromString(objString);

						scene->Parse(props);
					}
				}
			}
		}
	}

	// Create a SkyLight & SunLight
	//scene->Parse(
	//	Property("scene.lights.skyl.type")("sky") <<
	//	Property("scene.lights.skyl.dir")(0.166974f, 0.59908f, 0.783085f) <<
	//	Property("scene.lights.skyl.turbidity")(2.2f) <<
	//	Property("scene.lights.skyl.gain")(0.8f, 0.8f, 0.8f) <<
	//	Property("scene.lights.sunl.type")("sun") <<
	//	Property("scene.lights.sunl.dir")(0.166974f, 0.59908f, 0.783085f) <<
	//	Property("scene.lights.sunl.turbidity")(2.2f) <<
	//	Property("scene.lights.sunl.gain")(0.8f, 0.8f, 0.8f)
	//	);

	std::string tmpFilename = FileName.ToCStr();
	int halttime = (int)_wtof(halttimewstr);
	if (tmpFilename != NULL)
	{
		mprintf(_T("\nRendering to: %s \n"), FileName.ToMSTR());
	}

	renderWidth = GetCOREInterface11()->GetRendWidth();
	renderHeight = GetCOREInterface11()->GetRendHeight();

	RenderConfig *config = new RenderConfig(
		//filesaver
		//Property("renderengine.type")("FILESAVER") <<
		//Property("filesaver.directory")("C:/tmp/filesaveroutput/") <<
		//Property("filesaver.renderengine.type")("engine") <<
		//Filesaver

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
		Property("film.height")(renderHeight) <<
		Property("film.width")(renderWidth) <<
		Property("film.imagepipeline.1.value")(1.0f),
		scene);
	RenderSession *session = new RenderSession(config);

	session->Start();

	//We need to stop the rendering immidiately if debug output is selsected.

	DoRendering(session);
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