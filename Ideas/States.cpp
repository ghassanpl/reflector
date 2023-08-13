#if 0
#include <any>
#include <string>
#include <vector>
#include <array>

#include "../Include/ReflectorClasses.h"

/// IN REFLECTOR.H
#define RStateDef(...) struct BaseState : public IBaseStateForThis
#define RState(name, ...) \
	struct IBaseStateFor_##name : public BaseState { \
		virtual std::string_view StateName() const override; \
		virtual State StateID() const override { return State::name; } \
	}; \
	struct State_##name : public IBaseStateFor_##name
#define RStateMethod(...)
#define RBody(...)
#define RMethod(...)
#define RField(...)
/// END REFLECTOR.H

template <typename... TYPES>
struct alignas(TYPES...) aligned_union { std::byte storage[std::max({ sizeof(TYPES)... })]; };;

/*
- how to do state inheritance?
- test how subclassing classes with states works
	- I guess we could automatically inherit states in subclasses from their equivalently named states in base classes?
- should we support "no state"? this would just mean being in BaseState
- substates? that would get REALLY complicated
*/

/// ## Should States Store Persistent Data
/// ### Pros
/// - very powerful, allows trivial communication between states
/// ### Cons
/// - each state requires its own storage, which baloons class sizes
/// 
/// ### Conclusion
/// Just make it optional, set by `KeepStateData` bool-attribute

#define REFLECTOR_KEEP_STATE_VARIABLES 0

struct SomeObject //: Reflector::Reflectable
{
	RBody();

	/// START RBODY CODE
	
public:

	using self_type = SomeObject;

	/// TODO: Find a less common name for this struct
	enum class State : uint8_t
	{
		Walking,
		Running,
		StateWithAStupidName,
	};

private:

	State mCurrentState = State::Running;
	State mNextState = State::Running;
	State mInitializingState = State::Running;

protected:

	/// THIS WILL BE 'protected:' AS THE RState()s THAT DECLARED THEM
	struct State_Walking;
	struct State_Running;
	struct State_StateWithAStupidName;

	static thread_local inline std::vector<SomeObject*> mOwnerStack = std::vector<SomeObject*>(2);

	struct IBaseStateForThis {

		virtual ~IBaseStateForThis() noexcept = default;

		virtual std::string_view StateName() const = 0;
		virtual State StateID() const = 0;

		SomeObject const* This() const { return mParentObject; }
		SomeObject* This() { return mParentObject; }

		State_Walking* AsWalking() { return StateID() == State::Walking ? (State_Walking*)this : nullptr; }
		State_Running* AsRunning() { return StateID() == State::Running ? (State_Running*)this : nullptr; }
		State_StateWithAStupidName* AsStateWithAStupidName() { return StateID() == State::StateWithAStupidName ? (State_StateWithAStupidName*)this : nullptr; }

	private:

		self_type* mParentObject = self_type::mOwnerStack.back();

	protected:

		virtual void OnEnter(State from) {}
		virtual void OnLeave(State to) {}

		virtual void OnSuspended(State in_favor_of) {}
		virtual void OnPushed(State previous) {}
		virtual void OnPopping(State back_to) {}
		virtual void OnResumed(State previous) {}
		
		/// If `force_events` is true, OnEnter() will be called even if we're in the same state
		
		void ChangeState(State state, bool force_events = false, bool keep_stack = true);

		/// TODO: Should we be able to give arbitrary arguments to GoTo* functions to forward to OnEnter? If so, how? Maybe by storing a std::tuple or std::any of some sort? But then OnEnter cannot be virtual,
		///		but that's not a big issue since we're generating most of these functions anyway;
		///		Would OnEnter have to be reflected? Probably...
		/// TODO: These could also take an optional `function<> transition_action` that will be executed AFTER `OnLeave` but BEFORE `OnEnter`
		void GoToWalking(); /// { mNextState = State::Walking; } ?
		void GoToRunning();
		void GoToStateWithAStupidName();

		/*
		* State stacking
		* UE3 states: A state can be put on the stack only once, trying to push the same state on the stack a second time will fail.
		
		enum { Inactive, Active, Suspended } mState = (mParentObject->mCurrentState == mParentObject->mInitializingState) ? Active : Inactive;

		void PushWalking();
		void PushRunning();
		void PushStateWithAStupidName();

		void PopState();
		*/
	};

	/// THIS WILL BE 'protected:' AS RStateDef(); 
	struct BaseState; /// This line will only be generated if we used RStateDef();, otherwise:
	/// using BaseState = IBaseStateForThis;

	BaseState const* StateStruct(State state) const;

	static State InitialState() { return State::Walking; }
	BaseState* CurrentState() { return const_cast<BaseState*>(StateStruct(mCurrentState)); }

public:

	BaseState const* CurrentState() const { return StateStruct(mCurrentState); }
	State CurrentStateID() const { return mCurrentState; }

	/// bool IsSuspended(State state) const { return StateStruct(state)->mState == Suspended; }
	
	std::vector<State> GetStateStack() const;

	/// GENERATED DUE TO RStateMethod
	/// TODO: Should this be virtual?
	int GetShootPrecision() { return CurrentState()->GetShootPrecision(); }

	/// END RBODY CODE

protected:

	int BaseShootPrecision = 10;
	
	/// TODO: IF we disallow stateful states (so no variables), we could have std::variant<all, state, types> to regain a lot of data
	///		We could also have that if states are forced to be re-created on change

	RStateDef() {
		RStateMethod(); /// Or RExposedMethod() ? 
		virtual int GetShootPrecision() { return This()->BaseShootPrecision; };
	};

	RState(Walking) {
		void OnEnter(State from) override { }
		void OnLeave(State to) override { }
	};

	RState(Running, Initial) {
		int GetShootPrecision() { return This()->BaseShootPrecision / 2; };
	};

	RState(StateWithAStupidName, DisplayName = "Pretty Name") {
		int GetShootPrecision() { return This()->BaseShootPrecision * 2; };
	};


	/// IDEAS FOR TRANSITIONS
#if 0
#define RTransition(...)
	RTransition(From = Walking, To = Running, Trigger = ""/{} | Triggers = [ ... ], Properties = {Interruptable, Time = 10.0, SwitchTime = 5.0});

	/// If we're in the Walking state, this function will be called when calling CheckTransitions()
	/// and if it returns true, GoToRunning() will be called.
	RMethod(TransitionCondition = [ Walking, Running ]);
	bool ShouldStartRunning()
	{
		return false;
	}
#endif


	/// TODO: HOW TO GENERATE THIS?
	///		Maybe force something like RInitialState(Running); ?
	///		Or maybe RStateInit() { 
	///			/// some code
	///		}
protected:

	/// TODO: Should we call CurrentState()->OnLeave() in the destructor of the main class?
	///		Technically, that's what destructors are for...

#if REFLECTOR_KEEP_STATE_VARIABLES
	int StartStateInitialization() { this->mInitializingState = State::Running; mOwnerStack.push_back(this); return 0; }
	int EndStateInitialization() { mOwnerStack.pop_back(); return 0; this->StateInitFunc(); }
	[[no_unique_address]] struct SomeObject_NUA__ { SomeObject_NUA__(...) {} } SomeObject_NUA___{ StartStateInitialization() };
	State_Walking mState_Walking{};
	State_Running mState_Running{};
	State_StateWithAStupidName mState_StateWithAStupidName{};
	[[no_unique_address]] SomeObject_NUA__ SomeObject_NUA____{ EndStateInitialization() };
#else
	aligned_union<State_Walking, State_Running, State_StateWithAStupidName> mStateStorage;
	[[no_unique_address]] struct SomeObject_NUA__ { 
		SomeObject_NUA__(...) {} 
		~SomeObject_NUA__() noexcept {
			((SomeObject*)(uintptr_t(this) - offsetof(SomeObject, SomeObject_NUA___)))->CurrentState()->~BaseState();
		}
	} SomeObject_NUA___{ new (mStateStorage.storage) State_Running{} };
#endif

public: /// or protected or whatever we were before
	void StateInitFunc()

	{

	}

	/// TODO: Maybe:
	/// RState(SomeChildState, Extends = "AnotherState");
	/// RState(SomeChildState, Extends = ["AnotherState", "SomeNonStateClassWithFunctionality"]);
};

/// IN DATABASE.REFLECT.CPP

SomeObject::BaseState const* SomeObject::StateStruct(SomeObject::State state) const
{
#if REFLECTOR_KEEP_STATE_VARIABLES
	switch (state)
	{
	case State::Walking: return &mState_Walking;
	case State::Running: return &mState_Running;
	case State::StateWithAStupidName: return &mState_StateWithAStupidName;
	default:
		break;
	}
	return nullptr;
#else
	return reinterpret_cast<SomeObject::BaseState const*>(mStateStorage.storage);
#endif
}

/*
std::any SomeObject::InitState(SomeObject::State state)
{
	std::any result;
	switch (state)
	{
	case State::Walking: result = SomeObject::State_Walking{}; break;
	case State::Running: result = SomeObject::State_Running{}; break;
	case State::StateWithAStupidName: result = SomeObject::State_StateWithAStupidName{}; break;
	}
	
	return result;
}
*/

std::string_view SomeObject::IBaseStateFor_Walking::StateName() const { return "Walking"; }
std::string_view SomeObject::IBaseStateFor_Running::StateName() const { return "Running"; }
std::string_view SomeObject::IBaseStateFor_StateWithAStupidName::StateName() const { return "Pretty Name"; }

/// END DATABASE

void Test()
{
	SomeObject obj{};
	obj.GetShootPrecision();
}

#endif