
// ProtocolRecognizer.cpp

// Implements the cProtocolRecognizer class representing the meta-protocol that recognizes possibly multiple
// protocol versions and redirects everything to them

#include "Globals.h"

#include "ProtocolRecognizer.h"
#include "Protocol_1_8.h"
#include "Protocol_1_9.h"
#include "Protocol_1_10.h"
#include "Protocol_1_11.h"
#include "Protocol_1_12.h"
#include "Protocol_1_13.h"
#include "../ClientHandle.h"
#include "../Root.h"
#include "../Server.h"
#include "../World.h"
#include "../JsonUtils.h"
#include "../Bindings/PluginManager.h"





namespace cProtocolRecognizer
{
	struct cUnsupportedButPingableProtocolException : public std::runtime_error
	{
		explicit cUnsupportedButPingableProtocolException() :
			std::runtime_error("")
		{
		}
	};

	struct cTriedToJoinWithUnsupportedProtocolException : public std::runtime_error
	{
		explicit cTriedToJoinWithUnsupportedProtocolException(const std::string & a_Message) :
			std::runtime_error(a_Message)
		{
		}
	};

	/** Tries to recognize a protocol in the lengthed family (1.7+), based on m_Buffer; returns true if recognized.
	The packet length and type have already been read, type is 0
	The number of bytes remaining in the packet is passed as a_PacketLengthRemaining. */
	static std::unique_ptr<cProtocol> TryRecognizeLengthedProtocol(cClientHandle & a_Client, cByteBuffer & a_Buffer, UInt32 a_PacketLengthRemaining)
	{
		UInt32 PacketType;
		if (!a_Buffer.ReadVarInt(PacketType))
		{
			return {};
		}
		if (PacketType != 0x00)
		{
			// Not an initial handshake packet, we don't know how to talk to them
			LOGINFO("Client \"%s\" uses an unsupported protocol (lengthed, initial packet %u)",
				a_Client.GetIPString().c_str(), PacketType
			);
			a_Client.Kick("Unsupported protocol version");
			return {};
		}
		UInt32 ProtocolVersion;
		if (!a_Buffer.ReadVarInt(ProtocolVersion))
		{
			return {};
		}
		a_Client.SetProtocolVersion(ProtocolVersion);
		AString ServerAddress;
		UInt16 ServerPort;
		UInt32 NextState;
		if (!a_Buffer.ReadVarUTF8String(ServerAddress))
		{
			return {};
		}
		if (!a_Buffer.ReadBEUInt16(ServerPort))
		{
			return {};
		}
		if (!a_Buffer.ReadVarInt(NextState))
		{
			return {};
		}
		switch (ProtocolVersion)
		{
			case PROTO_VERSION_1_8_0: return std::make_unique<cProtocol_1_8_0>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_9_0: return std::make_unique<cProtocol_1_9_0>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_9_1: return std::make_unique<cProtocol_1_9_1>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_9_2: return std::make_unique<cProtocol_1_9_2>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_9_4: return std::make_unique<cProtocol_1_9_4>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_10_0: return std::make_unique<cProtocol_1_10_0>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_11_0: return std::make_unique<cProtocol_1_11_0>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_11_1: return std::make_unique<cProtocol_1_11_1>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_12: return std::make_unique<cProtocol_1_12>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_12_1: return std::make_unique<cProtocol_1_12_1>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_12_2: return std::make_unique<cProtocol_1_12_2>(&a_Client, ServerAddress, ServerPort, NextState);
			case PROTO_VERSION_1_13: return std::make_unique<cProtocol_1_13>(&a_Client, ServerAddress, ServerPort, NextState);
			default:
			{
				LOGD("Client \"%s\" uses an unsupported protocol (lengthed, version %u (0x%x))",
					a_Client.GetIPString().c_str(), ProtocolVersion, ProtocolVersion
				);

				if (NextState != 1)
				{
					throw cTriedToJoinWithUnsupportedProtocolException(
						Printf("Unsupported protocol version %u.\nTry connecting with Minecraft " MCS_CLIENT_VERSIONS, ProtocolVersion)
					);
				}

				throw cUnsupportedButPingableProtocolException();
			}
		}
	}





	static std::unique_ptr<cProtocol> TryRecognizeProtocol(cClientHandle & a_Client, cByteBuffer & a_Buffer)
	{
		// NOTE: If a new protocol is added or an old one is removed, adjust MCS_CLIENT_VERSIONS and MCS_PROTOCOL_VERSIONS macros in the header file

		// Lengthed protocol, try if it has the entire initial handshake packet:
		UInt32 PacketLen;
		UInt32 ReadSoFar = static_cast<UInt32>(a_Buffer.GetReadableSpace());
		if (!a_Buffer.ReadVarInt(PacketLen))
		{
			// Not enough bytes for the packet length, keep waiting
			return {};
		}
		ReadSoFar -= static_cast<UInt32>(a_Buffer.GetReadableSpace());
		if (!a_Buffer.CanReadBytes(PacketLen))
		{
			// Not enough bytes for the packet, keep waiting
			return {};
		}

		auto Protocol = TryRecognizeLengthedProtocol(a_Client, a_Buffer, PacketLen - ReadSoFar);
		if (Protocol != nullptr)
		{
			// The protocol has been recognized, initialize it:
			Protocol->Initialize(a_Client);
		}

		return Protocol;
	}





	/** Sends one or more packets inside a cByteBuffer.
	Can do this here since handshake packets are neither compressed nor encrypted.
	This is used only when handling an outdated server ping. */
	static void SendPacket(cClientHandle & a_Client, cByteBuffer & a_OutPacketBuffer)
	{
		// Writes out the packet normally.
		UInt32 PacketLen = static_cast<UInt32>(a_OutPacketBuffer.GetUsedSpace());
		cByteBuffer OutPacketLenBuffer(cByteBuffer::GetVarIntSize(PacketLen));

		// Compression doesn't apply to this state, send raw data:
		VERIFY(OutPacketLenBuffer.WriteVarInt32(PacketLen));
		AString LengthData;
		OutPacketLenBuffer.ReadAll(LengthData);
		a_Client.SendData(LengthData.data(), LengthData.size());

		// Send the packet's payload:
		AString PacketData;
		a_OutPacketBuffer.ReadAll(PacketData);
		a_OutPacketBuffer.CommitRead();
		a_Client.SendData(PacketData.data(), PacketData.size());
	}





	static UInt32 GetPacketID(cProtocol::ePacketType a_PacketType)
	{
		switch (a_PacketType)
		{
			case cProtocol::ePacketType::pktDisconnectDuringLogin: return 0x00;
			case cProtocol::ePacketType::pktStatusResponse:        return 0x00;
			case cProtocol::ePacketType::pktPingResponse:          return 0x01;
			default:
			{
				ASSERT(!"GetPacketID() called for an unhandled packet");
				return 0;
			}
		}
	}





	/* Status handler for unrecognised versions. */
	static void HandlePacketStatusRequest(cClientHandle & a_Client, cByteBuffer & a_Out)
	{
		cServer * Server = cRoot::Get()->GetServer();
		AString ServerDescription = Server->GetDescription();
		auto NumPlayers = static_cast<signed>(Server->GetNumPlayers());
		auto MaxPlayers = static_cast<signed>(Server->GetMaxPlayers());
		AString Favicon = Server->GetFaviconData();
		cRoot::Get()->GetPluginManager()->CallHookServerPing(a_Client, ServerDescription, NumPlayers, MaxPlayers, Favicon);

		// Version:
		Json::Value Version;
		Version["name"] = "Cuberite " MCS_CLIENT_VERSIONS;
		Version["protocol"] = MCS_LATEST_PROTOCOL_VERSION;

		// Players:
		Json::Value Players;
		Players["online"] = NumPlayers;
		Players["max"] = MaxPlayers;
		// TODO: Add "sample"

		// Description:
		Json::Value Description;
		Description["text"] = ServerDescription.c_str();

		// Create the response:
		Json::Value ResponseValue;
		ResponseValue["version"] = Version;
		ResponseValue["players"] = Players;
		ResponseValue["description"] = Description;
		if (!Favicon.empty())
		{
			ResponseValue["favicon"] = Printf("data:image/png;base64,%s", Favicon.c_str());
		}

		AString Response = JsonUtils::WriteFastString(ResponseValue);

		VERIFY(a_Out.WriteVarInt32(GetPacketID(cProtocol::ePacketType::pktStatusResponse)));
		VERIFY(a_Out.WriteVarUTF8String(Response));
	}





	/* Ping handler for unrecognised versions. */
	static void HandlePacketStatusPing(cClientHandle & a_Client, cByteBuffer & a_Buffer, cByteBuffer & a_Out)
	{
		Int64 Timestamp;
		if (!a_Buffer.ReadBEInt64(Timestamp))
		{
			return;
		}

		VERIFY(a_Out.WriteVarInt32(GetPacketID(cProtocol::ePacketType::pktPingResponse)));
		VERIFY(a_Out.WriteBEInt64(Timestamp));
	}





	AString GetVersionTextFromInt(int a_ProtocolVersion)
	{
		switch (a_ProtocolVersion)
		{
			case PROTO_VERSION_1_8_0:   return "1.8";
			case PROTO_VERSION_1_9_0:   return "1.9";
			case PROTO_VERSION_1_9_1:   return "1.9.1";
			case PROTO_VERSION_1_9_2:   return "1.9.2";
			case PROTO_VERSION_1_9_4:   return "1.9.4";
			case PROTO_VERSION_1_10_0:  return "1.10";
			case PROTO_VERSION_1_11_0:  return "1.11";
			case PROTO_VERSION_1_11_1:  return "1.11.1";
			case PROTO_VERSION_1_12:    return "1.12";
			case PROTO_VERSION_1_12_1:  return "1.12.1";
			case PROTO_VERSION_1_13:    return "1.13";
		}
		ASSERT(!"Unknown protocol version");
		return Printf("Unknown protocol (%d)", a_ProtocolVersion);
	}





	std::unique_ptr<cProtocol> TryRecogniseProtocol(cClientHandle & a_Client, cByteBuffer & a_SeenData, std::string_view & a_Data)
	{
		if (!a_SeenData.Write(a_Data.data(), a_Data.size()))
		{
			// We read more than the handshake packet here, oh well.

			a_Client.Kick("Too much data was sent");
			return {};
		}

		try
		{
			auto Protocol = TryRecognizeProtocol(a_Client, a_SeenData);
			if (Protocol != nullptr)
			{
				// The protocol has just been recognized, advance data start
				// to after the handshake and leave the rest to the protocol:
				a_Data = a_Data.substr(a_SeenData.GetUsedSpace() - a_SeenData.GetReadableSpace());

				// We read more than we can handle, purge the rest:
#ifdef NDEBUG
				a_SeenData.SkipRead(a_SeenData.GetReadableSpace());
#else
				ASSERT(a_SeenData.SkipRead(a_SeenData.GetReadableSpace()));
#endif
			}
			else
			{
				a_SeenData.ResetRead();
			}

			return Protocol;
		}
		catch (const cUnsupportedButPingableProtocolException &)
		{
			// A server list ping for an unrecognised version is currently occuring
			// Fall through to handler
		}
		catch (const std::exception & Oops)
		{
			a_Client.Kick(Oops.what());
			return {};
		}

		cByteBuffer OutPacketBuffer(6 KiB);

		// Handle server list ping packets
		for (;;)
		{
			UInt32 PacketLen;
			UInt32 PacketID;
			if (
				!a_SeenData.ReadVarInt32(PacketLen) ||
				!a_SeenData.CanReadBytes(PacketLen) ||
				!a_SeenData.ReadVarInt32(PacketID)
			)
			{
				// Not enough data
				break;
			}

			if ((PacketID == 0x00) && (PacketLen == 1))  // Request packet
			{
				HandlePacketStatusRequest(a_Client, OutPacketBuffer);
			}
			else if ((PacketID == 0x01) && (PacketLen == 9))  // Ping packet
			{
				HandlePacketStatusPing(a_Client, a_SeenData, OutPacketBuffer);
			}
			else
			{
				a_Client.Kick("Server list ping failed, unrecognized packet");
				return {};
			}
		}

		// Flush out all queued uncompressed, unencrypted data:
		SendPacket(a_Client, OutPacketBuffer);

		// In order to avoid making a whole dummy cProtocol just for old clients,
		// we depend on cClientHandle calling us to process old pings. We don't
		// persist state here, so restart parsing from scratch next time and return nothing:
		a_SeenData.ResetRead();
		return {};
	}





	void SendDisconnect(cClientHandle & a_Client, const AString & a_Reason)
	{
		const AString Message = Printf("{\"text\":\"%s\"}", EscapeString(a_Reason).c_str());
		const auto PacketID = GetPacketID(cProtocol::ePacketType::pktDisconnectDuringLogin);
		cByteBuffer Out(
			cByteBuffer::GetVarIntSize(static_cast<UInt32>(Message.size())) + Message.size() +
			cByteBuffer::GetVarIntSize(PacketID)
		);

		VERIFY(Out.WriteVarInt32(PacketID));
		VERIFY(Out.WriteVarUTF8String(Message));
		SendPacket(a_Client, Out);
	}
}
