/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include <iostream>
#include <filesystem>
#include <string_view>
#include <vector>
#include <charconv>
#include <fstream>
#include <nlohmann/json.hpp>
#include <args.hxx>
#include "../../baselib/Include/ASCII.h"
#include "../../baselib/Include/Strings.h"
#include "../../baselib/Include/EnumFlags.h"

using nlohmann::json;

enum class AccessMode { Unspecified, Public, Private, Protected };

static constexpr const char* AMStrings[] = { "Unspecified", "Public", "Private", "Protected" };

struct Declaration
{
	json Properties = json::object();
	std::string Name;
	size_t DeclarationLine = 0;
	AccessMode Access = AccessMode::Unspecified;
	std::vector<std::string> Comments;

	json ToJSON() const
	{
		json result = json::object();
		if (!Properties.empty())
			result["Properties"] = Properties;
		result["Name"] = Name;
		result["DeclarationLine"] = DeclarationLine;
		if (Access != AccessMode::Unspecified)
			result["Access"] = AMStrings[(int)Access];
		if (!Comments.empty())
			result["Comments"] = Comments;
		return result;
	}
};

struct Field : public Declaration
{
	enum class CommonFlags
	{
		NoGetter,
		NoSetter,
		NoEdit
	};
	baselib::EnumFlags<CommonFlags> Flags;
	std::string Type;
	std::string InitializingExpression;
	std::string DisplayName;

	json ToJSON() const
	{
		json result = Declaration::ToJSON();
		result["Type"] = Type;
		if (!InitializingExpression.empty())
			result["InitializingExpression"] = InitializingExpression;
		if (DisplayName != Name)
			result["DisplayName"] = DisplayName;
		#define ADDFLAG(n) if (Flags.IsSet(CommonFlags::n)) result[#n] = true
		ADDFLAG(NoGetter);
		ADDFLAG(NoSetter);
		ADDFLAG(NoEdit);
		#undef ADDFLAG
		return result;
	}
};

enum class MethodFlags
{
	Inline,
	Virtual,
	Static,
	Const,
	Noexcept,
	Final,
	Explicit,
	Artificial,
	HasBody,
	NoCallable
};

struct Method : public Declaration
{
	std::string Type;
	baselib::EnumFlags<MethodFlags> Flags;
	std::string Parameters;
	std::string Body;
	size_t SourceFieldDeclarationLine = 0;

	json ToJSON() const
	{
		json result = Declaration::ToJSON();
		result["Type"] = Type;
		result["Parameters"] = Parameters;
		if (!Body.empty())
			result["Body"] = Body;
		if (SourceFieldDeclarationLine != 0)
			result["SourceFieldDeclarationLine"] = SourceFieldDeclarationLine;
		#define ADDFLAG(n) if (Flags.IsSet(MethodFlags::n)) result[#n] = true
		ADDFLAG(Inline);
		ADDFLAG(Virtual);
		ADDFLAG(Static);
		ADDFLAG(Const);
		ADDFLAG(Noexcept);
		ADDFLAG(Final);
		ADDFLAG(Explicit);
		ADDFLAG(Artificial);
		ADDFLAG(HasBody);
		ADDFLAG(NoCallable);
		#undef ADDFLAG
		return result;
	}
};

enum class ClassFlags
{
	Struct,
	NoConstructors
};

struct Class : public Declaration
{
	std::string ParentClass;

	std::vector<Field> Fields;
	std::vector<Method> Methods;

	baselib::EnumFlags<ClassFlags> Flags;

	size_t BodyLine = 0;

	json ToJSON() const
	{
		auto result = Declaration::ToJSON();
		if (!ParentClass.empty())
			result["ParentClass"] = ParentClass;

		if (Flags.IsSet(ClassFlags::Struct))
			result["Struct"] = true;
		if (Flags.IsSet(ClassFlags::NoConstructors))
			result["NoConstructors"] = true;

		if (!Fields.empty())
		{
			auto& fields = result["Fields"] = json::object();
			for (auto& field : Fields)
			{
				fields[field.Name] = field.ToJSON();
			}
		}
		if (!Methods.empty())
		{
			auto& methods = result["Methods"] = json::object();
			for (auto& method : Methods)
			{
				methods[method.Name] = method.ToJSON();
			}
		}

		result["BodyLine"] = BodyLine;

		return result;
	}
};

struct Enumerator
{
	std::string Name;
	int64_t Value = 0;
};

struct Enum : public Declaration
{
	std::vector<Enumerator> Enumerators;

	json ToJSON() const
	{
		json result = Declaration::ToJSON();
		auto& enumerators = result["Enumerators"] = json::object();
		for (auto& enumerator : Enumerators)
			enumerators[enumerator.Name] = enumerator.Value;
		return result;
	}
};

struct FileMirror
{
	std::filesystem::path SourceFilePath;
	std::vector<Class> Classes;
	std::vector<Enum> Enums;

	json ToJSON() const
	{
		json result = json::object();
		result["SourceFilePath"] = SourceFilePath.string();
		auto& classes = result["Classes"] = json::object();
		for (auto& klass : Classes)
			classes[klass.Name] = klass.ToJSON();
		auto& enums = result["Enums"] = json::object();
		for (auto& enum_ : Enums)
			enums[enum_.Name] = enum_.ToJSON();
		return result;
	}
};

struct Options
{
	bool Recursive = false;
	bool Quiet = false;
	bool Force = false;
	bool Verbose = false;
	bool UseJSON = true;

	std::string_view EnumPrefix = "REnum";
	std::string_view ClassPrefix = "RClass";
	std::string_view FieldPrefix = "RField";
	std::string_view MethodPrefix = "RMethod";
	std::string_view BodyPrefix = "RBody";
	std::string_view MacroPrefix = "REFLECT";
};

using baselib::string_view;

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

inline std::string OnlyType(std::string str)
{
	auto last = str.find_last_of(':');
	if (last != std::string::npos)
		str = str.substr(last + 1);
	return str;
}

template <typename... ARGS>
void ReportError(std::filesystem::path path, size_t line_num, ARGS&&... args)
{
	std::cerr <<  path.string() << "(" << line_num << ",1): error: ";
	((std::cerr << std::forward<ARGS>(args)), ...);
	std::cerr << "\n";
}

json ParsePropertyList(string_view line)
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
	henum.Properties = ParsePropertyList(line);

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
		auto name_start = enumerator_line.begin();
		auto name_end = std::find_if_not(enumerator_line.begin(), enumerator_line.end(), baselib::isident);

		auto rest = TrimWhitespace(string_view{name_end, enumerator_line.end() });
		if (Consume(rest, '='))
		{
			rest = TrimWhitespace(rest);
			std::from_chars(rest.begin(), rest.end(), enumerator_value);
			/// TODO: Non-integer enumerator values (like 1<<5 and constexpr function calls/expression)
		}

		auto name = string_view{ name_start, name_end };
		if (!name.empty())
		{
			henum.Enumerators.push_back({ (std::string)TrimWhitespace(name), enumerator_value });
			enumerator_value++;
		}
		line_num++;
	}

	return henum;
}

std::pair<std::string, std::string> ParseClassDecl(string_view line)
{
	std::pair<std::string, std::string> result;

	if (line.starts_with("struct"))
		line = Expect(line, "struct");
	else
		line = Expect(line, "class");
	result.first = ParseIdentifier(line);
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

		result.second = { start.begin(), line.begin() };
	}

	return result;
}

std::tuple<std::string, std::string, std::string> ParseFieldDecl(string_view line)
{
	std::tuple<std::string, std::string, std::string> result;

	line = TrimWhitespace(line);

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
	field.Properties = ParsePropertyList(line);
	field.DeclarationLine = line_num;
	field.Comments = std::move(comments);
	auto decl = ParseFieldDecl(next_line);
	std::tie(field.Type, field.Name, field.InitializingExpression) = decl;
	if (field.Name.size() > 1 && field.Name[0] == 'm' && isupper(field.Name[1]))
		field.DisplayName.assign(field.Name.begin() + 1, field.Name.end());
	else
		field.DisplayName = field.Name;

	/// Disable if explictly stated
	if (field.Properties.value("Getter", true) == false)
		field.Flags.Set(Field::CommonFlags::NoGetter);
	if (field.Properties.value("Setter", true) == false)
		field.Flags.Set(Field::CommonFlags::NoSetter);
	if (field.Properties.value("Editor", true) == false || field.Properties.value("Edit", true) == false)
		field.Flags.Set(Field::CommonFlags::NoEdit);

	/// Private implies Getter = false, Setter = false, Editor = false
	if (field.Properties.value("Private", false))
		field.Flags.Set(Field::CommonFlags::NoEdit, Field::CommonFlags::NoSetter, Field::CommonFlags::NoGetter);

	/// ParentPointer implies Editor = false, Setter = false
	if (field.Properties.value("ParentPointer", false))
		field.Flags.Set(Field::CommonFlags::NoEdit, Field::CommonFlags::NoSetter);

	/// ChildVector implies Setter = false
	auto type = string_view{ field.Type };
	if (type.starts_with("ChildVector<"))
		field.Flags.Set(Field::CommonFlags::NoSetter);

	/// Enable if explictly stated
	if (field.Properties.value("Getter", false) == true)
		field.Flags.Unset(Field::CommonFlags::NoGetter);
	if (field.Properties.value("Setter", false) == true)
		field.Flags.Unset(Field::CommonFlags::NoSetter);
	if (field.Properties.value("Editor", false) == true || field.Properties.value("Edit", false) == true)
		field.Flags.Unset(Field::CommonFlags::NoEdit);

	if (!klass.Flags.IsSet(ClassFlags::Struct))
	{
		/// TODO:
		///CreateArtificialMethods(mirror, field, klass, std::move(comments));
	}

	return field;
}

void ParseMethodDecl(string_view line, Method& method)
{
	while (!line.starts_with("auto"))
	{
		if (SwallowOptional(line, "virtual")) method.Flags += MethodFlags::Virtual;
		else if (SwallowOptional(line, "static")) method.Flags += MethodFlags::Static;
		else if (SwallowOptional(line, "inline")) method.Flags += MethodFlags::Inline;
		else if (SwallowOptional(line, "explicit")) method.Flags += MethodFlags::Explicit;
		else throw std::exception{ "Method declaration does not start with 'auto'" };
	}
	line = Expect(line, "auto");

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

	while (!line.starts_with("->"))
	{
		if (SwallowOptional(line, "const")) method.Flags += MethodFlags::Const;
		else if (SwallowOptional(line, "final")) method.Flags += MethodFlags::Final;
		else if (SwallowOptional(line, "noexcept")) method.Flags += MethodFlags::Noexcept;
		else throw std::exception{ "Method does not have -> result" };
	}
	line = Expect(line, "->");
	auto end_line = line.find_first_of("{;");
	if (end_line != std::string::npos)
		line = line.substr(0, end_line);
	method.Type = (std::string)line;
}

Method ParseMethodDecl(Class& klass, string_view line, string_view next_line, size_t line_num, AccessMode mode, std::vector<std::string> comments, Options const& options)
{
	Method method;
	line.remove_prefix(options.MethodPrefix.size());
	method.Access = mode;
	method.Properties = ParsePropertyList(line);
	method.DeclarationLine = line_num;
	ParseMethodDecl(next_line, method);

	method.Comments = std::move(comments);

	return method;
}

Class ParseClassDecl(string_view line, string_view next_line, size_t line_num, std::vector<std::string> comments, Options const& options)
{
	Class klass;
	line.remove_prefix(options.ClassPrefix.size());
	klass.Properties = ParsePropertyList(line);
	klass.DeclarationLine = line_num;
	auto header = ParseClassDecl(next_line);
	klass.Name = header.first;
	klass.ParentClass = header.second;
	klass.Comments = std::move(comments);
	if (klass.ParentClass.empty())
		klass.Flags += ClassFlags::Struct;

	if (klass.Flags.IsSet(ClassFlags::Struct) || klass.Properties.value("Abstract", false) == true || klass.Properties.value("Singleton", false) == true)
		klass.Flags += ClassFlags::NoConstructors;

	return klass;
}

uint64_t ChangeTime = 0;
std::vector<FileMirror> Mirrors;

bool ParseClassFile(std::filesystem::path path, Options const& options)
{
	path = path.lexically_normal();

	if (options.Verbose)
		std::cout << "Analyzing file " << path << "\n";

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
					std::cout << "Found class " << mirror.Classes.back().Name << "\n";
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
		Mirrors.push_back(std::move(mirror));

	return true;
}

#define TIMESTAMP_TEXT "/// TIMESTAMP: "

uint64_t FileNeedsUpdating(const std::filesystem::path& target_path, const std::filesystem::path& source_path, const Options& opts)
{
	auto stat = std::filesystem::status(target_path);
	uint64_t file_change_time = std::max(ChangeTime, (uint64_t)std::filesystem::last_write_time(source_path).time_since_epoch().count());
	if (stat.type() != std::filesystem::file_type::not_found)
	{
		/// Open file and get first line
		std::ifstream f(target_path);
		std::string line;
		std::getline(f, line);

		uint64_t stored_change_time = 0;
		const auto time = string_view{ string_view{ line }.substr(sizeof(TIMESTAMP_TEXT) - 1) };
		const auto result = std::from_chars(time.begin(), time.end(), stored_change_time);
		if (!opts.Force && result.ec == std::errc{} && file_change_time == stored_change_time)
			return 0;
	}

	return file_change_time;
}

struct FileWriter
{
	std::ofstream mOutFile;
	std::filesystem::path mPath;
	size_t CurrentIndent = 0;
	bool InDefine = false;

	struct Indenter
	{
		FileWriter& out;
		Indenter(FileWriter& out_) : out(out_) { out.CurrentIndent++; }
		~Indenter() { out.CurrentIndent--; }
	};

	Indenter Indent() { return Indenter{ *this }; }

	FileWriter(std::filesystem::path path) : mOutFile{ path }, mPath(path) {}

	template <typename... ARGS>
	void WriteLine(ARGS&& ... args)
	{
		mOutFile << std::string(CurrentIndent, '\t');
		((mOutFile << std::forward<ARGS>(args)), ...);
		if (InDefine)
			mOutFile << " \\";
		mOutFile << '\n';
	}

	void WriteJSON(json const& value)
	{
		CurrentIndent++;
		WriteLine("return R\"_REFLECT_(", value.dump(), ")_REFLECT_\";");
		CurrentIndent--;
	}

	template <typename... ARGS>
	void StartDefine(ARGS&& ... args)
	{
		InDefine = true;
		WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent++;
	}

	template <typename... ARGS>
	void EndDefine(ARGS&& ... args)
	{
		InDefine = false;
		WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent--;
	}

	void WriteLine()
	{
		mOutFile << '\n';
	}

	void Close()
	{
		mOutFile.close();
	}

	~FileWriter()
	{
		if (mOutFile.is_open())
		{
			mOutFile.close();
			std::filesystem::remove(mPath);
		}
	}
};

bool BuildCommonClassEntry(FileWriter& output, const FileMirror& mirror, const Class& klass, const Options& options)
{
	output.WriteLine("/// From class: ", klass.Name);
	output.WriteLine("#undef ", options.MacroPrefix, "_CLASS_DECL_LINE_", klass.DeclarationLine);
	output.WriteLine("#define ", options.MacroPrefix, "_CLASS_DECL_LINE_", klass.DeclarationLine);

	/// [Note] we are a struct, all methods are artificial
	output.WriteLine("/// Methods");
	for (auto& func : klass.Methods)
	{
		if (func.DeclarationLine != 0)
		{
			output.WriteLine("#undef ", options.MacroPrefix, "_METHOD_DECL_LINE_", func.DeclarationLine);
			output.WriteLine("#define ", options.MacroPrefix, "_METHOD_DECL_LINE_", func.DeclarationLine);
		}
	}

	output.WriteLine("/// Fields");
	for (auto& field : klass.Fields)
	{
		output.WriteLine("#undef ", options.MacroPrefix, "_FIELD_DECL_LINE_", field.DeclarationLine);
		output.WriteLine("#define ", options.MacroPrefix, "_FIELD_DECL_LINE_", field.DeclarationLine);
	}

	/// Visitor macros
	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", klass.Name, "_FIELDS(", options.MacroPrefix, "_VISITOR)");
	//for (auto& field : klass.Fields)
	for (size_t i=0; i<klass.Fields.size(); i++)
	{
		const auto& field = klass.Fields[i];
		output.WriteLine("", options.MacroPrefix, "_VISITOR(&", klass.Name, "::StaticGetReflectionData().Fields[", i, "], &", klass.Name, "::", field.Name, ", ",
			(field.Flags.IsSet(Field::CommonFlags::NoEdit) ? "std::false_type{}" : "std::true_type{}"),
			");");
	}
	output.EndDefine("");

	/// [Note] we are a struct, all methods are NoCallable
	output.StartDefine("#define ", options.MacroPrefix, "_VISIT_", klass.Name, "_METHODS(", options.MacroPrefix, "_VISITOR)");
	for (size_t i = 0; i < klass.Methods.size(); i++)
	{
		const auto& method = klass.Methods[i];
		if (!method.Flags.IsSet(MethodFlags::NoCallable))
			output.WriteLine("", options.MacroPrefix, "_VISITOR(&", klass.Name, "::StaticGetReflectionData().Methods[", i, "], &", klass.Name, "::", method.Name, ", ", "&", klass.Name, "::ScriptFunction_", method.Name, ");");
	}
	output.EndDefine("");

	output.WriteLine("#undef ", options.MacroPrefix, "_GENERATED_CLASS_BODY_", klass.BodyLine);
	output.StartDefine("#define ", options.MacroPrefix, "_GENERATED_CLASS_BODY_", klass.BodyLine);

	/// All these are public
	output.CurrentIndent--;
	output.WriteLine("public:");
	output.CurrentIndent++;

	/// Typedefs
	output.WriteLine("typedef ", klass.Name, " self_type;");
	if (!klass.ParentClass.empty())
	{
		output.WriteLine("typedef ", klass.ParentClass, " parent_type;");
		output.WriteLine("using parent_type::parent_type;");

		/// Constructors
		if (!klass.Flags.IsSet(ClassFlags::NoConstructors))
		{
			output.WriteLine("static self_type* Construct(){ return new self_type{StaticGetReflectionData()}; }");
			//output.WriteLine("static self_type* Construct(const ::Reflector::ClassReflectionData& klass){ return new self_type{klass}; }");
			output.WriteLine(klass.Name, "(::Reflector::ClassReflectionData const& klass) : ", klass.ParentClass, "(klass) {}");
		}
	}

	/// Reflection Data Method:

	output.WriteLine("static ::Reflector::ClassReflectionData const& StaticGetReflectionData() {");
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::ClassReflectionData _data = {");
	output.CurrentIndent++;
	output.WriteLine(".Name = \"", klass.Name, "\",");
	output.WriteLine(".ParentClassName = \"", OnlyType(klass.ParentClass), "\",");

	if (!klass.Properties.empty())
	{
		output.WriteLine(".Properties = R\"_REFLECT_(", klass.Properties.dump(), ")_REFLECT_\",");
		if (options.UseJSON)
			output.WriteLine(".PropertiesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", klass.Properties.dump(), ")_REFLECT_\"),");
	}
	if (!klass.Flags.IsSet(ClassFlags::NoConstructors))
		output.WriteLine(".Constructor = +[](const ::Reflector::ClassReflectionData& klass){ return (void*)new self_type{klass}; },");

	/// Fields
	output.WriteLine(".Fields = {");
	output.CurrentIndent++;
	for (auto& field : klass.Fields)
	{
		output.WriteLine("::Reflector::FieldReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", field.Name, "\",");
		output.WriteLine(".FieldType = \"", field.Type, "\",");
		if (!field.InitializingExpression.empty())
			output.WriteLine(".Initializer = \"", field.InitializingExpression, "\",");
		if (!field.Properties.empty())
		{
			output.WriteLine(".Properties = R\"_REFLECT_(", field.Properties.dump(), ")_REFLECT_\",");
			if (options.UseJSON)
				output.WriteLine(".PropertiesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", field.Properties.dump(), ")_REFLECT_\"),");
		}
		output.WriteLine(".FieldTypeIndex = typeid(", field.Type, ")");
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");

	/// Methods
	output.WriteLine(".Methods = {");
	output.CurrentIndent++;
	for (auto& method : klass.Methods)
	{
		output.WriteLine("::Reflector::MethodReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", method.Name, "\",");
		output.WriteLine(".ReturnType = \"", method.Type, "\",");
		if (!method.Parameters.empty())
			output.WriteLine(".Parameters = \"", method.Parameters, "\",");
		if (!method.Properties.empty())
		{
			output.WriteLine(".Properties = R\"_REFLECT_(", method.Properties.dump(), ")_REFLECT_\",");
			if (options.UseJSON)
				output.WriteLine(".PropertiesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", method.Properties.dump(), ")_REFLECT_\"),");
		}
		output.WriteLine(".ReturnTypeIndex = typeid(", method.Type, "),");
		output.WriteLine(".ParentClass = &_data");
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");

	/// Rest
	output.WriteLine(".TypeIndex = typeid(self_type)");
	output.CurrentIndent--;
	output.WriteLine("}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}");

	if (!klass.ParentClass.empty())
	{
		output.WriteLine("virtual ::Reflector::ClassReflectionData const& GetReflectionData() const override { return StaticGetReflectionData(); }");
	}

	/// - StaticVisitMethods
	output.WriteLine("template <typename VISITOR> static void StaticVisitMethods(VISITOR&& visitor) {");
	output.WriteLine("\t", options.MacroPrefix, "_VISIT_", klass.Name, "_METHODS(visitor);");
	output.WriteLine("}");
	/// - StaticVisitFields
	output.WriteLine("template <typename VISITOR> static void StaticVisitFields(VISITOR&& visitor) {");
	output.WriteLine("\t", options.MacroPrefix, "_VISIT_", klass.Name, "_FIELDS(visitor);");
	output.WriteLine("}");

	return true;
}

bool BuildClassEntry(FileWriter& output, const FileMirror& mirror, const Class& klass, const Options& options)
{
	if (!BuildCommonClassEntry(output, mirror, klass, options))
		return false;

	for (auto& func : klass.Methods)
	{
		/// Callables for all methods
		if (!func.Flags.IsSet(MethodFlags::NoCallable))
			output.WriteLine("", options.MacroPrefix, "_CALLABLE((", func.Type, "), ", func.Name, ", (", func.Parameters, "))");
		if (func.Flags.IsSet(MethodFlags::Artificial))
		{
			output.WriteLine(func.Type, " ", func.Name, "(", func.Parameters, ") { ", func.Body, " }");
		}
	}

	/// Back to public
	output.EndDefine("public:");

	return true;
}

bool BuildStructEntry(FileWriter& output, const FileMirror& mirror, const Class& klass, const Options& options)
{
	if (!BuildCommonClassEntry(output, mirror, klass, options))
		return false;

	/// - StaticGetConstructor
	output.WriteLine("static std::function<void*(const ::", options.MacroPrefix, "::Framework::RClass*)> StaticGetConstructor() noexcept {");
	output.WriteLine("\treturn [](const ::", options.MacroPrefix, "::Framework::RClass* klass){ return (void*)new self_type; };");
	output.WriteLine("}");

	/// - Push
	output.WriteLine("template <typename SS> void Push(SS* sys) const {");
	{
		auto indent = output.Indent();
		output.WriteLine("sys->StartObject();");
		for (auto& field : klass.Fields)
		{
			if (field.Properties.value("Script", true) == false)
				continue;
			output.WriteLine("sys->Push(this->", field.Name, ");");
			output.WriteLine("sys->SetField(\"", field.Name, "\");");
		}
	}
	output.WriteLine("}");

	/// - GetAs
	output.WriteLine("template <typename SS> void GetAs(SS* sys, size_t index) {");
	{
		auto indent = output.Indent();
		for (auto& field : klass.Fields)
		{
			if (field.Properties.value("Script", true) == false)
				continue;
			output.WriteLine("sys->GetField(index, \"", field.Name, "\");");
			output.WriteLine("sys->GetAs(sys->GetTop(), this->", field.Name, ");");
			output.WriteLine("sys->Pop();");
		}
	}
	output.WriteLine("}");

	/// Field parameter getters
	for (auto& field : klass.Fields)
	{
		output.WriteLine("static const char* GetFieldReflectionData_", field.DisplayName, "() noexcept {");
		output.WriteJSON(field.Properties);
		output.WriteLine("}");
		if (options.UseJSON)
			output.WriteLine("static ::nlohmann::json const& GetFieldPropertiesJSON_", field.DisplayName, "() { static const auto _json = ::nlohmann::json::parse(GetFieldProperties_", field.DisplayName, "()); return _json; }");
	}

	output.EndDefine("");

	return true;
}

bool BuildEnumEntry(FileWriter& output, const FileMirror& mirror, const Enum& henum, const Options& options)
{
	output.WriteLine("/// From enum: ", henum.Name);
	output.WriteLine("#undef ", options.MacroPrefix, "_ENUM_", henum.DeclarationLine);
	output.StartDefine("#define ", options.MacroPrefix, "_ENUM_", henum.DeclarationLine);

	output.WriteLine("enum class ", henum.Name, ";"); /// forward decl;


	output.WriteLine("inline ::Reflector::EnumReflectionData const& StaticGetReflectionData(", henum.Name, ") {");
	output.CurrentIndent++;
	output.WriteLine("static const ::Reflector::EnumReflectionData _data = {");
	output.CurrentIndent++;

	output.WriteLine(".Name = \"", henum.Name, "\",");
	if (!henum.Properties.empty())
	{
		output.WriteLine(".Properties = R\"_REFLECT_(", henum.Properties.dump(), ")_REFLECT_\",");
		if (options.UseJSON)
			output.WriteLine(".PropertiesJSON = ::nlohmann::json::parse(R\"_REFLECT_(", henum.Properties.dump(), ")_REFLECT_\"),");
	}
	output.WriteLine(".Enumerators = {");
	output.CurrentIndent++;
	for (auto& enumerator : henum.Enumerators)
	{
		output.WriteLine("::Reflector::EnumeratorReflectionData {");
		output.CurrentIndent++;
		output.WriteLine(".Name = \"", enumerator.Name, "\",");
		output.WriteLine(".Value = ", enumerator.Value);
		output.CurrentIndent--;
		output.WriteLine("},");
	}
	output.CurrentIndent--;
	output.WriteLine("},");
	output.WriteLine(".TypeIndex = typeid(", henum.Name, ")");
	output.CurrentIndent--;
	output.WriteLine("}; return _data;");
	output.CurrentIndent--;
	output.WriteLine("}");

	output.WriteLine("inline const char* GetEnumName(", henum.Name, ") { return \"", henum.Name, "\"; }");
	output.WriteLine("inline const char* GetEnumeratorName(", henum.Name, " v) {");
	{
		auto indent = output.Indent();

		output.WriteLine("switch (int64_t(v)) {");
		for (auto& enumerator : henum.Enumerators)
		{
			output.WriteLine("case ", enumerator.Value, ": return \"", enumerator.Name, "\";");
		}
		output.WriteLine("default: return \"<Unknown>\";");
		output.WriteLine("}");
	}
	output.WriteLine("}");
	output.WriteLine("inline std::ostream& operator<<(std::ostream& strm, ", henum.Name, " v) { strm << GetEnumeratorName(v); return strm; }");

	output.EndDefine();

	return true;
}

int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();

	args::ArgumentParser parser{ "Reflector Tool" };
	parser.helpParams.addChoices = true;
	parser.helpParams.addDefault = true;
	parser.helpParams.width = 160;
	parser.helpParams.helpindent = 80;
	args::Group command_group{ parser, "Commands", args::Group::Validators::DontCare, args::Options::Required };

	args::HelpFlag h(parser, "help", "Show help", { 'h', "help" });
	args::Flag recursive{ parser, "recursive", "Recursively search the provided directories for files", {'r', "recursive"}, args::Options::Single };
	args::Flag quiet{ parser, "quiet", "Don't print out created file names",{ 'q', "quiet" }, args::Options::Single };
	args::Flag force{ parser, "force", "Ignore timestamps, regenerate all files", { 'f', "force" }, args::Options::Single };
	args::Flag verbose{ parser, "verbose", "Print additional information",{ 'v', "verbose" }, args::Options::Single };
	args::Flag use_json{ parser, "json", "Output code that uses nlohmann::json to store class properties", { 'j', "json" }, args::Options::Single };
	args::Flag create_db{ parser, "db", "Create a JSON database with reflection data", { 'd', "database" }, args::Options::Single };
	args::PositionalList<std::filesystem::path> paths_list{ parser, "files", "Files or directories to scan", args::Options::Required };

	try
	{
		parser.ParseCLI(argc, argv);

		Options options = { 
			.Recursive = recursive, 
			.Quiet = quiet, 
			.Force = force, 
			.Verbose = verbose,
			.UseJSON = use_json
		};

		std::vector<std::filesystem::path> final_files;
		for (auto& path : paths_list)
		{
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const std::filesystem::path& file) {
					auto u8file = file.string();
					auto full = string_view{ u8file };
					if (full.ends_with(".mirror.h")) return;

					auto ext = file.extension();
					if (!std::filesystem::is_directory(file) && (ext == ".cpp" || ext == ".hpp" || ext == ".h"))
					{
						final_files.push_back(file);
					}
				};
				if (recursive)
				{
					for (auto it = std::filesystem::recursive_directory_iterator{ std::filesystem::canonical(path) }; it != std::filesystem::recursive_directory_iterator{}; ++it)
					{
						add_files(*it);
					}
				}
				else
				{
					for (auto it = std::filesystem::directory_iterator{ std::filesystem::canonical(path) }; it != std::filesystem::directory_iterator{}; ++it)
					{
						add_files(*it);
					}
				}
			}
			else
				final_files.push_back(std::move(path));
		}

		std::cout << final_files.size() << " reflectable files found\n";

		/// Create all classes
		for (auto& file : final_files)
		{
			try
			{
				if (!ParseClassFile(std::filesystem::path(file), options))
					return -1;
			}
			catch (std::exception& e)
			{
				ReportError(file, 0, e.what());
				return -1;
			}
		}


		/// Output mirror files
		size_t modified_files = 0;

		for (auto& file : Mirrors)
		{
			auto file_path = file.SourceFilePath;
			file_path.replace_extension(".mirror.h");

			/// TOOD: Check if we actually need to update the file
			auto file_change_time = FileNeedsUpdating(file_path, file.SourceFilePath, options);
			if (file_change_time == 0) continue;

			modified_files++;

			if (!options.Quiet)
				std::cout << "Building class file " << file_path << "\n";

			FileWriter f(file_path);
			f.WriteLine(TIMESTAMP_TEXT, file_change_time);
			f.WriteLine("/// Source file: ", file.SourceFilePath);
			f.WriteLine("#pragma once");
			//f.WriteLine("#include <Reflector.h>");

			for (auto& klass : file.Classes)
			{
				const bool is_struct = klass.Flags.IsSet(ClassFlags::Struct);
				const bool build_result = is_struct ?
					BuildStructEntry(f, file, klass, options) :
					BuildClassEntry(f, file, klass, options);
				if (!build_result)
					continue;
				f.WriteLine();
			}

			for (auto& henum : file.Enums)
			{
				if (!BuildEnumEntry(f, file, henum, options))
					continue;
				f.WriteLine();
			}

			f.Close();
		}

		if (modified_files)
		{
			/// Output JSON db
			auto cwd = std::filesystem::current_path();
			std::ofstream classes_file(cwd / "Classes.reflect.h", std::ios_base::openmode{ std::ios_base::trunc });
			std::ofstream includes_file(cwd / "Includes.reflect.h", std::ios_base::openmode{ std::ios_base::trunc });

			json db;

			for (auto& mirror : Mirrors)
			{
				includes_file << "#include " << mirror.SourceFilePath << "" << std::endl;

				for (auto& klass : mirror.Classes)
				{
					if (!klass.Flags.IsSet(ClassFlags::Struct))
						classes_file << "ReflectClass(" << klass.Name << ");" << std::endl;
				}
				for (auto& henum : mirror.Enums)
					classes_file << "ReflectEnum(" << henum.Name << ");" << std::endl;

				if (create_db)
					db[mirror.SourceFilePath.string()] = mirror.ToJSON();
			}

			includes_file.close();
			classes_file.close();

			if (create_db)
			{
				std::ofstream jsondb{ cwd / "ReflectDatabase.json", std::ios_base::openmode{ std::ios_base::trunc } };
				jsondb << db.dump(1, '\t');
				jsondb.close();
			}

			if (!std::filesystem::exists(cwd / "Reflector.h") || options.Force)
			{
				FileWriter reflect_file{ cwd / "Reflector.h" };
				reflect_file.WriteLine("#pragma once");
				reflect_file.WriteLine("#include <ReflectorClasses.h>");
				reflect_file.WriteLine("#define TOKENPASTE2_IMPL(x, y) x ## y");
				reflect_file.WriteLine("#define TOKENPASTE2(x, y) TOKENPASTE2_IMPL(x, y)");
				reflect_file.WriteLine("");
				reflect_file.WriteLine("#define ", options.ClassPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_CLASS_DECL_LINE_, __LINE__)");
				reflect_file.WriteLine("#define ", options.FieldPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_FIELD_DECL_LINE_, __LINE__)");
				reflect_file.WriteLine("#define ", options.MethodPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_METHOD_DECL_LINE_, __LINE__)");
				reflect_file.WriteLine("#define ", options.BodyPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_GENERATED_CLASS_BODY_, __LINE__)");
				reflect_file.WriteLine("#define ", options.EnumPrefix, "(...) TOKENPASTE2(", options.MacroPrefix, "_ENUM_, __LINE__)");
				reflect_file.WriteLine("");
				reflect_file.WriteLine("#define ", options.MacroPrefix, "_CALLABLE(ret, name, args) static int ScriptFunction_##name(struct lua_State* thread) { return 0; }");
				reflect_file.Close();
				if (options.Verbose)
					std::cout << "Created " << cwd / "Reflector.h" << "\n";
			}
			else
			{
				if (options.Verbose)
					std::cout << cwd / "Reflector.h" << " exists, skipping\n";
			}


			if (!options.Quiet)
				std::cout << modified_files << " mirror files changed\n";
		}
		else
		{
			if (!options.Quiet)
				std::cout << "No mirror files changed\n";
		}

	}
	catch (args::Help)
	{
		std::cout << parser.Help();
	}
	catch (args::Error& e)
	{
		std::cerr << e.what() << std::endl << parser;
		return 1;
	}

	return 0;
}