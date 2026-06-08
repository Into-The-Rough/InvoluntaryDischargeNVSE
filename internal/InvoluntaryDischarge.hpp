#pragma once

namespace InvoluntaryDischarge {
	void ReadConfig(const char* iniPath);
	void InitHooks();
	void InitDeferredHooks();
	void OnGameLoad();
	void Update();
}
