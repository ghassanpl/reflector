#if 0
#include <string>
#include <format>
#include <future>
#include <map>
#include <any>
#include <optional>
#include <nlohmann/json.hpp>

#include "../Include/ReflectorClasses.h"

#define RClass(...)
#define RField(...)
#define RMethod(...)
using nlohmann::json;

struct Reflectable
{
	virtual ~Reflectable() noexcept = default;
};

namespace RPC
{
	struct IRPCService;

	struct Identity
	{
		IRPCService* InService = nullptr;
		std::string Host;
		std::string Path;
	};

	template <typename T>
	struct IdentityOf : Identity
	{

	};

	struct Response
	{
		template <typename MESSAGE_TYPE>
		void operator()(MESSAGE_TYPE &&) const
		{
		}
	};

	/// @brief One to many
	struct Connection /// : std::enable_shared_from_this<Connection>
	{
		/// TODO: Shared from this to ensure if there are any call results we're waiting for or call requests trying to send, we won't destroy ourselves
		std::vector<Identity> RemoteIdentities;
	};

	template <typename T>
	struct ConnectionTo : Connection
	{
	};

	using Topic = std::string;

	using CallRequestID = uint64_t;
	using CallSignatureHash = uint64_t;

	template <typename RESULT_TYPE>
	struct CallRequest
	{
		IRPCService* InService = nullptr;
		CallRequestID RequestID = 0;
		std::future<RESULT_TYPE> ResultFuture;

		/// TODO: Maybe make SendCall() return std::shared_ptr<CallRequest<T>> ?
		/// TODO: ~CallRequest() { if (InService) InService->ResultsUnwantedFor(RequestID); }
	};

	template <typename RESULT_TYPE>
	struct CallResult
	{
		IRPCService* ViaService = nullptr;
		Identity From;
		std::optional<RESULT_TYPE> Result;
	};

	struct IRPCService
	{
	private:
		virtual void DoShutdown() = 0;
		friend struct RemoteCallContext;
	public:
		virtual ~IRPCService() noexcept = default;

		std::atomic_bool mWorking;
		virtual void Shutdown() { DoShutdown(); mWorking = false; }
		virtual bool Working() const { return mWorking.load(); }

		template <typename T>
		auto CallOn(T* obj)
		{
			if (!Working()) throw "not working";
			using proxy_type = typename T::template RemoteProxy<T*>;
			return proxy_type{obj, *this};
		}
		
		template <typename T>
		auto CallOn(Identity id)
		{
			if (!Working()) throw "not working";
			using proxy_type = T::template RemoteProxy<RPC::Identity>;
			return proxy_type{std::move(id), *this};
		}
		
		template <typename T>
		auto CallOn(IdentityOf<T> id)
		{
			if (!Working()) throw "not working";
			using proxy_type = T::template RemoteProxy<RPC::Identity>;
			return proxy_type{std::move(id), *this};
		}

		virtual Reflectable* ResolveIdentity(Identity const&) = 0;

		template <typename T>
		auto CallOn(RPC::ConnectionTo<T> const& obj)
		{
			if (!Working()) throw "not working";
			using proxy_type = T::template RemoteProxy<RPC::Connection const&>;
			return proxy_type{obj, *this};
		}

		template <typename T>
		auto PublishTo(Topic id)
		{
			if (!Working()) throw "not working";
			using proxy_type = T::template RemoteProxy<RPC::Topic>;
			return proxy_type{std::move(id), *this};
		}

		template <typename T>
		void RespondTo(CallRequestID request, T&& value)
		{

		}

		template <typename T>
		int DequeueResults(CallRequestID for_call, std::vector<CallResult<T>>& into)
		{

		}

		Identity LocalIdentityFor(Reflectable const* obj);
		void Unregister(Identity const& id);
		void Unregister(Reflectable const* obj);

	public:

		template <typename RESPONSE_TYPE, typename MESSAGE_TYPE>
		CallRequest<RESPONSE_TYPE> SendCallWithResult(CallSignatureHash hash, MESSAGE_TYPE &&msg, Identity const &target);

		template <typename RESPONSE_TYPE, typename MESSAGE_TYPE>
		CallRequest<RESPONSE_TYPE> SendCallWithResult(CallSignatureHash hash, MESSAGE_TYPE &&msg, Connection const &connection);

		template <typename RESPONSE_TYPE, typename MESSAGE_TYPE>
		CallRequest<RESPONSE_TYPE> SendCallWithResult(CallSignatureHash hash, MESSAGE_TYPE &&msg, Topic const &topic);

		template <typename RESPONSE_TYPE, typename MESSAGE_TYPE>
		CallRequest<RESPONSE_TYPE> SendCallWithResult(CallSignatureHash hash, MESSAGE_TYPE &&msg, Reflectable const* target)
		{
			CallRequest<RESPONSE_TYPE> result { this };
			std::promise<RESPONSE_TYPE> promise;
			result.ResultFuture = promise.get_future();
			/// result.RequestID = this->SendCallMessage(hash, std::forward<MESSAGE_TYPE>(msg), std::any{std::move(promise)}, this->LocalIdentityFor(target));
			result.RequestID = this->SendCallMessage(hash, std::forward<MESSAGE_TYPE>(msg), std::any{}, this->LocalIdentityFor(target));
			return result;
		}

		template <typename MESSAGE_TYPE>
		void SendCall(CallSignatureHash hash, MESSAGE_TYPE &&msg, Reflectable const* target)
		{
			this->SendCallMessage(hash, std::forward<MESSAGE_TYPE>(msg), {}, this->LocalIdentityFor(target));
		}

		template <typename MESSAGE_TYPE>
		void SendCall(CallSignatureHash hash, MESSAGE_TYPE&& msg, Identity const& target)
		{
			this->SendCallMessage(hash, std::forward<MESSAGE_TYPE>(msg), {}, target);
		}

	protected:

		virtual bool ShouldSerializeForTarget(Identity const &target) const;
		virtual bool ShouldSerializeForTarget(Connection const &connection) const;
		virtual bool ShouldSerializeForTarget(Topic const &topic) const;

		virtual CallRequestID SendCallMessageAny(CallSignatureHash hash, std::any payload, std::any promise, Identity const& target) = 0;
		virtual CallRequestID SendCallMessageJSON(CallSignatureHash hash, json payload, std::any promise, Identity const& target) = 0;

	private:

		template <typename MESSAGE_TYPE>
		CallRequestID SendCallMessage(CallSignatureHash hash, MESSAGE_TYPE&& payload, std::any promise, Identity const& target_id)
		{
			if (ShouldSerializeForTarget(target_id))
				return SendCallMessageJSON(hash, (json)std::forward<MESSAGE_TYPE>(payload), std::any{ std::move(promise) }, target_id);
			else
				return SendCallMessageAny(hash, std::any{ std::forward<MESSAGE_TYPE>(payload) }, std::any{ std::move(promise) }, target_id);
		}
	};

	struct RemoteCallContext
	{
		IRPCService* const ViaService;
		Identity CallerIdentity;
		CallRequestID RequestID = 0;

		template <typename T>
		void SendResult(T&& val)
		{
			ViaService->RespondTo(RequestID, std::forward<T>(val));
		}
	};

	template <typename MESSAGE_TYPE>
	void SendTo(MESSAGE_TYPE &&msg, Identity const &target);

	template <typename MESSAGE_TYPE>
	void SendVia(MESSAGE_TYPE &&msg, Connection const &connection);

	template <typename MESSAGE_TYPE>
	void Publish(MESSAGE_TYPE &&msg, Topic const &topic);

	struct Messagable : Reflectable
	{
		/// TODO: If Options.Networking.MaxMessagingServices is > 1 and <= 3, then:
		///		std::array<Messaging::IService*, Options.Networking.MaxMessagingServices> mRegisteredWith;
		/// TODO: Is it possible to optimize this?

		std::vector<IRPCService*> mRegisteredWith;
		void UnregisterFromMessagingServices()
		{
			for (auto service : mRegisteredWith)
				if (service) service->Unregister(this);
			mRegisteredWith.clear();
		}
		~Messagable()
		{
			UnregisterFromMessagingServices();
		}
		Identity MyIdentity(IRPCService& in_service) const;
	};

}

/// IN REFLECTOR.H

namespace RPC::KnownServices
{
	inline RPC::IRPCService* CSP();
}

/// END REFLECTOR.H

/// RClass() => 
	using ChatUser_ChatMessage_Params = std::tuple<
		std::string, /// text;
		int /// timestamp;
	>;
	using ChatUser_Ding_Params = std::tuple<
	>;

	struct ChatUser;
	template <typename TARGET>
	struct ChatUser_RemoteProxy
	{
		TARGET Target;
		RPC::IRPCService& Service;
		auto ChatMessage(std::string text, int timestamp) -> RPC::CallRequest<bool>
		{
			return Service.SendCallWithResult<bool>(RPC::CallSignatureHash{ 0xDEADBEEFCAFEB00B }, ChatUser_ChatMessage_Params{ text, timestamp }, Target);
		}
		void Ding()
		{
			return Service.SendCall(RPC::CallSignatureHash{ 0xB00BAFEED }, ChatUser_Ding_Params{}, Target);
		}

		auto operator->() { return this; }
		auto operator->() const { return this; }
	};

RClass(DefaultService = CSP); /// Or, you know, IPC, Network, GameSession, etc. Calls of remote methods of this object will be routed to this service by default
struct ChatUser : public RPC::Messagable
{
	/// RBODY START
	using self_type = ChatUser;

	template <typename TARGET>
	using RemoteProxy = ChatUser_RemoteProxy<TARGET>;

	static RPC::IRPCService* GetDefaultService() { return RPC::KnownServices::CSP(); }

	static void Call_ChatMessage(RPC::RemoteCallContext& context, void* params_erased, void* on_obj);
	static void Call_Ding(RPC::RemoteCallContext& context, void* params_erased, void* on_obj);
	/// RBODY END

	/// `Remote` attribute will cause the class to have a RemoteProxy
	/// Argument types and return type must be json-serializable
	/// TODO: `msg_info` should probably be optional
	RMethod(Remote);
	auto ChatMessage(RPC::RemoteCallContext const& context, std::string text, int timestamp) -> bool
	{
		LastMessage = std::format("Message from {} at {}:\n{}", context.CallerIdentity.Path, timestamp, text);
		return true;
	}

	RMethod(Remote, Reliable = false/"DingReliabilityFunc()");
	void Ding()
	{
		
	}

	RField();
	std::string LastMessage;
};

void Test()
{
	RPC::IRPCService* service = nullptr;
	RPC::ConnectionTo<ChatUser> convo;
	ChatUser* fake_user_for_log = nullptr;
	RPC::IdentityOf<ChatUser> joe;
	service->CallOn(fake_user_for_log).ChatMessage("message to log", 5);
	service->CallOn(joe).ChatMessage("direct message to Joe", 5);
	service->CallOn(convo).ChatMessage("message to a few people I chose", 5);
	service->PublishTo<ChatUser>("#channel").ChatMessage("message to channel", 6);
	service->CallOn(joe).Ding();
}

/// IN DATABASE

static_assert(std::derived_from<ChatUser, RPC::Messagable>);

void ChatUser::Call_ChatMessage(RPC::RemoteCallContext& context, void* params_erased, void* on_obj)
{
	auto& [_param_text, _param_timestamp] = *reinterpret_cast<ChatUser_ChatMessage_Params*>(params_erased);
	if (auto obj_ptr = dynamic_cast<ChatUser*>(reinterpret_cast<Reflectable*>(on_obj)))
	{
		/// Of course, SendResult is only invoked if method is non-void
		context.SendResult(obj_ptr->ChatMessage(context, 
			std::move(_param_text),
			std::move(_param_timestamp)
		));
	}
	else
		throw 0;
}

void ChatUser::Call_Ding(RPC::RemoteCallContext& context, void* params_erased, void* on_obj)
{
	/// NOT OUTPUTTED BECAUSE PARAMS EMPTY: auto& params = *(ChatUser_Ding_Params*)params_erased;
	if (auto obj_ptr = dynamic_cast<ChatUser*>(reinterpret_cast<Reflectable*>(on_obj)))
	{
		obj_ptr->Ding(
		);
	}
	else
		throw 0;
}

using MessageCaller = void(*)(RPC::RemoteCallContext&, void*, void*);
static const std::map<RPC::CallSignatureHash, std::pair<Reflector::Method const*, MessageCaller>, std::less<>> MethodCallers = {
	{ RPC::CallSignatureHash{ 0x4A54 }, { nullptr, &ChatUser::Call_ChatMessage } },
	{ RPC::CallSignatureHash{ 0x4152 }, { nullptr, &ChatUser::Call_Ding } },
};

#endif