# reflector

> **NOTE**: This is still a work in progress. Right now the tool mostly works, but is imperfect, 
generates imperfect code, and has bugs. Use at your own risk.

> Pull requests welcomed and encouraged and begged for!

## What is it?

Reflector is a C++20 reflection generation tool similar to the Unreal Header Tool. It will scan the headers in your codebase for types/methods/fields annotated with a [special reflection syntax](https://github.com/ghassanpl/reflector/wiki/Annotations), and create files containing reflection information about those entities. The main type of file it will create is the [`*.mirror`](https://github.com/ghassanpl/reflector/wiki/Artifacts#mirror) file, which you are meant to include in the files Reflector is scanning. This will inject reflection information straight into your files, allowing for compile-time reflection of these entities.

```c++
#include "Component.h.mirror"

RClass(); /// Annotation will cause the class below to be reflected
class Component : public Reflector::Reflectable
{
	RBody(); /// Required in every reflected class

	RField(Required, Setter = false, Editor = false); /// Annotation will cause Name to be reflected
	std::string Name; 

	/// ^ The Name field will be required when deserializing, will not be settable
	/// from scripts, and will not be editable in the editor.

	/// These comment lines will be included in the auto-generated documentation
	/// for SetName. The `ScriptPrivate` attribute means that this function will
	/// not be callable from script. 
	RMethod(ScriptPrivate); /// Annotation will cause SetName to be reflected
	void SetName(std::string_view name) { if (VerifyName(name)) Name = name; }
	
protected:

	/// This field will be reflected, and a public GetParentObject function will 
	/// be generated for it.
	RField();
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

### Projects Using Reflector
* C+\+20 (C++17 support could be added if requested)
* optional [nlohmann/json](https://github.com/nlohmann/json) if you want out-of-the-box JSON serialization support

### The Reflector Tool Itself

* C++20
* [nlohmann/json](https://github.com/nlohmann/json)
* [magic_enum](https://github.com/Neargye/magic_enum)
* [tl::expected](https://github.com/TartanLlama/expected) - or `std::expected` if it's available to your compiler
* my personal [header_utils](https://github.com/ghassanpl/header_utils) library

See [the vcpkg.json file](https://github.com/ghassanpl/reflector/blob/master/vcpkg.json) for up-to date dependency information.

## UNTODO

These are things I am not planning on doing unless someone asks nicely:

* support for generating code for older C++ versions
* other platforms (the code is fairly portable, just needs the memory mapping code to be written for non-windows platforms)
* scriptability/pluginability
