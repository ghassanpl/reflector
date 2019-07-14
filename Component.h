#pragma once
#include <nlohmann/json.hpp>
#include "Component.mirror.h"

namespace Ass
{
	class Object;

	struct Point { float x = 0, y = 0; };

	/// The base class for all Components
	RClass({"Category": "Components", "Icon": "ICON_FA_CUBE"})
	class Component : public Reflector::Reflectable
	{
		RBody()

		/// The name of this component, must be unique within the owning Object
		RField({"ReadOnly": true, "Required": true, "Setter": false})
		std::string Name;

		/// Sets the name of this component; it must be unique within the owning Object
		RMethod()
		auto SetName(std::string_view name) -> void;

		///************************************************************************/
		/// Object passthrough methods
		///************************************************************************/

		/// Gets the position of the owning Object
		RMethod()
		auto GetPosition() -> Point;

		/// Sets the position of the owning Object
		RMethod()
		auto SetPosition(Point pos) -> void;

		/// Gets the scale of the owning Object
		RMethod()
		auto GetScale() -> Point;

		/// Sets the scale of the owning Object
		RMethod()
		auto SetScale(Point scale) -> void;

		/// Gets the rotation of the owning Object
		RMethod()
		auto GetRotation() -> float;

		/// Sets the rotation of the owning Object
		RMethod()
		auto SetRotation(float rot) -> void;

		/// Gets the local transform of the owning Object
		RMethod()
		auto GetLocalTransform() -> float;

		/// Gets the global transform of the owning Object
		RMethod()
		auto GetGlobalTransform() -> float;

		/// Checks whether a component of the given name exists in the owning Object
		RMethod()
		auto ComponentNameExists(std::string_view name) -> bool;

		/// Returns the component of the owning Object with the given name, or null otherwise
		RMethod()
		auto GetComponentByName(std::string_view name) -> Component*;

		///************************************************************************/
		/// Events
		///************************************************************************/

		/// Calls the method with the given name in this Component
		RMethod()
		auto CallEvent(std::string_view event_name) -> void;

		///************************************************************************/
		/// Game Events
		///************************************************************************/

		RMethod()
		auto OnInstantiate() -> void { Instantiate(); }

		/// Called when the owning Object requests a resource load of this Component.
		/// By default it's equivalent to LoadAsync()
		RMethod()
		auto OnLoadRequested() -> void { }
		
		/// Called when the owning Object requests a resource unload of this Component.
		/// By default it just unloads the component resources.
		RMethod()
		auto OnUnloadRequested() -> void { UnloadResources(); }

		/// Asynchronously loads the resources of this component. The exact behavior depends on the
		/// type of component.
		RMethod()
		auto LoadAsync() -> void { }

		/// Asynchronously loads the resources of this component, giving these loads priority. The exact behavior depends on the
		/// type of component.
		RMethod()
		auto LoadAsyncWithPriority() -> void { }

		/// Synchronously loads the resources of this component. The exact behavior depends on the
		/// type of component.
		RMethod()
		auto LoadSync() -> void {}

		virtual auto DrawDebug() -> void {}

	protected:

		virtual void LoadResources() {}
		virtual void UnloadResources() {}

		virtual void Instantiate() {}

		friend class Object;

		/// The parent owning Object of this component
		RField({"ParentPointer": true})
		Object* mParentObject = nullptr;
	};

	RClass()
	struct Meh
	{
	};

	REnum()
	enum class TestEnum
	{
		A = 5,
		B,
		C
	};

}