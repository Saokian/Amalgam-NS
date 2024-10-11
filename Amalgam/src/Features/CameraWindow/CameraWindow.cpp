#include "CameraWindow.h"

// Draws camera to the screen
void CCameraWindow::Draw()
{
	if (!m_bShouldDraw || !m_pCameraMaterial || !I::EngineClient->IsInGame())
		return;

	const WindowBox_t& info = Vars::Visuals::Simulation::ProjectileWindow.Value;

	// Draw to screen
	const auto renderCtx = I::MaterialSystem->GetRenderContext();
	renderCtx->DrawScreenSpaceRectangle(
		m_pCameraMaterial,
		info.x, info.y, info.w, info.h,
		0, 0, info.w, info.h,
		m_pCameraTexture->GetActualWidth(), m_pCameraTexture->GetActualHeight(),
		nullptr, 1, 1
	);
	renderCtx->Release();
}

// Renders another view onto a texture
void CCameraWindow::RenderView(void* ecx, const CViewSetup& pViewSetup)
{
	if (!m_bShouldDraw || !m_pCameraTexture)
		return;

	m_bDrawing = true;

	const WindowBox_t& info = Vars::Visuals::Simulation::ProjectileWindow.Value;

	CViewSetup viewSetup = pViewSetup;
	viewSetup.x = 0;
	viewSetup.y = 0;

	viewSetup.origin = m_vCameraOrigin;
	viewSetup.angles = m_vCameraAngles;

	viewSetup.width = info.w + 1;
	viewSetup.height = info.h + 1;
	viewSetup.m_flAspectRatio = static_cast<float>(viewSetup.width) / static_cast<float>(viewSetup.height);
	viewSetup.fov = 90;

	RenderCustomView(ecx, viewSetup, m_pCameraTexture);

	m_bDrawing = false;
}

void CCameraWindow::RenderCustomView(void* ecx, const CViewSetup& pViewSetup, ITexture* pTexture)
{
	const auto renderCtx = I::MaterialSystem->GetRenderContext();

	renderCtx->PushRenderTargetAndViewport();
	renderCtx->SetRenderTarget(pTexture);

	static auto ViewRender_RenderView = U::Hooks.m_mHooks["CViewRender_RenderView"];
	if (ViewRender_RenderView)
		ViewRender_RenderView->Original<void(__fastcall*)(void*, const CViewSetup&, int, int)>()(ecx, pViewSetup, VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH, RENDERVIEW_UNSPECIFIED);

	renderCtx->PopRenderTargetAndViewport();
	renderCtx->Release();
}

void CCameraWindow::Initialize()
{
	m_pCameraTexture = I::MaterialSystem->CreateNamedRenderTargetTextureEx("m_pCameraTexture", 1, 1, RT_SIZE_FULL_FRAME_BUFFER, IMAGE_FORMAT_RGB888, MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, CREATERENDERTARGETFLAGS_HDR);

	KeyValues* kv = new KeyValues("UnlitGeneric");
	kv->SetString("$basetexture", "m_pCameraTexture");
	m_pCameraMaterial = I::MaterialSystem->CreateMaterial("m_pCameraMaterial", kv);
}

void CCameraWindow::Unload()
{
	m_pCameraTexture->DecrementReferenceCount();
	m_pCameraTexture->DeleteIfUnreferenced();
	m_pCameraTexture = nullptr;

	m_pCameraMaterial->DecrementReferenceCount();
	m_pCameraMaterial->DeleteIfUnreferenced();
	m_pCameraMaterial = nullptr;
}