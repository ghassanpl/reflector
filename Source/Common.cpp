/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include <mutex>

string_view TrimWhitespaceAndComments(std::string_view str)
{
	while (true)
	{
		trim_whitespace_left(str);
		if (str.starts_with("//"))
			return { str.end(), str.end() };
		if (consume(str, "/*"))
		{
			std::ignore = consume_until(str, "*/");
			std::ignore = consume(str, "*/");
		}
		else
			break;
	}
	return str;
}

void PrintSafe(std::ostream& strm, std::string val)
{
	static std::mutex print_mutex;
	std::unique_lock locl{ print_mutex };
	strm << val;
}

std::string FormatPreFlags(enum_flags<Reflector::FieldFlags> flags, enum_flags<Reflector::FieldFlags> except)
{
	std::vector<std::string_view> prefixes;
	flags = flags - except;
	if (flags.is_set(FieldFlags::Mutable)) prefixes.push_back("mutable ");
	if (flags.is_set(FieldFlags::Static)) prefixes.push_back("static ");
	return join(prefixes, "");
}

std::string FormatAccess(AccessMode mode)
{
	switch (mode)
	{
	case AccessMode::Public: return "public: ";
	case AccessMode::Protected: return "protected: ";
	case AccessMode::Private: return "private: ";
	default:
		break;
	}
	return {};
}

std::string FormatPreFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	using enum Reflector::MethodFlags;

	std::vector<std::string_view> prefixes;
	flags = flags - except;
	if (flags.is_set(NoDiscard)) prefixes.push_back("[[nodiscard]] ");
	if (flags.is_set(Inline)) prefixes.push_back("inline ");
	if (flags.is_set(Static)) prefixes.push_back("static ");
	if (flags.is_set(Virtual)) prefixes.push_back("virtual ");
	if (flags.is_set(Explicit)) prefixes.push_back("explicit ");
	return join(prefixes, "");
}

std::string FormatPostFlags(enum_flags<Reflector::MethodFlags> flags, enum_flags<Reflector::MethodFlags> except)
{
	using enum Reflector::MethodFlags;

	std::vector<std::string_view> suffixes;
	flags = flags - except;
	if (flags.is_set(Const)) suffixes.push_back(" const");
	if (flags.is_set(Final)) suffixes.push_back(" final");
	if (flags.is_set(Noexcept)) suffixes.push_back(" noexcept");
	return join(suffixes, "");
}

std::string Icon(std::string_view icon)
{
	if (icon.empty())
		return {};
	return format(R"(<i class="codicon codicon-{}"></i>)", icon);
}
