
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
	sUnsupportedButPingableProtocolException::sUnsupportedButPingableProtocolException() :
		std::runtime_error("")
	{
	}

	struct sTriedToJoinWithUnsupportedProtocolException : public std::runtime_error
	{
		explicit sTriedToJoinWithUnsupportedProtocolException(const std::string & a_Message) :
			std::runtime_error(a_Message)
		{
		}
	};

	/** Tries to recognize a protocol in the lengthed family (1.7+), based on m_Buffer; returns true if recognized.
	The packet length and type have already been read, type is 0
	The number of bytes remaining in the packet is passed as a_PacketLengthRemaining. */
	static std::unique_ptr<cProtocol> TryRecognizeLengthedProtocol(cClientHandle & a_Client, cByteBuffer & a_Buffer, std::string_view & a_Data)
	{
		UInt32 PacketType;
		UInt32 ProtocolVersion;
		AString ServerAddress;
		UInt16 ServerPort;
		UInt32 NextState;

		if (!a_Buffer.ReadVarInt(PacketType) || (PacketType != 0x00))
		{
			// Not an initial handshake packet, we don't know how to talk to them
			LOGINFO("Client \"%s\" uses an unsupported protocol (lengthed, initial packet %u)",
				a_Client.GetIPString().c_str(), PacketType
			);

			throw sTriedToJoinWithUnsupportedProtocolException(
				Printf("Your client isn't supported.\nTry connecting with Minecraft " MCS_CLIENT_VERSIONS, ProtocolVersion)
			);
		}

		if (
			!a_Buffer.ReadVarInt(ProtocolVersion) ||
			!a_Buffer.ReadVarUTF8String(ServerAddress) ||
			!a_Buffer.ReadBEUInt16(ServerPort) ||
			!a_Buffer.ReadVarInt(NextState)
		)
		{
			// TryRecognizeProtocol guarantees that we will have as much
			// data to read as the client claims in the protocol length field:
			throw sTriedToJoinWithUnsupportedProtocolException("Incorrect amount of data received - hacked client?");
		}

		// TODO: this should be a protocol property, not ClientHandle:
		a_Client.SetProtocolVersion(ProtocolVersion);

		// The protocol has just been recognized, advance data start
		// to after the handshake and leave the rest to the protocol:
		a_Data = a_Data.substr(a_Buffer.GetUsedSpace() - a_Buffer.GetReadableSpace());

		// We read more than we can handle, purge the rest:
		[[maybe_unused]] const bool Success =
			a_Buffer.SkipRead(a_Buffer.GetReadableSpace());
		ASSERT(Success);

		// All good, eat up the data:
		a_Buffer.CommitRead();

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
					throw sTriedToJoinWithUnsupportedProtocolException(
						Printf("Unsupported protocol version %u.\nTry connecting with Minecraft " MCS_CLIENT_VERSIONS, ProtocolVersion)
					);
				}

				throw sUnsupportedButPingableProtocolException();
			}
		}
	}





	static std::unique_ptr<cProtocol> TryRecognizeProtocol(cClientHandle & a_Client, cByteBuffer & a_Buffer, std::string_view & a_Data)
	{
		// NOTE: If a new protocol is added or an old one is removed, adjust MCS_CLIENT_VERSIONS and MCS_PROTOCOL_VERSIONS macros in the header file

		// Lengthed protocol, try if it has the entire initial handshake packet:
		UInt32 PacketLen;
		if (!a_Buffer.ReadVarInt(PacketLen))
		{
			// Not enough bytes for the packet length, keep waiting
			return {};
		}

		if (!a_Buffer.CanReadBytes(PacketLen))
		{
			// Not enough bytes for the packet, keep waiting
			// More of a sanity check to make sure no one tries anything funny (since ReadXXX can wait for data themselves):
			return {};
		}

		auto Protocol = TryRecognizeLengthedProtocol(a_Client, a_Buffer, a_Data);
		ASSERT(Protocol != nullptr);

		// The protocol has been recognized, initialize it:
		Protocol->Initialize(a_Client);

		return Protocol;
	}





	/** Sends one packet inside a cByteBuffer.
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
		// We read more than the handshake packet here, oh well.
		if (!a_SeenData.Write(a_Data.data(), a_Data.size()))
		{
			a_Client.Kick("Your client sent too much data; please try again later.");
			return {};
		}

		auto Protocol = TryRecognizeProtocol(a_Client, a_SeenData, a_Data);
		if (Protocol == nullptr)
		{
			a_SeenData.ResetRead();
		}

		return Protocol;
	}





	void RespondToUnsupportedProtocolPing(cClientHandle & a_Client, cByteBuffer & a_SeenData, const std::string_view a_Data)
	{
		if (!a_SeenData.Write(a_Data.data(), a_Data.size()))
		{
			a_Client.Kick("Server list ping failed, too much data.");
			return;
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
				a_SeenData.ResetRead();
				break;
			}

			if ((PacketID == 0x00) && (PacketLen == 1))  // Request packet
			{
				HandlePacketStatusRequest(a_Client, OutPacketBuffer);
				SendPacket(a_Client, OutPacketBuffer);
			}
			else if ((PacketID == 0x01) && (PacketLen == 9))  // Ping packet
			{
				HandlePacketStatusPing(a_Client, a_SeenData, OutPacketBuffer);
				SendPacket(a_Client, OutPacketBuffer);
			}
			else
			{
				a_Client.Kick("Server list ping failed, unrecognized packet.");
				return;
			}

			a_SeenData.CommitRead();
		}
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
