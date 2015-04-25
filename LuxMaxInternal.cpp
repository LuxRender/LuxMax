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
	else if ((numNormals = rv->rFlags & NORCT_MASK) && smGroup)
	{
		if (numNormals == 1)
		{
			vertexNormal = rv->rn.getNormal();
		}
		else
		{
			for (int i = 0; i < numNormals; i++)
			{
				if (rv->ern[i].getSmGroup() & smGroup)
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
	replace(str, "¤", "_");
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
	replace(str, "¨", "_");
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

		objString = "scene.lights.";
		objString.append(ToNarrow(Omni->GetName()));
		objString.append(".type = point");
		objString.append("\n");
		props.SetFromString(objString);
		objString = "";

		objString = "scene.lights.";
		objString.append(ToNarrow(Omni->GetName()));
		objString.append(".position = ");
		objString.append(::to_string(trans.x) + " " + ::to_string(trans.y) + " " + ::to_string(trans.z));
		objString.append("\n");
		props.SetFromString(objString);
		objString = "";

		for (int i = 0, count = Omni->NumParamBlocks(); i < count; ++i)
		{
			IParamBlock2 *pBlock = Omni->GetParamBlock(i);
			color = pBlock->GetPoint3(0, GetCOREInterface()->GetTime(), 0);
		}

		//objString = "scene.lights.";
		//	objString.append(ToNarrow(currNode->GetName()));
		//	objString.append(".color");
		//	scene->Parse(
		//		Property(objString)(color.x, color.y, color.z) <<
		//		Property("")("")
		//		);

		objString = "";
		return props;
}

Properties exportSpotLight(INode* SpotLight)
{
	Properties props;
	std::string objString;
	::Point3 trans = SpotLight->GetNodeTM(GetCOREInterface11()->GetTime()).GetTrans();
	::Matrix3 targetPos;
	Object*	obj;
	ObjectState os = SpotLight->EvalWorldState(GetCOREInterface()->GetTime());
	LightObject*   lightPtr = (LightObject *)os.obj;

	//Standard spotlight

	/*scene.lights.l1.type = spot
	scene.lights.l1.position = -3.0 - 5.0 6.0
	scene.lights.l1.target = -3.0 - 5.0 0.0
	scene.lights.l1.gain = 500.0 500.0 500.0
	scene.lights.l1.coneangle = 60.0
	scene.lights.l1.conedeltaangle = 50.0*/
	
	
	SpotLight->GetTargetTM(GetCOREInterface11()->GetTime(), targetPos);
	trans = SpotLight->GetNodeTM(GetCOREInterface11()->GetTime(), 0).GetTrans();

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".type = spot");
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".position = ");
	objString.append(::to_string(trans.x) + " " + ::to_string(trans.y) + " " + ::to_string(trans.z));
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".target = ");
	objString.append(::to_string(targetPos.GetTrans().x) + " " + ::to_string(targetPos.GetTrans().y) + " " + ::to_string(targetPos.GetTrans().z));
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".coneangle = ");
	objString.append(::to_string(lightPtr->GetHotspot(GetCOREInterface11()->GetTime(), FOREVER)));
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".conedeltaangle = ");
	objString.append(to_string(lightPtr->GetFallsize(GetCOREInterface11()->GetTime(), FOREVER) * 180 / 3.14159265));
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";

	objString = "scene.lights.";
	objString.append(ToNarrow(SpotLight->GetName()));
	objString.append(".gain = 500 500 500");
	objString.append("\n");
	props.SetFromString(objString);
	objString = "";
	
	//scene.lights.l1.coneangle = 60.0
	//scene.lights.l1.conedeltaangle = 50.0
	//scene.lights.l1.target = -3.0 - 5.0 0.0

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
			//If we find a helper object - then we skip it (Sky\light helpers for example, target objects etc).
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

		//bool foundCamera = false;
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
				IDerivedObject *pDerObj;
				Modifier *Mod;
				Matrix3 nodeInitTM;
				Point4 nodeRotation;
				TriObject *p_triobj = NULL;

				BOOL fConvertedToTriObject = obj->CanConvertToType(triObjectClassID) && (p_triobj = (TriObject*)obj->ConvertToType(0, triObjectClassID)) != NULL;

				if (!fConvertedToTriObject)
				{
					mprintf(L"Debug: Did not triangulate object : %s\n", currNode->GetName());
					break;
					//return false;
				}
				else
				{
					mprintf(L"Info: Creating mesh for object : %s\n", currNode->GetName());
					const wchar_t *objName = L"";
					std::string tmpName = ToNarrow(currNode->GetName());
					removeUnwatedChars(tmpName);
					std::wstring replacedObjName = std::wstring(tmpName.begin(), tmpName.end());
					objName = replacedObjName.c_str();

					const wchar_t *matName = L"";
					matName = currNode->GetMtl()->GetName();
					std::string tmpMatName = ToNarrow(matName);
					removeUnwatedChars(tmpMatName);
					std::wstring replacedMaterialName = std::wstring(tmpMatName.begin(), tmpMatName.end());
					matName = replacedMaterialName.c_str();

					//use the ::Mesh to get the 'base class's' mesh class (3dsmax SDK)
					//If you do not do this then it conflicts with Luxrays's mesh class.
					::Mesh *p_trimesh = &p_triobj->mesh;

					int numverts = p_trimesh->getNumFaces() * 3;
					int numfaces = p_trimesh->getNumFaces();

					int faceCount = p_trimesh->getNumFaces();
					int numUvs = p_trimesh->getNumTVerts();

					Point *p = Scene::AllocVerticesBuffer(numverts);
					Triangle *vi = Scene::AllocTrianglesBuffer(numfaces);
					Normal *n = new Normal[numverts];

					UV *uv = NULL;

					if (numUvs > 0)
					{
						uv = new UV[numUvs];
						for (int u = 0; u < numUvs; u++)
						{
							uv[u].u = p_trimesh->getTVert(u).x;
							uv[u].v = p_trimesh->getTVert(u).y;
						}
					}

					p_trimesh->checkNormals(true);
					p_trimesh->buildNormals();

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

							normal.Normalize();
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

					int c = 0;

					for (int i = 0; i < p_trimesh->getNumFaces(); i++)
					{
						vi[i] = Triangle(c, c + 1, c + 2);
						c += 3;
					}

					if (p_trimesh->getNumTVerts() < 1) {
						// Define the object - without UV
						scene->DefineMesh(ToNarrow(objName), numverts, numfaces, p, vi, n, NULL, NULL, NULL);
					}
					else
					{
						// Define the object - with UV
						scene->DefineMesh(ToNarrow(objName), numverts, numfaces, p, vi, n, uv, NULL, NULL);
					}

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
					if (objmat != NULL)
					{
						int numsubs = 0;
						numsubs = objmat->NumSubMtls();

						for (int f = 0; f < numsubs; ++f)
						{
							if (objmat->ClassID() == LR_INTERNAL_MATTE_CLASSID)
							{
								objString.append("scene.materials.");
								objString.append(ToNarrow(matName));
								objString.append(".type");

								scene->Parse(
									Property(objString)("matte") <<
									Property("")("")
									);
								objString = "";

								for (int i = 0, count = objmat->NumParamBlocks(); i < count; ++i)
								{
									IParamBlock2 *pBlock = objmat->GetParamBlock(i);

									::Point3 diffcol;
									::std::string texmap1path;
									::std::string texmap1Filename;
									int texwidth;
									int textheight;

									diffcol = pBlock->GetPoint3(0, GetCOREInterface()->GetTime(), 0);
									texmap1path = getstring((pBlock->GetStr(2, GetCOREInterface()->GetTime(), 0)));
									texmap1Filename = getstring(pBlock->GetStr(3, GetCOREInterface()->GetTime(), 0));
									texwidth = pBlock->GetInt(4, GetCOREInterface()->GetTime(), 0);
									textheight = pBlock->GetInt(5, GetCOREInterface()->GetTime(), 0);

									const u_int size = texwidth + textheight;
									float *img = new float[size * size * 3];

									if (!scene->IsTextureDefined(texmap1Filename))
									{
										if (texmap1path != "")
										{
											::std::string tmpTexStr;
											::std::string tmpDefinePathstr;

											//we need to get the bitmap size, we pull this from maxscript property
											scene->DefineImageMap(texmap1Filename, img, 1.f, 3, texwidth, textheight, luxcore::Scene::ChannelSelectionType::DEFAULT);

											tmpTexStr.append("scene.textures.");
											tmpTexStr.append(texmap1Filename);
											tmpTexStr.append(".type");

											tmpDefinePathstr.append("scene.textures.");
											tmpDefinePathstr.append(texmap1Filename);
											tmpDefinePathstr.append(".file");

											scene->Parse(
												Property(tmpTexStr)("imagemap") <<
												Property(tmpDefinePathstr)(texmap1path) <<
												Property("")("")
												);
										}
									}
									//mprintf(L"Setting material diffuse RGB: %f %f %f\n", diffcol.x, diffcol.y, diffcol.z);

									if (scene->IsMaterialDefined(ToNarrow(matName)))
									{
										::std::string tmpMatStr;
										tmpMatStr.append("scene.materials.");
										tmpMatStr.append(ToNarrow(matName));
										tmpMatStr.append(".kd");

										if (texmap1path == "")
										{
											//tmpMatStr.append(".kd");
											mprintf(L"Material kd string: %s\n", tmpMatStr.c_str());
											scene->Parse(
												Property(tmpMatStr)(float(diffcol.x), float(diffcol.y), float(diffcol.z)) <<
												Property("")("")

												);
										}
										else
										{
											mprintf(L"Material kd string: %s\n", tmpMatStr.c_str());
											scene->Parse(
												Property(tmpMatStr)(texmap1Filename) <<
												Property("")("")

												);
										}

										tmpMatStr = "";
									}
									//scene->Parse(
									//										Property("scene.materials.tmpMat.type")("matte") <<
									//									Property("scene.materials.tmpMat.kd")(float(diffcol.x), float(diffcol.y), float(diffcol.z))
									//								);
								}
							}

							if (objmat->ClassID() == LUXCORE_MATTE_CLASSID)
							{
								//skip it - not supported.
							}
						}
					}

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