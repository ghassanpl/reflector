/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Parse.h"
#include <baselib/ASCII.h>
#include <charconv>
#include <fstream>

std::string TypeFromVar(string_view str)
{
	return (std::string)baselib::TrimWhitespace({ str.begin(), std::find_if(str.rbegin(), str.rend(), [](char32_t cp) { return !(baselib::isalnum(cp) || cp == '_'); }).base() });
}

string_view Expect(string_view str, string_view value)
{
	if (!str.starts_with(value))
	{
		throw std::exception(baselib::Stringify("Expected `", value, "`").c_str());
	}
	str.remove_prefix(value.size());
	return TrimWhitespace(str);
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
	for (; p != str.end(); p++)
		if (!baselib::isident(*p)) break;
	std::string result = { str.begin(), p };
	str = { p, str.end() };
	return result;
}

std::string ParseType(string_view& str)
{
	/// TODO: Unify all type parsing from entire codebase
	int brackets = 0, tris = 0, parens = 0;
	auto p = str.begin();
	for (; p != str.end(); p++)
	{
		switch (*p)
		{
		case '[': brackets++; continue;
		case ']': brackets--; continue;
		case '(': parens++; continue;
		case ')': parens--; continue;
		case '<': tris++; continue;
		case '>': tris--; continue;
		}

		if (baselib::isblank(*p))
		{
			if (parens == 0 && tris == 0 && brackets == 0)
				break;
		}
	}

	std::string result = { str.begin(), p };
	str = { p, str.end() };
	return result;
}

std::string ParseExpression(string_view& str)
{
	int brackets = 0, tris = 0, parens = 0;
	auto p = str.begin();
	for (; p != str.end(); p++)
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
	str = { p, str.end() };
	return result;
}

/*
std::tuple<std::string, std::string> ParseParameter(string_view& str)
{
	/// TODO: Function pointers, arrays, etc.
	auto type = ParseType(str);
	auto name = ParseIdentifier(str);
	str = baselib::TrimWhitespaceLeft(str);
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
	line.remove_suffix(1); /// )
	line = TrimWhitespace(line);
	if (line.empty())
		return json::object();
	return json::parse(line);
}

string_view TrimWhitespace(std::string_view str)
{
	return baselib::TrimWhitespace(baselib::string_view{ str });
}

Enum ParseEnum(const std::vector<std::string>& lines, size_t& line_num, Options const& options)
{
	Enum henum;

	line_num--;
	henum.DeclarationLine = line_num + 1;

	auto line = TrimWhitespace(lines[line_num]);
	line.remove_prefix(options.EnumPrefix.size());
	henum.Attributes = ParseAttributeList(line);

	line_num++;
	auto header_line = TrimWhitespace(lines[line_num]);

	header_line = Expect(header_line, "enum class");
	auto name_start = header_line.begin();
	auto name_end = std::find_if_not(header_line.begin(), header_line.end(), baselib::isident);

	henum.Name = TrimWhitespace(string_view{ name_start, name_end });
	///TODO: parse base type

	line_num++;
	Expect(TrimWhitespace(lines[line_num]), "{");

	line_num++;
	int64_t enumerator_value = 0;
	while (TrimWhitespace(lines[line_num]) != "};")
	{
		auto enumerator_line = TrimWhitespace(lines[line_num]);
		if (enumerator_line.empty() || enumerator_line.starts_with("//") || enumerator_line.starts_with("/*") || enumerator_line.starts_with(options.EnumeratorPrefix))
		{
			line_num++;
			continue;
		}

		auto name_start = enumerator_line.begin();
		auto name_end = std::find_if_not(enumerator_line.begin(), enumerator_line.end(), baselib::isident);

		auto rest = TrimWhitespace(string_view{ name_end, enumerator_line.end() });
		if (Consume(rest, '='))
		{
			rest = TrimWhitespace(rest);
			std::from_chars(rest.begin(), rest.end(), enumerator_value);
			/// TODO: Non-integer enumerator values (like 1<<5 and constexpr function calls/expression)
		}

		auto name = string_view{ name_start, name_end };
		if (!name.empty())
		{
			Enumerator enumerator;
			enumerator.Name = TrimWhitespace(name);
			enumerator.Value = enumerator_value;
			enumerator.DeclarationLine = line_num;
			/// TODO: enumerator.Attributes = {};
			/// TODO: enumerator.Comments = "";
			henum.Enumerators.push_back(std::move(enumerator));
			enumerator_value++;
		}
		line_num++;
	}

	return henum;
}

auto ParseClassDecl(string_view line)
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

		int parens = 0, triangles = 0, brackets = 0;

		line = TrimWhitespace(line);
		auto start = line;
		while (!line.empty() && !line.starts_with("{"))
		{
			auto ch = *line.begin();
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

			Consume(line);
		}

		std::get<1>(result) = { start.begin(), line.begin() };
	}

	return result;
}

std::tuple<std::string, std::string, std::string> ParseFieldDecl(string_view line)
{
	std::tuple<std::string, std::string, std::string> result;

	line = TrimWhitespace(line);

	SwallowOptional(line, "mutable");

	string_view eq = "=", colon = ";";
	string_view type_and_name = "";
	auto eq_start = std::find_first_of(line.begin(), line.end(), eq.begin(), eq.end());
	auto colon_start = std::find_first_of(line.begin(), line.end(), colon.begin(), colon.end());

	if (!TrimWhitespace(string_view{ colon_start + 1, line.end() }).empty())
	{
		throw std::exception{ "Field must be only thing on line" };
	}

	if (eq_start != line.end())
	{
		type_and_name = TrimWhitespace(string_view{ line.begin(), eq_start });
		std::get<2>(result) = TrimWhitespace(string_view{ eq_start + 1, colon_start });
	}
	else
	{
		type_and_name = TrimWhitespace(string_view{ line.begin(), colon_start });
	}

	if (type_and_name.empty())
	{
		throw std::exception{ "Field() must be followed by a proper class field declaration" };
	}

	auto name_start = ++std::find_if_not(type_and_name.rbegin(), type_and_name.rend(), baselib::isident);

	std::get<0>(result) = TrimWhitespace(string_view{ type_and_name.begin(), name_start.base() });
	std::get<1>(result) = TrimWhitespace(string_view{ name_start.base(), type_and_name.end() });

	return result;
}

Field ParseFieldDecl(const FileMirror& mirror, Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	Field field;
	line.remove_prefix(options.FieldPrefix.size());
	field.Access = mode;
	field.Attributes = ParseAttributeList(line);
	field.DeclarationLine = line_num;
	field.Comments = std::move(comments);
	auto decl = ParseFieldDecl(next_line);
	std::tie(field.Type, field.Name, field.InitializingExpression) = decl;
	if (field.Name.size() > 1 && field.Name[0] == 'm' && isupper(field.Name[1]))
		field.DisplayName.assign(field.Name.begin() + 1, field.Name.end());
	else
		field.DisplayName = field.Name;

	/// Disable if explictly stated
	if (field.Attributes.value("Getter", true) == false)
		field.Flags.set(Reflector::FieldFlags:: NoGetter);
	if (field.Attributes.value("Setter", true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSetter);
	if (field.Attributes.value("Editor", true) == false || field.Attributes.value("Edit", true) == false)
		field.Flags.set(Reflector::FieldFlags::NoEdit);
	if (field.Attributes.value("Save", true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSave);
	if (field.Attributes.value("Load", true) == false)
		field.Flags.set(Reflector::FieldFlags::NoLoad);

	/// Serialize = false implies Save = false, Load = false
	if (field.Attributes.value("Serialize", true) == false)
		field.Flags.set(Reflector::FieldFlags::NoSave, Reflector::FieldFlags::NoLoad);

	/// Private implies Getter = false, Setter = false, Editor = false
	if (field.Attributes.value("Private", false))
		field.Flags.set(Reflector::FieldFlags::NoEdit, Reflector::FieldFlags::NoSetter, Reflector::FieldFlags::NoGetter);

	/// ParentPointer implies Editor = false, Setter = false
	if (field.Attributes.value("ParentPointer", false))
		field.Flags.set(Reflector::FieldFlags::NoEdit, Reflector::FieldFlags::NoSetter);

	/// ChildVector implies Setter = false
	auto type = string_view{ field.Type };
	if (type.starts_with("ChildVector<"))
		field.Flags.set(Reflector::FieldFlags::NoSetter);

	/// Enable if explictly stated
	if (field.Attributes.value("Getter", false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoGetter);
	if (field.Attributes.value("Setter", false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoSetter);
	if (field.Attributes.value("Editor", false) == true || field.Attributes.value("Edit", false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoEdit);
	if (field.Attributes.value("Save", false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoSave);
	if (field.Attributes.value("Load", false) == true)
		field.Flags.unset(Reflector::FieldFlags::NoLoad);

	return field;
}

void ParseMethodDecl(string_view line, Method& method)
{
	/*
	while (!line.starts_with("auto"))
	{
		if (SwallowOptional(line, "virtual")) method.Flags += MethodFlags::Virtual;
		else if (SwallowOptional(line, "static")) method.Flags += MethodFlags::Static;
		else if (SwallowOptional(line, "inline")) method.Flags += MethodFlags::Inline;
		else if (SwallowOptional(line, "explicit")) method.Flags += MethodFlags::Explicit;
		else throw std::exception{ "Method declaration does not start with 'auto'" };
	}
	line = Expect(line, "auto");
	*/
	while (true)
	{
		if (SwallowOptional(line, "virtual")) method.Flags += Reflector::MethodFlags::Virtual;
		else if (SwallowOptional(line, "static")) method.Flags += Reflector::MethodFlags::Static;
		else if (SwallowOptional(line, "inline")) method.Flags += Reflector::MethodFlags::Inline;
		else if (SwallowOptional(line, "explicit")) method.Flags += Reflector::MethodFlags::Explicit;
		else break;
	}
	//line = Expect(line, "auto");
	auto pre_typestr = ParseType(line);
	auto pre_type = (std::string)TrimWhitespace(pre_typestr);
	line = TrimWhitespace(line);

	auto name_start = line.begin();
	auto name_end = std::find_if_not(line.begin(), line.end(), baselib::isident);
	method.Name = TrimWhitespace(string_view{ name_start, name_end });
	int num_pars = 0;
	auto start_args = name_end;
	line = { name_end, line.end() };
	do
	{
		if (line.starts_with("("))
			num_pars++;
		else if (line.starts_with(")"))
			num_pars--;
		line.remove_prefix(1);
	} while (num_pars);
	method.Parameters = string_view{ start_args + 1, line.begin() - 1 };

	line = TrimWhitespace(line);

	while (true)
	{
		if (SwallowOptional(line, "const")) method.Flags += Reflector::MethodFlags::Const;
		else if (SwallowOptional(line, "final")) method.Flags += Reflector::MethodFlags::Final;
		else if (SwallowOptional(line, "noexcept")) method.Flags += Reflector::MethodFlags::Noexcept;
		else break;
	}

	if (pre_type == "auto")
	{
		line = Expect(line, "->");

		auto end_line = line.find_first_of("{;");
		if (end_line != std::string::npos)
			line = TrimWhitespace(line.substr(0, end_line));
		if (line.ends_with("override"))
			line.remove_suffix(sizeof("override") - 1);
		method.Type = (std::string)TrimWhitespace(line);
	}
	else
	{
		method.Type = pre_type;
	}
}

auto SplitArgs(string_view argstring)
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
		case ',': if (parens == 0 && tris == 0 && brackets == 0) { args.push_back({ begin, argstring.begin() }); argstring.remove_prefix(1); begin = argstring.begin(); } break;
		}

		argstring.remove_prefix(1);

		if (argstring.empty())
		{
			args.push_back({ begin, argstring.begin() });
			break;
		}
	}

	return args;
}

Method ParseMethodDecl(Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	Method method;
	line.remove_prefix(options.MethodPrefix.size());
	method.Access = mode;
	method.Attributes = ParseAttributeList(line);
	method.DeclarationLine = line_num;
	ParseMethodDecl(next_line, method);

	if (auto getter = method.Attributes.find("UniqueName"); getter != method.Attributes.end())
		method.UniqueName = getter->get<std::string>();

	if (auto getter = method.Attributes.find("GetterFor"); getter != method.Attributes.end())
	{
		auto& property = klass.Properties[getter.value()];
		if (!property.GetterName.empty())
			throw std::exception(baselib::Stringify("Getter for this property already declared at line ", property.GetterLine).c_str());
		property.GetterName = method.Name;
		property.GetterLine = line_num;
		/// TODO: Match getter/setter types
		property.Type = method.Type;
		if (property.Name.empty()) property.Name = getter.value();
	}

	if (auto setter = method.Attributes.find("SetterFor"); setter != method.Attributes.end())
	{
		auto& property = klass.Properties[setter.value()];
		if (!property.SetterName.empty())
			throw std::exception(baselib::Stringify("Setter for this property already declared at line ", property.SetterLine).c_str());
		property.SetterName = method.Name;
		property.SetterLine = line_num;
		/// TODO: Match getter/setter types
		if (property.Type.empty())
		{
			auto args = SplitArgs(string_view{ method.Parameters });
			if (args.empty())
				throw std::exception(baselib::Stringify("Setter must have at least 1 argument").c_str());
			property.Type = TypeFromVar(args[0]);
		}
		if (property.Name.empty()) property.Name = setter.value();
	}

	method.Comments = std::move(comments);

	return method;
}

Class ParseClassDecl(string_view line, string_view next_line, size_t line_num, std::vector<std::string> comments, Options const& options)
{
	Class klass;
	line.remove_prefix(options.ClassPrefix.size());
	klass.Attributes = ParseAttributeList(line);
	klass.DeclarationLine = line_num;
	auto [name, parent, is_struct] = ParseClassDecl(next_line);
	klass.Name = name;
	klass.ParentClass = parent;
	klass.Comments = std::move(comments);
	if (klass.ParentClass.empty())
		klass.Flags += ClassFlags::Struct;
	if (is_struct)
		klass.Flags += ClassFlags::DeclaredStruct;

	if (klass.Flags.is_set(ClassFlags::Struct) || klass.Attributes.value("Abstract", false) == true || klass.Attributes.value("Singleton", false) == true)
		klass.Flags += ClassFlags::NoConstructors;

	return klass;
}

bool ParseClassFile(std::filesystem::path path, Options const& options)
{
	path = path.lexically_normal();

	if (options.Verbose)
		PrintLine("Analyzing file ", path.string());

	std::vector<std::string> lines;
	std::string line;
	std::ifstream infile{ path };
	while (std::getline(infile, line))
		lines.push_back(std::move(line));
	infile.close();

	FileMirror mirror;
	mirror.SourceFilePath = std::filesystem::absolute(path);

	AccessMode current_access = AccessMode::Unspecified;

	std::vector<std::string> comments;

	for (size_t line_num = 1; line_num < lines.size(); line_num++)
	{
		auto line = TrimWhitespace(lines[line_num - 1]);
		auto next_line = TrimWhitespace(lines[line_num]);

		try
		{
			if (line.starts_with("public:"))
				current_access = AccessMode::Public;
			else if (line.starts_with("protected:"))
				current_access = AccessMode::Protected;
			else if (line.starts_with("private:"))
				current_access = AccessMode::Private;
			else if (line.starts_with(options.EnumPrefix))
			{
				mirror.Enums.push_back(ParseEnum(lines, line_num, options));
				mirror.Enums.back().Comments = std::move(comments);
			}
			else if (line.starts_with(options.ClassPrefix))
			{
				current_access = AccessMode::Private;
				mirror.Classes.push_back(ParseClassDecl(line, next_line, line_num, std::move(comments), options));
				if (options.Verbose)
				{
					PrintLine("Found class ", mirror.Classes.back().Name);
				}
			}
			else if (line.starts_with(options.FieldPrefix))
			{
				if (mirror.Classes.size() == 0)
				{
					ReportError(path, line_num + 1, options.FieldPrefix, "() not in class");
					return false;
				}

				auto& klass = mirror.Classes.back();
				klass.Fields.push_back(ParseFieldDecl(mirror, klass, line, next_line, line_num, current_access, std::move(comments), options));
			}
			else if (line.starts_with(options.MethodPrefix))
			{
				if (mirror.Classes.size() == 0)
				{
					ReportError(path, line_num + 1, options.MethodPrefix, "() not in class");
					return false;
				}

				auto& klass = mirror.Classes.back();
				klass.Methods.push_back(ParseMethodDecl(klass, line, next_line, line_num, current_access, std::move(comments), options));
			}
			else if (line.starts_with(options.BodyPrefix))
			{
				if (mirror.Classes.size() == 0)
				{
					ReportError(path, line_num + 1, options.BodyPrefix, "() not in class");
					return false;
				}

				current_access = AccessMode::Public;

				mirror.Classes.back().BodyLine = line_num;
			}

			if (line.starts_with("///"))
			{
				comments.push_back((std::string)TrimWhitespace(line.substr(3)));
			}
			else
				comments.clear();
		}
		catch (std::exception& e)
		{
			ReportError(path, line_num + 1, e.what());
			return false;
		}
	}

	if (mirror.Classes.size() > 0 || mirror.Enums.size() > 0)
		AddMirror(std::move(mirror));

	return true;
}

