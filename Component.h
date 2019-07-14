#pragma once
#include "Component.mirror.h"

namespace Ass
{
	/// Classes are declared like this. Right now they have to derive (at some point) from Reflector::Reflectable.
	/// Support for "structs", reflected classes with no bases, is coming.
	RClass({"Category": "Components", "Icon": "ICON_FA_CUBE"})
	class Component : public Reflector::Reflectable
	{
		/// Classes need to have this macro at the beginning. It automatically sets the following members to `public:`.
		RBody()

		/// Fields are declared like this. All reflected entities can have properties. These can be queried at runtime. Compile-time property queries
		/// would also be possible, but are not available right now. Properties are stored as JSON strings, and, if nlohmann/json is included, as nlohmann::json values.
		/// Some properties modify the behavior of the Reflector, e.g. generate getters/setters.
		RField({"ReadOnly": true, "Required": true, "Setter": false})
		std::string Name;

		/// Methods are declared like this. They have to have trailing return types at the moment. This will change in the future, at least
		/// for simple types.
		RMethod()
		auto SetName(std::string_view name) -> void;

		/// Methods can be virtual, and can have inline bodies. Right now there is support for generating "binding" methods (for scripting languages
		/// like Lua, etc.) - static functions with a common signature that call the reflected method.
		RMethod()
		virtual auto OnLoadRequested() -> void
		{
			LoadResources();
		}
		
	protected:

		virtual void LoadResources() {}
		
		virtual void Instantiate() {}

		/// Fields and methods can be of any access level. Comments that start with '///' like this one will be included
		/// in the reflection data.
		RField({"ParentPointer": true})
		class Object* mParentObject = nullptr;
	};

	/// Enums can also be reflected.
	/// Right now, only enum classes are supported.
	/// Enumerators can have initializers, but only integer literal initializers are supported right now.
	REnum()
	enum class TestEnum
	{
		A = 5,
		B,
		C
	};

}