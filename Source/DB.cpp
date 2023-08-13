#if __has_include("Options.h.mirror")
#include "../Reflection/Database.reflect.cpp"
#else

#include "../Include/ReflectorClasses.h"
namespace Reflector
{
	Class const* Classes[] = { nullptr };
	Enum const* Enums[] = { nullptr };
}

#endif
