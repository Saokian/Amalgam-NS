#include "Misc.h"

#include "../Backtrack/Backtrack.h"
#include "../PacketManip/AntiAim/AntiAim.h"
#include "../TickHandler/TickHandler.h"
#include "../Players/PlayerUtils.h"

void CMisc::RunPre(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	CheatsBypass();
	PingReducer();
	WeaponSway();

	if (!pLocal)
		return;

	AntiAFK(pLocal, pCmd);
	InstantRespawnMVM(pLocal);

	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming() || pLocal->IsCharging() || pLocal->IsInBumperKart())
		return;

	AutoJump(pLocal, pCmd);
	AutoJumpbug(pLocal, pCmd);
	AutoStrafe(pLocal, pCmd);
	AutoPeek(pLocal, pCmd);
	MovementLock(pLocal, pCmd);
}

void CMisc::RunPost(CTFPlayer* pLocal, CUserCmd* pCmd, bool pSendPacket)
{
	if (!pLocal || !pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming() || pLocal->IsCharging())
		return;

	TauntKartControl(pLocal, pCmd);
	FastMovement(pLocal, pCmd);
	AntiWarp(pLocal, pCmd);
	LegJitter(pLocal, pCmd, pSendPacket);
}



void CMisc::AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::Bunnyhop.Value)
		return;

	static bool bStaticJump = false, bStaticGrounded = false, bLastAttempted = false;
	const bool bLastJump = bStaticJump, bLastGrounded = bStaticGrounded;
	const bool bCurJump = bStaticJump = pCmd->buttons & IN_JUMP, bCurGrounded = bStaticGrounded = pLocal->m_hGroundEntity();

	if (bCurJump && bLastJump)
	{
		if (!(bCurGrounded && !bLastGrounded))
			pCmd->buttons &= ~IN_JUMP;

		if (!(pCmd->buttons & IN_JUMP) && bCurGrounded && !bLastAttempted)
			pCmd->buttons |= IN_JUMP;
	}

	bLastAttempted = pCmd->buttons & IN_JUMP;
}

void CMisc::AutoJumpbug(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoJumpbug.Value || !(pCmd->buttons & IN_DUCK) || pLocal->m_hGroundEntity() || pLocal->m_vecVelocity().z > -650.f)
		return;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};

	Vec3 origin = pLocal->m_vecOrigin();
	SDK::TraceHull(origin, origin - Vec3(0, 0, 22), pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
	if (!trace.DidHit()) // don't try if we aren't in range to unduck
		return;

	const float flDist = origin.DistTo(trace.endpos);
	if (20.f < flDist /*&& flDist < 22.f*/) // this seems to be the range where this works
	{
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
	}
}

void CMisc::AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoStrafe.Value || pLocal->m_hGroundEntity() || !(pLocal->m_afButtonLast() & IN_JUMP) && (pCmd->buttons & IN_JUMP))
		return;

	switch (Vars::Misc::Movement::AutoStrafe.Value)
	{
	case Vars::Misc::Movement::AutoStrafeEnum::Legit:
	{
		static auto cl_sidespeed = U::ConVars.FindVar("cl_sidespeed");
		const float flSideSpeed = cl_sidespeed ? cl_sidespeed->GetFloat() : 450.f;

		if (pCmd->mousedx)
		{
			pCmd->forwardmove = 0.f;
			pCmd->sidemove = pCmd->mousedx > 0 ? flSideSpeed : -flSideSpeed;
		}
		break;
	}
	case Vars::Misc::Movement::AutoStrafeEnum::Directional:
	{
		// credits: KGB
		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			break;

		float flForwardMove = pCmd->forwardmove;
		float flSideMove = pCmd->sidemove;

		Vec3 vForward, vRight; Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);

		vForward.z = vRight.z = 0.f;

		vForward.Normalize();
		vRight.Normalize();

		Vec3 vWishDir = {};
		Math::VectorAngles({ (vForward.x * flForwardMove) + (vRight.x * flSideMove), (vForward.y * flForwardMove) + (vRight.y * flSideMove), 0.f }, vWishDir);

		Vec3 vCurDir = {};
		Math::VectorAngles(pLocal->m_vecVelocity(), vCurDir);

		float flDirDelta = Math::NormalizeAngle(vWishDir.y - vCurDir.y);
		float flTurnScale = Math::RemapValClamped(Vars::Misc::Movement::AutoStrafeTurnScale.Value, 0.f, 1.f, 0.9f, 1.f);
		float flRotation = DEG2RAD((flDirDelta > 0.f ? -90.f : 90.f) + (flDirDelta * flTurnScale));

		float flCosRot = cosf(flRotation);
		float flSinRot = sinf(flRotation);

		pCmd->forwardmove = (flCosRot * flForwardMove) - (flSinRot * flSideMove);
		pCmd->sidemove = (flSinRot * flForwardMove) + (flCosRot * flSideMove);
	}
	}
}

void CMisc::AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static bool bPosPlaced = false;
	static bool bReturning = false;

	if (Vars::CL_Move::AutoPeek.Value)
	{
		const Vec3 localPos = pLocal->GetAbsOrigin();

		// We just started peeking. Save the return position!
		if (!bPosPlaced)
		{
			if (pLocal->m_hGroundEntity())
			{
				vPeekReturnPos = localPos;
				bPosPlaced = true;
			}
		}
		else
		{
			static Timer particleTimer{};
			if (particleTimer.Run(700))
				H::Particles.DispatchParticleEffect("ping_circle", vPeekReturnPos, {});
		}

		// We've just attacked. Let's return!
		if (G::LastUserCmd->buttons & IN_ATTACK || G::IsAttacking)
			bReturning = true;

		if (bReturning)
		{
			if (localPos.DistTo(vPeekReturnPos) < 7.f)
			{
				bReturning = false;
				return;
			}

			SDK::WalkTo(pCmd, pLocal, vPeekReturnPos);
		}
	}
	else
	{
		bPosPlaced = bReturning = false;
		vPeekReturnPos = Vec3();
	}
}

void CMisc::MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static bool bLock = false;

	if (!Vars::Misc::Movement::MovementLock.Value)
	{
		bLock = false;
		return;
	}

	static Vec3 vDir = {};
	if (!bLock)
	{
		bLock = true;
		vDir = Math::RotatePoint({ pCmd->forwardmove * (fmodf(fabsf(pCmd->viewangles.x), 180.f) > 90.f ? -1 : 1), -pCmd->sidemove, 0.f }, {}, { 0, pCmd->viewangles.y, 0 });
		vDir.z = pCmd->upmove;
	}

	Vec3 vMove = Math::RotatePoint(vDir, {}, { 0, -pCmd->viewangles.y, 0 });
	pCmd->forwardmove = vMove.x * (fmodf(fabsf(pCmd->viewangles.x), 180.f) > 90.f ? -1 : 1), pCmd->sidemove = -vMove.y, pCmd->upmove = vDir.z;
}

void CMisc::AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static Timer afkTimer{};

	static auto mp_idledealmethod = U::ConVars.FindVar("mp_idledealmethod");
	static auto mp_idlemaxtime = U::ConVars.FindVar("mp_idlemaxtime");
	const int iIdleMethod = mp_idledealmethod ? mp_idledealmethod->GetInt() : 1;
	const float flMaxIdleTime = mp_idlemaxtime ? mp_idlemaxtime->GetFloat() : 3.f;

	if (pCmd->buttons & (IN_MOVELEFT | IN_MOVERIGHT | IN_FORWARD | IN_BACK) || !pLocal->IsAlive())
		afkTimer.Update();
	// Trigger 10 seconds before kick
	else if (Vars::Misc::Automation::AntiAFK.Value && iIdleMethod && afkTimer.Check(flMaxIdleTime * 60 * 1000 - 10000))
		pCmd->buttons |= pCmd->command_number % 2 ? IN_FORWARD : IN_BACK;
}

void CMisc::InstantRespawnMVM(CTFPlayer* pLocal)
{
	if (Vars::Misc::MannVsMachine::InstantRespawn.Value && I::EngineClient->IsInGame() && !pLocal->IsAlive())
	{
		KeyValues* kv = new KeyValues("MVM_Revive_Response");
		kv->SetInt("accepted", 1);
		I::EngineClient->ServerCmdKeyValues(kv);
	}
}

void CMisc::CheatsBypass()
{
	static bool bCheatSet = false;
	static auto sv_cheats = U::ConVars.FindVar("sv_cheats");
	if (sv_cheats)
	{
		if (Vars::Misc::Exploits::CheatsBypass.Value)
		{
			sv_cheats->m_nValue = 1;
			bCheatSet = true;
		}
		else if (bCheatSet)
		{
			sv_cheats->m_nValue = 0;
			bCheatSet = false;
		}
	}
}

void CMisc::PingReducer()
{
	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	if (!pNetChan)
		return;

	static auto cl_cmdrate = U::ConVars.FindVar("cl_cmdrate");
	const int iCmdRate = cl_cmdrate ? cl_cmdrate->GetInt() : 66;

	// force highest cl_updaterate command possible
	static auto sv_maxupdaterate = U::ConVars.FindVar("sv_maxupdaterate");
	const int iMaxUpdateRate = sv_maxupdaterate ? sv_maxupdaterate->GetInt() : 66;

	static Timer updateRateTimer{};
	if (updateRateTimer.Run(100))
	{
		if (iWishUpdaterate != iMaxUpdateRate)
		{
			NET_SetConVar cmd("cl_updaterate", std::to_string(iWishUpdaterate = iMaxUpdateRate).c_str());
			pNetChan->SendNetMsg(cmd);
		}

		const int iTarget = Vars::Misc::Exploits::PingReducer.Value ? Vars::Misc::Exploits::PingTarget.Value : iCmdRate;
		if (iWishCmdrate != iTarget)
		{
			NET_SetConVar cmd("cl_cmdrate", std::to_string(iWishCmdrate = iTarget).c_str());
			pNetChan->SendNetMsg(cmd);
		}
	}
}

void CMisc::WeaponSway()
{
	static auto cl_wpn_sway_interp = U::ConVars.FindVar("cl_wpn_sway_interp");
	static auto cl_wpn_sway_scale = U::ConVars.FindVar("cl_wpn_sway_scale");

	bool bSway = Vars::Visuals::Viewmodel::SwayInterp.Value || Vars::Visuals::Viewmodel::SwayScale.Value;
	if (cl_wpn_sway_interp)
		cl_wpn_sway_interp->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayInterp.Value : 0.f);
	if (cl_wpn_sway_scale)
		cl_wpn_sway_scale->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayScale.Value : 0.f);
}



void CMisc::TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	// Handle Taunt Slide
	if (Vars::Misc::Automation::TauntControl.Value && pLocal->IsTaunting())
	{
		if (pCmd->buttons & IN_BACK)
			pCmd->viewangles.x = 91.f;
		else if (!(pCmd->buttons & IN_FORWARD))
			pCmd->viewangles.x = 90.f;
		if (pCmd->buttons & IN_MOVELEFT)
			pCmd->sidemove = pCmd->viewangles.x != 90.f ? -50.f : -450.f;
		else if (pCmd->buttons & IN_MOVERIGHT)
			pCmd->sidemove = pCmd->viewangles.x != 90.f ? 50.f : 450.f;

		Vec3 vAngle = I::EngineClient->GetViewAngles();
		pCmd->viewangles.y = vAngle.y;

		G::SilentAngles = true;
	}
	else if (Vars::Misc::Automation::KartControl.Value && pLocal->IsInBumperKart())
	{
		const bool bForward = pCmd->buttons & IN_FORWARD;
		const bool bBack = pCmd->buttons & IN_BACK;
		const bool bLeft = pCmd->buttons & IN_MOVELEFT;
		const bool bRight = pCmd->buttons & IN_MOVERIGHT;

		const bool flipVar = pCmd->command_number % 2;
		if (bForward && (!bLeft && !bRight || !flipVar))
		{
			pCmd->forwardmove = 450.f;
			pCmd->viewangles.x = 0.f;
		}
		else if (bBack && (!bLeft && !bRight || !flipVar))
		{
			pCmd->forwardmove = 450.f;
			pCmd->viewangles.x = 91.f;
		}
		else if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))
		{
			if (flipVar || G::ShiftedTicks == G::MaxShift)
			{	// you could just do this if you didn't care about viewangles
				const Vec3 vecMove(pCmd->forwardmove, pCmd->sidemove, 0.f);
				const float flLength = vecMove.Length();
				Vec3 angMoveReverse;
				Math::VectorAngles(vecMove * -1.f, angMoveReverse);
				pCmd->forwardmove = -flLength;
				pCmd->sidemove = 0.f;
				pCmd->viewangles.y = fmodf(pCmd->viewangles.y - angMoveReverse.y, 360.f);
				pCmd->viewangles.z = 270.f;
				G::PSilentAngles = true;
			}
		}
		else
			pCmd->viewangles.x = 90.f;

		G::SilentAngles = true;
	}
}

void CMisc::FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal->m_hGroundEntity() || pLocal->IsInBumperKart())
		return;

	const float flSpeed = pLocal->m_vecVelocity().Length2D();
	const int flMaxSpeed = std::min(pLocal->m_flMaxspeed() * 0.9f, 520.f) - 10.f;
	const int iRun = !pCmd->forwardmove && !pCmd->sidemove ? 0 : flSpeed < flMaxSpeed ? 1 : 2;

	switch (iRun)
	{
	case 0:
	{
		if (!Vars::Misc::Movement::FastStop.Value || !flSpeed)
			return;

		Vec3 direction = pLocal->m_vecVelocity().toAngle();
		direction.y = pCmd->viewangles.y - direction.y;
		const Vec3 negatedDirection = direction.fromAngle() * -flSpeed;
		pCmd->forwardmove = negatedDirection.x;
		pCmd->sidemove = negatedDirection.y;

		break;
	}
	case 1:
	{
		if ((pLocal->IsDucking() ? !Vars::Misc::Movement::CrouchSpeed.Value : !Vars::Misc::Movement::FastAccel.Value) || G::IsAttacking || G::DoubleTap || G::Recharge || G::AntiAim || pCmd->command_number % 2)
			return;

		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			return;

		const Vec3 vecMove(pCmd->forwardmove, pCmd->sidemove, 0.f);
		const float flLength = vecMove.Length();
		Vec3 angMoveReverse;
		Math::VectorAngles(vecMove * -1.f, angMoveReverse);
		pCmd->forwardmove = -flLength;
		pCmd->sidemove = 0.f;
		pCmd->viewangles.y = fmodf(pCmd->viewangles.y - angMoveReverse.y, 360.f);
		pCmd->viewangles.z = 270.f;
		G::PSilentAngles = true;

		break;
	}
	}
}

void CMisc::AntiWarp(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static Vec3 vVelocity = {};
	if (G::AntiWarp)
	{
		const int iDoubletapTicks = F::Ticks.GetTicks(pLocal);

		Vec3 vAngles; Math::VectorAngles(vVelocity, vAngles);
		vAngles.y = pCmd->viewangles.y - vAngles.y;
		Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
		vForward *= vVelocity.Length();

		if (iDoubletapTicks > std::max(Vars::CL_Move::Doubletap::TickLimit.Value - 8, 3))
		{
			pCmd->forwardmove = -vForward.x;
			pCmd->sidemove = -vForward.y;
		}
		else if (iDoubletapTicks > 3)
		{
			pCmd->forwardmove = pCmd->sidemove = 0.f;
			pCmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
		}
		else
		{
			pCmd->forwardmove = vForward.x;
			pCmd->sidemove = vForward.y;
		}
	}
	else
		vVelocity = pLocal->m_vecVelocity();

	/*
	static bool bSet = false;

	if (!G::AntiWarp)
	{
		bSet = false;
		return;
	}

	if (!G::IsAttacking && !bSet)
	{
		bSet = true;
		SDK::StopMovement(pLocal, pCmd);
	}
	else
		pCmd->forwardmove = pCmd->sidemove = 0.f;
	*/
}

void CMisc::LegJitter(CTFPlayer* pLocal, CUserCmd* pCmd, bool pSendPacket)
{
	if (!Vars::AntiHack::AntiAim::MinWalk.Value || !F::AntiAim.YawOn() || G::IsAttacking || G::DoubleTap || pSendPacket || !pLocal->m_hGroundEntity() || pLocal->IsInBumperKart())
		return;

	static bool pos = true;
	const float scale = pLocal->IsDucking() ? 14.f : 1.f;
	if (pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f && pLocal->m_vecVelocity().Length2D() < 10.f)
	{
		pos ? pCmd->forwardmove = scale : pCmd->forwardmove = -scale;
		pos ? pCmd->sidemove = scale : pCmd->sidemove = -scale;
		pos = !pos;
	}
}



void CMisc::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		iWishCmdrate = iWishUpdaterate = -1;
		F::Backtrack.flWishInterp = 0.f;
		[[fallthrough]];
	case FNV1A::Hash32Const("teamplay_round_start"):
		G::LineStorage.clear();
		G::BoxStorage.clear();
		G::PathStorage.clear();
	}
}

void CMisc::DoubletapPacket(CUserCmd* pCmd, bool* pSendPacket)
{
	if (G::DoubleTap /*|| G::Warp*/)
	{
		*pSendPacket = G::ShiftedGoal == G::ShiftedTicks;
		if (I::ClientState->chokedcommands >= 21 // prevent overchoking
			|| G::ShiftedTicks == G::ShiftedGoal + Vars::CL_Move::Doubletap::TickLimit.Value - 1 && I::ClientState->chokedcommands) // unchoke if we are choking
			*pSendPacket = true;
	}
}

int CMisc::AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket)
{
	if (!Vars::Misc::Automation::AntiBackstab.Value || !bSendPacket || G::IsAttacking || pLocal->IsInBumperKart())
		return 0;

	std::vector<std::pair<Vec3, CBaseEntity*>> vTargets = {};
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->IsCloaked() || pPlayer->m_bFeignDeathReady())
			continue;

		auto pWeapon = pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
		if (!pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_KNIFE && pWeapon->m_iItemDefinitionIndex() != Heavy_t_TheHolidayPunch && pWeapon->m_iItemDefinitionIndex() != Pyro_m_TheBackburner && pWeapon->m_iItemDefinitionIndex() != Pyro_m_FestiveBackburner)
			continue;

		PlayerInfo_t pi{};
		if (I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &pi) && F::PlayerUtils.IsIgnored(pi.friendsID))
			continue;

		Vec3 vTargetPos = pPlayer->GetCenter() + pPlayer->m_vecVelocity() * F::Backtrack.GetReal();
		if (pLocal->GetCenter().DistTo(vTargetPos) > pLocal->TeamFortress_CalculateMaxSpeed() || !SDK::VisPos(pLocal, pPlayer, pLocal->GetCenter(), vTargetPos))
			continue;

		vTargets.push_back({ vTargetPos, pEntity });
	}

	std::sort(vTargets.begin(), vTargets.end(), [&](const auto& a, const auto& b) -> bool
		{
			return pLocal->GetCenter().DistTo(a.first) < pLocal->GetCenter().DistTo(b.first);
		});

	auto vTargetPos = vTargets.begin();
	if (vTargetPos == vTargets.end())
		return 0;

	switch (Vars::Misc::Automation::AntiBackstab.Value)
	{
	case Vars::Misc::Automation::AntiBackstabEnum::Yaw:
	{
		Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), vTargetPos->first);
		vAngleTo.x = pCmd->viewangles.x;
		SDK::FixMovement(pCmd, vAngleTo);
		pCmd->viewangles = vAngleTo;
		
		return 1;
	}
	case Vars::Misc::Automation::AntiBackstabEnum::Pitch:
	case Vars::Misc::Automation::AntiBackstabEnum::Fake:
	{
		bool bCheater = F::PlayerUtils.HasTag(vTargetPos->second->entindex(), CHEATER_TAG);
		// if the closest spy is a cheater, assume auto stab is being used, otherwise don't do anything if target is in front
		if (!bCheater)
		{
			auto TargetIsBehind = [&]()
				{
					Vec3 vToTarget = pLocal->m_vecOrigin() - vTargetPos->first;
					vToTarget.z = 0.f;
					const float flDist = vToTarget.Length();
					if (!flDist)
						return true;

					Vec3 vTargetAngles = pCmd->viewangles;

					vToTarget.Normalize();
					float flTolerance = 0.0625f;
					float flExtra = 2.f * flTolerance / flDist; // account for origin tolerance

					float flPosVsTargetViewMinDot = 0.f - 0.0031f - flExtra;

					Vec3 vTargetForward; Math::AngleVectors(vTargetAngles, &vTargetForward);
					vTargetForward.z = 0.f;
					vTargetForward.Normalize();

					return vToTarget.Dot(vTargetForward) > flPosVsTargetViewMinDot;
				};

			if (!TargetIsBehind())
				return 0;
		}

		if (!bCheater || Vars::Misc::Automation::AntiBackstab.Value == Vars::Misc::Automation::AntiBackstabEnum::Pitch)
		{
			pCmd->forwardmove *= -1;
			pCmd->viewangles.x = 269.f;
		}
		else
		{
			pCmd->viewangles.x = 271.f;
		}
		// may slip up some auto backstabs depending on mode, though we are still able to be stabbed

		return 2;
	}
	}

	return 0;
}

void CMisc::UnlockAchievements()
{
	const auto achievementmgr = reinterpret_cast<IAchievementMgr*(*)(void)>(U::Memory.GetVFunc(I::EngineClient, 114))();
	if (achievementmgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < achievementmgr->GetAchievementCount(); i++)
			achievementmgr->AwardAchievement(achievementmgr->GetAchievementByIndex(i)->GetAchievementID());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::LockAchievements()
{
	const auto achievementmgr = reinterpret_cast<IAchievementMgr*(*)(void)>(U::Memory.GetVFunc(I::EngineClient, 114))();
	if (achievementmgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < achievementmgr->GetAchievementCount(); i++)
			I::SteamUserStats->ClearAchievement(achievementmgr->GetAchievementByIndex(i)->GetName());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

bool CMisc::SteamRPC()
{
	/*
	if (!Vars::Misc::Steam::EnableRPC.Value)
	{
		if (!bSteamCleared) // stupid way to return back to normal rpc
		{
			I::SteamFriends->SetRichPresence("steam_display", ""); // this will only make it say "Team Fortress 2" until the player leaves/joins some server. its bad but its better than making 1000 checks to recreate the original
			bSteamCleared = true;
		}
		return false;
	}

	bSteamCleared = false;
	*/


	if (!Vars::Misc::Steam::EnableRPC.Value)
		return false;

	I::SteamFriends->SetRichPresence("steam_display", "#TF_RichPresence_Display");
	if (!I::EngineClient->IsInGame() && !Vars::Misc::Steam::OverrideMenu.Value)
		I::SteamFriends->SetRichPresence("state", "MainMenu");
	else
	{
		I::SteamFriends->SetRichPresence("state", "PlayingMatchGroup");

		switch (Vars::Misc::Steam::MatchGroup.Value)
		{
		case Vars::Misc::Steam::MatchGroupEnum::SpecialEvent: I::SteamFriends->SetRichPresence("matchgrouploc", "SpecialEvent"); break;
		case Vars::Misc::Steam::MatchGroupEnum::MvMMannUp: I::SteamFriends->SetRichPresence("matchgrouploc", "MannUp"); break;
		case Vars::Misc::Steam::MatchGroupEnum::Competitive: I::SteamFriends->SetRichPresence("matchgrouploc", "Competitive6v6"); break;
		case Vars::Misc::Steam::MatchGroupEnum::Casual: I::SteamFriends->SetRichPresence("matchgrouploc", "Casual"); break;
		case Vars::Misc::Steam::MatchGroupEnum::MvMBootCamp: I::SteamFriends->SetRichPresence("matchgrouploc", "BootCamp"); break;
		}
	}
	I::SteamFriends->SetRichPresence("currentmap", Vars::Misc::Steam::MapText.Value.c_str());
	I::SteamFriends->SetRichPresence("steam_player_group_size", std::to_string(Vars::Misc::Steam::GroupSize.Value).c_str());

	return true;
}

#ifdef DEBUG
void CMisc::DumpClassIDS() {
	std::ofstream fDump("CLASSIDDUMP.txt");
	fDump << "enum struct ETFClassID\n{\n";
	CClientClass* ClientClass = I::BaseClientDLL->GetAllClasses();
	while (ClientClass) {
		fDump << "	" << ClientClass->GetName() << " = " << ClientClass->m_ClassID << ",\n";
		ClientClass = ClientClass->m_pNext;
	}
	fDump << "}";
	fDump.close();
}
#endif