#include "Settings.h"

#include "NetDefines.h"

namespace Section {
	constexpr auto network{ L"network" };
	constexpr auto general{ L"general" };
	constexpr auto ui{ L"ui" };
}

namespace Setting {
	constexpr auto serverPort{ L"server_port" };
	constexpr auto clientPort{ L"client_port" };
	constexpr auto checkUpdates{ L"check_updates" };
	constexpr auto captureDevice{ L"capture_device" };
	constexpr auto lastUpdateCheck{ L"last_update_check" };
	constexpr auto language{ L"Language" };
	constexpr auto font{ L"Font" };
	constexpr auto fontSize{ L"Font.Size" };
	constexpr auto fontBold{ L"Font.Bold" };
}

namespace DefaultValue {
	constexpr auto serverPort{ Net::defaultServerPort };
	constexpr auto clientPort{ Net::defaultClientPort };
	constexpr bool checkUpdates{ true };
	constexpr auto captureDevice = defaultRenderDeviceId;
	constexpr auto language{ L"Auto" };
	constexpr auto font{ L"Segoe UI" };
	constexpr double fontSize{ 9.0 };
	constexpr bool fontBold{ false };
}

Settings::Settings(const std::wstring& fileName): fileName_(fileName) {
	ini_ = std::make_unique<CSimpleIniCaseW>(true);
	SI_Error rc = ini_->LoadFile(fileName.c_str());
	if (rc == SI_OK) {
		checkMissingSettings();
		return;
	}
	setDefaultValues();
	ini_->SaveFile(fileName_.c_str());
}

int Settings::getServerPort() const {
	return ini_->GetLongValue(Section::network, Setting::serverPort);
}

int Settings::getClientPort() const {
	return ini_->GetLongValue(Section::network, Setting::clientPort);
}

bool Settings::getCheckUpdates() const {
	return ini_->GetBoolValue(Section::general, Setting::checkUpdates);
}

std::wstring Settings::getCaptureDevice() const {
	return ini_->GetValue(Section::general, Setting::captureDevice);
}

std::wstring Settings::getLanguage() const {
	return ini_->GetValue(Section::ui, Setting::language, DefaultValue::language);
}

std::wstring Settings::getFont() const {
	return ini_->GetValue(Section::ui, Setting::font, DefaultValue::font);
}

double Settings::getFontSize() const {
	return ini_->GetDoubleValue(Section::ui, Setting::fontSize, DefaultValue::fontSize);
}

bool Settings::getFontBold() const {
	return ini_->GetBoolValue(Section::ui, Setting::fontBold, DefaultValue::fontBold);
}

void Settings::setLanguage(const std::wstring& language) {
	if (ini_->GetValue(Section::ui, Setting::language, DefaultValue::language) == language) return;
	ini_->SetValue(Section::ui, Setting::language, language.c_str());
	ini_->SaveFile(fileName_.c_str());
}

void Settings::setFont(const std::wstring& font) {
	if (ini_->GetValue(Section::ui, Setting::font, DefaultValue::font) == font) return;
	ini_->SetValue(Section::ui, Setting::font, font.c_str());
	ini_->SaveFile(fileName_.c_str());
}

void Settings::setFontSize(double size) {
	if (ini_->GetDoubleValue(Section::ui, Setting::fontSize, DefaultValue::fontSize) == size) return;
	ini_->SetDoubleValue(Section::ui, Setting::fontSize, size);
	ini_->SaveFile(fileName_.c_str());
}

void Settings::setFontBold(bool bold) {
	if (ini_->GetBoolValue(Section::ui, Setting::fontBold, DefaultValue::fontBold) == bold) return;
	ini_->SetBoolValue(Section::ui, Setting::fontBold, bold);
	ini_->SaveFile(fileName_.c_str());
}

long long Settings::getLastUpdateCheck() const {
	return static_cast<long long>(
		ini_->GetLongValue(Section::general, Setting::lastUpdateCheck, 0)
	);
}

void Settings::setLastUpdateCheck(long long timestamp) {
	ini_->SetLongValue(Section::general, Setting::lastUpdateCheck, static_cast<long>(timestamp));
	ini_->SaveFile(fileName_.c_str());
}


void Settings::setCheckUpdates(bool value) {
	if (ini_->GetBoolValue(Section::general, Setting::checkUpdates) == value) {
		return;
	}
	ini_->SetBoolValue(Section::general, Setting::checkUpdates, value);
	ini_->SaveFile(fileName_.c_str());
}

void Settings::setCaptureDevice(const std::wstring& deviceId) {
	if (ini_->GetValue(Section::general, Setting::captureDevice) == deviceId) {
		return;
	}
	ini_->SetValue(Section::general, Setting::captureDevice, deviceId.c_str());
	ini_->SaveFile(fileName_.c_str());
}

void Settings::setDefaultValues() {
	ini_->SetLongValue(Section::network, Setting::serverPort, DefaultValue::serverPort);
	ini_->SetLongValue(Section::network, Setting::clientPort, DefaultValue::clientPort);
	ini_->SetBoolValue(Section::general, Setting::checkUpdates, DefaultValue::checkUpdates);
	ini_->SetValue(Section::general, Setting::captureDevice, DefaultValue::captureDevice);
	ini_->SetValue(Section::ui, Setting::language, DefaultValue::language);
	ini_->SetValue(Section::ui, Setting::font, DefaultValue::font);
	ini_->SetDoubleValue(Section::ui, Setting::fontSize, DefaultValue::fontSize);
	ini_->SetBoolValue(Section::ui, Setting::fontBold, DefaultValue::fontBold);
}

void Settings::checkMissingSettings() {
	bool saveNeeded = false;
	if (!ini_->KeyExists(Section::network, Setting::serverPort)) {
		// todo: remove eventually 
		// Migrate value from version 0.5.2 or older
		auto serverPort = ini_->GetLongValue(L"", Setting::serverPort, DefaultValue::serverPort);
		ini_->SetLongValue(Section::network, Setting::serverPort, serverPort);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::network, Setting::clientPort)) {
		// todo: remove eventually 
		// Migrate value from version 0.5.2 or older
		auto clientPort = ini_->GetLongValue(L"", Setting::clientPort, DefaultValue::clientPort);
		ini_->SetLongValue(Section::network, Setting::clientPort, clientPort);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::general, Setting::checkUpdates)) {
		ini_->SetBoolValue(Section::general, Setting::checkUpdates, DefaultValue::checkUpdates);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::general, Setting::captureDevice)) {
		ini_->SetValue(Section::general, Setting::captureDevice, DefaultValue::captureDevice);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::ui, Setting::language)) {
		ini_->SetValue(Section::ui, Setting::language, DefaultValue::language);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::ui, Setting::font)) {
		ini_->SetValue(Section::ui, Setting::font, DefaultValue::font);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::ui, Setting::fontSize)) {
		ini_->SetDoubleValue(Section::ui, Setting::fontSize, DefaultValue::fontSize);
		saveNeeded = true;
	}
	if (!ini_->KeyExists(Section::ui, Setting::fontBold)) {
		ini_->SetBoolValue(Section::ui, Setting::fontBold, DefaultValue::fontBold);
		saveNeeded = true;
	}
	auto keysWithoutSection = ini_->GetSectionSize(L"");
	if (keysWithoutSection > 0) {
		ini_->Delete(L"", nullptr);
		saveNeeded = true;
	}
	if (saveNeeded) {
		ini_->SaveFile(fileName_.c_str());
	}
}
