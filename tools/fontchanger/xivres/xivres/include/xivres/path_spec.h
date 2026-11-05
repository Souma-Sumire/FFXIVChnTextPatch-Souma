#ifndef XIVRES_PATHSPEC_H_
#define XIVRES_PATHSPEC_H_

#include <cinttypes>
#include <filesystem>
#include <format>
#include <string>

#include "sqpack.h"
#include "util.unicode.h"

namespace xivres {
	union sqpack_spec {
		uint32_t FullId;

		struct {
			uint32_t CategoryId : 16;
			uint32_t PartId : 8;
			uint32_t ExpacId : 8;
		};

		sqpack_spec() : FullId(0xFFFFFFFF) {}

		sqpack_spec(uint32_t categoryId, uint32_t expacId, uint32_t partId)
			: CategoryId(categoryId)
			, PartId(partId)
			, ExpacId(expacId) { }

		sqpack_spec(std::string_view part1, std::string_view part2 = {}, std::string_view part3 = {});

		static sqpack_spec from_category_int(uint32_t n) {
			sqpack_spec spec;
			spec.FullId = n;
			return spec;
		}
		
		static sqpack_spec from_filename_int(uint32_t n) {
			return {n >> 16, (n >> 8) & 0xFF, n & 0xFF};
		}
		
		[[nodiscard]] uint8_t category_id() const { return static_cast<uint8_t>(CategoryId); }
		[[nodiscard]] uint8_t part_id() const { return static_cast<uint8_t>(PartId); }
		[[nodiscard]] uint8_t expac_id() const { return static_cast<uint8_t>(ExpacId); }

		[[nodiscard]] std::string exname() const {
			return static_cast<uint32_t>(ExpacId) == 0 ? "ffxiv" : std::format("ex{}", ExpacId);
		}

		[[nodiscard]] uint32_t packid() const {
			return (static_cast<uint32_t>(CategoryId) << 16) | (static_cast<uint32_t>(ExpacId) << 8) | static_cast<uint32_t>(PartId);
		}

		[[nodiscard]] std::string name() const {
			return std::format("{:06x}", packid());
		}

		[[nodiscard]] std::string required_prefix() const;

		bool operator==(const sqpack_spec& r) const {
			return FullId == r.FullId;
		}

		friend auto operator<=>(const sqpack_spec& l, const sqpack_spec& r) {
			return l.FullId <=> r.FullId;
		}
	};

	struct path_spec {
		static constexpr uint32_t EmptyHashValue = 0xFFFFFFFFU;
		static constexpr uint8_t EmptyId = 0xFF;
		static constexpr uint32_t SlashHashValue = 0x862C2D2BU;

	private:
		bool m_empty = true;
		sqpack_spec m_sqpack;
		uint32_t m_pathHash = EmptyHashValue;
		uint32_t m_nameHash = EmptyHashValue;
		uint32_t m_fullPathHash = EmptyHashValue;
		std::string m_text;
		std::vector<std::string_view> m_parts;

	public:
		path_spec() noexcept = default;
		~path_spec() = default;

		path_spec(const path_spec& r)
			: m_empty(r.m_empty)
			, m_sqpack(r.m_sqpack)
			, m_pathHash(r.m_pathHash)
			, m_nameHash(r.m_nameHash)
			, m_fullPathHash(r.m_fullPathHash)
			, m_text(r.m_text) {
			m_parts.reserve(r.m_parts.size());
			for (const auto& part : r.m_parts)
				m_parts.emplace_back(m_text.data() + (part.data() - r.m_text.data()), part.size());
		}
		path_spec(path_spec&& r) noexcept { swap(*this, r); }

		path_spec& operator=(path_spec&& r) noexcept {
			swap(*this, r);
			return *this;
		}
		
		path_spec& operator=(const path_spec& r) {
			path_spec newSpec(r);
			swap(*this, newSpec);
			return *this;
		}

		path_spec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, sqpack_spec sqpackSpec)
			: m_empty(false)
			, m_sqpack(sqpackSpec)
			, m_pathHash(pathHash)
			, m_nameHash(nameHash)
			, m_fullPathHash(fullPathHash) { }

		path_spec(uint32_t pathHash, uint32_t nameHash, uint32_t fullPathHash, uint8_t categoryId, uint8_t expacId, uint8_t partId)
			: path_spec(pathHash, nameHash, fullPathHash, sqpack_spec(categoryId, expacId, partId)) { }

		template<typename TElem>
		path_spec(const TElem* fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) { }

		template<typename TElem, typename TTrait = std::char_traits<TElem>>
		path_spec(std::basic_string_view<TElem, TTrait> fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) { }

		template<typename TElem, typename TTrait = std::char_traits<TElem>, typename TAlloc = std::allocator<TElem>>
		path_spec(const std::basic_string<TElem, TTrait, TAlloc>& fullPath) : path_spec(util::unicode::convert<std::string>(fullPath)) { }

		path_spec(std::string fullPath);

		path_spec(const std::filesystem::path& path) : path_spec(path.u8string()) { }

		friend void swap(path_spec& l, path_spec& r) noexcept {
			std::swap(l.m_empty, r.m_empty);
			std::swap(l.m_sqpack, r.m_sqpack);
			std::swap(l.m_pathHash, r.m_pathHash);
			std::swap(l.m_nameHash, r.m_nameHash);
			std::swap(l.m_fullPathHash, r.m_fullPathHash);

			const auto rts = l.m_text.data();
			const auto lts = r.m_text.data();
			std::swap(l.m_text, r.m_text);
			std::swap(l.m_parts, r.m_parts);
			for (auto& part : l.m_parts)
				part = {l.m_text.data() + (part.data() - lts), part.size()};
			for (auto& part : r.m_parts)
				part = {r.m_text.data() + (part.data() - rts), part.size()};
		}

		path_spec& operator/=(const path_spec& r);
		path_spec& operator+=(const path_spec& r);
		path_spec& replace_stem(const std::string& newStem = {});

		friend path_spec operator/(path_spec l, const path_spec& r) {
			l /= r;
			return l;
		}

		friend path_spec operator+(path_spec l, const path_spec& r) {
			l += r;
			return l;
		}

		[[nodiscard]] path_spec with_stem(const std::string& newStem = {}) const { return path_spec(*this).replace_stem(newStem); }
		[[nodiscard]] path_spec parent_path() const { return with_stem(); }

		void clear() noexcept {
			path_spec empty;
			swap(empty, *this);
		}

		void update_text_if_same(const path_spec& r) { if (!has_original() && r.has_original() && *this == r) m_text = r.m_text; }

		operator bool() const { return m_empty; }
		[[nodiscard]] bool empty() const { return m_empty; }

		[[nodiscard]] std::vector<std::string_view> parts() const { return m_parts; }
		[[nodiscard]] path_spec filename() const;

		[[nodiscard]] uint8_t category_id() const { return m_sqpack.category_id(); }
		[[nodiscard]] uint8_t expac_id() const { return m_sqpack.ExpacId; }
		[[nodiscard]] uint8_t part_id() const { return m_sqpack.PartId; }
		[[nodiscard]] static uint32_t hash(std::string_view text);

		[[nodiscard]] uint32_t path_hash() const { return m_pathHash; }
		[[nodiscard]] uint32_t name_hash() const { return m_nameHash; }
		[[nodiscard]] uint32_t full_path_hash() const { return m_fullPathHash; }

		[[nodiscard]] bool has_original() const { return !m_text.empty(); }
		[[nodiscard]] const std::string& text() const { return m_text; }
		[[nodiscard]] path_spec textless() const { return !has_original() ? *this : path_spec(m_pathHash, m_nameHash, m_fullPathHash, m_sqpack); }

		[[nodiscard]] uint32_t packid() const { return m_sqpack.packid(); }
		[[nodiscard]] std::string packname() const { return m_sqpack.name(); }
		[[nodiscard]] std::string exname() const { return m_sqpack.exname(); }

		bool operator==(const path_spec& r) const {
			if (m_empty && r.m_empty)
				return true;

			return m_sqpack == r.m_sqpack
				&& m_fullPathHash == r.m_fullPathHash
				&& m_nameHash == r.m_nameHash
				&& m_pathHash == r.m_pathHash
				&& (m_text.empty() || r.m_text.empty() || FullPathComparator::compare(*this, r) == 0);
		}

		bool operator!=(const path_spec& r) const {
			return !this->operator==(r);
		}

	private:
		void recalculate_hash_values(bool checkSqPack);

	public:
		struct AllHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct FullHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_fullPathHash < r.m_fullPathHash)
					return -1;
				if (l.m_fullPathHash > r.m_fullPathHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct PairHashComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;
				if (l.m_pathHash < r.m_pathHash)
					return -1;
				if (l.m_pathHash > r.m_pathHash)
					return 1;
				if (l.m_nameHash < r.m_nameHash)
					return -1;
				if (l.m_nameHash > r.m_nameHash)
					return 1;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct FullPathComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (l.m_empty && r.m_empty)
					return 0;
				if (l.m_empty && !r.m_empty)
					return -1;
				if (!l.m_empty && r.m_empty)
					return 1;

				return util::unicode::strcmp(l.m_text.c_str(), r.m_text.c_str(), &util::unicode::lower, l.m_text.size() + 1, r.m_text.size() + 1);
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		struct AllComparator {
			static int compare(const path_spec& l, const path_spec& r) {
				if (const auto res = AllHashComparator::compare(l, r))
					return res;
				if (const auto res = FullPathComparator::compare(l, r))
					return res;
				return 0;
			}

			bool operator()(const path_spec& l, const path_spec& r) const {
				return compare(l, r) < 0;
			}
		};

		std::weak_ordering operator <=>(const path_spec& r) const {
			const auto c = AllComparator::compare(*this, r);
			if (c < 0)
				return std::weak_ordering::less;
			else if (c == 0)
				return std::weak_ordering::equivalent;
			else
				return std::weak_ordering::greater;
		}

		struct LocatorComparator {
			bool operator()(const sqpack::sqindex::pair_hash_locator& l, uint32_t r) const {
				return l.NameHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::pair_hash_locator& r) const {
				return l < r.NameHash;
			}

			bool operator()(const sqpack::sqindex::path_hash_locator& l, uint32_t r) const {
				return l.PathHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::path_hash_locator& r) const {
				return l < r.PathHash;
			}

			bool operator()(const sqpack::sqindex::full_hash_locator& l, uint32_t r) const {
				return l.FullPathHash < r;
			}

			bool operator()(uint32_t l, const sqpack::sqindex::full_hash_locator& r) const {
				return l < r.FullPathHash;
			}

			bool operator()(const sqpack::sqindex::pair_hash_with_text_locator& l, const char* rt) const {
				return util::unicode::strcmp(l.FullPath, rt, &util::unicode::lower, sizeof l.FullPath) < 0;
			}

			bool operator()(const char* lt, const sqpack::sqindex::pair_hash_with_text_locator& r) const {
				return util::unicode::strcmp(lt, r.FullPath, &util::unicode::lower, sizeof r.FullPath) < 0;
			}

			bool operator()(const sqpack::sqindex::full_hash_with_text_locator& l, const char* rt) const {
				return util::unicode::strcmp(l.FullPath, rt, &util::unicode::lower, sizeof l.FullPath) < 0;
			}

			bool operator()(const char* lt, const sqpack::sqindex::full_hash_with_text_locator& r) const {
				return util::unicode::strcmp(lt, r.FullPath, &util::unicode::lower, sizeof r.FullPath) < 0;
			}
		};
	};
}

template<>
struct std::formatter<xivres::path_spec, char> : std::formatter<std::string> {
	template<class FormatContext>
	auto format(const xivres::path_spec& t, FormatContext& fc) const {
		if (t.has_original()) {
			return std::formatter<std::string>::format(std::format(
																			"{}({:08x}/{:08x}, {:08x})", t.text(), t.path_hash(), t.name_hash(), t.full_path_hash()), fc);
		} else {
			return std::formatter<std::string>::format(std::format(
																			R"(???({:08x}/{:08x}, {:08x}))", t.path_hash(), t.name_hash(), t.full_path_hash()), fc);
		}
	}
};

#endif
