#include "../include/xivres/sqpack.h"

#include "../include/xivres/util.span_cast.h"

void xivres::sqpack::header::verify_or_throw(file_type supposedType) const {
	if (HeaderSize != sizeof header)
		throw bad_data_error("sizeof Header != 0x400");
	if (std::memcmp(Signature, Signature_Value, sizeof Signature) != 0)
		throw bad_data_error("Invalid SqPack signature");
	Sha1.verify(this, offsetof(xivres::sqpack::header, Sha1), "SqPack Header SHA-1");
	if (!util::all_same_value(Padding_0x024))
		throw bad_data_error("Padding_0x024 != 0");
	if (!util::all_same_value(Padding_0x3D4))
		throw bad_data_error("Padding_0x3D4 != 0");
	if (supposedType != file_type::Unspecified && supposedType != Type)
		throw bad_data_error(std::format("Invalid sqpack::sqpack_type (expected {}, file is {})",
			static_cast<uint32_t>(supposedType),
			static_cast<uint32_t>(*Type)));
}

void xivres::sqpack::sqindex::path_hash_locator::verify_or_throw() const {
	if (PairHashLocatorSize % sizeof(pair_hash_locator))
		throw bad_data_error("FolderSegmentEntry.FileSegmentSize % sizeof FileSegmentEntry != 0");
}

void xivres::sqpack::sqindex::header::verify_or_throw(sqindex_type expectedIndexType) const {
	if (HeaderSize != sizeof header)
		throw bad_data_error("sizeof IndexHeader != 0x400");
	if (expectedIndexType != sqindex_type::Unspecified && expectedIndexType != Type)
		throw bad_data_error(std::format("Invalid sqpack::sqpack_type (expected {}, file is {})",
			static_cast<uint32_t>(expectedIndexType),
			static_cast<uint32_t>(*Type)));
	Sha1.verify(this, offsetof(xivres::sqpack::sqindex::header, Sha1), "SqIndex Header SHA-1");
	if (!util::all_same_value(Padding_0x04C))
		throw bad_data_error("Padding_0x04C");
	if (!util::all_same_value(Padding_0x128))
		throw bad_data_error("Padding_0x128");
	if (!util::all_same_value(Padding_0x130))
		throw bad_data_error("Padding_0x130");
	if (!util::all_same_value(Padding_0x3D4))
		throw bad_data_error("Padding_0x3D4");

	if (!util::all_same_value(HashLocatorSegment.Padding_0x020))
		throw bad_data_error("HashLocatorSegment.Padding_0x020");
	if (!util::all_same_value(TextLocatorSegment.Padding_0x020))
		throw bad_data_error("TextLocatorSegment.Padding_0x020");
	if (!util::all_same_value(UnknownSegment3.Padding_0x020))
		throw bad_data_error("UnknownSegment3.Padding_0x020");
	if (!util::all_same_value(PathHashLocatorSegment.Padding_0x020))
		throw bad_data_error("PathHashLocatorSegment.Padding_0x020");

	if (Type == sqindex_type::Index && HashLocatorSegment.Size % sizeof pair_hash_locator)
		throw bad_data_error("HashLocatorSegment.size % sizeof FileSegmentEntry != 0");
	else if (Type == sqindex_type::Index2 && HashLocatorSegment.Size % sizeof full_hash_locator)
		throw bad_data_error("HashLocatorSegment.size % sizeof FileSegmentEntry2 != 0");
	if (UnknownSegment3.Size % sizeof segment_3_entry)
		throw bad_data_error("UnknownSegment3.size % sizeof Segment3Entry != 0");
	if (PathHashLocatorSegment.Size % sizeof path_hash_locator)
		throw bad_data_error("PathHashLocatorSegment.size % sizeof FolderSegmentEntry != 0");

	if (HashLocatorSegment.Count != 1)
		throw bad_data_error("Segment1.Count == 1");
	if (UnknownSegment3.Count != 0)
		throw bad_data_error("Segment3.Count == 0");
	if (PathHashLocatorSegment.Count != 0)
		throw bad_data_error("Segment4.Count == 0");
}

void xivres::sqdata::header::verify_or_throw(uint32_t /*expectedSpanIndex*/) const {
	if (HeaderSize != sizeof header)
		throw bad_data_error("sizeof IndexHeader != 0x400");
	Sha1.verify(util::span_cast<char>(1, this).subspan(0, offsetof(xivres::sqdata::header, Sha1)), "IndexHeader SHA-1");
	if (*Null1)
		throw bad_data_error("Null1 != 0");
	if (Unknown1 != Unknown1_Value)
		throw bad_data_error(std::format("Unknown1({:x}) != Unknown1_Value({:x})", *Unknown1, Unknown1_Value));
	//if (SpanIndex != expectedSpanIndex)
	//	throw CorruptDataException(std::format("SpanIndex({}) != ExpectedSpanIndex({})", *SpanIndex, expectedSpanIndex));
	if (*Null2)
		throw bad_data_error("Null2 != 0");
	if (MaxFileSize > MaxFileSize_MaxValue)
		throw bad_data_error(std::format("MaxFileSize({:x}) != MaxFileSize_MaxValue({:x})", *MaxFileSize, MaxFileSize_MaxValue));
	if (!util::all_same_value(Padding_0x034))
		throw bad_data_error("Padding_0x034 != 0");
	if (!util::all_same_value(Padding_0x3D4))
		throw bad_data_error("Padding_0x3D4 != 0");
}
