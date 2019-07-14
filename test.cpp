
#include "Component.h"

struct Visitor
{
	template <typename T>
	void operator()(const char* name, T&& pointer, const Reflector::FieldReflectionData& (*field_reflection_getter)(), std::true_type can_edit)
	{
	}
	template <typename T>
	void operator()(const char* name, T&& pointer, const Reflector::FieldReflectionData& (*field_reflection_getter)(), std::false_type cant_edit)
	{
	}
};

int main()
{
	Ass::Component component;

	Ass::Component::StaticVisitFields(Visitor{});

}
