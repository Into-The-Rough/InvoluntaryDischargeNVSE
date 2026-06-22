#define _CRT_SECURE_NO_WARNINGS
#include "internal/InvoluntaryDischarge.hpp"
#include <Windows.h>
#include <cstdio>

typedef unsigned char UInt8;
typedef unsigned int UInt32;

enum {
	kInterface_Messaging = 2,
	kInterface_EventManager = 8,
	kMessage_PreLoadGame = 6,
	kMessage_PostPostLoad = 9,
	kMessage_MainGameLoop = 20,
	kMessage_ReloadConfig = 25,
};

struct PluginInfo {
	enum { kInfoVersion = 1 };
	UInt32 infoVersion;
	const char* name;
	UInt32 version;
};

struct NVSEInterface {
	UInt32 nvseVersion;
	UInt32 runtimeVersion;
	UInt32 editorVersion;
	UInt32 isEditor;
	void* RegisterCommand;
	void* SetOpcodeBase;
	void* (*QueryInterface)(UInt32 id);
	UInt32 (*GetPluginHandle)(void);
	void* RegisterTypedCommand;
	const char* (*GetRuntimeDirectory)(void);
	UInt32 isNogore;
};

struct NVSEMessagingInterface {
	UInt32 version;
	void (*RegisterListener)(UInt32 pluginHandle, const char* sender, void* callback);
	void* Dispatch;
};

struct NVSEMessage {
	const char* sender;
	UInt32 type;
	UInt32 dataLen;
	void* data;
};

void* g_eventInterface = nullptr;

static UInt32 g_pluginHandle = 0;
static char g_iniPath[MAX_PATH] = {0};

void SafeWrite8(UInt32 addr, UInt8 data) {
	DWORD oldProtect;
	VirtualProtect((void*)addr, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
	*(UInt8*)addr = data;
	VirtualProtect((void*)addr, 1, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), (void*)addr, 1);
}

static void MessageHandler(NVSEMessage* msg) {
	switch (msg->type) {
	case kMessage_PostPostLoad:
		InvoluntaryDischarge::InitDeferredHooks();
		break;
	case kMessage_PreLoadGame:
		InvoluntaryDischarge::OnGameLoad();
		break;
	case kMessage_MainGameLoop:
		InvoluntaryDischarge::Update();
		break;
	case kMessage_ReloadConfig:
		if (msg->data && _stricmp((const char*)msg->data, "InvoluntaryDischargeNVSE") == 0)
			InvoluntaryDischarge::ReadConfig(g_iniPath);
		break;
	}
}

extern "C" {

__declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "InvoluntaryDischargeNVSE";
	info->version = 100;
	return !nvse->isEditor;
}

__declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {
	sprintf(g_iniPath, "%sData\\config\\InvoluntaryDischargeNVSE.ini", nvse->GetRuntimeDirectory());
	InvoluntaryDischarge::ReadConfig(g_iniPath);

	g_pluginHandle = nvse->GetPluginHandle();

	auto* messaging = (NVSEMessagingInterface*)nvse->QueryInterface(kInterface_Messaging);
	g_eventInterface = nvse->QueryInterface(kInterface_EventManager);

	if (messaging)
		messaging->RegisterListener(g_pluginHandle, "NVSE", (void*)MessageHandler);

	InvoluntaryDischarge::InitHooks();
	return true;
}

}

BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved) {
	return TRUE;
}
