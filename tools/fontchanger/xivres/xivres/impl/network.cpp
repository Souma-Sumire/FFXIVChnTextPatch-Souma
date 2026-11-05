#include "../include/xivres/network.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>


const uint8_t xivres::network::bundle::MagicConstant1[]{
	0x52, 0x52, 0xa0, 0x41,
	0xff, 0x5d, 0x46, 0xe2,
	0x7f, 0x2a, 0x64, 0x4d,
	0x7b, 0x99, 0xc4, 0x75,
};
const uint8_t xivres::network::bundle::MagicConstant2[]{
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
};

std::span<const uint8_t> xivres::network::bundle::extract_front_trash(const std::span<const uint8_t>& buf) {
	const auto searchLength = std::min(sizeof Magic, buf.size());
	return {
		buf.begin(),
		std::min(
			std::search(buf.begin(), buf.end(), MagicConstant1, MagicConstant1 + searchLength),
			std::search(buf.begin(), buf.end(), MagicConstant2, MagicConstant2 + searchLength)
		)
	};
}

namespace {
	std::string format_epoch(int64_t epochMilliseconds) {
		const auto tp = std::chrono::sys_time(std::chrono::milliseconds(epochMilliseconds));
		return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::current_zone()->to_local(tp));
	}
}

std::string xivres::network::bundle::represent() const {
	return std::format(
		"[{}] Length={} ConnType={} Count={} CompressionType={}",
		format_epoch(Timestamp),
		TotalLength, ConnType, MessageCount, static_cast<int>(CompressionType)
	);
}

std::vector<std::vector<uint8_t>> xivres::network::bundle::split_messages(uint16_t expectedMessageCount, const std::span<const uint8_t>& buf) {
	std::vector<std::vector<uint8_t>> result;
	result.reserve(expectedMessageCount);
	for (size_t i = 0; i < buf.size();) {
		const auto& msg = *reinterpret_cast<const message*>(&buf[i]);
		if (i + msg.Length > buf.size() || !msg.Length)
			throw std::runtime_error("Could not parse game message (sum(message.length for each message) > total message length)");

		const auto sub = buf.subspan(i, msg.Length);
		result.emplace_back(sub.begin(), sub.end());
		i += msg.Length;
	}
	return result;
}

std::vector<std::vector<uint8_t>> xivres::network::bundle::get_messages(const inflate_fn& inflate, const oodle_decode_fn& oodleDecode) const {
	const auto view = std::span(Data, TotalLength - sizeof bundle_header);

	switch (CompressionType) {
		case compression_type::None:
			return split_messages(MessageCount, view);
		case compression_type::Deflate:
			return split_messages(MessageCount, inflate(view));
		case compression_type::Oodle:
			return split_messages(MessageCount, oodleDecode(view, DecodedBodyLength));
		default:
			throw bad_data_error(std::format("Unsupported compression type {}", static_cast<int>(CompressionType)));
	}
}

std::string xivres::network::message::represent(bool dump) const {
	std::string dumpstr;
	if (Type == message_type::ClientKeepAlive || Type == message_type::ServerKeepAlive) {
		dumpstr += std::format(
			"\n\tFFXIVMessage {} ID={}",
			format_epoch(Data.KeepAlive.Epoch * 1000LL),
			Data.KeepAlive.Id
		);
	} else if (Type == message_type::Ipc) {
		dumpstr += std::format(
			"\n\tFFXIVMessage {} Type={:04x} SubType={:04x} Unknown1={:04x} SeqId={:04x} Unknown2={:08x}",
			format_epoch(Data.Ipc.Epoch * 1000LL),
			static_cast<int>(Data.Ipc.Type), Data.Ipc.SubType, Data.Ipc.Unknown1, Data.Ipc.ServerId, Data.Ipc.Unknown2
		);
		if (dump) {
			dumpstr += "\n\t\t";
			const size_t dataLength = reinterpret_cast<const char*>(this) + this->Length - reinterpret_cast<const char*>(Data.Ipc.Data.Raw);
			for (size_t i = 0; i < dataLength; ++i) {
				dumpstr += std::format("{:02x} ", Data.Ipc.Data.Raw[i] & 0xFF);
				if (i % 4 == 3 && i != dataLength - 1)
					dumpstr += " ";
				if (i % 32 == 31 && i != dataLength - 1)
					dumpstr += "\n\t\t";
			}
		}
	}
	return std::format(
		"Length={} Source={:08x} Current={:08x} Type={}{}",
		Length, SourceActor, CurrentActor, static_cast<int>(Type), dumpstr
	);
}
