#if __has_include("Options.h.mirror")
#include "Reflection/Database.reflect.cpp"
#else

#include "Include/ReflectorClasses.h"
namespace Reflector
{
	ClassReflectionData const* Classes[] = { nullptr };
	EnumReflectionData const* Enums[] = { nullptr };
}

#endif
