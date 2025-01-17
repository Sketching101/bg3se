#include <Lua/Shared/Proxies/LuaPropertyMap.h>

BEGIN_NS(lua)

void GenericPropertyMap::Init(int registryIndex)
{
	assert(!IsInitializing && !Initialized);
	IsInitializing = true;
	RegistryIndex = registryIndex;
}

void GenericPropertyMap::Finish()
{
	assert(!Initialized && IsInitializing);
	IsInitializing = false;
	Initialized = true;
}

bool GenericPropertyMap::HasProperty(FixedString const& prop) const
{
	return Properties.find(prop) != Properties.end();
}

PropertyOperationResult GenericPropertyMap::GetRawProperty(lua_State* L, LifetimeHandle const& lifetime, void* object, FixedString const& prop) const
{
	auto it = Properties.find(prop);
	if (it == Properties.end()) {
		if (FallbackGetter) {
			return FallbackGetter(L, lifetime, object, prop);
		} else {
			return PropertyOperationResult::NoSuchProperty;
		}
	}

	return it->second.Get(L, lifetime, object, it->second);
}

PropertyOperationResult GenericPropertyMap::SetRawProperty(lua_State* L, void* object, FixedString const& prop, int index) const
{
	auto it = Properties.find(prop);
	if (it == Properties.end()) {
		if (FallbackSetter) {
			return FallbackSetter(L, object, prop, index);
		} else {
			return PropertyOperationResult::NoSuchProperty;
		}
	}

	return it->second.Set(L, object, index, it->second);
}

void GenericPropertyMap::AddRawProperty(char const* prop, typename RawPropertyAccessors::Getter* getter,
	typename RawPropertyAccessors::Setter* setter, typename RawPropertyAccessors::Serializer* serialize, 
	std::size_t offset, uint64_t flag, PropertyNotification notification, char const* newName)
{
	assert((!Initialized || !InheritanceUpdated) && IsInitializing);
	FixedString key{ prop };
	FixedString newNameKey{ newName ? newName : "" };
	assert(Properties.find(key) == Properties.end());
	Properties.insert(std::make_pair(key, RawPropertyAccessors{ key, offset, flag, getter, setter, serialize, notification, this, newNameKey }));

}

void GenericPropertyMap::AddRawValidator(char const* prop, typename RawPropertyValidators::Validator* validate, std::size_t offset, uint64_t flag)
{
	assert((!Initialized || !InheritanceUpdated) && IsInitializing);
	Validators.push_back(RawPropertyValidators{ FixedString(prop), validate, offset, flag });
}

void GenericPropertyMap::AddRawProperty(char const* prop, typename RawPropertyAccessors::Getter* getter,
	typename RawPropertyAccessors::Setter* setter, typename RawPropertyValidators::Validator* validate, 
	typename RawPropertyAccessors::Serializer* serialize, std::size_t offset, uint64_t flag, 
	PropertyNotification notification, char const* newName)
{
	if (getter != nullptr || setter != nullptr) {
		AddRawProperty(prop, getter, setter, serialize, offset, flag, notification, newName);
	}

	if (validate != nullptr) {
		AddRawValidator(prop, validate, offset, flag);
	}
}

bool GenericPropertyMap::ValidatePropertyMap(void const* object)
{
	switch (ecs::EntitySystemHelpersBase::CheckLevel) {
	case ecs::RuntimeCheckLevel::None: 
		return true;

	case ecs::RuntimeCheckLevel::Once:
		if (Validated == ValidationState::Unknown) {
			if (ValidateObject(object)) {
				Validated = ValidationState::Valid;
			}
			else {
				ERR("Object class %s failed validation; proxying of this class is disabled", Name.GetString());
				Validated = ValidationState::Invalid;
			}
		}

		return Validated == ValidationState::Valid;

	case ecs::RuntimeCheckLevel::Always:
	case ecs::RuntimeCheckLevel::FullECS:
	default:
		return ValidateObject(object);
	}
}

bool GenericPropertyMap::ValidateObject(void const* object)
{
	for (auto const& property : Validators) {
		if (!property.Validate(object, property.Offset, property.Flag)) {
			ERR("Validation of property '%s' failed on %s %p", property.Name.GetString(), Name.GetString(), object);
			return false;
		}
	}

	return true;
}

bool GenericPropertyMap::IsA(int typeRegistryIndex) const
{
	if (RegistryIndex == typeRegistryIndex) {
		return true;
	}

	for (auto const& parent : ParentRegistryIndices) {
		if (parent == typeRegistryIndex) {
			return true;
		}
	}

	return false;
}

void SerializeRawObject(lua_State* L, void const* obj, GenericPropertyMap const& pm)
{
	StackCheck _(L, 1);
	lua_createtable(L, 0, (int)pm.Properties.size());
	for (auto const& prop : pm.Properties) {
		if (prop.second.Serialize != nullptr) {
			auto result = prop.second.Serialize(L, obj, prop.second);
			if (result == PropertyOperationResult::Success) {
				lua_setfield(L, -2, prop.first.GetString());
			}
		}
	}
}

void UnserializeRawObjectFromTable(lua_State* L, int index, void* obj, GenericPropertyMap const& pm)
{
	StackCheck _(L);
	for (auto const& prop : pm.Properties) {
		lua_getfield(L, index, prop.first.GetString());
		if (lua_type(L, -1) != LUA_TNIL) {
			prop.second.Set(L, obj, lua_absindex(L, -1), prop.second);
		}
		lua_pop(L, 1);
	}
}

void UnserializeRawObjectFromUserdata(lua_State* L, int index, void* obj, GenericPropertyMap const& pm)
{
	StackCheck _(L);
	auto other = ObjectProxy::GetRaw(L, index, pm);

	if (pm.Assign != nullptr) {
		pm.Assign(obj, other);
	} else {
		luaL_error(L, "Type %s does not support assignment", pm.Name.GetString());
	}
}

std::atomic<uint32_t> gDisablePropertyWarningsCount{ 0 };

void DisablePropertyWarnings()
{
	gDisablePropertyWarningsCount++;
}

void EnablePropertyWarnings()
{
	gDisablePropertyWarningsCount--;
}

void ProcessPropertyNotifications(RawPropertyAccessors const& prop, bool isWriting)
{
	if (gDisablePropertyWarningsCount > 0) return;

	if ((unsigned)prop.PendingNotifications & (unsigned)PropertyNotification::Deprecated) {
		WARN("DEPRECATED: %s property %s.%s that is deprecated and will be removed in future versions", 
			(isWriting ? "Writing" : "Reading"), prop.PropertyMap->Name.GetString(), prop.Name.GetString());
	}
	
	if ((unsigned)prop.PendingNotifications & (unsigned)PropertyNotification::Renamed) {
		WARN("DEPRECATED: %s property %s.%s that has been renamed to %s", 
			(isWriting ? "Writing" : "Reading"), prop.PropertyMap->Name.GetString(), prop.Name.GetString(), prop.NewName.GetString());
	}
	
	if ((unsigned)prop.PendingNotifications & (unsigned)PropertyNotification::TemporaryName) {
		WARN("DEPRECATED: %s property %s.%s that is a placeholder and will be renamed in future versions", 
			(isWriting ? "Writing" : "Reading"), prop.PropertyMap->Name.GetString(), prop.Name.GetString());
	}

	prop.PendingNotifications = PropertyNotification::None;
}

END_NS()
