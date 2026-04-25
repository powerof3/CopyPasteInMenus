#pragma once

namespace detail
{
	std::string GetClipboardText();

	bool GetFocusedField(RE::GFxMovieView* view, std::string& a_fieldOut);
	bool ReadFromTextField(RE::GFxMovieView* view, const std::string& path, std::string& textFieldOut);
	bool WriteToTextField(RE::GFxMovieView* view, const std::string& path, std::string_view text);
	bool GetCaretIndex(RE::GFxMovieView* view, const std::string& path, double& caretIndexOut);

	RE::GFxMovieView* GetActiveTextInputView();
}

enum class PasteType
{
	kCursor,
	kEndOfText
};

class Manager final :
	public REX::Singleton<Manager>,
	public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static void Register();
	void        LoadSettings();

private:
	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_evn, RE::BSTEventSource<RE::InputEvent*>*) override;

	// members
	std::string   clipboardText;
	Key           primaryKey{ Key::kLeftControl };
	Key           secondaryKey{ Key::kV };
	PasteType     pasteType{ PasteType::kCursor };
	std::uint32_t inputDelay{ 10 };
	bool          keyCombo1{ false };
	bool          keyCombo2{ false };

	static constexpr const wchar_t* configPath{ L"Data/SKSE/Plugins/po3_CopyPasteInMenus.ini" };
};
