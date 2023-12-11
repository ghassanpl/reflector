#if 0
#include <vector>
#include <memory>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <bitset>

#define generator vector
#define RBEHAVIOR_ApplyAdditionalForces \
    struct MovementSystem_##name##_ComponentTuple { \
        ECS::Entity const* entity; \
        Transform* Transform; \
        AdditionalForces const* AdditionalForces; \
        Frozen const* Frozen; \
    }; \
    ECS::Behavior ApplyAdditionalForces = ApplyAdditionalForces_Default; \
	static const ECS::Behavior ApplyAdditionalForces_Default; \
	static void ApplyAdditionalForces_Func(MovementSystem const& system, ECS::World& world, std::generator<MovementSystem_##name##_ComponentTuple> requested)

#define RBEHAVIOR_ApplyRequestedMovement \
	ECS::Behavior ApplyRequestedMovement = ApplyRequestedMovement_Default; \
	static const ECS::Behavior ApplyRequestedMovement_Default; \
	static void ApplyRequestedMovement_Func(MovementSystem const& system, ECS::World& world, \
        ECS::Entity entity, \
        Transform* Transform, \
        InputRequestedMovement const* InputRequestedMovement, \
        PlayerClass const* PlayerClass) \

#define RBehavior(name, ...) RBEHAVIOR_##name

#define RClass(...)
#define RField(...)
#define RSystem(...)

#define RBody() \
	static Class* StaticGetClass(); \
	ECS::Entity mOwnerEntity = {}; \
	static constexpr size_t ComponentTypeID = 3; \

static constexpr size_t ComponentTypeCount = 53;

struct Class;
namespace ECS
{
	struct Entity;
}

/// GENERATED ECS.reflect.h/.cpp
namespace ECS
{
	struct World;

	struct Behavior
	{
		Behavior(...) {}

		Behavior& operator=(auto) { return *this; }

		void operator()(World& world) const
		{

		}
	};

	struct EntityData
	{
		uint32_t Generation = 0;
		std::bitset<ComponentTypeCount> mHasComponent;
	};

	struct Entity
	{
		size_t ID;
		uint32_t Index() const { return ID >> 32ULL; }
		uint32_t Generation() const { return ID & 0xFFFFFFFF; }
	};
}
template <>
struct std::hash<ECS::Entity>
{
	constexpr size_t operator()(ECS::Entity const& e) const noexcept { return e.ID; }
};

namespace ECS {
	template <typename T>
	using PerEntityMap = std::unordered_map<Entity, std::vector<T>>;
	
	template <typename T>
	using PerEntitySet = std::unordered_set<Entity>;

	struct IPool
	{
		void* mStorage;
		template <typename T>
		T* Get(Entity const* entity)
		{
			auto& vec = *reinterpret_cast<PerEntityMap<T>*>(mStorage);
			return &vec[entity];
		}
	};

	template <typename T>
	struct Pool : IPool
	{
		std::unique_ptr<PerEntityMap<T>> Storage = std::make_unique<PerEntityMap<T>>();
		Pool()
		{
			mStorage = Storage.get();
		}
	};

	extern Class* ComponentClasses[12];

	struct ITuplePool
	{
		void* mStorage;
		template <typename T>
		T* Get(Entity const* entity)
		{
			auto& vec = *reinterpret_cast<PerEntityMap<T>*>(mStorage);
			return &vec[entity];
		}
	};

	template <typename... TYPES>
	struct TuplePool : ITuplePool
	{
		using TupleType = std::tuple<TYPES*...>;
		std::unique_ptr<PerEntityMap<TupleType>> Storage = std::make_unique<PerEntityMap<TupleType>>();
		TuplePool()
		{
			mStorage = Storage.get();
		}
	};

	struct World
	{
		template <typename COMPONENT>
		COMPONENT* SingletonComponent() const;

		template <typename COMPONENT>
		COMPONENT& Add(Entity e);

		std::vector<EntityData> Entities;

		template <typename COMPONENT>
		COMPONENT* Get(Entity e)
		{
			auto& data = Data(e);
			if (data.mHasComponent[COMPONENT::ComponentTypeID])
			{
				if constexpr (std::is_empty_v<COMPONENT>)
					return (COMPONENT*)COMPONENT::StaticGetClass();
			}
			return nullptr;
		}
		
		EntityData const& Data(Entity e) const
		{
			return Entities[e.Index()];
		}

		template <typename COMPONENT>
		bool Has(Entity e)
		{
			if (!Valid(e)) return false;
			return Data(e).mHasComponent[COMPONENT::ComponentTypeID];
		}

		bool Valid(Entity e) const
		{
			return Data(e).Generation == e.Generation();
		}

		Entity ID(EntityData const* data) const
		{
			return { size_t((data - Entities.data()) << 32) | data->Generation };
		}

	private:

		template <typename T>
		Pool<T>* GetPoolFor()
		{
			return reinterpret_cast<Pool<T>*>(&mPoolsPerType[T::ComponentTypeIDComponentTypeID]);
		}

		void CreatePools();

		std::vector<std::unique_ptr<IPool>> mPoolsPerType;
		std::vector<std::unique_ptr<ITuplePool>> mTuplePoolsPerBehavior;
	};


};
/// END GENERATED

RClass(Component);
struct Transform
{
	RBody();

	RField();
	float Position = 0;
};
RClass(Component);
struct AdditionalForces
{
	RBody();
	float SumForces() const;
};
RClass(Component);
struct Frozen
{
	RBody();
};

RClass(Component);
struct SlowEffect
{
	RBody();
	RField();
	float Slowdown = 1.0f;
};
RClass(Component);
struct PlayerClass
{
	RBody();
	RField();
	float DefaultSpeed = 1.0f;
};
RClass(Component);
struct InputRequestedMovement
{
	RBody();
	RField();
	float RequestedDirection = 0.0f;
};

RClass(Component);
struct LevelParams
{
	RBody();
	RField();
	float AirFriction = 0.01f;
};

RSystem();
struct MovementSystem
{
	RBehavior(ApplyAdditionalForces, On([mutable Transform], AdditionalForces, [optional Frozen]))
	{
		const float air_friction = 1.0f - world.SingletonComponent<LevelParams>()->AirFriction;

		for (auto& t : requested)
		{
			if (!t.Frozen)
				t.Transform->Position += t.AdditionalForces->SumForces() * air_friction;
		}
	};

	float CalculateSpeedMultiplier(ECS::World & world, ECS::Entity for_entity) const
	{
		if (world.Has<Frozen>(for_entity))
			return 0;
		if (auto slow_effect = world.Get<SlowEffect>(for_entity))
			return slow_effect->Slowdown;
		return 1.0;
	}

	RBehavior(ApplyRequestedMovement, AutoLoop, On([mutable Transform], InputRequestedMovement, PlayerClass, [not Frozen]), AndOthers)
	{
		Transform->Position += InputRequestedMovement->RequestedDirection *
			(PlayerClass->DefaultSpeed * system.CalculateSpeedMultiplier(world, entity));
	};

	void Apply(ECS::World& world)
	{
		ApplyAdditionalForces(world);
		ApplyRequestedMovement(world);
	}
};

/// TODO: Relationships - one-to-many named mapping
/// TODO: Optimization for components with no fields
///		Entity::Get() and others return either nullptr or a pointer to a static value, since it can't be accessed anyway;

using namespace ECS;
void Test()
{
	World world;
	/// tu dodajemy i modyfikujemy encje i komponenty
	MovementSystem::ApplyAdditionalForces_Default(world);
	MovementSystem::ApplyRequestedMovement_Default(world);

	MovementSystem custom_movement_system;
	custom_movement_system.ApplyAdditionalForces = [](MovementSystem const& system, ECS::World& world, auto const& requested_component_tuples) {
		/// customowa implementacja zachowania
	};

	custom_movement_system.Apply(world);
}

/// TODO: Enforces components cannot have non-const functions or mutable fields

/// Attribute: AndOthers - allows you to read and write any other entities of the component (makes ComponentTuple::Entity non-const)
/// Attribute: Pool=true/false - if false, will not generate an entry into the `TuplePoolsPerBehavior` for this behavior
///		Useful when the behavior is rarely called but entities applicable to it are frequently changed


void ECS::World::CreatePools()
{
	mPoolsPerType.push_back(std::make_unique<Pool<Transform>>());

	/// Transform, AdditionalForces
	mTuplePoolsPerBehavior.push_back(std::make_unique<TuplePool<Transform, AdditionalForces, Frozen>>());
}

#endif