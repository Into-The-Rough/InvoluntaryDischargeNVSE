#define _CRT_SECURE_NO_WARNINGS
#include "Detours.hpp"
#include "InvoluntaryDischarge.hpp"
#include <Windows.h>
#include <cstdlib>
#include <cmath>
#include <ctime>

typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned int UInt32;
typedef int SInt32;

template <typename T_Ret = void, typename ...Args>
__forceinline T_Ret ThisCall(UInt32 addr, void* _this, Args ...args) {
	return ((T_Ret(__thiscall*)(void*, Args...))addr)(_this, args...);
}

template <typename T_Ret = void, typename ...Args>
__forceinline T_Ret CdeclCall(UInt32 addr, Args ...args) {
	return ((T_Ret(__cdecl*)(Args...))addr)(args...);
}

constexpr UInt32 kFunc_GetNiNode = 0x43FCD0;
constexpr UInt32 kFunc_FireWeapon = 0x523150;
constexpr UInt32 kFunc_ApplyLinearImpulse = 0x9B0C50;
constexpr UInt32 kFunc_LookupForm = 0x4839C0;
constexpr UInt32 kHook_KillDropItem = 0x89F71C;
constexpr UInt32 kHook_DisarmDropItem = 0x8919C5;
constexpr UInt32 kRTTI_bhkNiCollisionObject = 0x1267B70;
constexpr UInt32 kRTTI_NiAVObject = 0x11F4280;
constexpr UInt32 kVtbl_BSFadeNode = 0x10A8F90;

struct NiVector3 { float x, y, z; };

struct TESForm {
	void* vtbl;
	UInt8 typeID;     //0x04
	UInt8 pad05[3];
	UInt32 flags;     //0x08
	UInt32 refID;     //0x0C
	UInt8 pad10[8];
};

struct BSExtraData {
	void* vtbl;
	UInt8 type;       //0x04
	UInt8 pad05[3];
	BSExtraData* next; //0x08
};

struct ExtraHealth : BSExtraData {
	float health;     //0x0C
};

struct BaseExtraList {
	void* vtbl;
	BSExtraData* data; //0x04
	UInt8 presence[0x18];
};

struct TESObjectREFR : TESForm {
	UInt8 pad18[0x08];
	TESForm* baseForm;          //0x20
	UInt8 pad24[0x0C];
	NiVector3 position;         //0x30
	float scale;                //0x3C
	void* parentCell;           //0x40
	BaseExtraList extraDataList; //0x44

	void* Get3D() { return ThisCall<void*>(kFunc_GetNiNode, this); }
};

struct ActorValueOwner {
	void* vtbl;
	float GetActorValue(UInt32 avCode) {
		return ((float(__thiscall*)(void*, UInt32))(*(UInt32*)(*(UInt32*)this + 0x0C)))(this, avCode);
	}
};

struct Actor : TESObjectREFR {
	UInt8 padPostRef[0x04];     //0x64
	void* baseProcess;          //0x68
	UInt8 pad6C[0x38];
	ActorValueOwner avOwner;    //0xA4

	bool IsInCombat() {
		auto vtbl = *(UInt32**)this;
		return ((bool(__thiscall*)(void*, bool))vtbl[0x274 / 4])(this, false);
	}
};

enum {
	kWeaponSound_Loud = 0,
	kWeaponSound_Normal = 1,
};

struct TESObjectWEAP {
	UInt32 GetMaxHealth() { return *(UInt32*)((UInt8*)this + 0x098); }
	TESForm* GetAmmoForm() { return *(TESForm**)((UInt8*)this + 0x0A8); }
	UInt8 GetWeaponType() { return *(UInt8*)((UInt8*)this + 0x0F4); }
	UInt8 GetWeaponFlags1() { return *(UInt8*)((UInt8*)this + 0x100); }
	void* GetProjectile() { return *(void**)((UInt8*)this + 0x118); }
	float GetFireRate() { return *(float*)((UInt8*)this + 0x134); }

	bool IsAutomatic() { return GetWeaponFlags1() & 0x2; }
	bool IsGun() { auto t = GetWeaponType(); return t >= 3 && t <= 9; }

	UInt32 GetDetectionSoundLevel() {
		auto level = *(UInt32*)((UInt8*)this + 0x364) & 0x3;
		if (level == kWeaponSound_Loud) return 100;
		if (level == kWeaponSound_Normal) return 50;
		return 0;
	}
};

struct TESAmmo {
	float GetWeight() { return *(float*)((UInt8*)this + 0x0B8); }
};

enum {
	kFormType_TESObjectWEAP = 0x28,
	kFormType_TESAmmo = 0x2E,
	kFormType_BGSListForm = 0x55,
	kFormType_Character = 0x3B,
	kFormType_Creature = 0x3C,
};

enum { eActorVal_Luck = 11 };

extern void SafeWrite8(UInt32 addr, UInt8 data);
extern void* g_eventInterface;

namespace InvoluntaryDischarge {

static Detours::CallDetour s_killDropItemCall;
static Detours::CallDetour s_disarmDropItemCall;
typedef void(__cdecl* AppendDroppedItem_t)(Actor*, TESObjectREFR*);

namespace Config {
	static bool bCombatOnly = false;
	static bool bGuaranteedFire = false;
	static bool bIgnorePlayerDrops = true;
	static int iBurstCap = 8;
	static int iShotSpacingMs = 100;
	static float fBaseKick = 85.0f;
	static float fKickPerAmmoWeight = 10.0f;
	static float fLuckWeight = 1.9f;

	static float GetFloat(const char* section, const char* key, float def, const char* path) {
		char buf[32];
		GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path);
		return buf[0] ? (float)atof(buf) : def;
	}
	static int GetInt(const char* section, const char* key, int def, const char* path) {
		return GetPrivateProfileIntA(section, key, def, path);
	}
	static bool GetBool(const char* section, const char* key, bool def, const char* path) {
		return GetPrivateProfileIntA(section, key, def ? 1 : 0, path) != 0;
	}
}

struct PendingDischarge {
	UInt32 refID;
	UInt32 weapFormID;
	UInt32 dropperID;
	int roundsLeft;
	DWORD queuedAt;
	DWORD lastShotAt;
	DWORD fireIntervalMs;
	bool landed;
};

static constexpr int kMaxPending = 16;
static PendingDischarge g_pending[kMaxPending] = {};
static const char* g_iniPath = nullptr;

static TESObjectREFR* GetRefFromHavokBody(UInt8* hkpObj) {
	auto* parentData = *(UInt8**)(hkpObj + 0x78);
	int parentCount = *(int*)(hkpObj + 0x7C);
	UInt8* niNode = nullptr;
	for (int i = 0; i < parentCount && !niNode; i++) {
		auto* entry = parentData + i * 0x10;
		UInt32 type = *(UInt32*)entry;
		if (type == kRTTI_bhkNiCollisionObject) {
			auto* collObj = *(UInt8**)(entry + 0x08);
			if (collObj) niNode = *(UInt8**)(collObj + 0x08);
		} else if (type == kRTTI_NiAVObject) {
			niNode = *(UInt8**)(entry + 0x08);
		}
	}
	if (!niNode) return nullptr;

	for (auto* node = niNode; node; node = *(UInt8**)(node + 0x18))
		if (*(UInt32*)node == kVtbl_BSFadeNode)
			if (auto* ref = *(TESObjectREFR**)(node + 0xCC))
				return ref;
	return nullptr;
}

static bool HasContactExcluding(TESObjectREFR* ref, UInt32 excludeRefID) {
	auto* ni = (UInt8*)ref->Get3D();
	if (!ni) return false;
	auto* collObj = *(UInt8**)(ni + 0x1C);
	if (!collObj) return false;
	auto* worldObj = *(UInt8**)((UInt8*)collObj + 0x10);
	if (!worldObj) return false;
	auto* hkpObj = *(UInt8**)((UInt8*)worldObj + 0x08);
	if (!hkpObj || hkpObj[0x28] != 1) return false;

	auto* contactsArr = *(UInt8**)(hkpObj + 0xAC);
	UInt16 contactsSize = *(UInt16*)(hkpObj + 0xB0);
	for (UInt16 i = 0; i < contactsSize && contactsArr; i++) {
		auto* contactBody = *(UInt8**)(contactsArr + i * 0x30 + 0x08);
		if (!contactBody) continue;
		auto* contactRef = GetRefFromHavokBody(contactBody);
		if (!contactRef || contactRef->refID != excludeRefID)
			return true;
	}

	auto** constraints = *(UInt8***)(hkpObj + 0xB4);
	UInt32 constraintCount = *(UInt32*)(hkpObj + 0xB8);
	for (UInt32 i = 0; i < constraintCount && constraints; i++) {
		auto* inst = constraints[i];
		if (!inst) continue;
		auto* contactBody = *(UInt8**)(inst + 0x14);
		if (!contactBody) continue;
		auto* contactRef = GetRefFromHavokBody(contactBody);
		if (!contactRef || contactRef->refID != excludeRefID)
			return true;
	}

	return false;
}

static float GetRefHealthPercent(TESObjectREFR* ref, TESObjectWEAP* weapon) {
	for (auto* xData = ref->extraDataList.data; xData; xData = xData->next) {
		if (xData->type == 0x25) {
			float current = ((ExtraHealth*)xData)->health;
			float max = (float)weapon->GetMaxHealth();
			if (max > 0.0f) return (current / max) * 100.0f;
			return 100.0f;
		}
	}
	return 100.0f;
}

static float GetAmmoWeight(TESObjectWEAP* weapon) {
	auto* ammo = weapon->GetAmmoForm();
	if (!ammo) return 0.0f;
	if (ammo->typeID == kFormType_BGSListForm) {
		auto* firstAmmo = *(TESForm**)((UInt8*)ammo + 0x18);
		if (firstAmmo) ammo = firstAmmo;
		else return 0.0f;
	}
	if (!ammo || ammo->typeID != kFormType_TESAmmo) return 0.0f;
	return ((TESAmmo*)ammo)->GetWeight();
}

static float RandomFloat() {
	return (float)rand() / (float)RAND_MAX;
}

static bool IsAlreadyQueued(UInt32 refID) {
	for (int i = 0; i < kMaxPending; i++)
		if (g_pending[i].refID == refID) return true;
	return false;
}

static void QueueDischarge(UInt32 refID, UInt32 weapFormID, UInt32 dropperID, int rounds, DWORD fireIntervalMs) {
	if (IsAlreadyQueued(refID)) return;
	DWORD now = GetTickCount();
	for (int i = 0; i < kMaxPending; i++) {
		if (!g_pending[i].refID) {
			g_pending[i] = { refID, weapFormID, dropperID, rounds, now, 0, fireIntervalMs, false };
			return;
		}
	}
}

static void ApplyImpulse(TESObjectREFR* ref, TESObjectWEAP* weapon) {
	auto* ni = ref->Get3D();
	if (!ni) return;

	auto* collObj = *(void**)((UInt8*)ni + 0x1C);
	if (!collObj) return;

	auto* worldObj = *(void**)((UInt8*)collObj + 0x10);
	if (!worldObj) return;

	auto* rigidBody = *(UInt8**)((UInt8*)worldObj + 0x08);
	if (!rigidBody) return;
	if (rigidBody[0x28] != 1) return;
	if (!(rigidBody[0xE8] & 2)) return;

	float ammoWeight = GetAmmoWeight(weapon);
	float magnitude = Config::fBaseKick + (Config::fKickPerAmmoWeight * ammoWeight);

	float yaw = RandomFloat() * 6.2831853f;
	__declspec(align(16)) float impulse[4] = {
		cosf(yaw) * magnitude,
		sinf(yaw) * magnitude,
		magnitude * 0.5f,
		0.0f
	};

	ThisCall(kFunc_ApplyLinearImpulse, worldObj, &impulse);
}

static void CreateDetectionEvent(TESObjectREFR* ref, UInt32 dropperID, UInt32 soundLevel) {
	auto* dropperForm = CdeclCall<TESForm*>(kFunc_LookupForm, dropperID);
	if (!dropperForm) return;
	if (dropperForm->typeID != kFormType_Character && dropperForm->typeID != kFormType_Creature) return;

	auto* actor = (Actor*)dropperForm;
	auto* process = actor->baseProcess;
	if (!process) return;

	auto vtbl = *(UInt32**)process;
	((void(__thiscall*)(void*, void*, float, float, float, UInt32, UInt32, void*))vtbl[0x0FC / 4])(
		process, actor, ref->position.x, ref->position.y, ref->position.z,
		soundLevel, 3, ref
	);
}

static void FireOneRound(TESObjectREFR* ref, TESObjectWEAP* weapon, UInt32 dropperID) {
	ThisCall(kFunc_FireWeapon, weapon, ref);
	ApplyImpulse(ref, weapon);
	CreateDetectionEvent(ref, dropperID, weapon->GetDetectionSoundLevel());
}

static void ProcessOneSlot(PendingDischarge& p) {
	DWORD now = GetTickCount();
	DWORD elapsed = now - p.queuedAt;

	auto* refForm = CdeclCall<TESForm*>(kFunc_LookupForm, p.refID);
	if (!refForm) { p.refID = 0; return; }

	auto* ref = (TESObjectREFR*)refForm;
	auto* ni = ref->Get3D();
	if (!ni) {
		if (elapsed > 5000) p.refID = 0;
		return;
	}

	if (!p.landed) {
		if (!HasContactExcluding(ref, p.dropperID)) {
			if (elapsed > 5000) p.refID = 0;
			return;
		}
		p.landed = true;
		p.lastShotAt = now - p.fireIntervalMs;
	}

	if (now - p.lastShotAt < p.fireIntervalMs) return;
	p.lastShotAt = now;

	auto* weapForm = CdeclCall<TESForm*>(kFunc_LookupForm, p.weapFormID);
	if (!weapForm) { p.refID = 0; return; }

	FireOneRound(ref, (TESObjectWEAP*)weapForm, p.dropperID);
	if (--p.roundsLeft <= 0) p.refID = 0;
}

static bool RollChance(TESObjectREFR* droppedRef, TESObjectWEAP* weapon, float luck) {
	float healthPercent = GetRefHealthPercent(droppedRef, weapon);
	float chance = 100.0f - healthPercent + ((10.0f - luck) * Config::fLuckWeight);
	return RandomFloat() * 100.0f < chance;
}

static void TryQueueWeapon(TESObjectREFR* droppedRef, TESForm* baseForm, TESObjectWEAP* weapon,
                           float luck, UInt32 dropperID) {
	if (!Config::bGuaranteedFire && !RollChance(droppedRef, weapon, luck)) return;

	int rounds = 1;
	if (weapon->IsAutomatic() && Config::iBurstCap > 1)
		rounds = 1 + (rand() % Config::iBurstCap);

	DWORD intervalMs = Config::iShotSpacingMs;

	QueueDischarge(droppedRef->refID, baseForm->refID, dropperID, rounds, intervalMs);
}

static void HandleDroppedItem(Detours::CallDetour& detour, Actor* actor, TESObjectREFR* droppedRef) {
	auto original = (AppendDroppedItem_t)detour.GetOverwrittenAddr();
	original(actor, droppedRef);

	if (!actor || !droppedRef) return;

	auto* baseForm = droppedRef->baseForm;
	if (!baseForm || baseForm->typeID != kFormType_TESObjectWEAP) return;

	auto* weapon = (TESObjectWEAP*)baseForm;
	if (!weapon->IsGun() || !weapon->GetProjectile()) return;
	if (Config::bCombatOnly && !actor->IsInCombat()) return;

	float luck = actor->avOwner.GetActorValue(eActorVal_Luck);
	TryQueueWeapon(droppedRef, baseForm, weapon, luck, actor->refID);
}

void __cdecl KillDropItemHook(Actor* actor, TESObjectREFR* droppedRef) {
	HandleDroppedItem(s_killDropItemCall, actor, droppedRef);
}

void __cdecl DisarmDropItemHook(Actor* actor, TESObjectREFR* droppedRef) {
	HandleDroppedItem(s_disarmDropItemCall, actor, droppedRef);
}

void OnDropHandler(TESObjectREFR* thisObj, void* params) {
	if (!params) return;
	auto** p = (TESForm**)params;

	auto* dropperRef = (TESObjectREFR*)p[0];
	auto* droppedRef = (TESObjectREFR*)p[1];
	if (!dropperRef || !droppedRef) return;
	if (Config::bIgnorePlayerDrops && dropperRef->refID == 0x14) return;

	auto* baseForm = droppedRef->baseForm;
	if (!baseForm || baseForm->typeID != kFormType_TESObjectWEAP) return;

	auto* weapon = (TESObjectWEAP*)baseForm;
	if (!weapon->IsGun() || !weapon->GetProjectile()) return;

	bool isActor = (dropperRef->typeID == kFormType_Character || dropperRef->typeID == kFormType_Creature);
	float luck = 5.0f;

	if (isActor) {
		auto* actor = (Actor*)dropperRef;
		if (Config::bCombatOnly && !actor->IsInCombat()) return;
		luck = actor->avOwner.GetActorValue(eActorVal_Luck);
	} else if (Config::bCombatOnly) {
		return;
	}

	TryQueueWeapon(droppedRef, baseForm, weapon, luck, dropperRef->refID);
}

void ReadConfig(const char* iniPath) {
	g_iniPath = iniPath;
	WritePrivateProfileStringA(NULL, NULL, NULL, iniPath);
	Config::bCombatOnly = Config::GetBool("Main", "bCombatOnly", false, iniPath);
	Config::bGuaranteedFire = Config::GetBool("Main", "bGuaranteedFire", false, iniPath);
	Config::bIgnorePlayerDrops = Config::GetBool("Main", "bIgnorePlayerDrops", true, iniPath);
	Config::iBurstCap = Config::GetInt("Main", "iBurstCap", 8, iniPath);
	Config::iShotSpacingMs = Config::GetInt("Main", "iShotSpacingMs", 100, iniPath);
	Config::fBaseKick = Config::GetFloat("Main", "fBaseKick", 85.0f, iniPath);
	Config::fKickPerAmmoWeight = Config::GetFloat("Main", "fKickPerAmmoWeight", 10.0f, iniPath);
	Config::fLuckWeight = Config::GetFloat("Main", "fLuckWeight", 1.9f, iniPath);
}

void InitHooks() {
	s_killDropItemCall.WriteRelCall(kHook_KillDropItem, KillDropItemHook);
	s_disarmDropItemCall.WriteRelCall(kHook_DisarmDropItem, DisarmDropItemHook);

	if (Config::bGuaranteedFire) {
		SafeWrite8(0x89F177, 0xEB);
		SafeWrite8(0x89F1E0, 0xEB);
	}

	srand((unsigned int)time(nullptr));
}

void InitDeferredHooks() {
	struct EventMgr {
		bool (*RegisterEvent)(const char*, UInt8, void*, UInt32);
		bool (*DispatchEvent)(const char*, TESObjectREFR*, ...);
		int (*DispatchEventAlt)(const char*, void*, void*, TESObjectREFR*, ...);
		bool (*SetNativeEventHandler)(const char*, void (*)(TESObjectREFR*, void*));
	};

	if (g_eventInterface) {
		auto* em = (EventMgr*)g_eventInterface;
		em->SetNativeEventHandler("OnDrop", OnDropHandler);
	}
}

void OnGameLoad() {
	for (int i = 0; i < kMaxPending; i++)
		g_pending[i].refID = 0;
}

void Update() {
	for (int i = 0; i < kMaxPending; i++)
		if (g_pending[i].refID)
			ProcessOneSlot(g_pending[i]);
}

}
