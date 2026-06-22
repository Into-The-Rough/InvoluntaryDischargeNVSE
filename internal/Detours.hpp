#pragma once

#include <Windows.h>
#include <cstring>

typedef unsigned char UInt8;
typedef unsigned int UInt32;

namespace Detours {

class CallDetour {
	UInt32 overwrittenAddr = 0;
	UInt32 sourceAddr = 0;
	UInt32 installedRel = 0;
	UInt8 originalBytes[5] = {};

public:
	bool WriteRelCall(UInt32 src, UInt32 dst) {
		if (sourceAddr)
			return false;
		if (*(UInt8*)src != 0xE8)
			return false;

		DWORD oldProtect;
		if (!VirtualProtect((void*)src, sizeof(originalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
			return false;

		memcpy(originalBytes, (void*)src, sizeof(originalBytes));
		overwrittenAddr = *(UInt32*)(src + 1) + src + sizeof(originalBytes);
		sourceAddr = src;
		installedRel = dst - src - sizeof(originalBytes);
		*(UInt32*)(src + 1) = installedRel;

		VirtualProtect((void*)src, sizeof(originalBytes), oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), (void*)src, sizeof(originalBytes));
		return true;
	}

	template <typename T>
	bool WriteRelCall(UInt32 src, T dst) {
		return WriteRelCall(src, (UInt32)dst);
	}

	UInt32 GetOverwrittenAddr() const { return overwrittenAddr; }
	bool IsInstalled() const { return sourceAddr != 0; }

	bool Remove() {
		if (!sourceAddr)
			return false;
		if (*(UInt8*)sourceAddr != 0xE8 || *(UInt32*)(sourceAddr + 1) != installedRel)
			return false;

		DWORD oldProtect;
		if (!VirtualProtect((void*)sourceAddr, sizeof(originalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
			return false;

		memcpy((void*)sourceAddr, originalBytes, sizeof(originalBytes));
		VirtualProtect((void*)sourceAddr, sizeof(originalBytes), oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), (void*)sourceAddr, sizeof(originalBytes));

		sourceAddr = 0;
		overwrittenAddr = 0;
		installedRel = 0;
		memset(originalBytes, 0, sizeof(originalBytes));
		return true;
	}
};

}
