# reflector

## What is it?

Reflector is a tool similar to the Unreal Header Tool. It will scan the headers in you codebase for types/methods/fields annotated with a [special reflection syntax](https://github.com/ghassanpl/reflector/wiki/Usage#code-requirements). It will then create files containing reflection information about those entities. The main type of file it will create is the [`*.mirror.h`](https://github.com/ghassanpl/reflector/wiki/Artifacts#mirrorh) file, which you are meant to include in the files Reflector is scanning. This will inject reflection information straight into your files, allowing for compile-time reflection of these entities.

```c++
#include "Component.mirror.h"

RClass()
class Component : public Reflector::Reflectable
{
	RBody()

	RField({"ReadOnly": true, "Required": true, "Setter": false})
	std::string Name;

	RMethod()
	auto SetName(std::string_view name) -> void
	{
		Name = name;
	}

protected:

	RField({"ParentPointer": true})
	class Object* mParentObject = nullptr;
};

REnum()
enum class TestEnum
{
	A = 5, B, C
};
```

## How to use

TODO

## Example

See the [example in the wiki](https://github.com/ghassanpl/reflector/wiki/Example).

## Dependencies

* C++17 (C++20 even)
* [nlohmann/json](https://github.com/nlohmann/json/)
* [args](https://github.com/Taywee/args)
* my personal [baselib](https://github.com/ghassanpl/baselib)

## Command line

	Reflector.exe files... {OPTIONS}
	OPTIONS:
		Commands
		-h, --help           Show help
		-r, --recursive      Recursively search the provided directories for files
		-q, --quiet          Don't print out created file names
		-f, --force          Ignore timestamps, regenerate all files
		-v, --verbose        Print additional information
		-j, --json           Output code that uses nlohmann::json to store class properties
		-d, --database       Create a JSON database with reflection data
		files...             Files or directories to scan
		"--" can be used to terminate flag options and force all following arguments to be treated as positional options
