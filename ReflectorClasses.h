#pragma once

namespace Reflector
{
	struct Serializer;
	struct Reflectable;

	struct Serializer
	{
		struct ValueHandle
		{
			Serializer* ParentSerializer = nullptr;

			template <typename T>
			void ReadField(const char* name, T& value, bool required) const;

			const ValueHandle* operator->() const noexcept { return this; }
		};

		virtual ~Serializer() = default;

		template <typename T>
		void WriteField(const char* name, T const& value);
	};

	void SerializeWriteField(Serializer& serializer, const char* name, int const& value);
	void SerializeWriteField(Serializer& serializer, const char* name, std::string const& value);
	void SerializeWriteField(Serializer& serializer, const char* name, bool const& value);
	void SerializeReadField(Serializer& serializer, Serializer::ValueHandle const& handle, const char* name, int& value, bool required);
	void SerializeReadField(Serializer& serializer, Serializer::ValueHandle const& handle, const char* name, std::string& value, bool required);
	void SerializeReadField(Serializer& serializer, Serializer::ValueHandle const& handle, const char* name, bool& value, bool required);

	struct Class
	{

	};

	struct Reflectable
	{
		virtual const char* GetClassName() const noexcept { return "Reflectable"; }
		virtual const char* GetParentClassName() const noexcept { return "Reflectable"; }
		virtual void SerializeWrite(::Reflector::Serializer* ser) const {}
		virtual void SerializeRead(::Reflector::Serializer::ValueHandle handle) {}

		virtual ~Reflectable() = default;
	};
	
	template<typename T>
	void Serializer::ValueHandle::ReadField(const char* name, T& value, bool required) const
	{
		SerializeReadField(*ParentSerializer, *this, name, value, required);
	}
	
	template<typename T>
	void Serializer::WriteField(const char* name, T const& value)
	{
		SerializeWriteField(*this, name, value);
	}
}