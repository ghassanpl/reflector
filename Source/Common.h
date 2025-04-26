/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include <iostream>
#include <filesystem>
#include <vector>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && __has_include(<expected>)
#include <expected>
namespace tl
{
	using std::expected;
	using std::unexpected;
}
#else
#include <tl/expected.hpp>
#endif
#include <nlohmann/json.hpp>
#include <ghassanpl/enum_flags.h>
#include <ghassanpl/string_ops.h>
#include <magic_enum/magic_enum_format.hpp>
#include <format>
using std::string_view;
using nlohmann::json;

#include "../Include/ReflectorClasses.h"
using Reflector::ClassFlags;
using Reflector::EnumeratorFlags;
using Reflector::EnumFlags;
using Reflector::FieldFlags;
using Reflector::MethodFlags;
using Reflector::AccessMode;
using std::filesystem::path;

using namespace ghassanpl;
using namespace ghassanpl::string_ops;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct Options;
struct Artifactory;
struct FileMirror;
struct TypeDeclaration;
struct Class;
struct Method;
struct Enum;
struct Enumerator;
struct Declaration;
enum class DeclarationType;

inline string_view TrimWhitespace(std::string_view str) { return trimmed_whitespace(str); }
string_view TrimWhitespaceAndComments(std::string_view str);

void PrintSafe(std::ostream& strm, std::string val);

template <typename... ARGS>
void ReportError(path const& path, size_t line_num, std::string_view fmt, ARGS&& ... args)
{
	PrintSafe(std::cerr, std::format("{}({},1): error: {}\n", path.string(), line_num, std::vformat(fmt, std::make_format_args(args...))));
}
template <typename... ARGS>
void ReportWarning(path const& path, size_t line_num, std::string_view fmt, ARGS&& ... args)
{
	PrintSafe(std::cerr, std::format("{}({},1): warning: {}\n", path.string(), line_num, std::vformat(fmt, std::make_format_args(args...))));
}

template <typename... ARGS>
void PrintLine(std::string_view fmt, ARGS&&... args)
{
	PrintSafe(std::cout, std::vformat(fmt, std::make_format_args(args...)) + "\n");
}

template <typename... ARGS>
void Print(std::string_view fmt, ARGS&&... args)
{
	PrintSafe(std::cout, std::vformat(fmt, std::make_format_args(std::forward<ARGS>(args)...)));
}

std::string EscapeJSON(json const& json);
std::string EscapeString(std::string_view str);

std::string FormatAccess(AccessMode mode);
std::string FormatPreFlags(enum_flags<Reflector::FieldFlags> flags, enum_flags<Reflector::FieldFlags> except = {});
std::string FormatPreFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except = {});
std::string FormatPostFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except = {});
std::string Icon(std::string_view icon);

inline uint64_t ExecutableChangeTime = 0;
inline uint64_t InvocationTime = 0;
inline bool CaseInsensitiveFileSystem = false;

inline std::string OnlyType(std::string str)
{
	if (const auto last = str.find_last_of(':'); last != std::string::npos)
		str.erase(0, last);
	return str;
}
