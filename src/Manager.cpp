#include "Manager.h"

namespace detail
{
	// https://stackoverflow.com/a/14763025
	std::string GetClipboardText()
	{
		std::string text;

		// Try opening the clipboard
		if (!IsClipboardFormatAvailable(CF_TEXT) || !OpenClipboard(nullptr)) {
			return text;
		}

		// Get handle of clipboard object for ANSI text
		const HANDLE hData = GetClipboardData(CF_TEXT);
		if (hData == nullptr) {
			return text;
		}

		// Lock the handle to get the actual text pointer
		if (const auto pszText = static_cast<const char*>(GlobalLock(hData)); pszText != nullptr) {
			text = pszText;
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

		RE::GFxValue maxChars(text.size());
		view->SetVariable((basePath + ".maxChars").c_str(), maxChars);

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

	RE::GFxMovieView* GetActiveTextInputView()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return nullptr;
		}

		std::string path;
		for (const auto& menuPtr : ui->menuStack) {
			if (auto menu = menuPtr.get(); menu && menu->uiMovie) {
				if (detail::GetFocusedField(menu->uiMovie.get(), path) && path.contains("textField")) {
					return menu->uiMovie.get();
				}
			}
		}

		return nullptr;
	}
}

void Manager::Register()
{
	logger::info("{:*^30}", "EVENTS");
	RE::BSInputDeviceManager::GetSingleton()->AddEventSink(GetSingleton());
}

void Manager::LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	ini.LoadFile(configPath);

	ini::get_value(ini, primaryKey, "CopyPaste", "iPrimaryKey", ";Keyboard scan codes : https://wiki.nexusmods.com/index.php/DirectX_Scancodes_And_How_To_Use_Them\n;Default: Left Ctrl");
	ini::get_value(ini, secondaryKey, "CopyPaste", "iSecondaryKey", ";Default: V");
	ini::get_value(ini, pasteType, "CopyPaste", "iPasteType", ";0 - insert text at cursor position | 1 - append text");
	ini::get_value(ini, inputDelay, "CopyPaste", "iInputDelay", ";Delay between key press and text paste (in milliseconds). Increase if V is not removed when pasting text.");

	(void)ini.SaveFile(configPath);
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

					std::string oldText;
					detail::ReadFromTextField(view, path, oldText);

					if (pasteType == PasteType::kEndOfText) {
						auto newText = oldText + clipboardText;
						if (!oldText.empty()) {
							newText.erase(oldText.length() - 1, 1);
						}
						detail::WriteToTextField(view, path, newText);

						const RE::GFxValue args[2]{ newText.length(), newText.length() };
						view->Invoke("Selection.setSelection", nullptr, args, 2);
					} else {
						if (oldText.size() == 1) {
							oldText.erase(0, 1);
						}

						std::string newText = oldText;
						bool        appended{ false };

						if (double cursorPos = 0; detail::GetCaretIndex(view, path, cursorPos)) {
							// insert at cursor pos
							if (oldText.size() > cursorPos) {
								appended = false;
								newText.insert((std::uint64_t)cursorPos, clipboardText);
								newText.erase((std::uint64_t)cursorPos - 1, 1);
							} else {  // or append if cursor is at end
								appended = true;
								newText += clipboardText;
								if (!oldText.empty()) {
									newText.erase(oldText.length() - 1, 1);
								}
							}

							detail::WriteToTextField(view, path, newText);

							// move cursor
							const auto index =
								appended ?
									newText.length() :
									cursorPos + (clipboardText.length() - 1);
							const RE::GFxValue args[2]{ index, index };
							view->Invoke("Selection.setSelection", nullptr, args, 2);
						}
					}

					keyCombo1 = false;
					keyCombo2 = false;
				});
			});
			thread.detach();
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}
