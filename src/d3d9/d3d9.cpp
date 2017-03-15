
#define CINTERFACE

#include "d3d9.h"
#include "d3dx9.h"
#define NOMINMAX 
#include <windows.h>
#include <string>
#include <sstream>
#include <tchar.h>
#include <fstream>
#include <algorithm>
#include <shlwapi.h>
#include <Commdlg.h>

#include <commctrl.h>
#include <richedit.h>

#include <process.h>

#include "bridge_parameter.h"
#include "optix.h"
#include "resource.h"
#include "MMDExport.h"
#include "UMStringUtil.h"
#include "UMPath.h"

#ifdef _WIN64
#define _LONG_PTR LONG_PTR
#else
#define _LONG_PTR LONG
#endif

template <class T> std::string to_string(T value)
{
	return umbase::UMStringUtil::number_to_string(value);
}

//ワイド文字列からutf8文字列に変換
static void to_string(std::string &dest, const std::wstring &src) 
{
	dest = umbase::UMStringUtil::wstring_to_utf8(src);
}

static void messagebox(std::string title, std::string message)
{
	::MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK);
}

static void message(std::string message)
{
	::MessageBoxA(NULL, message.c_str(), "message", MB_OK);
}

// IDirect3DDevice9のフック関数
void hookDevice(void);
void originalDevice(void);
// フックしたデバイス
IDirect3DDevice9 *p_device = NULL;

RenderData renderData;

std::vector<std::pair<IDirect3DTexture9*, bool> > finishTextureBuffers;

std::map<IDirect3DTexture9*, RenderedTexture> renderedTextures;
std::map<int, std::map<int , RenderedMaterial*> > renderedMaterials;
//-----------------------------------------------------------------------------------------------------------------

//------------------------------------------Python呼び出し--------------------------------------------------------
static int pre_frame = 0;
static int presentCount = 0;
static int process_frame = -1;
static int ui_frame = 0;
int script_call_setting = 0; // スクリプト呼び出し設定
std::map<int, int> exportedFrames;

struct PrimitiveInfo
{
	D3DPRIMITIVETYPE type;
	INT baseVertexIndex;
	UINT minIndex;
	UINT numVertices;
	UINT startIndex;
	UINT primitiveCount;
	RenderData renderData;
};

static std::vector<PrimitiveInfo> primitveInfoList;

//-----------------------------------------------------------Hook関数ポインタ-----------------------------------------------------------

// Direct3DCreate9
IDirect3D9 *(WINAPI *original_direct3d_create)(UINT)(NULL);

HRESULT (WINAPI *original_direct3d9ex_create)(UINT, IDirect3D9Ex**)(NULL);
// IDirect3D9::CreateDevice
HRESULT (WINAPI *original_create_device)(IDirect3D9*,UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**)(NULL);

HRESULT(WINAPI *original_create_deviceex)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**)(NULL);
// IDirect3DDevice9::BeginScene
HRESULT (WINAPI *original_begin_scene)(IDirect3DDevice9*)(NULL);
// IDirect3DDevice9::EndScene
HRESULT(WINAPI *original_end_scene)(IDirect3DDevice9*)(NULL);
// IDirect3DDevice9::Release
ULONG(WINAPI *original_release)(IDirect3D9*)(NULL);
// IDirect3DDevice9Ex::Release
ULONG(WINAPI *original_release_ex)(IDirect3D9Ex*)(NULL);
// IDirect3DDevice9::SetFVF
HRESULT (WINAPI *original_set_fvf)(IDirect3DDevice9*, DWORD);
// IDirect3DDevice9::Clear
HRESULT (WINAPI *original_clear)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD)(NULL);
// IDirect3DDevice9::Present
HRESULT (WINAPI *original_present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA *)(NULL);
// IDirect3DDevice9::Reset
HRESULT (WINAPI *original_reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*)(NULL);

// IDirect3DDevice9::BeginStateBlock
// この関数で、lpVtblが修正されるので、lpVtbl書き換えなおす
HRESULT (WINAPI *original_begin_state_block)(IDirect3DDevice9 *)(NULL);

// IDirect3DDevice9::EndStateBlock
// この関数で、lpVtblが修正されるので、lpVtbl書き換えなおす
HRESULT (WINAPI *original_end_state_block)(IDirect3DDevice9*, IDirect3DStateBlock9**)(NULL);

// IDirect3DDevice9::DrawIndexedPrimitive
HRESULT (WINAPI *original_draw_indexed_primitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT)(NULL);

// IDirect3DDevice9::DrawPrimitive
HRESULT(WINAPI *original_draw_primitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT)(NULL);

// IDirect3DDevice9::SetStreamSource
HRESULT (WINAPI *original_set_stream_source)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT)(NULL);

// IDirect3DDevice9::SetIndices
HRESULT (WINAPI *original_set_indices)(IDirect3DDevice9*, IDirect3DIndexBuffer9*)(NULL);

// IDirect3DDevice9::CreateVertexBuffer
HRESULT (WINAPI *original_create_vertex_buffer)(IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*)(NULL);

// IDirect3DDevice9::SetTexture
HRESULT (WINAPI *original_set_texture)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9 *)(NULL);

// IDirect3DDevice9::CreateTexture
HRESULT (WINAPI *original_create_texture)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*)(NULL);
//-----------------------------------------------------------------------------------------------------------------------------

static bool writeTextureToFile(const std::string &texturePath, IDirect3DTexture9 * texture, D3DXIMAGE_FILEFORMAT fileFormat, RenderData& renderData);

static bool writeTextureToFiles(const std::string &texturePath, const std::string &textureType, RenderData& renderData, bool uncopied = false);

static bool copyTextureToFiles(const std::u16string &texturePath, RenderData& renderData);

static bool writeTextureToMemory(const std::string &textureName, IDirect3DTexture9 * texture, bool copied, RenderData& renderData);


static void getTextureParameter(TextureParameter &param, RenderData& renderData);
static bool writeBuffersToMemory(IDirect3DDevice9 *device, RenderData& renderData);
static bool writeMaterialsToMemory(TextureParameter & textureParameter, RenderData& renderData);
static void writeMatrixToMemory(IDirect3DDevice9 *device, RenderedBuffer &dst);
static void writeLightToMemory(IDirect3DDevice9 *device, RenderedBuffer &renderedBuffer);

static void drawPreview(IDirect3DDevice9 *device);
//-----------------------------------------------------------------------------------------------------------------------------


static bool writeTextureToFile(const std::string &texturePath, IDirect3DTexture9 * texture, D3DXIMAGE_FILEFORMAT fileFormat, RenderData& renderData)
{
	TextureBuffers::iterator tit = renderData.textureBuffers.find(texture);
	if(tit != renderData.textureBuffers.end())
	{
		if (texture->lpVtbl) {
			D3DXSaveTextureToFileA(texturePath.c_str(), fileFormat,(LPDIRECT3DBASETEXTURE9) texture, NULL);
			return true;
		}
	}
	return false;
}

static bool writeTextureToFiles(const std::string &texturePath, const std::string &textureType, RenderData& renderData, bool uncopied)
{
	bool res = true;

	D3DXIMAGE_FILEFORMAT fileFormat;
	if (textureType == "bmp" || textureType == "BMP") { fileFormat = D3DXIFF_BMP; }
	else if (textureType == "png" || textureType == "PNG") { fileFormat = D3DXIFF_PNG; }
	else if (textureType == "jpg" || textureType == "JPG") { fileFormat = D3DXIFF_JPG; }
	else if (textureType == "tga" || textureType == "TGA") { fileFormat = D3DXIFF_TGA; }
	else if (textureType == "dds" || textureType == "DDS") { fileFormat = D3DXIFF_DDS; }
	else if (textureType == "ppm" || textureType == "PPM") { fileFormat = D3DXIFF_PPM; }
	else if (textureType == "dib" || textureType == "DIB") { fileFormat = D3DXIFF_DIB; }
	else if (textureType == "hdr" || textureType == "HDR") { fileFormat = D3DXIFF_HDR; }
	else if (textureType == "pfm" || textureType == "PFM") { fileFormat = D3DXIFF_PFM; }
	else { return false; }

	char dir[MAX_PATH];
	std::strcpy(dir, texturePath.c_str());
	PathRemoveFileSpecA(dir);
	
	for (size_t i = 0; i <  finishTextureBuffers.size(); ++i)
	{
		IDirect3DTexture9* texture = finishTextureBuffers[i].first;
		bool copied = finishTextureBuffers[i].second;
		if (texture) {
			if (uncopied)
			{
				if (!copied)
				{
					char path[MAX_PATH];
					PathCombineA(path, dir, to_string(texture).c_str());
					if (!writeTextureToFile(std::string(path) + "." + textureType, texture, fileFormat, renderData)) { res = false; }
				}
			} else {
				char path[MAX_PATH];
				PathCombineA(path, dir, to_string(texture).c_str());
				if (!writeTextureToFile(std::string(path) + "." + textureType, texture, fileFormat, renderData)) { res = false; }
			}
		}
	}

	return res;
}

static bool copyTextureToFiles(const std::u16string &texturePath)
{
	if (texturePath.empty()) return false;

	std::wstring path = umbase::UMStringUtil::utf16_to_wstring(texturePath);
	PathRemoveFileSpec(&path[0]);
	PathAddBackslash(&path[0]);
	if (!PathIsDirectory(path.c_str())) { return false; }
	
	bool res = true;
	for (size_t i = 0; i <  finishTextureBuffers.size(); ++i)
	{
		IDirect3DTexture9* texture = finishTextureBuffers[i].first;
		if (texture) {
			if (!UMCopyTexture(path.c_str(), texture)) { res = false; }
		}
	}
	return res;
}

static bool writeTextureToMemory(const std::string &textureName, IDirect3DTexture9 * texture, bool copied, RenderData& renderData)
{
	// すでにfinishTexutureBufferにあるかどうか
	bool found = false;
	for (size_t i = 0; i < finishTextureBuffers.size(); ++i)
	{
		if (finishTextureBuffers[i].first == texture) { found = true; break; }
	}

	if (!found)
	{
		// 書き出していなかったので書き出しファイルリストに入れる
		std::pair<IDirect3DTexture9*, bool> texturebuffer(texture, copied);
		finishTextureBuffers.push_back(texturebuffer);
	}

	if (BridgeParameter::instance().is_texture_buffer_enabled)
	{
		TextureBuffers::iterator tit = renderData.textureBuffers.find(texture);
		if(tit != renderData.textureBuffers.end())
		{
			// テクスチャをメモリに書き出し
			D3DLOCKED_RECT lockRect;
			HRESULT isLocked = texture->lpVtbl->LockRect(texture, 0, &lockRect, NULL, D3DLOCK_READONLY);
			if (isLocked != D3D_OK) { return false; }
			
			int width = tit->second.wh.x;
			int height = tit->second.wh.y;

			RenderedTexture tex;
			tex.size.x = width;
			tex.size.y = height;
			tex.name = textureName;

			D3DFORMAT format = tit->second.format;
			for(int y = 0; y < height; y++)
			{
				unsigned char *lineHead = (unsigned char*)lockRect.pBits + lockRect.Pitch * y;

				for (int x = 0; x < width; x++)
				{
					if (format == D3DFMT_A8R8G8B8) {
						UMVec4f rgba;
						rgba.x = lineHead[4 * x + 0];
						rgba.y = lineHead[4 * x + 1];
						rgba.z = lineHead[4 * x + 2];
						rgba.w = lineHead[4 * x + 3];
						tex.texture.push_back(rgba);
					} else {
						::MessageBoxA(NULL, std::string("not supported texture format:" + format).c_str(), "info", MB_OK);
					}
				}
			}
			renderedTextures[texture] = tex;

			texture->lpVtbl->UnlockRect(texture, 0);
			return true;
		}
	}
	return false;
}

static HRESULT WINAPI beginScene(IDirect3DDevice9 *device) 
{
	HRESULT res = (*original_begin_scene)(device);

	return res;
}

static HRESULT WINAPI endScene(IDirect3DDevice9 *device)
{
	HRESULT res = (*original_end_scene)(device);
	return res;

}

HWND g_hWnd=NULL;	//ウィンドウハンドル
HMENU g_hMenu=NULL;	//メニュー
HWND g_hFrame = NULL; //フレーム数


static void GetFrame(HWND hWnd)
{
	char text[256];
	::GetWindowTextA(hWnd, text, sizeof(text)/sizeof(text[0]));
		
	ui_frame= atoi(text);
}

static BOOL CALLBACK enumChildWindowsProc(HWND hWnd, LPARAM lParam)
{
	RECT rect;
	GetClientRect(hWnd, &rect);

	WCHAR buf[10];
	GetWindowText(hWnd, buf, 10);

	if (!g_hFrame && rect.right == 48 && rect.bottom == 22)
	{
		g_hFrame = hWnd;
		GetFrame(hWnd);
	}
	if (g_hFrame)
	{
		return FALSE;
	}
	return TRUE;	//continue
}

//乗っ取り対象ウィンドウの検索
static BOOL CALLBACK enumWindowsProc(HWND hWnd,LPARAM lParam)
{
	if (g_hWnd && g_hFrame) {
		GetFrame(g_hFrame);
		return FALSE;
	}
	HANDLE hModule=(HANDLE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);
	if(GetModuleHandle(NULL)==hModule)
	{
		//自分のプロセスが作ったウィンドウを見つけた
		char szClassName[256];
		GetClassNameA(hWnd,szClassName,sizeof(szClassName)/sizeof(szClassName[0]));

		std::string name(szClassName);

		if (name == "Polygon Movie Maker"){
			g_hWnd = hWnd;
			EnumChildWindows(hWnd, enumChildWindowsProc, 0);
			return FALSE;	//break
		}
	}
	return TRUE;	//continue
}

const int MENUID_PATHTRACE = 1030;
const int MENUID_AO = 1040;
const int MENUID_HDRI = 1050;
const int MENUID_BLEND_PREVIEW = 1060;

static void setMyMenu()
{
	if (g_hMenu) return;
	if (g_hWnd)
	{
		HMENU hmenu = GetMenu(g_hWnd);
		HMENU hsubs = CreatePopupMenu();
		int count = GetMenuItemCount(hmenu);
		
		MENUITEMINFO minfo;
		minfo.cbSize = sizeof(MENUITEMINFO);
		minfo.fMask = MIIM_ID | MIIM_TYPE | MIIM_SUBMENU;
		minfo.fType = MFT_STRING;
		minfo.dwTypeData = TEXT("MMDBridgeRT");
		minfo.hSubMenu = hsubs;

		InsertMenuItem(hmenu, count + 1, TRUE, &minfo);

		minfo.fMask = MIIM_ID | MIIM_TYPE;
		minfo.dwTypeData = TEXT("パストレーシング");
		minfo.wID = MENUID_PATHTRACE;
		InsertMenuItem(hsubs, 1, TRUE, &minfo);

		minfo.fMask = MIIM_ID | MIIM_TYPE;
		minfo.dwTypeData = TEXT("アンビエントオクルージョン");
		minfo.wID = MENUID_AO;
		InsertMenuItem(hsubs, 2, TRUE, &minfo);

		minfo.fMask = MIIM_ID | MIIM_TYPE;
		minfo.dwTypeData = TEXT("MMDビューを重ねて表示");
		minfo.wID = MENUID_BLEND_PREVIEW;
		InsertMenuItem(hsubs, 3, TRUE, &minfo);

		minfo.fMask = MIIM_ID | MIIM_TYPE;
		minfo.dwTypeData = TEXT("HDRIテクスチャ");
		minfo.wID = MENUID_HDRI;
		InsertMenuItem(hsubs, 4, TRUE, &minfo);

		SetMenu(g_hWnd, hmenu);
		g_hMenu = hmenu;
	}
}

LONG_PTR originalWndProc  =NULL;
HINSTANCE hInstance= NULL;

static BOOL openHDRIFile()
{
	if (g_hWnd)
	{
		OPENFILENAMEW ofn;
		wchar_t szFileName[4096] = L"";

		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn); // SEE NOTE BELOW
		ofn.hwndOwner = g_hWnd;
		ofn.lpstrFilter = TEXT("HDRI(*.exr)\0**.exr\0")
			TEXT("すべてのファイル(*.*)\0*.*\0\0");

		ofn.lpstrFile = szFileName;
		ofn.nMaxFile = 4096;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT;
		ofn.lpstrDefExt = L"exr";

		if (GetOpenFileName(&ofn))
		{
			return LoadHDRI(std::wstring(szFileName));
		}
	}
	return FALSE;
}


static bool IsValidCallSetting() { 
	return script_call_setting > 0;
}

static bool IsValidFrame() {
	return true;
	//HWND recWindow = FindWindowA("RecWindow", NULL);
	//return (recWindow != NULL);
}
static bool IsRecWindow() {
	HWND recWindow = FindWindowA("RecWindow", NULL);
	return (recWindow != NULL);
}
static int pre_buffer_size = 0;
static D3DXMATRIX pre_world;
static D3DXVECTOR3 pre_eye;
static D3DXVECTOR4 pre_fov;
static int frame_for_pt = 0;
static bool isRecWindow = false;

static LRESULT CALLBACK overrideWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_COMMAND:
	{
		switch (LOWORD(wp))
		{
		case MENUID_PATHTRACE:
			if (hInstance)
			{
				HMENU hmenu = GetMenu(g_hWnd);
				UINT state = GetMenuState(hmenu, MENUID_PATHTRACE, MF_BYCOMMAND);
				if (state & MFS_CHECKED)
				{
					script_call_setting &= ~1;
					ChangeOptixRenderType(script_call_setting);
					CheckMenuItem(hmenu, MENUID_PATHTRACE, MF_BYCOMMAND | MFS_UNCHECKED);
				}
				else
				{
					script_call_setting |= 1;
					ChangeOptixRenderType(script_call_setting);
					CheckMenuItem(hmenu, MENUID_PATHTRACE, MF_BYCOMMAND | MFS_CHECKED);
				}
				if (script_call_setting == 0) {
					pre_buffer_size = 0;
					DisposeOptix();
				}
				frame_for_pt = 0;
			}
			break;
		case MENUID_AO:
			if (hInstance)
			{
				HMENU hmenu = GetMenu(g_hWnd);
				UINT state = GetMenuState(hmenu, MENUID_AO, MF_BYCOMMAND);
				if (state & MFS_CHECKED)
				{
					script_call_setting &= ~2;
					ChangeOptixRenderType(script_call_setting);
					CheckMenuItem(hmenu, MENUID_AO, MF_BYCOMMAND | MFS_UNCHECKED);
				}
				else
				{
					script_call_setting |= 2;
					ChangeOptixRenderType(script_call_setting);
					CheckMenuItem(hmenu, MENUID_AO, MF_BYCOMMAND | MFS_CHECKED);
				}
				if (script_call_setting == 0) {
					pre_buffer_size = 0;
					DisposeOptix();
				}
				frame_for_pt = 0;
			}
			break;
		case MENUID_BLEND_PREVIEW:
			if (hInstance)
			{
				HMENU hmenu = GetMenu(g_hWnd);
				BridgeParameter& parameter = BridgeParameter::mutable_instance();
				UINT state = GetMenuState(hmenu, MENUID_BLEND_PREVIEW, MF_BYCOMMAND);
				if (state & MFS_CHECKED)
				{
					parameter.blend_preview = false;
					CheckMenuItem(hmenu, MENUID_BLEND_PREVIEW, MF_BYCOMMAND | MFS_UNCHECKED);
				}
				else
				{
					parameter.blend_preview = true;
					CheckMenuItem(hmenu, MENUID_BLEND_PREVIEW, MF_BYCOMMAND | MFS_CHECKED);
				}
			}
			break;
		case MENUID_HDRI:
			if (hInstance)
			{
				HMENU hmenu = GetMenu(g_hWnd);
				UINT state = GetMenuState(hmenu, MENUID_HDRI, MF_BYCOMMAND);
				if (state & MFS_CHECKED)
				{
					EnableHDRI(false);
					CheckMenuItem(hmenu, MENUID_HDRI, MF_BYCOMMAND | MFS_UNCHECKED);
				}
				else
				{
					if (openHDRIFile()) {
						EnableHDRI(true);
						CheckMenuItem(hmenu, MENUID_HDRI, MF_BYCOMMAND | MFS_CHECKED);
					}
				}
				frame_for_pt = 0;
			}
			break;
		}
	}
	break;
	}

	// サブクラスで処理しなかったメッセージは、本来のウィンドウプロシージャに処理してもらう
	return CallWindowProc((WNDPROC)originalWndProc, hWnd, msg, wp, lp);
}

//ウィンドウの乗っ取り
static void overrideGLWindow()
{
	EnumWindows(enumWindowsProc, 0);
	setMyMenu();
	// サブクラス化
	if (g_hWnd && !originalWndProc){
		originalWndProc = GetWindowLongPtr(g_hWnd, GWLP_WNDPROC);
		SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (_LONG_PTR)overrideWndProc);
	}
}

static bool IsValidTechniq() {
	const int technic = ExpGetCurrentTechnic();
	return (technic == 0 || technic == 1 || technic == 2);
}

static void updatePrimitive(IDirect3DDevice9 *device, PrimitiveInfo& info)
{
	const int currentMaterial = ExpGetCurrentMaterial();
	const int currentObject = ExpGetCurrentObject();

	const bool validCallSetting = IsValidCallSetting();
	const bool validFrame = IsValidFrame();
	const bool validTechniq = IsValidTechniq();
	const bool validBuffer = (!BridgeParameter::instance().is_exporting_without_mesh);

	if (validBuffer && validCallSetting && validFrame && validTechniq && info.type == D3DPT_TRIANGLELIST)
	{
		RenderData& renderData = info.renderData;

		//IDirect3DVertexBuffer9  *lpVB;
		//UINT offset, stride;
		//device->lpVtbl->GetStreamSource(device, renderData.streamNumber, 
		//	&lpVB, &offset, &stride);

		// レンダリング開始
		if (renderData.pIndexData && renderData.pStreamData && renderData.pos_xyz)
		{
			// テクスチャ情報取得
			TextureParameter textureParameter;
			getTextureParameter(textureParameter, renderData);

			// テクスチャをメモリに保存
			if (textureParameter.texture)
			{
				if (!textureParameter.textureName.empty())
				{
					writeTextureToMemory(textureParameter.textureName, textureParameter.texture, true, renderData);
				}
				else
				{
					writeTextureToMemory(textureParameter.textureName, textureParameter.texture, false, renderData);
				}
			}

			// 頂点バッファ・法線バッファ・テクスチャバッファをメモリに書き込み
			if (!writeBuffersToMemory(device, renderData))
			{
				return;
			}

			// マテリアルをメモリに書き込み
			if (!writeMaterialsToMemory(textureParameter, renderData))
			{
				return;
			}

			// インデックスバッファをメモリに書き込み
			// 法線がない場合法線を計算
			IDirect3DVertexBuffer9 *pStreamData = renderData.pStreamData;
			IDirect3DIndexBuffer9 *pIndexData = renderData.pIndexData;

			D3DINDEXBUFFER_DESC indexDesc;
			if (pIndexData->lpVtbl->GetDesc(pIndexData, &indexDesc) == D3D_OK)
			{
				void *pIndexBuf;
				if (pIndexData->lpVtbl->Lock(pIndexData, 0, 0, (void**)&pIndexBuf, D3DLOCK_READONLY) == D3D_OK)
				{
					RenderBufferMap& renderedBuffers = BridgeParameter::mutable_instance().render_buffer_map;
					RenderedBuffer &renderedBuffer = renderedBuffers[renderData.pIndexData];
					RenderedSurface &renderedSurface = renderedBuffer.material_map[currentMaterial]->surface;
					renderedSurface.faces.clear();

					// 変換行列をメモリに書き込み
					writeMatrixToMemory(device, renderedBuffer);

					// ライトをメモリに書き込み
					writeLightToMemory(device, renderedBuffer);

					// インデックスバッファをメモリに書き込み
					// 法線を修正
					for (size_t i = 0, size = info.primitiveCount * 3; i < size; i += 3)
					{
						UMVec3i face;
						if (indexDesc.Format == D3DFMT_INDEX16)
						{
							WORD* p = (WORD*)pIndexBuf;
							face.x = static_cast<int>((p[info.startIndex + i + 0]) + 1);
							face.y = static_cast<int>((p[info.startIndex + i + 1]) + 1);
							face.z = static_cast<int>((p[info.startIndex + i + 2]) + 1);
						}
						else
						{
							DWORD* p = (DWORD*)pIndexBuf;
							face.x = static_cast<int>((p[info.startIndex + i + 0]) + 1);
							face.y = static_cast<int>((p[info.startIndex + i + 1]) + 1);
							face.z = static_cast<int>((p[info.startIndex + i + 2]) + 1);
						}
						const int vsize = renderedBuffer.vertecies.size();
						if (face.x > vsize || face.y > vsize || face.z > vsize) {
							continue;
						}
						if (face.x <= 0 || face.y <= 0 || face.z <= 0) {
							continue;
						}
						renderedSurface.faces.push_back(face);
						if (!renderData.normal)
						{
							if (renderedBuffer.normals.size() != vsize)
							{
								renderedBuffer.normals.resize(vsize);
							}
							D3DXVECTOR3 n;
							D3DXVECTOR3 v0 = renderedBuffer.vertecies[face.x - 1];
							D3DXVECTOR3 v1 = renderedBuffer.vertecies[face.y - 1];
							D3DXVECTOR3 v2 = renderedBuffer.vertecies[face.z - 1];
							D3DXVECTOR3 v10 = v1 - v0;
							D3DXVECTOR3 v20 = v2 - v0;
							::D3DXVec3Cross(&n, &v10, &v20);
							renderedBuffer.normals[face.x - 1] += n;
							renderedBuffer.normals[face.y - 1] += n;
							renderedBuffer.normals[face.z - 1] += n;
						}
						if (!renderData.normal)
						{
							for (size_t i = 0, size = renderedBuffer.normals.size(); i < size; ++i)
							{
								D3DXVec3Normalize(
									&renderedBuffer.normals[i],
									&renderedBuffer.normals[i]);
							}
						}
					}
				}
			}
			pIndexData->lpVtbl->Unlock(pIndexData);
		}
		//if (renderData.pStreamData) {
		//	renderData.pStreamData->lpVtbl->Release(renderData.pStreamData);
		//	renderData.pStreamData = NULL;
		//}
	}
}

static void drawPreview(IDirect3DDevice9 *device)
{
	BridgeParameter& parameter = BridgeParameter::mutable_instance();
	if (parameter.preview_tex)
	{
		// 頂点フォーマットの定義
		struct  VTX
		{
			float       x, y, z;
			float       rhw;
			D3DCOLOR    color;
			float       tu, tv;
		};
		D3DVIEWPORT9 viewport;
		device->lpVtbl->GetViewport(device, &viewport);
		const int x0 = viewport.X - 1;
		const int y0 = viewport.Y - 1;
		const int x1 = x0 + viewport.Width + 1;
		const int y1 = y0 + viewport.Height + 1;

		int color = 0xFFFFFFFF;
		if (parameter.blend_preview) {
			color = 0x77FFFFFF;
		}

		// 頂点を準備
		VTX vertex[4] =
		{
			{ x0, y0, 0, 1.0f, color, 0, 0 },    //左上
			{ x1, y0, 0, 1.0f, color, 1, 0 },    //右上
			{ x0, y1, 0, 1.0f, color, 0, 1 },    //左下
			{ x1, y1, 0, 1.0f, color, 1, 1 },    //右下
		};
		UINT numPass = 0;

		device->lpVtbl->SetTexture(device, 0, reinterpret_cast<IDirect3DBaseTexture9*>(parameter.preview_tex));

		device->lpVtbl->SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		device->lpVtbl->SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

		device->lpVtbl->SetTextureStageState(device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

		device->lpVtbl->SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		device->lpVtbl->SetTextureStageState(device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		// 頂点フォーマットの設定
		device->lpVtbl->SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		// 描画処理

		device->lpVtbl->SetRenderState(device, D3DRS_ZENABLE, D3DZB_FALSE);
		device->lpVtbl->DrawPrimitiveUP(device, D3DPT_TRIANGLESTRIP, 2, vertex, sizeof(VTX));
		device->lpVtbl->SetRenderState(device, D3DRS_ZENABLE, D3DZB_TRUE);
	}
}

static int get_vertex_buffer_size()
{
	return BridgeParameter::instance().finish_buffer_list.size();
}

static void remakePreviewTex(IDirect3DDevice9 *device)
{
	BridgeParameter& parameter = BridgeParameter::mutable_instance();
	if (parameter.preview_tex)
	{
		parameter.preview_tex->lpVtbl->Release(parameter.preview_tex);
		parameter.preview_tex = NULL;
	}

	D3DXCreateTexture(device, parameter.viewport_width, parameter.viewport_height, 0
		, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &parameter.preview_tex);
}

bool isGeometryUpdated = false;
static HRESULT WINAPI present(
	IDirect3DDevice9 *device, 
	const RECT * pSourceRect, 
	const RECT * pDestRect, 
	HWND hDestWindowOverride, 
	const RGNDATA * pDirtyRegion)
{
	const float time = ExpGetFrameTime();

	if (pDestRect)
	{
		BridgeParameter::mutable_instance().frame_width = pDestRect->right - pDestRect->left;
		BridgeParameter::mutable_instance().frame_height = pDestRect->bottom - pDestRect->top;
	}

	BridgeParameter& parameter = BridgeParameter::mutable_instance();

	D3DVIEWPORT9 viewport;
	device->lpVtbl->GetViewport(device, &viewport);
	if (parameter.viewport_width != viewport.Width || parameter.viewport_height != viewport.Height)
	{
		parameter.viewport_width = viewport.Width;
		parameter.viewport_height = viewport.Height;
		remakePreviewTex(device);
	}

	BridgeParameter::mutable_instance().is_exporting_without_mesh = false;
	overrideGLWindow();
	const bool validFrame = IsValidFrame();
	const bool validCallSetting = IsValidCallSetting();
	const bool validTechniq = IsValidTechniq();
	if (validFrame && validCallSetting && validTechniq)
	{
		if (script_call_setting > 0 && get_vertex_buffer_size() > 0)
		{
			const BridgeParameter& parameter = BridgeParameter::instance();
			int frame = static_cast<int>(time * BridgeParameter::instance().export_fps + 0.5f);
			bool recWindow = IsRecWindow();
			if (pre_buffer_size != get_vertex_buffer_size() || isRecWindow != IsRecWindow()) {
				if (!parameter.preview_tex) {
					remakePreviewTex(device);
				}
				frame_for_pt = 0;
				RemoveGeometry();
				StartOptix(script_call_setting, frame_for_pt);
				pre_buffer_size = get_vertex_buffer_size();
			}
			isRecWindow = recWindow;

			if (isGeometryUpdated) {
				UpdateOptixGeometry();
				frame_for_pt = 0;
				isGeometryUpdated = false;
			}

			if (pre_frame != frame)
			{
				UpdateOptixGeometry();
				pre_frame = frame;
				isGeometryUpdated = true;
			}

			D3DXMATRIX world = BridgeParameter::mutable_instance().first_noaccessory_buffer().world;
			D3DXVECTOR3 eye;
			UMGetCameraEye(&eye);
			D3DXVECTOR4 fov;
			UMGetCameraFovLH(&fov);

			if (pre_eye != eye || pre_fov != fov || pre_world != world) {
				frame_for_pt = 0;
				UpdateOptix(frame_for_pt);
				pre_world = world;
				pre_eye = eye;
				pre_fov = fov;
			}
			else {
				UpdateOptix(frame_for_pt);
			}
			++frame_for_pt;
		}
		BridgeParameter::mutable_instance().finish_buffer_list.clear();
		presentCount++;
	}
	drawPreview(device);
	HRESULT res = (*original_present)(device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);

	return res;
}

static HRESULT WINAPI reset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	HRESULT res = (*original_reset)(device, pPresentationParameters);
	::MessageBox(NULL, _T("MMDBridgeは、3D vision 未対応です"), _T("HOGE"), MB_OK);
	return res;
}

static HRESULT WINAPI setFVF(IDirect3DDevice9 *device, DWORD fvf)
{
	HRESULT res = (*original_set_fvf)(device, fvf);

	if (script_call_setting > 0)
	{
		renderData.fvf = fvf;
		DWORD pos = (fvf & D3DFVF_POSITION_MASK);
		renderData.pos = (pos > 0);
		renderData.pos_xyz	= ((pos & D3DFVF_XYZ) > 0);
		renderData.pos_rhw	= ((pos & D3DFVF_XYZRHW) > 0);
		renderData.pos_xyzb[0] = ((fvf & D3DFVF_XYZB1) == D3DFVF_XYZB1);
		renderData.pos_xyzb[1] = ((fvf & D3DFVF_XYZB2) == D3DFVF_XYZB2);
		renderData.pos_xyzb[2] = ((fvf & D3DFVF_XYZB3) == D3DFVF_XYZB3);
		renderData.pos_xyzb[3] = ((fvf & D3DFVF_XYZB4) == D3DFVF_XYZB4);
		renderData.pos_xyzb[4] = ((fvf & D3DFVF_XYZB5) == D3DFVF_XYZB5);
		renderData.pos_last_beta_ubyte4 = ((fvf & D3DFVF_LASTBETA_UBYTE4) > 0);
		renderData.normal	= ((fvf & D3DFVF_NORMAL) > 0);
		renderData.psize	= ((fvf & D3DFVF_PSIZE) > 0);
		renderData.diffuse	= ((fvf & D3DFVF_DIFFUSE) > 0);
		renderData.specular	= ((fvf & D3DFVF_SPECULAR) > 0);
		renderData.texcount	= (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	}

	return res;
}

static void setFVF(DWORD fvf)
{
	renderData.fvf = fvf;

	DWORD pos = (fvf & D3DFVF_POSITION_MASK);
	renderData.pos = (pos > 0);
	renderData.pos_xyz	= ((pos & D3DFVF_XYZ) > 0);
	renderData.pos_rhw	= ((pos & D3DFVF_XYZRHW) > 0);
	renderData.pos_xyzb[0] = ((fvf & D3DFVF_XYZB1) == D3DFVF_XYZB1);
	renderData.pos_xyzb[1] = ((fvf & D3DFVF_XYZB2) == D3DFVF_XYZB2);
	renderData.pos_xyzb[2] = ((fvf & D3DFVF_XYZB3) == D3DFVF_XYZB3);
	renderData.pos_xyzb[3] = ((fvf & D3DFVF_XYZB4) == D3DFVF_XYZB4);
	renderData.pos_xyzb[4] = ((fvf & D3DFVF_XYZB5) == D3DFVF_XYZB5);
	renderData.pos_last_beta_ubyte4 = ((fvf & D3DFVF_LASTBETA_UBYTE4) > 0);
	renderData.normal	= ((fvf & D3DFVF_NORMAL) > 0);
	renderData.psize	= ((fvf & D3DFVF_PSIZE) > 0);
	renderData.diffuse	= ((fvf & D3DFVF_DIFFUSE) > 0);
	renderData.specular	= ((fvf & D3DFVF_SPECULAR) > 0);
	renderData.texcount	= (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

}

HRESULT WINAPI clear(
	IDirect3DDevice9 *device, 
	DWORD count, 
	const D3DRECT *pRects, 
	DWORD flags, 
	D3DCOLOR color, 
	float z, 
	DWORD stencil)
{
	HRESULT res = (*original_clear)(device, count, pRects, flags, color, z, stencil);
	return res;
}

static void getTextureParameter(TextureParameter &param, RenderData& renderData)
{
	TextureSamplers::iterator tit0 = renderData.textureSamplers.find(0);
	TextureSamplers::iterator tit1 = renderData.textureSamplers.find(1);
	TextureSamplers::iterator tit2 = renderData.textureSamplers.find(2);

	param.hasTextureSampler0 = (tit0 != renderData.textureSamplers.end());
	param.hasTextureSampler1 = (tit1 != renderData.textureSamplers.end());
	param.hasTextureSampler2 = (tit2 != renderData.textureSamplers.end());

	if (param.hasTextureSampler1) {
		LPWSTR name = UMGetTextureName(tit1->second);
		param.texture = tit1->second;
		param.textureMemoryName = to_string(param.texture);
		if (name)
		{
			std::wstring wname(name);
			to_string(param.textureName, wname);
		}
		if (UMIsAlphaTexture(param.texture))
		{
			param.hasAlphaTexture = true;
		}
	}
}

// 頂点・法線バッファ・テクスチャをメモリに書き込み
static bool writeBuffersToMemory(IDirect3DDevice9 *device, RenderData& renderData)
{
	const int currentTechnic = ExpGetCurrentTechnic();
	const int currentMaterial = ExpGetCurrentMaterial();
	const int currentObject = ExpGetCurrentObject();

	BYTE *pVertexBuf;
	IDirect3DVertexBuffer9 *pStreamData = renderData.pStreamData;

	IndexBufferList& finishBuffers = BridgeParameter::mutable_instance().finish_buffer_list;
	if (std::find(finishBuffers.begin(), finishBuffers.end(), renderData.pIndexData) == finishBuffers.end())
	{
		VertexBuffers::iterator vit = renderData.vertexBuffers.find(pStreamData);
		if(vit != renderData.vertexBuffers.end())
		{
			RenderBufferMap& renderedBuffers = BridgeParameter::mutable_instance().render_buffer_map;
			pStreamData->lpVtbl->Lock(pStreamData, 0, 0, (void**)&pVertexBuf, D3DLOCK_READONLY);

			// FVF取得
			DWORD fvf;
			device->lpVtbl->GetFVF(device, &fvf);
			if (renderData.fvf != fvf)
			{
				setFVF(fvf);
			}

			RenderedBuffer renderedBuffer;

			::D3DXMatrixIdentity(&renderedBuffer.world);
			::D3DXMatrixIdentity(&renderedBuffer.view);
			::D3DXMatrixIdentity(&renderedBuffer.projection);
			::D3DXMatrixIdentity(&renderedBuffer.world_inv);
			device->lpVtbl->GetTransform(device ,D3DTS_WORLD, &renderedBuffer.world);
			device->lpVtbl->GetTransform(device ,D3DTS_VIEW, &renderedBuffer.view);
			device->lpVtbl->GetTransform(device ,D3DTS_PROJECTION, &renderedBuffer.projection);

			::D3DXMatrixInverse(&renderedBuffer.world_inv, NULL, &renderedBuffer.world);

			int bytePos = 0;

			if (renderedBuffer.world.m[0][0] == 0 && renderedBuffer.world.m[1][1]== 0 && renderedBuffer.world.m[2][2]== 0)
			{
				return false;
			}

			renderedBuffer.isAccessory = false;
			D3DXMATRIX accesosoryMat;
			for (int i = 0; i < ExpGetAcsNum(); ++i)
			{
				int order = ExpGetAcsOrder(i);
				if (order == currentObject)
				{
					renderedBuffer.isAccessory = true;
					renderedBuffer.accessory = i;
					accesosoryMat = ExpGetAcsWorldMat(i);
				}
			}

			// 頂点
			if (renderData.pos_xyz)
			{
				int initialVertexSize = renderedBuffer.vertecies.size();
				const int size = (vit->second - bytePos) / renderData.stride;
				renderedBuffer.vertecies.resize(size);
				for (size_t i =  bytePos, n = 0; i < vit->second; i += renderData.stride, ++n)
				{
					D3DXVECTOR3 v;
					memcpy(&v, &pVertexBuf[i], sizeof(D3DXVECTOR3));
					if (renderedBuffer.isAccessory)
					{
						D3DXVECTOR4 dst;
						::D3DXVec3Transform(&dst, &v, &accesosoryMat);
						v.x = dst.x;
						v.y = dst.y;
						v.z = dst.z;
					}

					renderedBuffer.vertecies[n] = v;
				}
				bytePos += (sizeof(DWORD) * 3);
			}

			// ウェイト（略）

			// 法線
			if (renderData.normal)
			{
				for (size_t i = bytePos; i < vit->second; i += renderData.stride)
				{
					D3DXVECTOR3 n;
					memcpy(&n, &pVertexBuf[i], sizeof( D3DXVECTOR3 ));
					renderedBuffer.normals.push_back(n);
				}
				bytePos += (sizeof(DWORD) * 3);
			}

			// 頂点カラー
			if (renderData.diffuse)
			{
				for (size_t i = 0; i < vit->second; i += renderData.stride)
				{
					DWORD diffuse;
					memcpy(&diffuse, &pVertexBuf[i], sizeof( DWORD ));
					renderedBuffer.diffuses.push_back(diffuse);

				}
				bytePos += (sizeof(DWORD));
			}

			// UV
			if (renderData.texcount > 0) 
			{
				for (int n = 0; n < renderData.texcount; ++n) 
				{
					for (size_t i = bytePos; i < vit->second; i += renderData.stride) 
					{
						UMVec2f uv;
						memcpy(&uv, &pVertexBuf[i], sizeof( UMVec2f ));
						renderedBuffer.uvs.push_back(uv);
					}
					bytePos += (sizeof(DWORD) * 2);
				}
			}

			
			pStreamData->lpVtbl->Unlock(pStreamData);

			// メモリに保存
			finishBuffers.push_back(renderData.pIndexData);
			renderedBuffers[renderData.pIndexData] = renderedBuffer;
		}
	}
	return true;
}

static bool writeMaterialsToMemory(TextureParameter & textureParameter, RenderData& renderData)
{
	const int currentTechnic = ExpGetCurrentTechnic();
	const int currentMaterial = ExpGetCurrentMaterial();
	const int currentObject = ExpGetCurrentObject();

	IDirect3DVertexBuffer9 *pStreamData = renderData.pStreamData;
	RenderBufferMap& renderedBuffers = BridgeParameter::mutable_instance().render_buffer_map;
	if (renderedBuffers.find(renderData.pIndexData) == renderedBuffers.end())
	{
		return false;
	}

	bool notFoundObjectMaterial = (renderedMaterials.find(currentObject) == renderedMaterials.end());
	if (notFoundObjectMaterial || renderedMaterials[currentObject].find(currentMaterial) == renderedMaterials[currentObject].end())
	{
		// D3DMATERIAL9 取得
		D3DMATERIAL9 material = ExpGetPmdMaterial(currentObject, currentMaterial);
		//p_device->lpVtbl->GetMaterial(p_device, &material);
		
		RenderedMaterial *mat = new RenderedMaterial();
		mat->diffuse.x = material.Diffuse.r;
		mat->diffuse.y = material.Diffuse.g;
		mat->diffuse.z = material.Diffuse.b;
		mat->diffuse.w = material.Diffuse.a;
		mat->specular.x = material.Specular.r;
		mat->specular.y = material.Specular.g;
		mat->specular.z = material.Specular.b;
		mat->ambient.x = material.Ambient.r;
		mat->ambient.y = material.Ambient.g;
		mat->ambient.z = material.Ambient.b;
		mat->emissive.x = material.Emissive.r;
		mat->emissive.y = material.Emissive.g;
		mat->emissive.z = material.Emissive.b;
		mat->power = material.Power;
		
		// シェーダー時
		if (currentTechnic == 2) {
			LPD3DXEFFECT* effect =  UMGetEffect();

			if (effect) {
				D3DXHANDLE current = (*effect)->lpVtbl->GetCurrentTechnique(*effect);
				D3DXHANDLE texHandle1 = (*effect)->lpVtbl->GetTechniqueByName(*effect, "DiffuseBSSphiaTexTec");
				D3DXHANDLE texHandle2 = (*effect)->lpVtbl->GetTechniqueByName(*effect, "DiffuseBSTextureTec");
				D3DXHANDLE texHandle3 = (*effect)->lpVtbl->GetTechniqueByName(*effect, "BShadowSphiaTextureTec");
				D3DXHANDLE texHandle4 = (*effect)->lpVtbl->GetTechniqueByName(*effect, "BShadowTextureTec");
				if (current == texHandle1) {
					//::MessageBoxA(NULL, "1", "transp", MB_OK);
					textureParameter.hasTextureSampler2 = true;
				}
				if (current == texHandle2) {
					//::MessageBoxA(NULL, "2", "transp", MB_OK);
					textureParameter.hasTextureSampler2 = true;
				}
				if (current == texHandle3) {
					//::MessageBoxA(NULL, "3", "transp", MB_OK);
					textureParameter.hasTextureSampler2 = true;
				}
				if (current == texHandle4) {
					//::MessageBoxA(NULL, "4", "transp", MB_OK);
					textureParameter.hasTextureSampler2 = true;
				}

				D3DXHANDLE hEdge = (*effect)->lpVtbl->GetParameterByName(*effect, NULL, "EgColor");
				D3DXHANDLE hDiffuse = (*effect)->lpVtbl->GetParameterByName(*effect, NULL, "DifColor");
				D3DXHANDLE hToon = (*effect)->lpVtbl->GetParameterByName(*effect, NULL, "ToonColor");
				D3DXHANDLE hSpecular = (*effect)->lpVtbl->GetParameterByName(*effect, NULL, "SpcColor");
				D3DXHANDLE hTransp = (*effect)->lpVtbl->GetParameterByName(*effect, NULL, "transp");
				
				float edge[4];
				float diffuse[4];
				float specular[4];
				float toon[4];
				BOOL transp;
				(*effect)->lpVtbl->GetFloatArray(*effect, hEdge, edge, 4);
				(*effect)->lpVtbl->GetFloatArray(*effect, hToon, toon, 4);
				(*effect)->lpVtbl->GetFloatArray(*effect, hDiffuse, diffuse, 4);
				(*effect)->lpVtbl->GetFloatArray(*effect, hSpecular, specular, 4);
				(*effect)->lpVtbl->GetBool(*effect, hTransp, &transp);
				mat->diffuse.x = edge[0] + specular[0];
				if (mat->diffuse.x > 1) { mat->diffuse.x = 1.0f; }
				mat->diffuse.y = edge[1] + specular[1];
				if (mat->diffuse.y > 1) { mat->diffuse.y = 1.0f; }
				mat->diffuse.z = edge[2] + specular[2];
				if (mat->diffuse.z > 1) { mat->diffuse.z = 1.0f; }
				mat->diffuse.w = edge[3];

				if (specular[0] != 0 || specular[1] != 0 || specular[2] != 0)
				{
					mat->specular.x = specular[0];
					mat->specular.y = specular[1];
					mat->specular.z = specular[2];
				}
			}
		}

		if (renderData.texcount > 0)
		{
			DWORD colorRop0;
			DWORD colorRop1;

			p_device->lpVtbl->GetTextureStageState(p_device, 0, D3DTSS_COLOROP, &colorRop0);
			p_device->lpVtbl->GetTextureStageState(p_device, 1, D3DTSS_COLOROP, &colorRop1);

			if (textureParameter.hasTextureSampler2) {

				mat->tex = textureParameter.texture;
				mat->texture = textureParameter.textureName;
				mat->memoryTexture = textureParameter.textureMemoryName;

				if (!textureParameter.hasAlphaTexture)
				{
					mat->diffuse.w = 1.0f;
				}
			} else	if (textureParameter.hasTextureSampler0 || textureParameter.hasTextureSampler1) {
				if (colorRop0 != D3DTOP_DISABLE && colorRop1 != D3DTOP_DISABLE)
				{
					mat->tex = textureParameter.texture;
					mat->texture = textureParameter.textureName;
					mat->memoryTexture = textureParameter.textureMemoryName;
					if (!textureParameter.hasAlphaTexture)
					{
						mat->diffuse.w = 1.0f;
					}
				}
			}
		}

		RenderedBuffer &renderedBuffer = renderedBuffers[renderData.pIndexData];
		if (renderedBuffer.isAccessory)
		{
			D3DMATERIAL9 accessoryMat = ExpGetAcsMaterial(renderedBuffer.accessory, currentMaterial);
			mat->diffuse.x = accessoryMat.Diffuse.r;
			mat->diffuse.y = accessoryMat.Diffuse.g;
			mat->diffuse.z = accessoryMat.Diffuse.b;
			mat->specular.x = accessoryMat.Specular.r;
			mat->specular.y = accessoryMat.Specular.g;
			mat->specular.z = accessoryMat.Specular.b;
			mat->ambient.x = accessoryMat.Ambient.r;
			mat->ambient.y = accessoryMat.Ambient.g;
			mat->ambient.z = accessoryMat.Ambient.b;
			mat->diffuse.w = ::ExpGetAcsTr(renderedBuffer.accessory);
		}

		renderedBuffer.materials.push_back(mat);
		renderedBuffer.material_map[currentMaterial] = mat;
		renderedMaterials[currentObject][currentMaterial] = mat;
	}
	else
	{
		std::map<int, RenderedMaterial*>& materialMap = renderedMaterials[currentObject];
		renderedBuffers[renderData.pIndexData].materials.push_back(materialMap[currentMaterial]);
		renderedBuffers[renderData.pIndexData].material_map[currentMaterial] = materialMap[currentMaterial];
		renderedMaterials[currentObject][currentMaterial] = materialMap[currentMaterial];
	}

	if (renderedBuffers[renderData.pIndexData].materials.size() > 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

static void writeMatrixToMemory(IDirect3DDevice9 *device, RenderedBuffer &dst)
{
	::D3DXMatrixIdentity(&dst.world);
	::D3DXMatrixIdentity(&dst.view);
	::D3DXMatrixIdentity(&dst.projection);
	device->lpVtbl->GetTransform(device ,D3DTS_WORLD, &dst.world);
	device->lpVtbl->GetTransform(device ,D3DTS_VIEW, &dst.view);
	device->lpVtbl->GetTransform(device ,D3DTS_PROJECTION, &dst.projection);
}

static void writeLightToMemory(IDirect3DDevice9 *device, RenderedBuffer &renderedBuffer)
{
	BOOL isLight;
	int lightNumber = 0;			
	 p_device->lpVtbl->GetLightEnable(p_device, lightNumber, &isLight);
	 if (isLight)
	 {
		D3DLIGHT9  light;
		p_device->lpVtbl->GetLight(p_device, lightNumber, &light);
		UMVec3f& umlight = renderedBuffer.light;
		D3DXVECTOR3 v(light.Direction.x, light.Direction.y, light.Direction.z);
		D3DXVECTOR4 dst;
		//D3DXVec3Transform(&dst, &v, &renderedBuffer.world);
		umlight.x = v.x;
		umlight.y = v.y;
		umlight.z = v.z;
		
		renderedBuffer.light_color.x = light.Ambient.r;
		renderedBuffer.light_color.y = light.Ambient.g;
		renderedBuffer.light_color.z = light.Ambient.b;
		//renderedBuffer.light_diffuse.x = light.Diffuse.r;
		//renderedBuffer.light_diffuse.y = light.Diffuse.g;
		//renderedBuffer.light_diffuse.z = light.Diffuse.b;
		//renderedBuffer.light_diffuse.w = light.Diffuse.a;
		//renderedBuffer.light_specular.x = light.Specular.r;
		//renderedBuffer.light_specular.y = light.Specular.g;
		//renderedBuffer.light_specular.z = light.Specular.b;
		//renderedBuffer.light_specular.w = light.Specular.a;
		//renderedBuffer.light_position.x = light.Position.x;
		//renderedBuffer.light_position.y = light.Position.y;
		//renderedBuffer.light_position.z = light.Position.z;
	}
}

static HRESULT WINAPI drawIndexedPrimitive(
	IDirect3DDevice9 *device, 
	D3DPRIMITIVETYPE type, 
	INT baseVertexIndex, 
	UINT minIndex,
	UINT numVertices, 
	UINT startIndex, 
	UINT primitiveCount)
{
	PrimitiveInfo info;
	info.type = type;
	info.baseVertexIndex = baseVertexIndex;
	info.minIndex = minIndex;
	info.numVertices = numVertices;
	info.startIndex = startIndex;
	info.primitiveCount = primitiveCount;
	info.renderData = renderData;
	updatePrimitive(device, info);

	HRESULT res = (*original_draw_indexed_primitive)(device, type, baseVertexIndex, minIndex, numVertices, startIndex, primitiveCount);
	UMSync();
	return res;
}

static HRESULT WINAPI drawPrimitive(
	IDirect3DDevice9 *device, 
	D3DPRIMITIVETYPE primitiveType, 
	UINT startVertex,
	UINT primitiveCount)
{
	const int currentTechnic = ExpGetCurrentTechnic();
	HRESULT res = (*original_draw_primitive)(device, primitiveType, startVertex, primitiveCount);
	UMSync();
	return res;
}

static HRESULT WINAPI createTexture(
	IDirect3DDevice9* device,
	UINT width,
	UINT height,
	UINT levels,
	DWORD usage,
	D3DFORMAT format,
	D3DPOOL pool,
	IDirect3DTexture9** ppTexture,
	HANDLE* pSharedHandle)
{
	HRESULT res = (*original_create_texture)(device, width, height, levels, usage, format, pool, ppTexture, pSharedHandle);

	TextureInfo info;
	info.wh.x = width;
	info.wh.y = height;
	info.format = format;
	renderData.textureBuffers[*ppTexture] = info;

	return res;

}

static HRESULT WINAPI createVertexBuffer(
	IDirect3DDevice9* device,
	UINT length,
	DWORD usage,
	DWORD fvf,
	D3DPOOL pool,
	IDirect3DVertexBuffer9** ppVertexBuffer,
	HANDLE* pHandle)
{
	HRESULT res = (*original_create_vertex_buffer)(device, length, usage, fvf, pool, ppVertexBuffer, pHandle);
	
	renderData.vertexBuffers[*ppVertexBuffer] = length;

	return res;
}

static HRESULT WINAPI setTexture(
	IDirect3DDevice9* device,
	DWORD sampler,	
	IDirect3DBaseTexture9 * pTexture)
{
	if (presentCount == 0) {
		IDirect3DTexture9* texture = reinterpret_cast<IDirect3DTexture9*>(pTexture);
		renderData.textureSamplers[sampler] = texture;
	}

	HRESULT res = (*original_set_texture)(device, sampler, pTexture);

	return res;
}

static HRESULT WINAPI setStreamSource(
	IDirect3DDevice9 *device, 
	UINT streamNumber,
	IDirect3DVertexBuffer9 *pStreamData,
	UINT offsetInBytes,
	UINT stride)
{
	HRESULT res = (*original_set_stream_source)(device, streamNumber, pStreamData, offsetInBytes, stride);
	
	int currentTechnic = ExpGetCurrentTechnic();

	const bool validCallSetting = IsValidCallSetting();
	const bool validFrame = IsValidFrame();
	const bool validTechniq = IsValidTechniq() || currentTechnic == 5;

	if (validCallSetting && validFrame && validTechniq) 
	{
		if (pStreamData) {
			renderData.streamNumber = streamNumber;
			renderData.pStreamData = pStreamData;
			renderData.offsetInBytes = offsetInBytes;
			renderData.stride = stride;
		}
	}

	return res;
}

// IDirect3DDevice9::SetIndices
static HRESULT WINAPI setIndices(IDirect3DDevice9 *device, IDirect3DIndexBuffer9 * pIndexData)
{
	HRESULT res = (*original_set_indices)(device, pIndexData);
			
	int currentTechnic = ExpGetCurrentTechnic();

	const bool validCallSetting = IsValidCallSetting();
	const bool validFrame = IsValidFrame();
	const bool validTechniq =  IsValidTechniq() || currentTechnic == 5;
	if (validCallSetting && validFrame && validTechniq) 
	{
		renderData.pIndexData = pIndexData;
	}
	
	return res;
}

// IDirect3DDevice9::BeginStateBlock
// この関数で、lpVtblが修正されるので、lpVtbl書き換えなおす
static HRESULT WINAPI beginStateBlock(IDirect3DDevice9 *device)
{
	originalDevice();
	HRESULT res = (*original_begin_state_block)(device);
	
	p_device = device;
	hookDevice();

	return res;
}

// IDirect3DDevice9::EndStateBlock
// この関数で、lpVtblが修正されるので、lpVtbl書き換えなおす
static HRESULT WINAPI endStateBlock(IDirect3DDevice9 *device, IDirect3DStateBlock9 **ppSB)
{
	originalDevice();
	HRESULT res = (*original_end_state_block)(device, ppSB);

	p_device = device;
	hookDevice();

	return res;
}

//static ULONG WINAPI release(
//	IDirect3D9 *direct3d)
//{
//	return (*original_release)(direct3d);
//}
//
//static ULONG WINAPI releaseEx(
//	IDirect3D9Ex *direct3d)
//{
//	DisposeOptix();
//	return (*original_release_ex)(direct3d);
//}

static void hookDevice()
{
	if (p_device) 
	{
		// 書き込み属性付与
		DWORD old_protect;
		VirtualProtect(reinterpret_cast<void *>(p_device->lpVtbl), sizeof(p_device->lpVtbl), PAGE_EXECUTE_READWRITE, &old_protect);
		
		p_device->lpVtbl->BeginScene = beginScene;
		p_device->lpVtbl->EndScene = endScene;
		//p_device->lpVtbl->Release = release;
		//p_device->lpVtbl->Clear = clear;
		p_device->lpVtbl->Present = present;
		//p_device->lpVtbl->Reset = reset;
		p_device->lpVtbl->BeginStateBlock = beginStateBlock;
		p_device->lpVtbl->EndStateBlock = endStateBlock;		
		p_device->lpVtbl->SetFVF = setFVF;
		p_device->lpVtbl->DrawIndexedPrimitive = drawIndexedPrimitive;
		p_device->lpVtbl->DrawPrimitive = drawPrimitive;
		p_device->lpVtbl->SetStreamSource = setStreamSource;
		p_device->lpVtbl->SetIndices = setIndices;
		p_device->lpVtbl->CreateVertexBuffer = createVertexBuffer;
		p_device->lpVtbl->SetTexture = setTexture;
		p_device->lpVtbl->CreateTexture = createTexture;
		//p_device->lpVtbl->SetTextureStageState = setTextureStageState;

		// 書き込み属性元に戻す
		VirtualProtect(reinterpret_cast<void *>(p_device->lpVtbl), sizeof(p_device->lpVtbl), old_protect, &old_protect);
	}
}

static void originalDevice()
{
	if (p_device) 
	{
		// 書き込み属性付与
		DWORD old_protect;
		VirtualProtect(reinterpret_cast<void *>(p_device->lpVtbl), sizeof(p_device->lpVtbl), PAGE_EXECUTE_READWRITE, &old_protect);
		
		p_device->lpVtbl->BeginScene = original_begin_scene;
		p_device->lpVtbl->EndScene = original_end_scene;
		//p_device->lpVtbl->Clear = clear;
		p_device->lpVtbl->Present = original_present;
		//p_device->lpVtbl->Reset = reset;
		p_device->lpVtbl->BeginStateBlock = original_begin_state_block;
		p_device->lpVtbl->EndStateBlock = original_end_state_block;		
		p_device->lpVtbl->SetFVF = original_set_fvf;
		p_device->lpVtbl->DrawIndexedPrimitive = original_draw_indexed_primitive;
		p_device->lpVtbl->DrawPrimitive = original_draw_primitive;
		p_device->lpVtbl->SetStreamSource = original_set_stream_source;
		p_device->lpVtbl->SetIndices = original_set_indices;
		p_device->lpVtbl->CreateVertexBuffer = original_create_vertex_buffer;
		p_device->lpVtbl->SetTexture = original_set_texture;
		p_device->lpVtbl->CreateTexture = original_create_texture;
		//p_device->lpVtbl->SetTextureStageState = setTextureStageState;

		// 書き込み属性元に戻す
		VirtualProtect(reinterpret_cast<void *>(p_device->lpVtbl), sizeof(p_device->lpVtbl), old_protect, &old_protect);
	}
}
static HRESULT WINAPI createDevice(
	IDirect3D9 *direct3d,
	UINT adapter,
	D3DDEVTYPE type,
	HWND window,
	DWORD flag,
	D3DPRESENT_PARAMETERS *param,
	IDirect3DDevice9 **device) 
{
	HRESULT res = (*original_create_device)(direct3d, adapter, type, window, flag, param, device);
	p_device = (*device);
	
	if (p_device) {
		original_begin_scene = p_device->lpVtbl->BeginScene;
		original_end_scene = p_device->lpVtbl->EndScene;
		//original_release = p_device->lpVtbl->Release;
		//original_clear = p_device->lpVtbl->Clear;
		original_present = p_device->lpVtbl->Present;
		//original_reset = p_device->lpVtbl->Reset;
		original_begin_state_block = p_device->lpVtbl->BeginStateBlock;
		original_end_state_block = p_device->lpVtbl->EndStateBlock;
		original_set_fvf = p_device->lpVtbl->SetFVF;
		original_draw_indexed_primitive = p_device->lpVtbl->DrawIndexedPrimitive;
		original_draw_primitive = p_device->lpVtbl->DrawPrimitive;
		original_set_stream_source = p_device->lpVtbl->SetStreamSource;
		original_set_indices = p_device->lpVtbl->SetIndices;
		original_create_vertex_buffer = p_device->lpVtbl->CreateVertexBuffer;
		original_set_texture = p_device->lpVtbl->SetTexture;
		original_create_texture = p_device->lpVtbl->CreateTexture;
		//original_set_texture_stage_state = p_device->lpVtbl->SetTextureStageState;

		hookDevice();
	}
	return res;
}

static HRESULT WINAPI createDeviceEx(
	IDirect3D9Ex *direct3dex,
	UINT adapter,
	D3DDEVTYPE type,
	HWND window,
	DWORD flag,
	D3DPRESENT_PARAMETERS *param,
	D3DDISPLAYMODEEX* displayMode,
	IDirect3DDevice9Ex **device)
{
	HRESULT res = (*original_create_deviceex)(direct3dex, adapter, type, window, flag, param, displayMode, device);
	p_device = reinterpret_cast<IDirect3DDevice9*>(*device);

	if (p_device) {
		original_begin_scene = p_device->lpVtbl->BeginScene;
		original_end_scene = p_device->lpVtbl->EndScene;
		//original_release = p_device->lpVtbl->Release;
		//original_clear = p_device->lpVtbl->Clear;
		original_present = p_device->lpVtbl->Present;
		//original_reset = p_device->lpVtbl->Reset;
		original_begin_state_block = p_device->lpVtbl->BeginStateBlock;
		original_end_state_block = p_device->lpVtbl->EndStateBlock;
		original_set_fvf = p_device->lpVtbl->SetFVF;
		original_draw_indexed_primitive = p_device->lpVtbl->DrawIndexedPrimitive;
		original_draw_primitive = p_device->lpVtbl->DrawPrimitive;
		original_set_stream_source = p_device->lpVtbl->SetStreamSource;
		original_set_indices = p_device->lpVtbl->SetIndices;
		original_create_vertex_buffer = p_device->lpVtbl->CreateVertexBuffer;
		original_set_texture = p_device->lpVtbl->SetTexture;
		original_create_texture = p_device->lpVtbl->CreateTexture;
		//original_set_texture_stage_state = p_device->lpVtbl->SetTextureStageState;

		hookDevice();
	}
	return res;
}

extern "C" {
	// 偽Direct3DCreate9
	IDirect3D9 * WINAPI Direct3DCreate9(UINT SDKVersion) {
		IDirect3D9 *direct3d((*original_direct3d_create)(SDKVersion));
		original_create_device = direct3d->lpVtbl->CreateDevice;
		//original_release = direct3d->lpVtbl->Release;

		// 書き込み属性付与
		DWORD old_protect;
		VirtualProtect(reinterpret_cast<void *>(direct3d->lpVtbl), sizeof(direct3d->lpVtbl), PAGE_EXECUTE_READWRITE, &old_protect);

		direct3d->lpVtbl->CreateDevice = createDevice;
		//direct3d->lpVtbl->Release = release;

		// 書き込み属性元に戻す
		VirtualProtect(reinterpret_cast<void *>(direct3d->lpVtbl), sizeof(direct3d->lpVtbl), old_protect, &old_protect);

		return direct3d;
	}

	HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D) {
		IDirect3D9Ex *direct3d9ex = NULL;
		(*original_direct3d9ex_create)(SDKVersion, &direct3d9ex);

		if (direct3d9ex) 
		{
			original_create_deviceex = direct3d9ex->lpVtbl->CreateDeviceEx;
			//original_release_ex = direct3d9ex->lpVtbl->Release;
			if (original_create_deviceex)
			{
				// 書き込み属性付与
				DWORD old_protect;
				VirtualProtect(reinterpret_cast<void *>(direct3d9ex->lpVtbl), sizeof(direct3d9ex->lpVtbl), PAGE_EXECUTE_READWRITE, &old_protect);

				direct3d9ex->lpVtbl->CreateDeviceEx = createDeviceEx;
				//direct3d9ex->lpVtbl->Release = releaseEx;

				// 書き込み属性元に戻す
				VirtualProtect(reinterpret_cast<void *>(direct3d9ex->lpVtbl), sizeof(direct3d9ex->lpVtbl), old_protect, &old_protect);

				*ppD3D = direct3d9ex;
				return S_OK;
			}
		}
		return E_ABORT;
	}

} // extern "C"

bool d3d9_initialize()
{
	// MMDフルパスの取得.
	{
		wchar_t app_full_path[1024];
		GetModuleFileName(NULL, app_full_path, sizeof(app_full_path) / sizeof(wchar_t));
		std::wstring path(app_full_path);
		BridgeParameter::mutable_instance().base_path = path.substr(0, path.rfind(_T("MikuMikuDance.exe")));
	}

	// システムパス保存用
	TCHAR system_path_buffer[1024];
	GetSystemDirectory(system_path_buffer, MAX_PATH );
	std::wstring d3d9_path(system_path_buffer);
	d3d9_path.append(_T("\\D3D9.DLL"));
	// オリジナルのD3D9.DLLのモジュール
	HMODULE d3d9_module(LoadLibrary(d3d9_path.c_str()));

	if (!d3d9_module) {
		return FALSE;
	}

	// オリジナルDirect3DCreate9の関数ポインタを取得
	original_direct3d_create = reinterpret_cast<IDirect3D9 *(WINAPI*)(UINT)>(GetProcAddress(d3d9_module, "Direct3DCreate9"));
	if (!original_direct3d_create) {
		return FALSE;
	}
	original_direct3d9ex_create = reinterpret_cast<HRESULT(WINAPI*)(UINT, IDirect3D9Ex**)>(GetProcAddress(d3d9_module, "Direct3DCreate9Ex"));
	if (!original_direct3d9ex_create) {
		return FALSE;
	}

	return TRUE;
}
	
void d3d9_dispose() 
{
	renderData.dispose();
}

// DLLエントリポイント
BOOL APIENTRY DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
	switch (reason) 
	{
		case DLL_PROCESS_ATTACH:
			hInstance=hinst;
			d3d9_initialize();
			break;
		case DLL_THREAD_DETACH:
			break;
		case DLL_PROCESS_DETACH:
			//DisposeOptix();
			d3d9_dispose();
			break;
	}
	return TRUE;
}

