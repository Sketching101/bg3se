#pragma once

#include <GameDefinitions/Base/Base.h>
#include <GameDefinitions/EntitySystem.h>

BEGIN_SE()

struct PassiveContainerComponent : public BaseComponent
{
	static constexpr ExtComponentType ComponentType = ExtComponentType::PassiveContainer;
	static constexpr auto EngineClass = "eoc::PassiveContainerComponent";

	Array<EntityHandle> Passives;
};

struct PassiveComponent : public BaseComponent
{
	static constexpr ExtComponentType ComponentType = ExtComponentType::Passive;
	static constexpr auto EngineClass = "eoc::PassiveComponent";

	uint8_t field_0;
	FixedString PassiveId;
	EntityHandle field_8;
	EntityHandle field_10;
	uint8_t field_18;
	uint8_t field_19;
	uint32_t field_1C;
};

struct PassiveUsageCount
{
	uint16_t field_0;
	uint16_t field_2;
	uint16_t field_4;
	uint16_t field_6;
	uint16_t field_8;
};

struct UsageCountComponent : public BaseComponent
{
	static constexpr ExtComponentType ComponentType = ExtComponentType::PassiveUsageCount;
	static constexpr auto EngineClass = "eoc::passive::UsageCountComponent";

	MultiHashMap<FixedString, PassiveUsageCount> Passives;
};

struct ToggledPassivesComponent : public BaseComponent
{
	static constexpr ExtComponentType ComponentType = ExtComponentType::ServerToggledPassives;
	static constexpr auto EngineClass = "esv::passive::ToggledPassivesComponent";

	MultiHashMap<FixedString, bool> Passives;
};

END_SE()
