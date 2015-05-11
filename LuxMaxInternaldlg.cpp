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

#include "LuxMaxInternalpch.h"
#include "resource.h"
#include "LuxMaxInternal.h"
#include <maxscript\maxscript.h>
#include "3dsmaxport.h"
#include <sstream>

class LuxMaxInternalParamDlg : public RendParamDlg {
public:
	LuxMaxInternal *rend;
	IRendParams *ir;
	HWND hPanel;
	BOOL prog;
	HFONT hFont;
	TSTR workFileName;
	int workRenderType;
	int halttime;
	TSTR halttimewstr = L"30";

	LuxMaxInternalParamDlg(LuxMaxInternal *r, IRendParams *i, BOOL prog);
	~LuxMaxInternalParamDlg();
	void AcceptParams();
	void DeleteThis() { delete this; }
	void InitParamDialog(HWND hWnd);
	void InitProgDialog(HWND hWnd);
	void ReleaseControls() {}
	BOOL FileBrowse();

	INT_PTR WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

LuxMaxInternalParamDlg::~LuxMaxInternalParamDlg()
{
	DeleteObject(hFont);
	ir->DeleteRollupPage(hPanel);
}

INT_PTR LuxMaxInternalParamDlg::WndProc(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
		if (prog) InitProgDialog(hWnd);
		else InitParamDialog(hWnd);
		break;

	case WM_DESTROY:
		if (!prog) ReleaseControls();
		break;
	default:
		return FALSE;
	}
	return TRUE;
}



static INT_PTR CALLBACK LuxMaxInternalParamDlgProc(
	HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

	DisableAccelerators();
	LuxMaxInternalParamDlg *dlg = DLGetWindowLongPtr<LuxMaxInternalParamDlg*>(hWnd);
	switch (msg) 
	{
	case WM_INITDIALOG:
	{
		dlg = (LuxMaxInternalParamDlg*)lParam;
		DLSetWindowLongPtr(hWnd, lParam);
		
		SendDlgItemMessage(hWnd, IDC_COMBO1, CB_ADDSTRING, 0, (LPARAM)L"PATHCPU");
		SendDlgItemMessage(hWnd, IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"random");
		//SendDlgItemMessage(hWnd, IDC_CUSTOM1, CB_ADDSTRING, 0, (LPARAM)L"33");
		SendDlgItemMessage(hWnd, IDC_COMBO2, CB_SETCURSEL, 0, (LPARAM)L"");
		//store value back into workRenderType = rend->renderType
		SendDlgItemMessage(hWnd, IDC_COMBO1, CB_SETCURSEL, 0, (LPARAM)L"");

		SendDlgItemMessage(hWnd, IDC_COMBO_FILM_OUTPUT_TYPE, CB_ADDSTRING, 0, (LPARAM)L"RGB_TONEMAPPED");
		SendDlgItemMessage(hWnd, IDC_COMBO_FILM_OUTPUT_TYPE, CB_SETCURSEL, 0, (LPARAM)L"");
		
		CheckDlgButton(hWnd, IDC_CHECK_OPENCL_GPU, BST_UNCHECKED);
		CheckDlgButton(hWnd, IDC_CHECK_OPENCL_CPU, BST_CHECKED);
		//CheckDlgButton(hWnd, IDC_OUTPUTSCENE, BST_UNCHECKED);

		break;
	}
	case WM_LBUTTONDOWN:
	{}
	case WM_MOUSEMOVE:
	{}
	case WM_LBUTTONUP:
	{
			dlg->ir->RollupMouseMessage(hWnd, msg, wParam, lParam);
			break;
	}

		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDC_BUTTON1:
			{
				if (dlg->FileBrowse()) 
				{
					SetDlgItemText(hWnd, IDC_FILENAME, dlg->workFileName.data());
					break;
				}
				break;
			}
			case IDC_HALTTIME:
				{
					HWND hwndOutput = GetDlgItem(hWnd, IDC_HALTTIME);
					dlg->halttimewstr = GetWindowText(hwndOutput);
				break;
			}
			//case IDC_ANGLE_SPINNER: // A specific spinner ID.
				//angle = ((ISpinnerControl *)lParam)->GetFVal();
				//break;
		}

		}
		
		case CC_SPINNER_CHANGE:
		{
		}
		break;
	}
	if (dlg) return dlg->WndProc(hWnd, msg, wParam, lParam);
	else return FALSE;
}

LuxMaxInternalParamDlg::LuxMaxInternalParamDlg(
	LuxMaxInternal *r, IRendParams *i, BOOL prog)
{
	hFont = hFont = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, VARIABLE_PITCH | FF_SWISS, _T(""));
	rend = r;
	ir = i;
	this->prog = prog;
	if (prog) {
		hPanel = ir->AddRollupPage(
			hInstance,
			MAKEINTRESOURCE(IDD_RENDER_PROG),
			LuxMaxInternalParamDlgProc,
			GetString(IDS_VRENDTITLE),
			(LPARAM)this);
	}
	else {
		hPanel = ir->AddRollupPage(
			hInstance,
			MAKEINTRESOURCE(IDD_RENDER_PARAMS),
			LuxMaxInternalParamDlgProc,
			GetString(IDS_VRENDTITLE),
			(LPARAM)this);
	}
}

void LuxMaxInternalParamDlg::InitParamDialog(HWND hWnd) {
	workFileName = rend->FileName;
	halttimewstr = rend->halttimewstr;

	SetDlgItemText(hWnd, IDC_FILENAME, workFileName);

	HWND hwndOutput = GetDlgItem(hWnd, IDC_HALTTIME);
	SetWindowText(hwndOutput, rend->halttimewstr);
}

void LuxMaxInternalParamDlg::InitProgDialog(HWND hWnd) {
	SetDlgItemText(hWnd, IDC_FILENAME, rend->FileName.data());

	HWND hwndOutput = GetDlgItem(hWnd, IDC_HALTTIME);
	SetWindowText(hwndOutput, rend->halttimewstr.data());
}

void LuxMaxInternalParamDlg::AcceptParams() {
	rend->FileName = workFileName;
	rend->renderType = workRenderType;
	rend->halttimewstr= halttimewstr;
}

RendParamDlg * LuxMaxInternal::CreateParamDialog(IRendParams *ir, BOOL prog) {
	return new LuxMaxInternalParamDlg(this, ir, prog);
}

// File Browse ------------------------------------------------------------
BOOL FileExists(const TCHAR *filename) {
	HANDLE findhandle;
	WIN32_FIND_DATA file;
	findhandle = FindFirstFile(filename, &file);
	FindClose(findhandle);
	if (findhandle == INVALID_HANDLE_VALUE)
		return(FALSE);
	else
		return(TRUE);
}

BOOL RunningNewShell()
{
	OSVERSIONINFO os;
	os.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os);
	if (os.dwPlatformId == VER_PLATFORM_WIN32_NT && os.dwMajorVersion < 4)
		return FALSE;
	return TRUE;
}

#define FileEXT _T(".png")
#define FileFILTER _T("*.png")

void FixFileExt(OPENFILENAME &ofn, TCHAR* ext = FileEXT) {
	int l = static_cast<int>(_tcslen(ofn.lpstrFile));  // SR DCAST64: Downcast to 2G limit.
	int e = static_cast<int>(_tcslen(ext));   // SR DCAST64: Downcast to 2G limit.
	if (_tcsicmp(ofn.lpstrFile + l - e, ext)) {
		_tcscat(ofn.lpstrFile, ext);
	}
}

#if 0
UINT_PTR WINAPI FileHook( HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetDlgItemText(hDlg, IDOK, _T("OK"));
		break;
	case WM_COMMAND:{
	}

		break;
	}
	return FALSE;
}

UINT_PTR PMFileHook(HWND hWnd,UINT message,WPARAM wParam,LPARAM   lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		SetDlgItemText(hWnd, IDOK, _T("OK"));
		break;
	case WM_COMMAND:{
	}

		break;
	}
	return 0;
}
#endif

BOOL LuxMaxInternalParamDlg::FileBrowse() {
	int tried = 0;
	FilterList filterList;
	HWND hWnd = hPanel;
	static int filterIndex = 1;
	OPENFILENAME  ofn;
	
	TSTR filename;
	TCHAR fname[512];
	TCHAR saveDir[1024];
	{
		TSTR dir;
		SplitFilename(workFileName, &dir, &filename, NULL);
		_tcscpy(saveDir, dir.data());
	}
	_tcscpy(fname, filename.data());
	_tcscat(fname, FileEXT);

	filterList.Append(GetString(IDS_FILE));
	memset(&ofn, 0, sizeof(ofn));

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hWnd;
	ofn.hInstance = hInstance;

	ofn.nFilterIndex = filterIndex;
	ofn.lpstrFilter = filterList;

	ofn.lpstrTitle = GetString(IDS_WRITE_FILE);
	ofn.lpstrFile = fname;
	ofn.nMaxFile = _countof(fname);

	Interface *iface = GetCOREInterface();

	if (saveDir[0])
		ofn.lpstrInitialDir = saveDir;
	else
		ofn.lpstrInitialDir = iface->GetDir(APP_SCENE_DIR);

	if (RunningNewShell()) {
		ofn.Flags = OFN_HIDEREADONLY | OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLESIZING;
		ofn.lpfnHook = NULL;
		ofn.lCustData = 0;   // 0 for save, 1 for open

	}
	else {
		ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLESIZING;
		ofn.lpfnHook = NULL; 
		ofn.lCustData = 0;
	}

	FixFileExt(ofn, FileEXT);
	while (GetSaveFileName(&ofn))    {
	//while ((&ofn)){
	FixFileExt(ofn, FileEXT); // add ".vue" if absent

		workFileName = ofn.lpstrFile;
		return TRUE;
	}
	return FALSE;
}