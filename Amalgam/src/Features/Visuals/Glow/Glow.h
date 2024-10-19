#pragma once
#include "../../../SDK/SDK.h"

#include <boost/functional/hash.hpp>

class CGlow
{
	bool GetGlow(CTFPlayer* pLocal, CBaseEntity* pEntity, Glow_t* pGlow, Color_t* pColor);

	void SetupBegin(Glow_t glow, IMatRenderContext* pRenderContext, IMaterial* m_pMatGlowColor, IMaterial* m_pMatBlurY);
	void SetupMid(IMatRenderContext* pRenderContext, int w, int h);
	void SetupEnd(Glow_t glow, IMatRenderContext* pRenderContext, IMaterial* m_pMatBlurX, IMaterial* m_pMatBlurY, IMaterial* m_pMatHaloAddToScreen, int w, int h);

	void StencilBegin(IMatRenderContext* pRenderContext);
	void StencilPreDraw(IMatRenderContext* pRenderContext);
	void StencilEnd(IMatRenderContext* pRenderContext);

	void DrawModel(CBaseEntity* pEntity, bool bModel = false);

	void RenderBacktrack(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);
	void RenderFakeAngle(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo);

	ITexture* m_pRtFullFrame;
	ITexture* m_pRenderBuffer1;
	ITexture* m_pRenderBuffer2;



	struct GlowHasher_t
	{
		std::size_t operator()(const Glow_t& k) const
		{
			std::size_t seed = 0;

			boost::hash_combine(seed, boost::hash_value(k.Stencil));
			boost::hash_combine(seed, boost::hash_value(k.Blur));

			return seed;
		}
	};
	struct GlowInfo_t
	{
		CBaseEntity* m_pEntity;
		Color_t m_cColor;
		bool m_bExtra = false;
	};
	std::unordered_map<Glow_t, std::vector<GlowInfo_t>, GlowHasher_t> mvEntities = {};

	float flSavedBlend = 1.f;
	bool bExtra = false;

public:
	void Initialize();
	void Unload();

	void Store(CTFPlayer* pLocal);

	void RenderMain();
	void RenderHandler(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	void RenderViewmodel(void* ecx, int flags);
	void RenderViewmodel(const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld);

	bool bRendering = false;
};

ADD_FEATURE(CGlow, Glow)