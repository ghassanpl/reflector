/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"

#include <boost/icl/interval_set.hpp>

Enum const* FindEnum(string_view name);
std::vector<Class const*> FindClasses(string_view name);
std::vector<TypeDeclaration const*> FindTypes(string_view name);

enum class DeclarationType
{
	Field,
	Method,
	Property,
	Class,
	Enum,
	Enumerator,
	Namespace,
	Parameter,
	ReturnType,

	/*
	* Constant,
	* Event,
	* Interface,
	* Operator,
	*/
};

std::string IconFor(DeclarationType type);

/// TODO: How much of the data in these classes should we output into the reflection system (*ReflectionData classes, etc)?
/// In particular: Should we output doc note and comments?

struct DocNote
{
	std::string Header;
	std::string Contents;
	bool ShowInMemberList = false;
	std::string Icon;
};

void to_json(json& j, DocNote const& p);

struct SimpleDeclaration
{
	std::string Name;
	std::vector<std::string> Comments;

	void ForEachCommentDirective(std::string_view directive_name, std::function<void(std::span<const std::string>)> callback) const;

	auto NonDirectiveCommentLines() const
	{
		return Comments | std::ranges::views::filter([](std::string_view s) { return !trimmed_whitespace_left(s).starts_with('@'); });
	}

	std::optional<bool> ForceDocument;
	std::optional<std::string> Deprecation;

	virtual bool Document() const;

	std::vector<DocNote> DocNotes;

	enum_flags<EntityFlags> DeclarationFlags;

	bool Unimplemented() const { return DeclarationFlags.is_set(EntityFlags::Unimplemented); }

	template <typename... ARGS>
	DocNote& AddDocNote(std::string header, std::string_view str, ARGS&& ... args)
	{
		return DocNotes.emplace_back(std::move(header), std::vformat(str, std::make_format_args(args...)));
	}

	template <typename... ARGS>
	DocNote& AddWarningDocNote(std::string header, std::string_view str, ARGS&& ... args)
	{
		auto& note = DocNotes.emplace_back(std::move(header), std::vformat(str, std::make_format_args(args...)));
		note.ShowInMemberList = true;
		note.Icon = "warning";
		return note;
	}

	virtual json ToJSON() const;

	virtual ~SimpleDeclaration() noexcept = default;
};

enum class LinkFlag
{
	Parent,
	SignatureSpecifiers, /// const, noexcept
	Specifiers, /// rest
	ReturnType,
	Parameters,
	Namespace,
	DeclarationType,
};
using LinkFlags = enum_flags<LinkFlag>;

struct Declaration : SimpleDeclaration
{
	Declaration() noexcept = default;
	explicit Declaration(FileMirror* parent) noexcept : ParentMirror(parent) {}

	FileMirror* ParentMirror = nullptr;
	virtual FileMirror* GetParentMirror() const { return ParentMirror; }

	size_t DeclarationLine = 0;
	json Attributes = json::object();
	AccessMode Access = AccessMode::Unspecified;

	/// TODO: Fill this in for every declaration!
	uint64_t ReflectionUID = 0;

	/// A name to be displayed in editors and such
	std::string DisplayName;

	/// These are in a map for historical reasons mostly, but it's easier to find bugs this way
	std::map<std::string, Method const*, std::less<>> AssociatedArtificialMethods;

	bool DocumentMembers = true;

	std::string GeneratedUniqueName() const { return std::format("{}_{:016x}", Name, ReflectionUID); }

	virtual std::string FullName(std::string_view sep = "_") const
	{
		return Name;
	}

	virtual std::string MakeLink(LinkFlags flags = {}) const = 0;

	virtual DeclarationType DeclarationType() const = 0;

	virtual void CreateArtificialMethodsAndDocument(Options const& options);

	virtual void AddNoDiscard(std::optional<std::string> reason) {}

	virtual json ToJSON() const;
};

struct TypeDeclaration : Declaration
{
	std::string Namespace;
	std::string GUID;

	explicit TypeDeclaration(FileMirror* parent) : Declaration(parent) {}

	std::string FullType() const
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}::{}", Namespace, Name);
	}

	virtual std::string FullName(std::string_view sep = "_") const override
	{
		if (Namespace.empty())
			return Name;
		return std::format("{}{}{}", replaced(Namespace, "::", sep), sep, Name);
	}

	virtual json ToJSON() const override
	{
		auto result = Declaration::ToJSON();
		if (!Namespace.empty())
			result["Namespace"] = Namespace;
		return result;
	}

	virtual void AddNoDiscard(std::optional<std::string> reason) override
	{
		AddDocNote("No Discard", "The compiler will warn you if you discard a function return value of this type.");
	}

	static TypeDeclaration const* FindTypeByPossiblyQualifiedName(std::string_view type_name, TypeDeclaration const* search_context)
	{
		if (const auto types = FindTypes(type_name); !types.empty())
		{
			if (types.size() == 1)
				return types[0];

			/// TODO: Use search_context to locate single candidate
			/// ALgo:
			///		auto path = split(search_context->Namespace, "::");
			///		while (!path.empty()) {
			///			if (auto klass = FindClassByFullName(join(path, "::") + "::" + class_name)) return klass;
			///			path = path.parent_path();
			///		}
		}
		return nullptr;
	}

	virtual std::string MakeLink(LinkFlags flags = {}) const override;
};

struct BaseMemberDeclaration : Declaration
{
	std::string ScriptName;

	virtual Declaration* ParentDecl() const = 0;

	virtual FileMirror* GetParentMirror() const override { if (const auto parent = ParentDecl()) return parent->GetParentMirror(); return ParentMirror; }

	virtual bool Document() const override;

	void CreateArtificialMethodsAndDocument(Options const& options) override;
};

template <typename PARENT_TYPE>
struct MemberDeclaration : BaseMemberDeclaration
{
	PARENT_TYPE* ParentType{};

	explicit MemberDeclaration(PARENT_TYPE* parent) : ParentType(parent) {}

	virtual Declaration* ParentDecl() const override { return ParentType; }

	PARENT_TYPE* Parent() const { return ParentType; }

	virtual std::string FullName(std::string_view sep = "_") const override
	{
		return std::format("{}{}{}", ParentType->FullName(sep), sep, Declaration::FullName(sep));
	}
};

struct Field final : MemberDeclaration<Class>
{
	using MemberDeclaration::MemberDeclaration;

	enum_flags<FieldFlags> Flags;
	std::string Type;
	std::string InitializingExpression;

	/// A identifier name without any member prefixes
	std::string CleanName;

	/// Name it will be loaded from
	std::string LoadName;

	/// Name it will be saved under
	std::string SaveName;

	Method* AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body, std::vector<std::string> comments, enum_flags<MethodFlags> additional_flags = {}, enum_flags<EntityFlags> entity_flags = {});
	void CreateArtificialMethodsAndDocument(Options const& options) override;

	virtual json ToJSON() const override;

	virtual std::string MakeLink(LinkFlags flags = {}) const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Field; }
};

struct MethodParameter final : SimpleDeclaration
{
	std::string Type;
	std::string Initializer;

	json ToJSON() const;
};

struct Method final : MemberDeclaration<Class>
{
	using MemberDeclaration::MemberDeclaration;

	enum_flags<MethodFlags> Flags;

	void SetParameters(std::string params);
	auto const& GetParameters() const { return mParameters; }

	std::vector<MethodParameter> ParametersSplit;
	std::string ParametersNamesOnly;
	std::string ParametersTypesOnly;
	std::string ArtificialBody;
	Declaration const* SourceDeclaration{};
	std::string UniqueName;
	SimpleDeclaration Return;

	virtual std::string MakeLink(LinkFlags flags = {}) const override;

	size_t ActualDeclarationLine() const
	{
		return DeclarationLine ? DeclarationLine : SourceDeclaration->DeclarationLine;
	}

	std::string GetSignature(Class const& parent_class) const;

	Method* AddArtificialMethod(std::string function_type, std::string results, std::string name, std::string parameters, std::string body,
		std::vector<std::string> comments, enum_flags<MethodFlags> additional_flags = {}, enum_flags<EntityFlags> entity_flags = {});
	void CreateArtificialMethodsAndDocument(Options const& options) override;

	virtual std::string FullName(std::string_view sep = "_") const override;

	virtual json ToJSON() const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Method; }

	virtual void AddNoDiscard(std::optional<std::string> reason) override
	{
		Flags += MethodFlags::NoDiscard;
		AddDocNote("No Discard", "The compiler will warn you if you discard this function's return value.");
	}

private:
	std::string mParameters;
	void Split();
};

struct Property final : MemberDeclaration<Class>
{
	using MemberDeclaration::MemberDeclaration;

	enum_flags<PropertyFlags> Flags;

	Method const* Setter = nullptr;
	Method const* Getter = nullptr;
	Field const* SourceField = nullptr;
	std::string Type;

	void CreateArtificialMethodsAndDocument(Options const& options) override;

	virtual json ToJSON() const override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Property; }

	virtual std::string MakeLink(LinkFlags flags = {}) const override;
};

struct Class final : TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;

	std::string BaseClass;

	std::vector<std::unique_ptr<Field>> Fields;
	std::vector<std::unique_ptr<Method>> Methods;
	std::map<std::string, Property, std::less<>> Properties; /// TODO: Document these! These are generated via 'GetterFor'/'SetterFor' methods and via private fields with getters/setters
	std::vector<std::string> AdditionalBodyLines;

	struct ClassDeclaredFlag
	{
		std::string Name;
		Field* SourceField = nullptr;
		Enumerator* Represents = nullptr;
		std::vector<Method*> GeneratedArtificialMethods;
	};

	std::vector<ClassDeclaredFlag> ClassDeclaredFlags; /// TODO: Generated from Flags=... fields. Should create a Fields section in the class documentation if not empty

	json DefaultFieldAttributes = json::object();
	json DefaultMethodAttributes = json::object();

	enum_flags<ClassFlags> Flags;

	size_t BodyLine = 0;

	std::map<std::string, std::vector<Method const*>> MethodsByName;

	Method* AddArtificialMethod(
		Declaration& for_decl,
		std::string function_type,
		std::string results,
		std::string name,
		std::string parameters,
		std::string body,
		std::vector<std::string> comments,
		enum_flags<MethodFlags> additional_flags = {},
		enum_flags<EntityFlags> entity_flags = {}
	);
	void CreateArtificialMethodsAndDocument(Options const& options) override;

	Property& EnsureProperty(std::string name);

	virtual json ToJSON() const override;

	static Class const* FindClassByPossiblyQualifiedName(std::string_view class_name, Class const* search_context)
	{
		/// TODO: This needs to be better, as it won't find a semi-qualified class, e.g. `B::C` won't find class '::A::B::C`
		if (const auto klasses = FindClasses(class_name); !klasses.empty())
		{
			if (klasses.size() == 1)
				return klasses[0];

			/// TODO: Use search_context to locate single candidate
			/// ALgo:
			///		auto path = split(search_context->Namespace, "::");
			///		while (!path.empty()) {
			///			if (auto klass = FindClassByFullName(join(path, "::") + "::" + class_name)) return klass;
			///			path = path.parent_path();
			///		}
		}
		return nullptr;
	}

	std::vector<Class const*> GetInheritanceList() const
	{
		std::vector<Class const*> result;
		auto current = this;
		while ((current = FindClassByPossiblyQualifiedName(current->BaseClass, current)))
		{
			result.push_back(current);
		}
		return result;
	}

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Class; }

private:

	std::vector<std::unique_ptr<Method>> mArtificialMethods;
};

struct Enumerator final : MemberDeclaration<Enum>
{
	using MemberDeclaration::MemberDeclaration;

	int64_t Value = 0;

	std::string Opposite; /// TODO: Initialize this from options & attribute

	enum_flags<EnumeratorFlags> Flags;

	virtual json ToJSON() const override;

	/// TODO: We should also use the artificial method subsystem for enumerators, as it means they will get docnotes
	void CreateArtificialMethodsAndDocument(Options const& options) override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Enumerator; }

	virtual std::string MakeLink(LinkFlags flags = {}) const override;
};

struct Enum final : TypeDeclaration
{
	using TypeDeclaration::TypeDeclaration;

	std::string BaseType;

	std::vector<std::unique_ptr<Enumerator>> Enumerators;

	json DefaultEnumeratorAttributes = json::object();

	enum_flags<EnumFlags> Flags;

	bool IsConsecutive() const
	{
		for (size_t i = 1; i < Enumerators.size(); ++i)
			if (Enumerators[i]->Value != Enumerators[i - 1]->Value + 1)
				return false;
		return true;
	}

	bool IsTrivial() const
	{
		return !Enumerators.empty() && Enumerators[0]->Value == 0 && IsConsecutive();
	}

	virtual json ToJSON() const override;

	/// TODO: We should also use the artificial method subsystem to create functions like GetNext, etc.
	/// as they will be documented/referenced that way!
	void CreateArtificialMethodsAndDocument(Options const& options) override;

	virtual ::DeclarationType DeclarationType() const override { return DeclarationType::Enum; }
};

struct FileMirror
{
	path SourceFilePath;
	std::vector<std::unique_ptr<Class>> Classes;
	std::vector<std::unique_ptr<Enum>> Enums;

	bool IsEmpty() const { return Classes.empty() && Enums.empty(); }

	json ToJSON() const;

	void CreateArtificialMethodsAndDocument(Options const& options);

	void Sort();

	FileMirror() = default;
	FileMirror(FileMirror const&) = delete;
	FileMirror(FileMirror&&) noexcept = default;
	FileMirror& operator=(FileMirror const&) = delete;
	FileMirror& operator=(FileMirror&&) noexcept = default;

	/// These fields and methods are only usable during parsing
	std::string SourceFileContents;
	std::vector<std::string_view> SourceFileLines;
	boost::icl::interval_set<intptr_t> SourceInactiveSpans;

	bool LineIsInactive(std::string_view line) const;
	bool LineIsInactive(size_t line) const { return LineIsInactive(SourceFileLines.at(line)); }

};

std::vector<FileMirror const*> GetMirrors();
FileMirror* AddMirror();
void RemoveEmptyMirrors();
void SortMirrors();
void CreateArtificialMethodsAndDocument(Options const& options);

template <typename... ARGS>
void ReportError(Declaration const& decl, std::string_view fmt, ARGS&& ... args)
{
	ReportError(decl.GetParentMirror()->SourceFilePath, decl.DeclarationLine, fmt, std::forward<ARGS>(args)...);
}

template <typename... ARGS>
void ReportWarning(Declaration const& decl, std::string_view fmt, ARGS&& ... args)
{
	ReportWarning(decl.GetParentMirror()->SourceFilePath, decl.DeclarationLine, fmt, std::forward<ARGS>(args)...);
}
