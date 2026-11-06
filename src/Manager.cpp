#include "Manager.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef ERROR
#undef GetObject

namespace detail
{
	// https://stackoverflow.com/a/14763025
	std::string GetClipboardText()
	{
		std::string text;

		// Try opening the clipboard
		if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) {
			return text;
		}

		// Get handle of clipboard object for ANSI text
		const HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData == nullptr) {
			CloseClipboard();
			return text;
		}

		// Lock the handle to get the actual text pointer
		if (const auto pszText = static_cast<const wchar_t*>(GlobalLock(hData)); pszText != nullptr) {
			REX::UTF16_TO_UTF8(pszText, text);
		}

		// Release the lock
		GlobalUnlock(hData);

		// Release the clipboard
		CloseClipboard();

		return text;
	}

	bool GetFocusedField(RE::GFxMovieView* view, std::string& a_fieldOut)
	{
		RE::GFxValue result;
		if (!view->Invoke("Selection.getFocus", &result, nullptr, 0) || !result.IsString()) {
			return false;
		}
		a_fieldOut = result.GetString();
		return true;
	}

	bool ReadFromTextField(RE::GFxMovieView* view, const std::string& path, std::string& textFieldOut)
	{
		std::string  textPath = path + ".text";
		RE::GFxValue value;
		if (!view->GetVariable(&value, textPath.c_str()) || !value.IsString()) {
			return false;
		}
		textFieldOut = value.GetString();
		return true;
	}

	bool WriteToTextField(RE::GFxMovieView* view, const std::string& path, std::string_view text)
	{
		std::string basePath = path;

		if (basePath.contains(".TextInputInstance.textField") || basePath.contains(".textInput.textField")) {
			if (auto pos = basePath.find(".textField"); pos != std::string::npos) {
				basePath.erase(pos, sizeof(".textField") - 1);
			}
		}

		std::string maxCharsPath = basePath + ".maxChars";
		constexpr std::size_t CHAR_LIMIT = 260;

		RE::GFxValue maxChars;
		if (view->GetVariable(&maxChars, maxCharsPath.c_str()) && maxChars.GetUInt() < CHAR_LIMIT) {
			view->SetVariable(maxCharsPath.c_str(), CHAR_LIMIT);
		}

		RE::GFxValue textValue(text);
		return view->SetVariable((basePath + ".text").c_str(), textValue);
	}

	bool GetCaretIndex(RE::GFxMovieView* view, const std::string& path, double& caretIndexOut)
	{
		std::string  textPath = path + ".caretIndex";
		RE::GFxValue value;
		if (!view->GetVariable(&value, textPath.c_str()) || !value.IsNumber()) {
			return false;
		}
		caretIndexOut = value.GetNumber();
		return true;
	}

	bool CreateStringObject(RE::GFxMovieView* view, std::string_view text, RE::GFxValue& objectOut)
	{
		const RE::GFxValue args(text);
		view->CreateObject(&objectOut, "String", &args, 1);
		if (!objectOut.IsObject()) {
			return false;
		}
		return true;
	}

	bool GetStringObjectLength(const RE::GFxValue& object, double& lengthOut)
	{
		RE::GFxValue value;
		object.GetMember("length", &value);
		if (!value.IsNumber()) {
			return false;
		}
		lengthOut = value.GetNumber();
		return true;
	}

	bool GetStringObjectSubStr(RE::GFxValue& object, double start, double length, std::string& textOut)
	{
		const RE::GFxValue args[2]{ start, length };
		RE::GFxValue result;
		if (!object.Invoke("substr", &result, args, 2) || !result.IsString()) {
			return false;
		}
		textOut = result.GetString();
		return true;
	}

	RE::GFxMovieView* GetActiveTextInputView()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return nullptr;
		}

		std::string path;
		for (const auto& menuPtr : ui->menuStack) {
			if (auto menu = menuPtr.get(); menu && menu->uiMovie) {
				if (detail::GetFocusedField(menu->uiMovie.get(), path) && (REX::STR::ICONTAINS(path, "textField") || REX::STR::ICONTAINS(path, "TextInput"))) {
					return menu->uiMovie.get();
				}
			}
		}

		return nullptr;
	}
}

void Manager::Register()
{
	REX::INFO("{:*^30}", "EVENTS");
	RE::BSInputDeviceManager::GetSingleton()->AddEventSink(GetSingleton());
}

void Manager::LoadSettings()
{
	const auto store = REX::FIniSettingStore::GetSingleton();
	store->Init(configPath.data(), "");

	store->Load();
	store->Save();
}

RE::BSEventNotifyControl Manager::ProcessEvent(RE::InputEvent* const* a_evn, RE::BSTEventSource<RE::InputEvent*>*)
{
	if (!a_evn || keyCombo1 && keyCombo2) {
		return RE::BSEventNotifyControl::kContinue;
	}

	for (auto event = *a_evn; event; event = event->next) {
		if (const auto button = event->AsButtonEvent()) {
			const auto key = static_cast<RE::BSKeyboardDevice::Keys::Key>(button->GetIDCode());
			if (key == primaryKey) {  // hold left shift
				keyCombo1 = button->IsHeld();
			}
			if (keyCombo1 && key == secondaryKey && button->IsDown()) {  // wait for V to be down, not pressed!
				keyCombo2 = true;
			}
		}
	}

	if (keyCombo1 && keyCombo2) {
		if (clipboardText = detail::GetClipboardText(); !clipboardText.empty()) {
			std::jthread thread([this] {
				std::this_thread::sleep_for(std::chrono::milliseconds(inputDelay));  // for V to be added to the text field so we can remove it
				SKSE::GetTaskInterface()->AddUITask([this] {
					auto* view = detail::GetActiveTextInputView();
					if (!view) {
						keyCombo1 = false;
						keyCombo2 = false;

						return;
					}

					std::string path;
					if (!detail::GetFocusedField(view, path) || path.empty()) {
						keyCombo1 = false;
						keyCombo2 = false;

						return;
					}

					if (path.starts_with("_level0.")) {
						path.replace(0, 7, "_root");
					}

					PasteText(view, path);

					keyCombo1 = false;
					keyCombo2 = false;
				});
			});
			thread.detach();
		} else {
			keyCombo1 = false;
			keyCombo2 = false;
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

bool Manager::PasteText(RE::GFxMovieView* view, const std::string& path)
{
	constexpr auto isPastedKey = [](std::string_view str) { return str == "v" || str == "V"; };

	std::string oldText;
	double oldIndex = 0.0;
	if (!detail::ReadFromTextField(view, path, oldText)) {
		return false;
	}
	if (!detail::GetCaretIndex(view, path, oldIndex)) {
		return false;
	}

	RE::GFxValue oldString;
	RE::GFxValue clipboardString;
	double oldLength = 0.0;
	double clipboardLength = 0.0;
	if (!detail::CreateStringObject(view, oldText, oldString) || !detail::CreateStringObject(view, clipboardText, clipboardString)) {
		return false;
	}
	if (!detail::GetStringObjectLength(oldString, oldLength) || !detail::GetStringObjectLength(clipboardString, clipboardLength)) {
		return false;
	}

	double prefixLength = oldIndex;
	double suffixLength = oldLength - oldIndex;
	if (oldIndex > 0.0) {
		std::string prevChar;
		if (!detail::GetStringObjectSubStr(oldString, oldIndex - 1.0, 1.0, prevChar)) {
			return false;
		}

		if (isPastedKey(prevChar)) {
			prefixLength = oldIndex - 1.0;
		}
	}

	std::string newText = oldText;
	double newIndex = oldIndex;
	std::string prefix;
	std::string suffix;
	if (!detail::GetStringObjectSubStr(oldString, 0.0, prefixLength, prefix) || !detail::GetStringObjectSubStr(oldString, oldIndex, suffixLength, suffix)) {
		return false;
	}
	if (GetPasteType() == PasteType::kEndOfText) {
		newText = prefix + suffix + clipboardText;
		newIndex = prefixLength + suffixLength + clipboardLength;
	} else {
		newText = prefix + clipboardText + suffix;
		newIndex = prefixLength + clipboardLength;
	}
	if (!detail::WriteToTextField(view, path, newText)) {
		return false;
	}

	// move cursor
	const RE::GFxValue args[2]{ newIndex, newIndex };
	view->Invoke("Selection.setSelection", nullptr, args, 2);
	return true;
}
