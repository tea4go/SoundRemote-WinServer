#include "NetUtil.h"

#include <iphlpapi.h>
#include <WS2tcpip.h>

#include <cassert>

namespace {
	uint32_t readUInt32B(const std::span<char>& data, size_t offset) {
		assert((offset + 4) <= data.size_bytes());
		return (static_cast<unsigned char>(data[offset]) << 24)
			| (static_cast<unsigned char>(data[offset + 1]) << 16)
			| (static_cast<unsigned char>(data[offset + 2]) << 8)
			| static_cast<unsigned char>(data[offset + 3]);
	}

	uint32_t readUInt32L(const std::span<char>& data, size_t offset) {
		assert((offset + 4) <= data.size_bytes());
		return static_cast<unsigned char>(data[offset])
			| (static_cast<unsigned char>(data[offset + 1]) << 8)
			| (static_cast<unsigned char>(data[offset + 2]) << 16)
			| (static_cast<unsigned char>(data[offset + 3]) << 24);
	}

	uint16_t readUInt16B(const std::span<char>& data, size_t offset) {
		assert((offset + 2) <= data.size_bytes());
		return static_cast<unsigned char>(data[offset]) << 8
			| (static_cast<unsigned char>(data[offset + 1]));
	}

	uint16_t readUInt16L(const std::span<char>& data, size_t offset) {
		assert((offset + 2) <= data.size_bytes());
		return static_cast<unsigned char>(data[offset])
			| (static_cast<unsigned char>(data[offset + 1]) << 8);
	}

	uint8_t readUInt8(const std::span<char>& data, size_t offset) {
		assert((offset + 1) <= data.size_bytes());
		return data[offset];
	}

	void writeUInt32B(uint32_t value, const std::span<char>& dest, size_t offset) {
		assert((offset + 4) <= dest.size_bytes());
		dest[offset] = value >> 24;
		dest[offset + 1] = value >> 16;
		dest[offset + 2] = value >> 8;
		dest[offset + 3] = value >> 0;
	}

	void writeUInt16B(uint16_t value, const std::span<char>& dest, size_t offset) {
		assert((offset + 2) <= dest.size_bytes());
		dest[offset] = value >> 8;
		dest[offset + 1] = value >> 0;
	}

	void writeUInt16L(uint16_t value, const std::span<char>& dest, size_t offset) {
		assert((offset + 2) <= dest.size_bytes());
		dest[offset] = value >> 0;
		dest[offset + 1] = value >> 8;
	}

	void writeUInt8(uint8_t value, const std::span<char>& dest, size_t offset) {
		assert((offset + 1) <= dest.size_bytes());
		dest[offset] = value;
	}

	void writeHeader(Net::Packet::Category category, const std::span<char>& packetData) {
		writeUInt16B(Net::Packet::protocolSignature, packetData, Net::Packet::signatureOffset);
		writeUInt8(static_cast<Net::Packet::CategoryType>(category), packetData, Net::Packet::categoryOffset);
		writeUInt16B(static_cast<Net::Packet::SizeType>(packetData.size_bytes()), packetData, Net::Packet::sizeOffset);
	}

	void writeAck(Net::Packet::RequestIdType requestId, const std::span<char>& packetData) {
		writeUInt16B(requestId, packetData, Net::Packet::dataOffset);
	}
};

std::forward_list<std::wstring> Net::getLocalAddresses() {
	std::forward_list<std::wstring> result;

	ULONG bufSize = 15000;  // Recommended initial size
	std::vector<char> buffer(bufSize);
	IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
	const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

	ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &bufSize);
	if (rc == ERROR_BUFFER_OVERFLOW) {
		buffer.resize(bufSize);
		adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &bufSize);
	}
	if (rc != NO_ERROR) {
		return {};
	}

	std::wstring ipBuf(16, 0);
	for (IP_ADAPTER_ADDRESSES* a = adapters; a != nullptr; a = a->Next) {
		// Skip loopback interfaces and non-operational adapters
		if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
		if (a->OperStatus != IfOperStatusUp) continue;

		for (IP_ADAPTER_UNICAST_ADDRESS* ua = a->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
			if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
			auto* sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
			// Skip 0.0.0.0 and 127.x.x.x
			if (sin->sin_addr.S_un.S_addr == 0) continue;
			if ((sin->sin_addr.S_un.S_addr & 0xFF) == 127) continue;

			if (InetNtopW(AF_INET, &sin->sin_addr, ipBuf.data(), ipBuf.size()) == nullptr) continue;

			std::wstring line{ ipBuf.c_str() };
			if (a->FriendlyName && a->FriendlyName[0]) {
				line += L" (";
				line += a->FriendlyName;
				line += L")";
			}
			result.push_front(line);
		}
	}
	return result;
}

std::optional<Audio::Compression> Net::compressionFromNetworkValue(Net::Packet::CompressionType compression) {
	switch (compression) {
	case 0:
		return Audio::Compression::none;
	case 1:
		return Audio::Compression::kbps_64;
	case 2:
		return Audio::Compression::kbps_128;
	case 3:
		return Audio::Compression::kbps_192;
	case 4:
		return Audio::Compression::kbps_256;
	case 5:
		return Audio::Compression::kbps_320;
	default:
		return std::nullopt;
	}
}

std::vector<char> Net::createAudioPacket(
	Net::Packet::Category category,
	Net::Packet::SequenceNumberType sequenceNumber,
	const std::span<const char>& audioData
) {
	std::vector<char> packet(Net::Packet::headerSize + Net::Packet::sequenceNumberSize + audioData.size_bytes());
	std::span<char> packetData{ packet.data(), packet.size() };
	writeHeader(category, packetData);
	writeUInt32B(sequenceNumber, packetData, Net::Packet::dataOffset);
	std::copy_n(audioData.data(), audioData.size_bytes(), packet.data() + Net::Packet::audioDataOffset);
	return packet;
}

std::vector<char> Net::createKeepAlivePacket() {
	std::vector<char> packet(Net::Packet::headerSize);
	writeHeader(Net::Packet::Category::ServerKeepAlive, { packet.data(), packet.size() });
	return packet;
}

std::vector<char> Net::createDisconnectPacket() {
	std::vector<char> packet(Net::Packet::headerSize);
	writeHeader(Net::Packet::Category::Disconnect, { packet.data(), packet.size() });
	return packet;
}

std::vector<char> Net::createAckConnectPacket(Net::Packet::RequestIdType requestId) {
	std::vector<char> packet(Net::Packet::headerSize + Net::Packet::ackSize);
	std::span<char> packetData{ packet.data(), packet.size() };
	writeHeader(Net::Packet::Category::Ack, packetData);
	writeAck(requestId, packetData);
	writeUInt8(Net::protocolVersion, packetData, Net::Packet::ackCustomDataOffset);
	return packet;
}

std::vector<char> Net::createAckSetFormatPacket(Net::Packet::RequestIdType requestId) {
	std::vector<char> packet(Net::Packet::headerSize + Net::Packet::ackSize);
	std::span<char> packetData{ packet.data(), packet.size() };
	writeHeader(Net::Packet::Category::Ack, packetData);
	writeAck(requestId, packetData);
	return packet;
}

Net::Packet::Category Net::getPacketCategory(const std::span<char>& packet) {
	if (packet.size_bytes() < Packet::headerSize) {
		return Net::Packet::Category::Error;
	}
	const Packet::SignatureType signature = readUInt16B(packet, 0);
	if (signature != Packet::protocolSignature) {
		return Net::Packet::Category::Error;
	}
	return static_cast<Net::Packet::Category>(readUInt8(packet, Net::Packet::categoryOffset));
}

std::optional<Keystroke> Net::getKeystroke(const std::span<char>& packet) {
	if (static_cast<int>(packet.size()) < Packet::dataOffset + Packet::keystrokeSize) {
		return std::nullopt;
	}
	int offset = Packet::dataOffset;
	Packet::KeyType key = readUInt8(packet, offset);
	offset += sizeof(key);
	Packet::ModsType mods = readUInt8(packet, offset);
	return Keystroke{ static_cast<int>(key), static_cast<int>(mods) };
}

std::optional<Net::Packet::ConnectData> Net::getConnectData(const std::span<char>& packet) {
	if (static_cast<int>(packet.size()) < Packet::dataOffset + Packet::ConnectData::size) {
		return std::nullopt;
	}
	int offset = Packet::dataOffset;
	Net::Packet::ConnectData data{};
	data.protocol = readUInt8(packet, offset);
	offset += sizeof(Net::Packet::ProtocolVersionType);
	data.requestId = readUInt16B(packet, offset);
	offset += sizeof(Net::Packet::RequestIdType);
	data.compression = readUInt8(packet, offset);
	return data;
}

std::optional<Net::Packet::SetFormatData> Net::getSetFormatData(const std::span<char>& packet) {
	if (static_cast<int>(packet.size()) < Packet::dataOffset + Packet::SetFormatData::size) {
		return std::nullopt;
	}
	int offset = Packet::dataOffset;
	Net::Packet::SetFormatData data{};
	data.requestId = readUInt16B(packet, offset);
	offset += sizeof(Net::Packet::RequestIdType);
	data.compression = readUInt8(packet, offset);
	return data;
}
