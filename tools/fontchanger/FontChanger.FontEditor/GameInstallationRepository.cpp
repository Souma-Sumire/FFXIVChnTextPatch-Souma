#include "pch.h"
#include "GameInstallationRepository.h"

static std::wstring GetPublisherCountryName(const std::filesystem::path& path) {
	// See: https://docs.microsoft.com/en-US/troubleshoot/windows/win32/get-information-authenticode-signed-executables

	constexpr auto ENCODING = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;

	HCRYPTMSG hMsg = nullptr;
	HCERTSTORE hStore = nullptr;
	DWORD dwEncoding = 0, dwContentType = 0, dwFormatType = 0;
	if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
		path.c_str(),
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
		CERT_QUERY_FORMAT_FLAG_BINARY,
		0,
		&dwEncoding,
		&dwContentType,
		&dwFormatType,
		&hStore,
		&hMsg,
		nullptr))
		return {};

	DWORD cbData = 0;
	if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &cbData)) {
		CryptMsgClose(hMsg);
		CertCloseStore(hStore, 0);
		return {};
	}

	std::vector<uint8_t> signerInfoBuf(cbData, {});
	if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, signerInfoBuf.data(), &cbData)) {
		CryptMsgClose(hMsg);
		CertCloseStore(hStore, 0);
		return {};
	}

	const auto& signerInfo = *reinterpret_cast<CMSG_SIGNER_INFO*>(&signerInfoBuf[0]);

	CERT_INFO certInfo{};
	certInfo.Issuer = signerInfo.Issuer;
	certInfo.SerialNumber = signerInfo.SerialNumber;
	const auto pCertContext = CertFindCertificateInStore(hStore, ENCODING, 0, CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);

	std::wstring country;
	if (pCertContext) {
		const auto pvTypePara = const_cast<char*>(szOID_COUNTRY_NAME);
		country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, nullptr, 0));
		country.resize(CertGetNameStringW(pCertContext, CERT_NAME_ATTR_TYPE, 0, pvTypePara, &country[0], static_cast<DWORD>(country.size())) - 1);
	}

	CertFreeCertificateContext(pCertContext);

	CryptMsgClose(hMsg);
	CertCloseStore(hStore, 0);
	return country;
}

static std::wstring ReadRegistryAsString(HKEY rootKey, const wchar_t* lpSubKey, const wchar_t* lpValueName, int mode = 0) {
	if (mode == 0) {
		auto res1 = ReadRegistryAsString(rootKey, lpSubKey, lpValueName, KEY_WOW64_32KEY);
		if (res1.empty())
			res1 = ReadRegistryAsString(rootKey, lpSubKey, lpValueName, KEY_WOW64_64KEY);
		return res1;
	}
	HKEY hKey;
	if (RegOpenKeyExW(rootKey,
		lpSubKey,
		0, KEY_READ | mode, &hKey))
		return {};
	const auto hKeyCleanup = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&RegCloseKey)>(hKey, &RegCloseKey);

	DWORD buflen = 0;
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, nullptr, &buflen))
		return {};

	std::wstring buf;
	buf.resize(buflen + 1);
	if (RegQueryValueExW(hKey, lpValueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(&buf[0]), &buflen))
		return {};

	buf.erase(std::ranges::find(buf, L'\0'), buf.end());

	return buf;
}

App::GameReleaseVendor App::GameInstallationRepository::DetermineGameRelease(std::filesystem::path path, std::filesystem::path& normalizedPath) {
	for (std::filesystem::path gameVersionPath; !exists(gameVersionPath = path / "game" / "ffxivgame.ver"); ) {
		auto parentPath = path.parent_path();
		if (parentPath == path)
			throw std::runtime_error("Game installation not found");
		path = std::move(parentPath);
	}

	if (GetPublisherCountryName(path / L"boot" / L"ffxivboot.exe") == L"JP") {
		normalizedPath = path / L"game";
		return GameReleaseVendor::SquareEnix;
	}

	if (GetPublisherCountryName(path / L"FFXIVBoot.exe") == L"CN") {
		normalizedPath = path / L"game";
		return GameReleaseVendor::ShandaGames;
	}

	if (GetPublisherCountryName(path / L"boot" / L"FFXIV_Boot.exe") == L"KR") {
		normalizedPath = path / L"game";
		return GameReleaseVendor::ActozSoft;
	}

	if (GetPublisherCountryName(path / L"boot" / L"FfxivLauncherTC.exe") == L"TW") {
		normalizedPath = path / L"game";
		return GameReleaseVendor::UserjoyGames;
	}

	return GameReleaseVendor::None;
}

std::vector<std::pair<App::GameReleaseVendor, std::filesystem::path>> App::GameInstallationRepository::AutoDetectInstalledGameReleases() {
	// Registry values that hold the path of a file of an installation, or a command line that starts with one.
	struct Source {
		HKEY Root;
		const wchar_t* SubKey;
		const wchar_t* ValueName;
		bool IsCommandLine;
	};
	static const Source Sources[]{
		{HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{2B41E132-07DF-4925-A3D3-F2D1765CCDFE})", L"DisplayIcon", false},
		{HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 39210)", L"InstallLocation", false}, // paid
		{HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 312060)", L"InstallLocation", false}, // free trial
		{HKEY_CLASSES_ROOT, LR"(ff14kr\shell\open\command)", L"", true},
		{HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\FFXIV)", L"DisplayIcon", false},
		{HKEY_CLASSES_ROOT, LR"(com.userjoy.ffxiv\shell\open\command)", L"", true},
	};

	std::vector<std::pair<GameReleaseVendor, std::filesystem::path>> res;
	for (const auto& source : Sources) {
		auto value = ReadRegistryAsString(source.Root, source.SubKey, source.ValueName);
		if (source.IsCommandLine && !value.empty()) {
			int n;
			const auto args = CommandLineToArgvW(value.c_str(), &n);
			value = args && n ? args[0] : L"";
			if (args)
				LocalFree(args);
		}
		if (value.empty())
			continue;

		std::filesystem::path path;
		if (const auto r = DetermineGameRelease(value, path); r != GameReleaseVendor::None)
			res.emplace_back(r, std::move(path));
	}

	return res;
}
