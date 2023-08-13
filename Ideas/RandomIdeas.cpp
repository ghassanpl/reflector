#include <source_location>
#include <string_view>
#include <atomic>

struct ReflectedClass
{

	/// The singular keyword, which appears immediately before a function declaration, prevents a function from calling itself recursively. The rule is this: If a certain actor is already in the middle of a singular function, any subsequent calls to singular functions will be skipped over. This is useful in avoiding infinite-recursive bugs in some cases. For example, if you try to move an actor inside of your Bump function, there is a good chance that the actor will bump into another actor during its move, resulting in another call to the Bump function, and so on. You should be very careful in avoiding such behavior, but if you can't write code with complete confidence that you're avoiding such potential recursive situations, use the singular keyword. Note that this is not limited just to the particular function that is flagged singular -- all singular functions within a single object will not execute if you are currently in a singular function that is executing. 

	std::pair<const char*, std::source_location>* mReentrancyLockLocation = nullptr;
	int mReentrancyCount = 0;

	struct ReentrancyGuard
	{
		std::atomic_ref<int> ReentrancyCount;
		bool DoBody = false;
		ReflectedClass* Me;
		explicit ReentrancyGuard(int& count, ReflectedClass* me, auto* loc)
			: ReentrancyCount(count)
			, DoBody(count++ == 0)
			, Me(me)
		{
			if (DoBody) /// we can update fields here as ReentrancyCount acts as a sort of mutex, no one else has DoBody == true for this type
			{
				me->mReentrancyLockLocation = loc;
			}
		}
		~ReentrancyGuard()
		{
			if (DoBody)
				Me->mReentrancyLockLocation = nullptr;
			--ReentrancyCount;
		}
		explicit operator bool() const noexcept { return DoBody; }
	};

	ReentrancyGuard GetSingularGuard(auto* loc_ptr)
	{
		return ReentrancyGuard{ mReentrancyCount, this, loc_ptr };
	}

#define RSingular() ReentrancyGuard _singular_guard = this->GetSingularGuard([]{ static auto StaticInstanceAtLoc = std::pair{ __func__, std::source_location::current() }; return &StaticInstanceAtLoc; }()); _singular_guard

	void SingularMethod()
	{
		if (RSingular())
		{
			printf("Only once");
			SingularMethod(); /// no infinite recursion here, hehe
		}
	}

};