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
	return ghassanpl::hash(file_path.string(), declaration_line);
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
	return result;
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

	str = string_ops::trimmed_whitespace(str);

	int brackets = 0, tris = 0, parens = 0;
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
		
	str = string_ops::trimmed_whitespace(str);
	
	while (true)
	{
		if (SwallowOptional(str, "const") || SwallowOptional(str, "*") || SwallowOptional(str, "&"))
			continue;
		break;
	}
	
	str = string_ops::trimmed_whitespace(str);

	return ghassanpl::string_ops::make_string(start, str.begin());
}

std::string ParseExpression(string_view& str)
{
	int brackets = 0, tris = 0, parens = 0;
	auto p = str.begin();
	for (; p != str.end(); ++p)
	{
		switch (*p)
		{
		case '[': brackets++; continue;
		case ']': if (brackets > 0) { brackets--; continue; } else break;
		case '(': parens++; continue;
		case ')': if (parens > 0) { parens--; continue; } else break;
		case '<': tris++; continue;
		case '>': tris--; continue;
		}

		if (*p == ',' || *p == ')')
		{
			if (parens == 0 && tris == 0 && brackets == 0)
				break;
		}
	}

	std::string result = { str.begin(), p };
	str = make_sv(p, str.end());
	return result;
}

/*
std::tuple<std::string, std::string> ParseParameter(string_view& str)
{
	/// TODO: Function pointers, arrays, etc.
	auto type = ParseType(str);
	auto name = ParseIdentifier(str);
	str = string_ops::trim_whitespaceLeft(str);
	if (str[0] == '=')
	{
	}
		auto initializer = ParseExpression(str);
		return { std::move(type), std::move(name), std::move(initializer) };
	}
	else
		return { std::move(type), std::move(name), {} };
}
*/

json ParseAttributeList(string_view line)
{
	line = TrimWhitespace(line);
	line = Expect(line, "(");
	line = TrimWhitespace(line);
	if (line.empty())
		return json::object();
	//return json::parse(line);
	return ghassanpl::formats::wilson::parse_object(line, ')');
}

std::unique_ptr<Enum> ParseEnum(FileMirror* mirror, const std::vector<std::string>& lines, size_t& line_num, Options const& options)
{
	auto result = std::make_unique<Enum>(mirror);
	Enum& henum = *result;

	line_num--;
	henum.DeclarationLine = line_num + 1;

	auto line = TrimWhitespace(string_view{ lines[line_num] });
	line.remove_prefix(options.EnumAnnotationName.size());
	henum.Attributes = ParseAttributeList(line);

	henum.DefaultEnumeratorAttributes = Attribute::DefaultEnumeratorAttributes(henum, json::object());

	line_num++;
	auto header_line = TrimWhitespace(string_view{ lines[line_num] });

	header_line = Expect(header_line, "enum class");
	auto enum_name_start = header_line.begin();
	auto enum_name_end = std::ranges::find_if_not(header_line, ascii::isident);

	henum.Name = TrimWhitespace(string_ops::make_sv(enum_name_start, enum_name_end));
	///TODO: parse base type

	line_num++;
	Expect(TrimWhitespace(string_view{ lines[line_num] }), "{");

	json attribute_list = json::object();

	line_num++;
	int64_t enumerator_value = 0;
	while (TrimWhitespace(string_view{ lines[line_num] }) != "};")
	{
		auto enumerator_line = TrimWhitespace(string_view{ lines[line_num] });
		if (enumerator_line.empty() || enumerator_line.starts_with("//") || enumerator_line.starts_with("/*"))
		{
			line_num++;
			continue;
		}

		if (enumerator_line.starts_with(options.EnumeratorAnnotationName))
		{
			enumerator_line.remove_prefix(options.EnumeratorAnnotationName.size());
			attribute_list = ParseAttributeList(enumerator_line);

			line_num++;
			continue;
		}

		auto enumerator_name_start = enumerator_line.begin();
		auto enumerator_name_end = std::ranges::find_if_not(enumerator_line, ascii::isident);

		if (auto rest = TrimWhitespace(make_sv(enumerator_name_end, enumerator_line.end())); consume(rest, '='))
		{
			rest = TrimWhitespace(rest);
			std::from_chars(std::to_address(rest.begin()), std::to_address(rest.end()), enumerator_value);
			/// TODO: Non-integer enumerator values (like 1<<5 and constexpr function calls/expression)
		}

		if (auto name = make_sv(enumerator_name_start, enumerator_name_end); !name.empty())
		{
			auto result = std::make_unique<Enumerator>(&henum);
			Enumerator& enumerator = *result;
			enumerator.Name = TrimWhitespace(name);
			enumerator.Value = enumerator_value;
			enumerator.DeclarationLine = line_num;
			enumerator.Attributes = henum.DefaultEnumeratorAttributes;
			enumerator.Attributes.update(std::exchange(attribute_list, json::object()), true);
			/// TODO: enumerator.Comments = "";
			henum.Enumerators.push_back(std::move(result));
			enumerator_value++;
		}
		line_num++;
	}

	henum.Namespace = Attribute::Namespace(henum);

	return result;
}

auto ParseClassLine(string_view line)
{
	std::tuple<std::string, std::string, bool> result;

	if (line.starts_with("struct"))
	{
		std::get<2>(result) = true;
		line = Expect(line, "struct");
	}
	else
	{
		std::get<2>(result) = false;
		line = Expect(line, "class");
	}
	std::get<0>(result) = ParseIdentifier(line);
	line = TrimWhitespace(line);

	if (line.starts_with(":"))
	{
		line = Expect(line, ":");
		SwallowOptional(line, "public");
		SwallowOptional(line, "protected");
		SwallowOptional(line, "private");
		SwallowOptional(line, "virtual");

		int parens = 0, triangles = 0, brackets = 0;

		line = TrimWhitespace(line);
		auto start = line;
		while (!line.empty() && !line.starts_with("{"))
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
		}

		std::get<1>(result) = { std::to_address(start.begin()), std::to_address(line.begin()) };
	}

	return result;
}

struct ParsedFieldDecl
{
	std::string Type;
	std::string Name;
	std::string Initializer;
	enum_flags<FieldFlags> Flags{};
};

ParsedFieldDecl ParseFieldDecl(string_view line)
{
	ParsedFieldDecl result;

	line = TrimWhitespace(line);

	/// TODO: Parse attributes

	/// thread_local? extern? inline?

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
	

	string_view eq = "=", open_brace = "{", colon = ";";
	string_view type_and_name;
	const auto eq_start = std::ranges::find_first_of(line, eq);
	const auto brace_start = std::ranges::find_first_of(line, open_brace);
	const auto colon_start = std::ranges::find_first_of(line, colon);

	if (colon_start != line.end() && !TrimWhitespace(string_ops::make_sv(colon_start + 1, line.end())).empty())
	{
		throw std::exception{ "Field must be only thing on line" };
	}

	if (eq_start != line.end())
	{
		type_and_name = TrimWhitespace(string_ops::make_sv(line.begin(), eq_start));
		result.Initializer = TrimWhitespace(string_ops::make_sv(eq_start + 1, colon_start));
	}
	else if (brace_start != line.end())
	{
		type_and_name = TrimWhitespace(string_ops::make_sv(line.begin(), brace_start));
		result.Initializer = TrimWhitespace(string_ops::make_sv(brace_start, colon_start));
	}
	else
	{
		type_and_name = TrimWhitespace(make_sv(line.begin(), colon_start));
	}

	if (type_and_name.empty())
	{
		throw std::exception{ "Field() must be followed by a proper class field declaration" };
	}

	const auto name_start = ++std::find_if_not(type_and_name.rbegin(), type_and_name.rend(), ascii::isident);

	result.Type = TrimWhitespace(make_sv(type_and_name.begin(), name_start.base()));
	result.Name = TrimWhitespace(make_sv(name_start.base(), type_and_name.end()));

	return result;
}

std::unique_ptr<Field> ParseFieldDecl(const FileMirror& mirror, Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	auto result = std::make_unique<Field>(&klass);
	Field& field = *result;
	line.remove_prefix(options.FieldAnnotationName.size());
	field.Access = mode;
	if (field.Access != AccessMode::Public && field.Access != AccessMode::Unspecified)
		field.Flags += FieldFlags::DeclaredPrivate;
	field.Attributes = klass.DefaultFieldAttributes;
	field.Attributes.update(ParseAttributeList(line), true);
	field.DeclarationLine = line_num;
	field.Comments = std::move(comments);
	const auto&& [type, name, initializer, flags] = ParseFieldDecl(next_line);
	field.Type = type;
	field.Name = name;
	field.InitializingExpression = initializer;
	field.Flags += flags;
	if (field.Name.size() > 1 && field.Name[0] == 'm' && isupper(field.Name[1])) /// TODO: Change this to an optionable regex
		field.DisplayName.assign(field.Name.begin() + 1, field.Name.end());
	else
		field.DisplayName = field.Name;
	field.CleanName = field.DisplayName;

	/// Get display name from attributes if set
	Attribute::DisplayName.TryGet(field, field.DisplayName);

	/// Disable if explictly stated
	if (Attribute::Getter.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoGetter);
	if (Attribute::Setter.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSetter);
	if (Attribute::Editor.GetOr(field, true) == false)
		field.Flags.set(Reflector::FieldFlags::NoEdit);
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

	/// ParentPointer implies Editor = false, Setter = false
	if (Attribute::ParentPointer.GetOr(field, false))
		field.Flags.set(Reflector::FieldFlags::NoEdit, Reflector::FieldFlags::NoSetter);

	/// ChildVector implies Setter = false
	if (type.starts_with("ChildVector<"))
		field.Flags.set(Reflector::FieldFlags::NoSetter);


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

	return result;
}

std::vector<string_view> SplitArgs(string_view argstring)
{
	std::vector<string_view> args;
	int brackets = 0, tris = 0, parens = 0;
	auto begin = argstring.begin();
	while (!argstring.empty())
	{
		switch (argstring[0])
		{
		case '(': parens++; break;
		case ')': parens--; break;
		case '<': tris++; break;
		case '>': tris--; break;
		case '[': brackets++; break;
		case ']': brackets--; break;
		case ',': if (parens == 0 && tris == 0 && brackets == 0) { args.push_back(make_sv(begin, argstring.begin())); argstring.remove_prefix(1); begin = argstring.begin(); } break;
		}

		argstring.remove_prefix(1);

		if (argstring.empty())
		{
			args.push_back({ to_address(begin), to_address(argstring.begin()) });
			break;
		}
	}

	return args;
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
	
	while (true)
	{
		if (SwallowOptional(next_line, "virtual")) method.Flags += Reflector::MethodFlags::Virtual;
		else if (SwallowOptional(next_line, "static")) method.Flags += Reflector::MethodFlags::Static;
		else if (SwallowOptional(next_line, "inline")) method.Flags += Reflector::MethodFlags::Inline;
		else if (SwallowOptional(next_line, "explicit")) method.Flags += Reflector::MethodFlags::Explicit;
		else break;
	}

	auto pre_typestr = ParseType(next_line);
	auto pre_type = (std::string)TrimWhitespace(string_view{ pre_typestr });
	next_line = TrimWhitespace(next_line);

	auto name_start = next_line.begin();
	auto name_end = std::ranges::find_if_not(next_line, ascii::isident);
	method.Name = TrimWhitespace(make_sv(name_start, name_end));
	int num_pars = 0;
	auto start_args = name_end;
	next_line = make_sv(name_end, next_line.end());
	do
	{
		if (next_line.starts_with("("))
			num_pars++;
		else if (next_line.starts_with(")"))
			num_pars--;
		next_line.remove_prefix(1);
	} while (num_pars);
	auto params = string_ops::make_sv(start_args + 1, next_line.begin());
	params.remove_suffix(1);
	method.SetParameters(std::string{params});

	next_line = TrimWhitespace(next_line);

	while (true)
	{
		if (SwallowOptional(next_line, "const")) method.Flags += Reflector::MethodFlags::Const;
		else if (SwallowOptional(next_line, "final")) method.Flags += Reflector::MethodFlags::Final;
		else if (SwallowOptional(next_line, "noexcept")) method.Flags += Reflector::MethodFlags::Noexcept;
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
		method.ReturnType = (std::string)TrimWhitespace(next_line);
	}
	else
	{
		method.ReturnType = pre_type;

		auto end_line = next_line.find_first_of("{;=");
		if (end_line != std::string::npos && next_line[end_line] == '=')
			method.Flags += MethodFlags::Abstract;
	}

	if (auto getter = Attribute::UniqueName.SafeGet(method))
		method.UniqueName = *getter;

	if (auto getter = Attribute::GetterFor.SafeGet(method))
	{
		auto& property = klass.Properties[*getter];
		if (!property.GetterName.empty())
			throw std::exception(std::format("Getter for this property already declared at line {}", property.GetterLine).c_str());
		property.GetterName = method.Name;
		property.GetterLine = line_num;
		/// TODO: Match getter/setter types
		property.Type = method.ReturnType;
		if (property.Name.empty()) property.Name = *getter;
	}

	if (auto setter = Attribute::SetterFor.SafeGet(method))
	{
		auto& property = klass.Properties[*setter];
		if (!property.SetterName.empty())
			throw std::exception(std::format("Setter for this property already declared at line {}", property.SetterLine).c_str());
		property.SetterName = method.Name;
		property.SetterLine = line_num;
		/// TODO: Match getter/setter types
		if (property.Type.empty())
		{
			if (method.ParametersSplit.empty())
				throw std::exception("Setter must have at least 1 argument");
			property.Type = method.ParametersSplit[0].Type;
		}
		if (property.Name.empty()) property.Name = *setter;
	}

	method.Comments = std::move(comments);

	return result;
}

std::unique_ptr<Class> ParseClassDecl(FileMirror* mirror, string_view line, string_view next_line, size_t line_num, std::vector<std::string> comments, Options const& options)
{
	auto result = std::make_unique<Class>(mirror);
	Class& klass = *result;
	line.remove_prefix(options.ClassAnnotationName.size());
	klass.Attributes = ParseAttributeList(line);
	klass.DeclarationLine = line_num;
	auto [name, parent, is_struct] = ParseClassLine(next_line);
	klass.Name = name;
	klass.BaseClass = parent;
	klass.Comments = std::move(comments);
	if (klass.BaseClass.empty())
		klass.Flags += ClassFlags::Struct;
	if (is_struct)
		klass.Flags += ClassFlags::DeclaredStruct;

	klass.DefaultFieldAttributes = Attribute::DefaultFieldAttributes(klass, json::object());
	klass.DefaultMethodAttributes = Attribute::DefaultMethodAttributes(klass, json::object());

	if (klass.Flags.is_set(ClassFlags::Struct) || Attribute::Abstract(klass) == true || Attribute::Singleton(klass) == true)
		klass.Flags += ClassFlags::NoConstructors;

	/// If we declared an actual class it has to derive from Reflectable
	if (!klass.Flags.is_set(ClassFlags::NoConstructors) && klass.BaseClass.empty())
	{
		throw std::exception(std::format("Non-struct class '{}' must derive from Reflectable or a Reflectable class", klass.FullType()).c_str());
	}

	klass.Namespace = Attribute::Namespace(klass);

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
		auto next_line = TrimWhitespace(string_view{ lines[line_num] });

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
				mirror.Enums.back()->UID = GenerateUID(path, line_num);
				if (options.Verbose)
				{
					PrintLine("Found enum {}", mirror.Enums.back()->FullType());
				}
			}
			else if (current_line.starts_with(options.ClassAnnotationName))
			{
				current_access = AccessMode::Private;
				mirror.Classes.push_back(ParseClassDecl(&mirror, current_line, next_line, line_num, std::exchange(comments, {}), options));
				mirror.Classes.back()->UID = GenerateUID(path, line_num);
				if (options.Verbose)
				{
					PrintLine("Found class {}", mirror.Classes.back()->FullType());
				}
			}
			else if (current_line.starts_with(options.FieldAnnotationName))
			{
				if (mirror.Classes.size() == 0)
				{
					ReportError(path, line_num + 1, "{}() not in class", options.FieldAnnotationName);
					return false;
				}

				auto& klass = mirror.Classes.back();
				klass->Fields.push_back(ParseFieldDecl(mirror, *klass, current_line, next_line, line_num, current_access, std::exchange(comments, {}), options));
				klass->Fields.back()->UID = GenerateUID(path, line_num);
			}
			else if (current_line.starts_with(options.MethodAnnotationName))
			{
				if (mirror.Classes.size() == 0)
				{
					ReportError(path, line_num + 1, "{}() not in class", options.MethodAnnotationName);
					return false;
				}

				auto& klass = mirror.Classes.back();
				klass->Methods.push_back(ParseMethodDecl(*klass, current_line, next_line, line_num, current_access, std::exchange(comments, {}), options));
				klass->Methods.back()->UID = GenerateUID(path, line_num);
			}
			else if (current_line.starts_with(options.BodyAnnotationName))
			{
				if (mirror.Classes.size() == 0)
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

