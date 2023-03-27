# reflector

> **NOTE**: This is still a work in progress. Right now the tool mostly works, but is imperfect, generates imperfect code, and has bugs. Use at your own risk.

> Pull requests welcomed and encouraged and begged for!

## What is it?

Reflector is a C++20 tool similar to the Unreal Header Tool. It will scan the headers in you codebase for types/methods/fields annotated with a [special reflection syntax](https://github.com/ghassanpl/reflector/wiki/Usage#code-requirements). It will then create files containing reflection information about those entities. The main type of file it will create is the [`*.mirror`](https://github.com/ghassanpl/reflector/wiki/Artifacts#mirror) file, which you are meant to include in the files Reflector is scanning. This will inject reflection information straight into your files, allowing for compile-time reflection of these entities.

```c++
#include "Component.h.mirror"

RClass();
class Component : public Reflector::Reflectable
{
	RBody();

	RField(ReadOnly, Required, Setter = false);
	std::string Name;

	RMethod();
	void SetName(std::string_view name) { Name = name; }
	
protected:

	RField(ParentPointer = true);
	class Object* mParentObject = nullptr;
};

REnum();
enum class TestEnum
{
	Open = 5,
	
	Visible,

	REnumerator(Opposite = Dead);
	Alive,
};
```

## How to use

See [Usage in the wiki](https://github.com/ghassanpl/reflector/wiki/Usage).

## Example

See the [example in the wiki](https://github.com/ghassanpl/reflector/wiki/Example).

## Dependencies

* C++20
* [nlohmann/json](https://github.com/nlohmann/json)
* [xxhash](https://github.com/Cyan4973/xxHash) (for now, easily removable thanks to `header_utils`)
* [magic_enum](https://github.com/Neargye/magic_enum)
* my personal [header_utils](https://github.com/ghassanpl/header_utils) library

See [the vcpkg.json file](https://github.com/ghassanpl/reflector/blob/master/vcpkg.json) for up-to date dependency information.

## UNTODO

These are things I am not planning on doing unless someone asks nicely:

* support for generating code for older C++ versions
* other platforms (the code is fairly portable, just needs the memory mapping code to be written for non-windows platforms)
* scriptability/pluginability
