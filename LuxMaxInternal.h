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

#ifndef LUXMAXINTERNAL__H
#define LUXMAXINTERNAL__H
#include "maxtextfile.h"
#include <iostream>
#include <string>
#include <luxcore/luxcore.h>

#define REND_CLASS_ID Class_ID(98,0);

class LuxMaxInternal : public Renderer {
	public:
		
		TSTR FileName;
		TSTR halttimewstr = L"60";
		TSTR LensRadiusstr = L"0";
		//string rendertype = "";
		TSTR vbinterval = L"1";
		bool defaultlightchk = true;
		bool defaultlightauto = true;

		int renderType = 4;
		MaxSDK::Util::TextFile::Writer *file;
		INode *sceneNode;
		INode *viewNode;
		ViewParams viewParams; // view params for rendering ortho or user viewport
		RendParams RP;  	// common renderer parameters
		BOOL anyLights;
		TCHAR buffer[256];
		int nlts,nobs;
		LuxMaxInternal() { file = NULL; sceneNode = NULL; viewNode = NULL; anyLights = FALSE; nlts = nobs = 0; }
		int Open(
			INode *scene,     	// root node of scene to render
			INode *vnode,     	// view node (camera or light), or NULL
			ViewParams *viewPar,// view params for rendering ortho or user viewport
			RendParams &rp,  	// common renderer parameters
			HWND hwnd, 				// owner window, for messages
			DefaultLight* defaultLights=NULL, // Array of default lights if none in scene
			int numDefLights=0,	// number of lights in defaultLights array
			RendProgressCallback* prog = NULL
			);
		int Render(
			TimeValue t,   			// frame to render.
   			Bitmap *tobm, 			// optional target bitmap
			FrameRendParams &frp,	// Time dependent parameters
			HWND hwnd, 				// owner window
			RendProgressCallback *prog=NULL,
			ViewParams *vp=NULL
			);
		void Close(	HWND hwnd, RendProgressCallback* prog = NULL );		
		RefTargetHandle Clone(RemapDir &remap);
		//static void CreateBox(Scene *scene, const string &objName, const string &meshName,
		//	const string &matName, const bool enableUV, const BBox &bbox);
		// Adds rollup page(s) to renderer configure dialog
		// If prog==TRUE then the rollup page should just display the parameters
		// so the user has them for reference while rendering, they should not be editable.
		RendParamDlg *CreateParamDialog(IRendParams *ir,BOOL prog=FALSE);
		void ResetParams();
		void DeleteThis() { delete this;  }
		Class_ID ClassID() { return REND_CLASS_ID;}
		void GetClassName(TSTR& s) {s = GetString(IDS_VRENDTITLE);}
		// IO
		IOResult Save(ISave *isave);
		IOResult Load(ILoad *iload);

		virtual RefResult NotifyRefChanged (
			const Interval		&changeInt, 
			RefTargetHandle		 hTarget, 
			PartID				&partID,  
			RefMessage			 message, 
			BOOL				 propagate
			);
	};



#endif