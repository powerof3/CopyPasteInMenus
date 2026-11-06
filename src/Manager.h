#pragma once

namespace detail
{
	std::string GetClipboardText();

	bool GetFocusedField(RE::GFxMovieView* view, std::string& a_fieldOut);
	bool ReadFromTextField(RE::GFxMovieView* view, const std::string& path, std::string& textFieldOut);
	bool WriteToTextField(RE::GFxMovieView* view, const std::string& path, std::string_view text);
	bool GetCaretIndex(RE::GFxMovieView* view, const std::string& path, double& caretIndexOut);
	bool CreateStringObject(RE::GFxMovieView* view, std::string_view text, RE::GFxValue& objectOut);
	bool GetStringObjectLength(const RE::GFxValue& object, double& lengthOut);
	bool GetStringObjectSubStr(RE::GFxValue& object, double start, double length, std::string& textOut);

	RE::GFxMovieView* GetActiveTextInputView();
}

enum class PasteType
{
	kCursor,
	kEndOfText
};

class Manager final :
	public REX::TSingleton<Manager>,
	public RE::BSTEventSink<RE::InputEvent*>
{
public:
	static void Register();
	void        LoadSettings();

private:
	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_evn, RE::BSTEventSource<RE::InputEvent*>*) override;

	bool PasteText(RE::GFxMovieView* view, const std::string& path);

	[[nodiscard]] PasteType GetPasteType() const { return static_cast<PasteType>(pasteType.GetValue()); }

	// members
	std::string                     clipboardText;
	REX::TIniSetting<std::uint32_t> primaryKey{ "CopyPaste", "iPrimaryKey", std::to_underlying(Key::kLeftControl) };
	REX::TIniSetting<std::uint32_t> secondaryKey{ "CopyPaste", "iSecondaryKey", std::to_underlying(Key::kV) };
	REX::TIniSetting<std::uint32_t> pasteType{ "CopyPaste", "iPasteType", std::to_underlying(PasteType::kCursor) };
	REX::TIniSetting<std::uint32_t> inputDelay{ "CopyPaste", "iInputDelay", 10 };
	bool                            keyCombo1{ false };
	bool                            keyCombo2{ false };

	static constexpr auto configPath{ R"(Data\SKSE\Plugins\po3_CopyPasteInMenus.ini)"sv };
};
