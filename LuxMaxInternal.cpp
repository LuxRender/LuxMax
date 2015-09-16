﻿/***************************************************************************
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
#define MAX2016_PHYSICAL_CAMERA Class_ID(1181315608,686293133)

#define OMNI_CLASSID Class_ID(4113, 0)
#define SPOTLIGHT_CLASSID Class_ID(4114,0)

#define SKYLIGHT_CLASSID Class_ID(2079724664, 1378764549)
#define DIRLIGHT_CLASSID Class_ID(4115, 0) // free directional light and sun light classid

#include "LuxMaxInternalpch.h"
#include "resource.h"
#include "LuxMaxInternal.h"

#include "LuxMaxCamera.h"
#include "LuxMaxUtils.h"
#include "LuxMaxMaterials.h"
#include "LuxMaxLights.h"
#include "LuxMaxMesh.h"

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

LuxMaxCamera lxmCamera;
LuxMaxLights lxmLights;
LuxMaxMaterials lxmMaterials;
LuxMaxUtils lxmUtils;
LuxMaxMesh lxmMesh;

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
		bool renderabort = prog->Progress(elapsedTime + 1, haltTime);
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

	//In the camera 'export' function we check for supported camera, it returns false if something is not right.
	if (!lxmCamera.exportCamera((float)_wtof(LensRadiusstr), *scene))
	{
		return false;
	}

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
		prog->Progress(a + 1, maxscene->NumChildren());

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

		/*case CAMERA_CLASS_ID:
		{
		break;
		}*/

		case GEOMOBJECT_CLASS_ID:
		{
			if (doExport)
			{
				lxmMesh.createMesh(currNode, *scene);
			}
			break;
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