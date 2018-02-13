#include <unordered_map>
#include <fstream>
#include <string>
#include <sstream>

#include "f4se/PluginAPI.h"
#include "f4se/GameAPI.h"
#include "f4se_common/f4se_version.h"

#include "f4se/GameData.h"
#include "f4se/GameRTTI.h"
#include "f4se/PapyrusNativeFunctions.h"

IDebugLog gLog("AutomatronDisassembler.log");

typedef std::unordered_map<BGSMod::Attachment::Mod*, TESObjectMISC*> ModMap;

struct {
	PluginHandle pluginHandle = kPluginHandle_Invalid;
	F4SEMessagingInterface* messaging = nullptr;
	F4SEPapyrusInterface* papyrus = nullptr;
	ModMap looseModTable;
	ModMap looseModTableIndex1;
	ModMap looseModTableIndex2;
} global;

void LoadExternalModMap(const char* fileName, ModMap& modMap) {
	std::ifstream fs(fileName);
	if (!fs) {
		_MESSAGE("Failed to load external mod map '%s'", fileName);
		return;
	}

	DataHandler* dataHandler = *g_dataHandler;

	std::string line;
	while (getline(fs, line)) {
		if (line.empty() || line[0] == ';') {
			continue;
		}

		std::stringstream str(line);

		std::string modName;
		getline(str, modName, '|');
		UInt32 formID;
		str >> std::hex >> formID;

		UInt32 modID = (UInt32)dataHandler->GetLoadedModIndex(modName.c_str()) << 24;
		BGSMod::Attachment::Mod* mod = (BGSMod::Attachment::Mod*)Runtime_DynamicCast(LookupFormByID(modID | formID), RTTI_TESForm, RTTI_BGSMod__Attachment__Mod);
		if (!mod) {
			continue;
		}

		str.ignore();
		getline(str, modName, '|');
		str >> std::hex >> formID;

		modID = (UInt32)dataHandler->GetLoadedModIndex(modName.c_str()) << 24;
		TESObjectMISC* looseMod = DYNAMIC_CAST(LookupFormByID(modID | formID), TESForm, TESObjectMISC);
		if (!looseMod) {
			continue;
		}

		modMap[mod] = looseMod;
		_MESSAGE("(External) Mapped '%s' (0x%.8x) to loose mod '%s'", mod->GetFullName(), mod->formID, looseMod->GetFullName());
	}
}

TESObjectMISC* GetLooseMod(BGSMod::Attachment::Mod* mod)
{
	auto pair = g_modAttachmentMap->Find(&mod);
	if (pair) {
		return pair->miscObject;
	}
	return nullptr;
}

TESObjectWEAP* GetForcedInventoryWeapon(BGSMod::Attachment::Mod* mod) {
	if (mod->targetType == BGSMod::Attachment::Mod::kTargetType_Actor) {
		for (UInt32 i = 0, end = mod->modContainer.dataSize / sizeof(BGSMod::Container::Data); i < end; i++)
		{
			BGSMod::Container::Data* data = &mod->modContainer.data[i];
			if (data->target == BGSMod::Container::kActorTarget_piForcedInventory &&
				(data->op == BGSMod::Container::Data::kOpFlag_Set_Form ||
				 data->op == BGSMod::Container::Data::kOpFlag_Add_Form) &&
				data->value.form && data->value.form->GetFormType() == kFormType_WEAP) {
				return static_cast<TESObjectWEAP*>(data->value.form);
			}
		}
	}
	return nullptr;
}

void HandleF4SEMessage(F4SEMessagingInterface::Message* msg) {
	switch (msg->type) {
	case F4SEMessagingInterface::kMessage_GameDataReady:
		DataHandler* dataHandler = *g_dataHandler;
		if (!dataHandler) {
			return;
		}

		UInt8 automatronLoadOrder = dataHandler->GetLoadedModIndex("DLCRobot.esm");
		if (automatronLoadOrder == 0xff) {
			_MESSAGE("Automatron not loaded!");
			return;
		}

		auto& objectMods = dataHandler->arrOMOD;
		for (UInt64 i = 0, end = dataHandler->arrOMOD.count; i < end; ++i) {
			BGSMod::Attachment::Mod* mod1 = dataHandler->arrOMOD[i];
			if (mod1->targetType == BGSMod::Attachment::Mod::kTargetType_Actor && !(mod1->materialSwap.name == "")) {
				TESObjectMISC* looseMod = GetLooseMod(mod1);
				if (looseMod) {
					TESObjectWEAP* forcedInventoryWeapon = GetForcedInventoryWeapon(mod1);
					for (UInt64 j = 0; j < end; ++j) {
						if (j != i) {
							BGSMod::Attachment::Mod* mod2 = dataHandler->arrOMOD[j];
							if (mod2->targetType == BGSMod::Attachment::Mod::kTargetType_Actor && !GetLooseMod(mod2) &&
								mod2->materialSwap.name == mod1->materialSwap.name &&
								GetForcedInventoryWeapon(mod2) == forcedInventoryWeapon) {
								global.looseModTable[mod2] = looseMod;
								_MESSAGE("Mapped '%s' (0x%.8x) to loose mod '%s'", mod2->GetFullName(), mod2->formID, looseMod->GetFullName());
							}
						}
					}
				}
			}
		}

		LoadExternalModMap("Data\\F4SE\\Plugins\\AutomatronDisassembler_ModMap.txt", global.looseModTable);
		LoadExternalModMap("Data\\F4SE\\Plugins\\AutomatronDisassembler_ModMapIndex1.txt", global.looseModTableIndex1);
		LoadExternalModMap("Data\\F4SE\\Plugins\\AutomatronDisassembler_ModMapIndex2.txt", global.looseModTableIndex2);

		break;
	}
}

TESObjectMISC* GetRobotLooseMod(StaticFunctionTag*, BGSMod::Attachment::Mod* mod, SInt32 attachIndex) {
	if (mod) {
		TESObjectMISC* looseMod = GetLooseMod(mod);
		if (looseMod) {
			return looseMod;
		}
		else {
			if (attachIndex == 2) {
				auto found = global.looseModTableIndex2.find(mod);
				if (found != global.looseModTableIndex2.end()) {
					return found->second;
				}
			}
			else if (attachIndex == 1) {
				auto found = global.looseModTableIndex1.find(mod);
				if (found != global.looseModTableIndex1.end()) {
					return found->second;
				}
			}
			else {
				auto found = global.looseModTable.find(mod);
				if (found != global.looseModTable.end()) {
					return found->second;
				}
			}
		}
	}
	return nullptr;
}
VMArray<TESObjectMISC*> GetRobotLooseMods(StaticFunctionTag*, VMArray<BGSMod::Attachment::Mod*> mods) {
	VMArray<TESObjectMISC*> result;
	if (!mods.IsNone()) {
		std::map<BGSMod::Attachment::Mod*, SInt32> addedMods;
		for (UInt64 i = 0; i < mods.Length(); ++i) {
			BGSMod::Attachment::Mod* mod;
			mods.Get(&mod, i);
			if (mod) {
				TESObjectMISC* looseMod = GetRobotLooseMod(nullptr, mod, addedMods[mod]);
				if (looseMod) {
					result.Push(&looseMod);
				}
				++addedMods[mod];
			}
		}
	}
	return result;
}

bool RegisterPapyrusFunctions(VirtualMachine* vm) {
	_MESSAGE("RegisterPapyrusFunctions begin");

	constexpr char* papyrusClassName = "AutomatronDisassembler";

	vm->RegisterFunction(new NativeFunction2<StaticFunctionTag, TESObjectMISC*, BGSMod::Attachment::Mod*, SInt32>("GetRobotLooseMod", papyrusClassName, GetRobotLooseMod, vm));
	vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, VMArray<TESObjectMISC*>, VMArray<BGSMod::Attachment::Mod*>>("GetRobotLooseMods", papyrusClassName, GetRobotLooseMods, vm));

	_MESSAGE("RegisterPapyrusFunctions end");

	return true;
}

/* Plugin Query */

extern "C" {
	bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info) {
		_MESSAGE("F4SEPlugin_Query begin");

		// populate info structure
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "Automatron Disassembler";
		info->version = 1;

		// store plugin handle so we can identify ourselves later
		global.pluginHandle = f4se->GetPluginHandle();

		if (f4se->isEditor) {
			_MESSAGE("\tloaded in editor, marking as incompatible");

			return false;
		}
		else if (f4se->runtimeVersion != RUNTIME_VERSION_1_10_64) {
			_MESSAGE("\tunsupported runtime version %08X", f4se->runtimeVersion);

			return false;
		}

		global.messaging = (F4SEMessagingInterface*)f4se->QueryInterface(kInterface_Messaging);
		if (!global.messaging) {
			_MESSAGE("\tcouldn't get messaging interface");
			return false;
		}
		if (global.messaging->interfaceVersion < F4SEMessagingInterface::kInterfaceVersion) {
			_MESSAGE("\tmessaging interface too old (%d expected %d)", global.messaging->interfaceVersion, F4SEMessagingInterface::kInterfaceVersion);
			return false;
		}

		global.papyrus = (F4SEPapyrusInterface*)f4se->QueryInterface(kInterface_Papyrus);
		if (!global.papyrus) {
			_MESSAGE("\tcouldn't get papyrus interface");

			return false;
		}
		if (global.papyrus->interfaceVersion < F4SEPapyrusInterface::kInterfaceVersion) {
			_MESSAGE("\tpapyrus interface too old (%d expected %d)", global.papyrus->interfaceVersion, F4SEPapyrusInterface::kInterfaceVersion);

			return false;
		}

		// ### do not do anything else in this callback
		// ### only fill out PluginInfo and return true/false

		_MESSAGE("F4SEPlugin_Query end");

		// supported runtime version
		return true;
	}

	bool F4SEPlugin_Load(const F4SEInterface * f4se) {
		_MESSAGE("F4SEPlugin_Load begin");

		global.messaging->RegisterListener(global.pluginHandle, "F4SE", HandleF4SEMessage);
		global.papyrus->Register(RegisterPapyrusFunctions);

		_MESSAGE("F4SEPlugin_Load end");

		return true;
	}

};
