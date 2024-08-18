/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Parse.h"
#include "Attributes.h"
#include "Options.h"
#include <ghassanpl/string_ops.h>
#include <ghassanpl/wilson.h>
#include <ghassanpl/hashes.h>
#include <ghassanpl/containers.h>
#include <charconv>
#include <fstream>
using namespace std::string_literals;

std::string TypeFromVar(string_view str)
{
	return (std::string)TrimWhitespace({ str.begin(), std::find_if(str.rbegin(), str.rend(), [](char32_t cp) { return !(ascii::isalnum(cp) || cp == '_'); }).base() });
}

string_view Expect(string_view str, string_view value)
{
	if (!str.starts_with(value))
	{
		throw std::exception(std::format("Expected `{}`", value).c_str());
	}
	str.remove_prefix(value.size());
	return string_ops::trimmed_whitespace(str);
}

std::string EscapeJSON(json const& json)
{
	return "R\"_REFLECT_(" + json.dump() + ")_REFLECT_\"";
}

std::string EscapeString(std::string_view string)
{
	return std::format("R\"_REFLECT_({})_REFLECT_\"", string);
}

uint64_t GenerateUID(std::filesystem::path const& file_path, size_t declaration_line)
{
	return ghassanpl::hash64(file_path.string(), declaration_line);
}

bool SwallowOptional(string_view& str, string_view swallow)
{
	if (str.starts_with(swallow))
	{
		str = Expect(str, swallow);
		return true;
	}
	return false;
}

std::string ParseIdentifier(string_view& str)
{
	auto p = str.begin();
	for (; p != str.end(); ++p)
		if (!ascii::isident(*p)) break;
	std::string result = { str.begin(), p };
	str = make_sv(p, str.end());
	if (result.empty())
		throw std::runtime_error("Expected identifier");
	return result;
}

void ParseCppAttributes(std::string_view& line, json& target_attrs)
{
	static const std::map<std::string, std::string, std::less<>> cpp_attributes_to_reflector_attributes = {
		{"noreturn", "NoReturn"},
		{"deprecated", "Deprecated"},
		{"nodiscard", "NoDiscard"},
		{"no_unique_address", "NoUniqueAddress"},
		/// maybe_unused is not really relevant to anything
	};

	if (!consume(line, "[["))
		return;

	while (!consume(line, "]]") && !line.empty())
	{
		trim_whitespace_left(line);

		auto id = ParseIdentifier(line);
		id = ghassanpl::map_at_or_default(cpp_attributes_to_reflector_attributes, id, id);
		auto& entry = target_attrs[id] = true;

		trim_whitespace_left(line);
		if (consume(line, "("))
		{
			trim_whitespace_left(line);
			entry = ghassanpl::formats::wilson::consume_word_or_string(line).value_or("");
			trim_whitespace_left(line);
			std::ignore = consume(line, ")");
			trim_whitespace_left(line);
		}
		std::ignore = consume(line, ',');
	}

	trim_whitespace_left(line);
}

std::string ParseType(string_view& str)
{
	/// TODO: Unify all type parsing from entire codebase

	const auto start = str.begin();
	while (true)
	{
		if (SwallowOptional(str, "struct") ||
			SwallowOptional(str, "class") ||
			SwallowOptional(str, "enum") ||
			SwallowOptional(str, "union") ||
			SwallowOptional(str, "const"))
			continue;
		break;
	}

	trim_whitespace_left(str);

	int brackets = 0;
	int tris = 0;
	int parens = 0;
	for (; !str.empty(); str.remove_prefix(1))
	{
		switch (str[0])
		{
		case '[': brackets++; continue;
		case ']': brackets--; continue;
		case '(': parens++; continue;
		case ')': parens--; continue;
		case '<': tris++; continue;
		case '>': tris--; continue;
		}

		if (ascii::isblank(str[0]))
		{
			if (parens == 0 && tris == 0 && brackets == 0)
				break;
		}
	}
		
	trim_whitespace_left(str);
	
	while (true)
	{
		if (SwallowOptional(str, "const") || SwallowOptional(str, "*") || SwallowOptional(str, "&"))
			continue;
		break;
	}
	
	trim_whitespace_left(str);

	return ghassanpl::string_ops::make_string(start, str.begin());
}

std::string ParseExpression(string_view& str)
{
	trim_whitespace_left(str);

	int brackets = 0;
	int tris = 0;
	int parens = 0;
	int braces = 0;
	auto p = str.begin();
	for (; p != str.end(); ++p)
	{
		switch (*p)
		{
		case '[': brackets++; continue;
		case ']': if (brackets > 0) { brackets--; continue; } else break;
		case '(': parens++; continue;
		case ')': if (parens > 0) { parens--; continue; } else break;
		case '{': braces++; continue;
		case '}': if (braces > 0) { braces--; continue; } else break;
		case '<': tris++; continue;
		case '>': tris--; continue;
		}

		if ((*p == ',' || *p == ')' || *p == ';') && parens == 0 && tris == 0 && brackets == 0 && braces == 0)
			break;
	}

	std::string result = { str.begin(), p };
	str = make_sv(p, str.end());
	return result;
}

json ParseAttributeList(string_view line)
{
	line = TrimWhitespace(line);
	line = Expect(line, "(");
	line = TrimWhitespace(line);
	if (line.empty())
		return json::object();
	auto result = ghassanpl::formats::wilson::consume_object(line, ')').value_or(json{});
	if (auto unsettable = AttributeProperties::FindUnsettable(result); !unsettable.empty())
		throw std::runtime_error(format("The following attributes: '{}' cannot be set by the user, try using the C++ equivalents if applicable.", join(unsettable, ", ")));
	return result;
}

std::unique_ptr<Enum> ParseEnum(FileMirror* mirror, const std::vector<std::string>& lines, size_t& line_num, Options const& options)
{
	auto result = std::make_unique<Enum>(mirror);
	Enum& henum = *result;

	line_num--;
	henum.DeclarationLine = line_num + 1;

	auto line = TrimWhitespace(lines[line_num]);
	line.remove_prefix(options.EnumAnnotationName.size());
	henum.Attributes = ParseAttributeList(line);

	henum.DefaultEnumeratorAttributes = Attribute::DefaultEnumeratorAttributes(henum, json::object());

	line_num++;
	auto header_line = TrimWhitespace(lines[line_num]);

	header_line = Expect(header_line, "enum class");
	
	ParseCppAttributes(header_line, henum.Attributes);

	/// TODO: We should generalize it, so that setting a name checks attributes and sets display name, etc.
	henum.Name = ParseIdentifier(header_line);
	henum.DisplayName = henum.Name;
	Attribute::DisplayName.TryGet(henum, henum.DisplayName);

	header_line = TrimWhitespace(header_line);

	if (SwallowOptional(header_line, ":"))
		henum.BaseType = ParseType(header_line);

	if (!SwallowOptional(header_line, "{"))
	{
		line_num++;
		Expect(TrimWhitespace(lines[line_num]), "{");
	}

	json attribute_list = json::object();

	/// TODO: Gather comments

	std::vector<std::string> comments;

	line_num++;
	int64_t enumerator_value = 0;
	while (TrimWhitespace(lines[line_num]) != "};")
	{
		auto enumerator_line = TrimWhitespace(string_view{ lines[line_num] });

		if (consume(enumerator_line, "///"))
		{
			comments.push_back((std::string)TrimWhitespace(enumerator_line));
		}
		else if (enumerator_line.empty() || enumerator_line.starts_with("//") || enumerator_line.starts_with("/*"))
		{
			/// just skip
		}
		else if (consume(enumerator_line, options.EnumeratorAnnotationName))
		{
			attribute_list = ParseAttributeList(enumerator_line);
		}
		else
		{
			auto name = ParseIdentifier(enumerator_line);

			json cpp_attributes = json::object();
			ParseCppAttributes(header_line, cpp_attributes);

			auto rest = TrimWhitespace(enumerator_line);
			if (consume(rest, '='))
			{
				rest = TrimWhitespace(rest);

				/// TODO: Parse C++ integer literal (including 0x/0b bases, ' separators, suffixes, etc.)
				int base = 10;
				if (string_ops::ascii::string_starts_with_ignore_case(rest, "0x"))
				{
					rest.remove_prefix(2);
					base = 16;
				} 
				else if (rest.starts_with('0'))
				{
					base = 8;
				}
				auto fc_result = std::from_chars(std::to_address(rest.begin()), std::to_address(rest.end()), enumerator_value, base);

				if (fc_result.ec != std::errc{})
					throw std::runtime_error("Non-integer enumerator values are not supported");
				
				rest = TrimWhitespace(make_sv(fc_result.ptr, rest.end()));
			}

			(void)consume(rest, ",");
			rest = TrimWhitespace(rest);

			if (consume(rest, "///"))
				comments.push_back((std::string)trimmed_whitespace_left(rest));
			else if (!rest.empty())
				throw std::runtime_error("Enumerators must be the only thing on their line (except comments)");

			if (!name.empty())
			{
				auto enumerator_result = std::make_unique<Enumerator>(&henum);
				Enumerator& enumerator = *enumerator_result;
				enumerator.Name = TrimWhitespace(name);
				enumerator.DisplayName = enumerator.Name;
				enumerator.Value = enumerator_value;
				enumerator.DeclarationLine = line_num;
				enumerator.Attributes = henum.DefaultEnumeratorAttributes;
				enumerator.Attributes.update(std::exchange(attribute_list, json::object()), true);
				enumerator.Attributes.update(cpp_attributes);
				enumerator.Comments = std::exchange(comments, {});
				Attribute::DisplayName.TryGet(enumerator, enumerator.DisplayName);
				henum.Enumerators.push_back(std::move(enumerator_result));
				enumerator_value++;
			}

			comments.clear();
		}

		line_num++;
	}

	henum.Namespace = Attribute::Namespace.GetOr(henum, options.DefaultNamespace);

	return result;
}

struct ParsedClassLine
{
	std::string Name;
	std::string BaseClass;
	bool IsStruct = false;
	json Attributes = json::object();
};

void RemoveBlockComments(std::string& str)
{
	while (!str.empty())
	{
		auto start = str.find("/*");
		if (start == std::string::npos)
			break;
		auto end = str.find("*/", start);
		if (end == std::string::npos)
		{
			str.erase(start);
			break;
		}
		str.erase(start, end - start + 2);
	}
}

ParsedClassLine ParseClassLine(string_view line)
{
	ParsedClassLine result;

	if (line.starts_with("struct"))
	{
		result.IsStruct = true;
		line = Expect(line, "struct");
	}
	else
	{
		line = Expect(line, "class");
	}

	ParseCppAttributes(line, result.Attributes);
	
	/// TODO: Parse C++ [[attributes]], specifically [[deprecated]]

	result.Name = ParseIdentifier(line);
	line = TrimWhitespaceAndComments(line);

	if (line.starts_with(":"))
	{
		line = Expect(line, ":");
		SwallowOptional(line, "public");
		SwallowOptional(line, "protected");
		SwallowOptional(line, "private");
		SwallowOptional(line, "virtual");

		int parens = 0;
		int triangles = 0;
		int brackets = 0;

		line = TrimWhitespaceAndComments(line);
		auto start = line;
		while (!line.empty() && !line.starts_with("{") && !line.starts_with("//"))
		{
			const auto ch = *line.begin();
			if (ch == '(') parens++;
			else if (ch == ')') parens--;
			else if (ch == '<') triangles++;
			else if (ch == '>') triangles--;
			else if (ch == '[') brackets++;
			else if (ch == ']') brackets--;
			else if (ch == ',' && (parens == 0 && triangles == 0 && brackets == 0))
				break;

			if (parens < 0 || triangles < 0 || brackets < 0)
				throw std::exception{ "Mismatch class parents" };

			(void)consume(line);
			line = TrimWhitespace(line);
		}

		result.BaseClass = std::string{ TrimWhitespace(make_sv(start.begin(), line.begin())) };
		RemoveBlockComments(result.BaseClass);
	}

	return result;
}

struct ParsedFieldDecl
{
	std::string Type;
	std::string Name;
	std::string Initializer;
	enum_flags<FieldFlags> Flags{};
	json Attributes = json::object();
};

ParsedFieldDecl ParseFieldDecl(string_view line)
{
	ParsedFieldDecl result;

	line = TrimWhitespace(line);

	ParseCppAttributes(line, result.Attributes);

	/// TODO: thread_local? extern? inline?

	while (true)
	{
		if (SwallowOptional(line, "mutable"))
		{
			result.Flags += FieldFlags::Mutable;
			continue;
		}
		if (SwallowOptional(line, "static"))
		{
			result.Flags += FieldFlags::Static;
			continue;
		}
		if (SwallowOptional(line, "inline"))
			continue;
		break;
	}
	

	const string_view eq = "=";
	const string_view open_brace = "{";
	string_view type_and_name;
	const auto eq_start = std::ranges::find_first_of(line, eq);
	const auto brace_start = std::ranges::find_first_of(line, open_brace);
	
	if (eq_start != line.end())
	{
		type_and_name = TrimWhitespace(string_ops::make_sv(line.begin(), eq_start));
		line = string_ops::make_sv(eq_start + 1, line.end());
		result.Initializer = ParseExpression(line);
	}
	else if (brace_start != line.end())
	{
		type_and_name = TrimWhitespace(string_ops::make_sv(line.begin(), brace_start));
		line = string_ops::make_sv(brace_start, line.end());
		result.Initializer = ParseExpression(line);
		result.Flags += FieldFlags::BraceInitialized;
	}
	else
	{
		trim_whitespace_left(line);
		type_and_name = consume_until(line, ';');
	}

	line = Expect(line, ";");

	if (type_and_name.empty())
	{
		throw std::exception{ "Field() must be followed by a proper class field declaration" };
	}

	const auto name_start = ++std::find_if_not(type_and_name.rbegin(), type_and_name.rend(), ascii::isident);

	result.Type = TrimWhitespace(make_sv(type_and_name.begin(), name_start.base()));
	result.Name = TrimWhitespace(make_sv(name_start.base(), type_and_name.end()));

	if (!std::ranges::all_of(result.Name, ascii::isident))
		throw std::runtime_error("Invalid field name");

	return result;
}

std::unique_ptr<Field> ParseFieldDecl(const FileMirror& mirror, Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	auto result = std::make_unique<Field>(&klass);
	Field& field = *result;
	line.remove_prefix(options.FieldAnnotationName.size());
	field.Access = mode;
	if (field.Access != AccessMode::Public && field.Access != AccessMode::Unspecified)
	{
		field.Flags += FieldFlags::DeclaredPrivate;
		field.ForceDocument = false; /// Default
	}
	field.Attributes = klass.DefaultFieldAttributes;
	field.Attributes.update(ParseAttributeList(line), true);
	field.DeclarationLine = line_num;
	field.Comments = std::move(comments);
	const auto&& [type, name, initializer, flags, cpp_attributes] = ParseFieldDecl(next_line);
	field.Attributes.update(cpp_attributes);
	field.Type = type;
	field.Name = name;
	field.InitializingExpression = initializer;
	field.Flags += flags;
	if (field.Name.size() > 1 && field.Name[0] == 'm' && isupper(field.Name[1])) /// TODO: Change this to an optionable regex
		field.DisplayName.assign(field.Name.begin() + 1, field.Name.end());
	else
		field.DisplayName = field.Name;
	field.CleanName = field.DisplayName;
	field.LoadName = field.Name;
	field.SaveName = field.Name;

	/// Get names from attributes if set
	Attribute::DisplayName.TryGet(field, field.DisplayName);
	Attribute::LoadName.TryGet(field, field.LoadName);
	Attribute::SaveName.TryGet(field, field.SaveName);

	/// TODO: Check for uniqueness of LoadName/SaveName as well as make sure regular Names of reflected fields are unique up the class hierarchy

	const bool is_public = field.Access == AccessMode::Public;

	/// Disable if explictly stated
	if (Attribute::Getter.GetOr(field, true) == false 
		|| (!is_public && Attribute::PrivateGetters(klass) == false)
		|| (is_public && !options.GenerateAccessorsForPublicFields)
		)
		field.Flags.set(Reflector::FieldFlags::NoGetter);
	if (Attribute::Setter.GetOr(field, true) == false 
		|| (!is_public && Attribute::PrivateSetters(klass) == false)
		|| (is_public && !options.GenerateAccessorsForPublicFields)
		)
		field.Flags.set(Reflector::FieldFlags::NoSetter);
	if (Attribute::Editor.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoEdit);
	if (Attribute::Script.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoScript);
	if (Attribute::Save.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSave);
	if (Attribute::Load.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoLoad);

	/// Serialize = false implies Save = false, Load = false
	if (Attribute::Serialize.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSave, Reflector::FieldFlags::NoLoad);

	/// Private implies Getter = false, Setter = false, Editor = false
	if (Attribute::Private.GetOr(field, false))
		field.Flags.set(Reflector::FieldFlags::NoEdit, Reflector::FieldFlags::NoSetter, Reflector::FieldFlags::NoGetter);
	if (Attribute::Transient.GetOr(field, false))
		field.Flags.set(Reflector::FieldFlags::NoSetter, Reflector::FieldFlags::NoSave, Reflector::FieldFlags::NoLoad);
	if (Attribute::ScriptPrivate.GetOr(field, false))
		field.Flags.set(Reflector::FieldFlags::NoSetter, Reflector::FieldFlags::NoGetter);

	/// ParentPointer implies Editor = false, Setter = false
	/*
	if (Attribute::ParentPointer.GetOr(field, false))
		field.Flags.set(Reflector::FieldFlags::NoEdit, Reflector::FieldFlags::NoSetter);
	*/

	/// ChildVector implies Setter = false
	/// TODO: This needs to go, move to hierarchies I guess
	/*
	if (type.starts_with("ChildVector<"))
		field.Flags.set(Reflector::FieldFlags::NoSetter);
		*/


	/// Enable if explictly stated
	if (Attribute::Getter.GetOr(field, false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoGetter);
	if (Attribute::Setter.GetOr(field, false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoSetter);
	if (Attribute::Editor.GetOr(field, false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoEdit);
	if (Attribute::Save.GetOr(field, false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoSave);
	if (Attribute::Load.GetOr(field, false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoLoad);

	if (Attribute::Required(field))
		field.Flags.set(Reflector::FieldFlags::Required);

	if (Attribute::NoUniqueAddress(field))
		field.Flags += FieldFlags::NoUniqueAddress;

	return result;
}

std::unique_ptr<Method> ParseMethodDecl(Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	auto result = std::make_unique<Method>(&klass);
	Method& method = *result;
	line.remove_prefix(options.MethodAnnotationName.size());
	method.Access = mode;
	method.Attributes = klass.DefaultMethodAttributes;
	method.Attributes.update(ParseAttributeList(line), true);
	method.DeclarationLine = line_num;

	ParseCppAttributes(next_line, result->Attributes);

	while (true)
	{
		using enum Reflector::MethodFlags;

		if (SwallowOptional(next_line, "virtual")) method.Flags += Virtual;
		else if (SwallowOptional(next_line, "static")) method.Flags += Static;
		else if (SwallowOptional(next_line, "inline")) method.Flags += Inline;
		else if (SwallowOptional(next_line, "explicit")) method.Flags += Explicit;
		else break;
	}

	if (next_line.starts_with('~'))
		throw std::runtime_error(std::format("Destructor reflection is not supported"));

	auto pre_typestr = ParseType(next_line);
	auto pre_type = (std::string)TrimWhitespace(string_view{ pre_typestr });
	next_line = TrimWhitespace(next_line);

	method.Name = ParseIdentifier(next_line);
	if (method.Name == "operator")
		throw std::runtime_error(std::format("Operator method reflection is not supported yet"));

	ParseCppAttributes(next_line, result->Attributes); /// Unfortunately, C++ attributes on functions can also be after the name: `void q [[ noreturn ]] (int i);`

	if (!string_ops::string_contains(next_line, '('))
		throw std::runtime_error(std::format("Misformed method declaration"));

	int num_pars = 0;
	auto start_args = next_line.begin();
	do
	{
		if (next_line.starts_with("("))
			num_pars++;
		else if (next_line.starts_with(")"))
			num_pars--;
		next_line.remove_prefix(1);
	} while (num_pars);
	auto params = string_ops::make_sv(start_args + 1, next_line.begin());
	
	if (params.empty())
		throw std::runtime_error(std::format("Misformed method declaration"));

	params.remove_suffix(1);
	method.SetParameters(std::string{params});

	next_line = TrimWhitespace(next_line);

	while (true)
	{
		using enum Reflector::MethodFlags;

		if (SwallowOptional(next_line, "const")) method.Flags += Const;
		else if (SwallowOptional(next_line, "final")) method.Flags += Final;
		else if (SwallowOptional(next_line, "noexcept")) method.Flags += Noexcept;
		else break;
	}

	if (pre_type == "auto")
	{
		next_line = Expect(next_line, "->");

		auto end_line = next_line.find_first_of("{;=");
		if (end_line != std::string::npos)
		{
			if (next_line[end_line] == '=')
				method.Flags += MethodFlags::Abstract;
			next_line = TrimWhitespace(next_line.substr(0, end_line));
		}
		if (next_line.ends_with("override"))
			next_line.remove_suffix(sizeof("override") - 1);
		method.Return.Name = (std::string)TrimWhitespace(next_line);
	}
	else
	{
		method.Return.Name = pre_type;

		auto end_line = next_line.find_first_of("{;=");
		if (end_line != std::string::npos && next_line[end_line] == '=')
			method.Flags += MethodFlags::Abstract;
	}

	if (auto getter = Attribute::UniqueName.SafeGet(method))
		method.UniqueName = *getter;

	method.DisplayName = method.Name;
	Attribute::DisplayName.TryGet(method, method.DisplayName);

	if (Attribute::NoReturn(method))
		method.Flags += MethodFlags::NoReturn;

	if (Attribute::Script.GetOr(method, true) == false)
		method.Flags.set(Reflector::MethodFlags::NoScript);

	method.Comments = std::move(comments);

	/// TODO: How to docnote this?
	auto getter = Attribute::GetterFor.SafeGet(method);
	if (!getter && Attribute::Property(method) == true)
		getter = method.Name;
	if (getter)
	{
		auto& property = klass.EnsureProperty(*getter);
		if (property.Getter)
			throw std::exception(std::format("Getter for this property already declared at line {}", property.Getter->DeclarationLine).c_str());
		property.Getter = &method;
		/// TODO: Match getter/setter types
		property.Type = method.Return.Name;
		if (property.Name.empty()) property.Name = *getter;
	}

	if (auto setter = Attribute::SetterFor.SafeGet(method))
	{
		auto& property = klass.EnsureProperty(*setter);
		if (property.Setter)
			throw std::exception(std::format("Setter for this property already declared at line {}", property.Setter->DeclarationLine).c_str());
		property.Setter = &method;
		/// TODO: Match getter/setter types
		if (property.Type.empty())
		{
			if (method.ParametersSplit.empty())
				throw std::exception("Setter must have at least 1 argument");
			property.Type = method.ParametersSplit[0].Type;
		}
		if (property.Name.empty()) property.Name = *setter;
	}

	return result;
}

std::unique_ptr<Class> ParseClassDecl(FileMirror* mirror, string_view line, string_view next_line, size_t line_num, std::vector<std::string> comments, Options const& options)
{
	auto result = std::make_unique<Class>(mirror);
	Class& klass = *result;
	line.remove_prefix(options.ClassAnnotationName.size());
	klass.Attributes = ParseAttributeList(line);
	klass.DeclarationLine = line_num;
	auto [name, parent, is_struct, cpp_attributes] = ParseClassLine(next_line);
	klass.Attributes.update(cpp_attributes);
	klass.Name = name;
	klass.BaseClass = parent;
	klass.Comments = std::move(comments);
	if (klass.BaseClass.empty())
		klass.Flags += ClassFlags::Struct;
	if (is_struct)
		klass.Flags += ClassFlags::DeclaredStruct;

	klass.DefaultFieldAttributes = Attribute::DefaultFieldAttributes(klass, json::object());
	klass.DefaultMethodAttributes = Attribute::DefaultMethodAttributes(klass, json::object());

	/// TODO: This procedure doesn't really make sense, why does struct imply no constructors?
	
	if (klass.Flags.is_set(ClassFlags::Struct) || Attribute::Abstract(klass) == true || Attribute::Singleton(klass) == true)
		klass.Flags += ClassFlags::NoConstructors;

	if (Attribute::Serialize(klass) == false)
		klass.Flags += ClassFlags::NotSerializable;
	if (Attribute::Editor(klass) == false)
		klass.Flags += ClassFlags::NotEditable;
	if (Attribute::Script(klass) == false)
		klass.Flags += ClassFlags::NotScriptable;

	/// If we declared an actual class it has to derive from Reflectable
	if (!klass.Flags.is_set(ClassFlags::NoConstructors) && klass.BaseClass.empty())
	{
		throw std::exception(std::format("Non-struct class '{}' must derive from Reflectable or a Reflectable class", klass.FullType()).c_str());
	}

	klass.Namespace = Attribute::Namespace.GetOr(klass, options.DefaultNamespace);
	klass.GUID = Attribute::GUID(klass);

	klass.DisplayName = klass.Name;
	Attribute::DisplayName.TryGet(klass, klass.DisplayName);

	return result;
}

bool ParseClassFile(std::filesystem::path path, Options const& options)
{
	path = path.lexically_normal();

	if (options.Verbose)
		PrintLine("Analyzing file {}", path.string());

	std::vector<std::string> lines;
	std::string input_line;
	std::ifstream infile{ path };
	while (std::getline(infile, input_line))
		lines.push_back(std::exchange(input_line, {}));
	infile.close();

	FileMirror& mirror = *AddMirror();
	mirror.SourceFilePath = std::filesystem::absolute(path);

	AccessMode current_access = AccessMode::Unspecified;

	std::vector<std::string> comments;

	for (size_t line_num = 1; line_num < lines.size(); line_num++)
	{
		auto current_line = TrimWhitespace(string_view{ lines[line_num - 1] });
		const auto next_line = TrimWhitespace(string_view{ lines[line_num] });

		/// TODO: RAlias(); for `using`s

		try
		{
			if (current_line.starts_with("public:"))
				current_access = AccessMode::Public;
			else if (current_line.starts_with("protected:"))
				current_access = AccessMode::Protected;
			else if (current_line.starts_with("private:"))
				current_access = AccessMode::Private;
			else if (current_line.starts_with(options.EnumAnnotationName))
			{
				mirror.Enums.push_back(ParseEnum(&mirror, lines, line_num, options));
				mirror.Enums.back()->Comments = std::exchange(comments, {});
				mirror.Enums.back()->ReflectionUID = GenerateUID(path, line_num);
				if (options.Verbose)
				{
					PrintLine("Found enum {}", mirror.Enums.back()->FullType());
				}
			}
			else if (current_line.starts_with(options.ClassAnnotationName))
			{
				current_access = AccessMode::Private;
				mirror.Classes.push_back(ParseClassDecl(&mirror, current_line, next_line, line_num, std::exchange(comments, {}), options));
				mirror.Classes.back()->ReflectionUID = GenerateUID(path, line_num);
				if (options.Verbose)
				{
					PrintLine("Found class {}", mirror.Classes.back()->FullType());
				}
			}
			else if (current_line.starts_with(options.FieldAnnotationName))
			{
				if (mirror.Classes.empty())
				{
					ReportError(path, line_num + 1, "{}() not in class", options.FieldAnnotationName);
					return false;
				}

				auto& klass = mirror.Classes.back();
				if (klass->BodyLine == 0)
				{
					ReportError(path, line_num + 1, "Field before Body annotation (did you forget an {}?)", options.BodyAnnotationName);
					return false;
				}

				klass->Fields.push_back(ParseFieldDecl(mirror, *klass, current_line, next_line, line_num, current_access, std::exchange(comments, {}), options));
				klass->Fields.back()->ReflectionUID = GenerateUID(path, line_num);
			}
			else if (current_line.starts_with(options.MethodAnnotationName))
			{
				if (mirror.Classes.empty())
				{
					ReportError(path, line_num + 1, "{}() not in class", options.MethodAnnotationName);
					return false;
				}

				auto& klass = mirror.Classes.back();
				if (klass->BodyLine == 0)
				{
					ReportError(path, line_num + 1, "Method before Body annotation (did you forget an {}?)", options.BodyAnnotationName);
					return false;
				}

				klass->Methods.push_back(ParseMethodDecl(*klass, current_line, next_line, line_num, current_access, std::exchange(comments, {}), options));
				klass->Methods.back()->ReflectionUID = GenerateUID(path, line_num);
			}
			else if (current_line.starts_with(options.BodyAnnotationName))
			{
				if (mirror.Classes.empty())
				{
					ReportError(path, line_num + 1, "{}() not in class", options.BodyAnnotationName);
					return false;
				}

				current_access = AccessMode::Public;

				mirror.Classes.back()->BodyLine = line_num;
			}

			if (current_line.starts_with("///"))
			{
				comments.push_back((std::string)TrimWhitespace(current_line.substr(3)));
			}
			else
				comments.clear();
		}
		catch (std::exception& e)
		{
			ReportError(path, line_num + 1, "{}", e.what());
			return false;
		}
	}

	return true;
}

