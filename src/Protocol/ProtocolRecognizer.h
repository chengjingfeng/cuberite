#pragma once

#include "Protocol.h"





// Adjust these if a new protocol is added or an old one is removed:
#define MCS_CLIENT_VERSIONS "1.8.x-1.12.x"
#define MCS_PROTOCOL_VERSIONS "47, 107, 108, 109, 110, 210, 315, 316, 335, 338, 340"
#define MCS_LATEST_PROTOCOL_VERSION 340





/** Meta-protocol that recognizes multiple protocol versions, creates the specific
protocol version instance and redirects everything to it. */
namespace cProtocolRecognizer
{
	enum
	{
		PROTO_VERSION_1_8_0  = 47,
		PROTO_VERSION_1_9_0  = 107,
		PROTO_VERSION_1_9_1  = 108,
		PROTO_VERSION_1_9_2  = 109,
		PROTO_VERSION_1_9_4  = 110,
		PROTO_VERSION_1_10_0 = 210,
		PROTO_VERSION_1_11_0 = 315,
		PROTO_VERSION_1_11_1 = 316,
		PROTO_VERSION_1_12   = 335,
		PROTO_VERSION_1_12_1 = 338,
		PROTO_VERSION_1_12_2 = 340,
		PROTO_VERSION_1_13   = 393
	};

	/** Translates protocol version number into protocol version text: 49 -> "1.4.4" */
	AString GetVersionTextFromInt(int a_ProtocolVersion);

	/** Tries to recognize protocol based on a_Data and a_ReceivedData contents.
	a_SeenData represents a buffer for the incoming data.
	Returns the procotol if recognized. */
	std::unique_ptr<cProtocol> TryRecogniseProtocol(cClientHandle & a_Client, cByteBuffer & a_SeenData, std::string_view & a_Data);

	/* Sends a disconnect to the client as a result of a recognition error.
	This function can be used to disconnect before any protocol has been recognised. */
	void SendDisconnect(cClientHandle & a_Client, const AString & a_Reason);
} ;
