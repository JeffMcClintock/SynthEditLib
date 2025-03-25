#pragma once
#include "helpers/Timer.h"
#include "../shared/simdutf/simdutf.h"

struct SsgNumberEditClient
{
    virtual void repaintText() = 0;
    virtual void setEditValue(std::string value) = 0;
    virtual void endEditValue() = 0;
};

std::string toUtf8(const std::u32string& utf32)
{
	std::string utf8;
	utf8.resize(simdutf::utf8_length_from_utf32(utf32.data(), utf32.size()));
	[[maybe_unused]] const auto r = simdutf::convert_utf32_to_utf8(utf32.data(), utf32.size(), utf8.data());
	return utf8;
}

class GlyphArrangement
{
public:
	struct Glyph
	{
        char32_t c;
        float left{};
        float right{};
	};
    float height{};

    std::u32string text_utf32;
	std::string text_utf8;
    std::vector<Glyph> glyphs;

#if 0
    void init(std::string_view s, gmpi::drawing::TextFormat& textFormat)
    {
        glyphs.clear();

        const auto overallSize = textFormat.getTextExtentU(s);
		height = overallSize.height;

        // calculate the glyfs top-left aligned, then adjust later.
        for (int i = 0; i < s.size(); ++i)
        {
			auto& c = s[i];
            auto boundsIndividual = textFormat.getTextExtentU({ &c, 1 });
            auto sizeCumulative   = textFormat.getTextExtentU({s.begin(), s.begin() + i + 1});

            glyphs.push_back({ (char32_t) c, sizeCumulative.width - boundsIndividual.width, sizeCumulative.width });
        }
    }
#endif

    bool setText(std::u32string newText)
    {
		const bool changed = text_utf32 != newText;

        if (changed)
        {
            text_utf32 = newText;
			text_utf8 = toUtf8(newText);

            // glyphs are expensive to measure, so only do it if text has changed.
            // calc number of characters in common from start of string.
            int common = 0;
            for (; common < glyphs.size() && common < text_utf32.size(); ++common)
            {
                if (glyphs[common].c != newText[common])
                    break;
            }
            // erase any no longer wanted extra glyfs
            if (common < glyphs.size())
            {
                glyphs.erase(glyphs.begin() + common, glyphs.end());
            }
        }

        return changed;
	}

    bool setText(std::string_view newText)
    {
        std::u32string text;
        // convert string to UTF-32 on the assumption that most glyfs will resolve to one utf32 char (not always true)
        const size_t expected_utfwords = simdutf::utf32_length_from_utf8(newText.data(), newText.size());
        text.resize(expected_utfwords);
        [[maybe_unused]] const auto r = simdutf::convert_utf8_to_utf32(newText.data(), newText.size(), (char32_t*)text.data());

        return setText(text);
    }

    void update(gmpi::drawing::TextFormat& textFormat)
    {
        // calculate the glyfs top-left aligned, then adjust later.
        if (glyphs.size() < text_utf32.size())
        {
            std::string allChars = text_utf8.substr(0, glyphs.size());

            gmpi::drawing::Size sizeCumulative{};
            for (int i = glyphs.size(); i < text_utf32.size(); ++i)
            {
                const size_t expected_utf8words = simdutf::utf8_length_from_utf32(&text_utf32[i], 1);
                
                std::string charUtf8;
                charUtf8.resize(expected_utf8words);
                [[maybe_unused]] const auto r = simdutf::convert_utf32_to_utf8(&text_utf32[i], 1, charUtf8.data());

                allChars += charUtf8;
                auto boundsIndividual = textFormat.getTextExtentU({ charUtf8.data(), charUtf8.size() });
                sizeCumulative = textFormat.getTextExtentU({ allChars.begin(), allChars.end() });

                glyphs.push_back({ text_utf32[i], sizeCumulative.width - boundsIndividual.width, sizeCumulative.width });
            }

            height = sizeCumulative.height;
        }
    }

	gmpi::drawing::Rect getGlyfBounds(int i) const
	{
        if (glyphs.empty())
            return {};

		return { glyphs[i].left, 0.0f, glyphs[i].right, height};
	}
};

class SsgNumberEdit : public gmpi::TimerClient, public gmpi::api::IKeyListenerCallback // juce::Component, public juce::Timer, public juce::ModalComponentManager::Callback
{
    SsgNumberEditClient& client;
    int timerCounter = 0;

    int selectedFrom = -1;
    int selectedTo = -1;
	int cursorPos = -1;     // 0 - left off all chars, through to text.length() - right of all chars.
    GlyphArrangement numberEditGlyfs;
    gmpi::shared_ptr<gmpi::api::IKeyListener> listener;

public:
    SsgNumberEdit(SsgNumberEditClient& pclient) : client(pclient)
    {
//        setWantsKeyboardFocus(true);
    }
    ~SsgNumberEdit()
    {
//        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

    std::string unsavedText() const
    {
        return toUtf8(numberEditGlyfs.text_utf32);
    }

	void setText(std::string newText)
	{
		if (numberEditGlyfs.setText(newText))
		{
			client.repaintText();
		}
	}

    void show(gmpi::api::IDialogHost* dialogHost, const gmpi::drawing::Rect* bounds)
    {
        selectedFrom = 0;
        selectedTo = cursorPos = numberEditGlyfs.text_utf32.length();

        // grab Keyboard Focus
        gmpi::shared_ptr<gmpi::api::IUnknown> ret;
        dialogHost->createKeyListener(bounds, ret.put());
		listener = ret.as<gmpi::api::IKeyListener>();

        if(listener)
        {
            listener->showAsync(static_cast<gmpi::api::IKeyListenerCallback*>(this));
        }

        client.repaintText();

		startTimerHz(10);
    }

    void hide()
    {
        stopTimer();

        listener = {};

        cursorPos = -2;
        selectedFrom = selectedTo = -1;

        client.endEditValue();
        client.repaintText();
    }

    /* todo
    void visibilityChanged() // override
    {
        if (isVisible())
        {
            startTimer()
            startTimerHz(10);
            timerCounter = 0;
            client.repaintText();
        }
        else
        {
            stopTimer();
            timerCounter = 100;
        }
    }
    */

    bool cursorBlinkState(int glyfCount)
    {
        return timerCounter < 6 && cursorPos >= 0 && cursorPos <= glyfCount;
    }

    bool onTimer() override
    {
        if (timerCounter++ > 10)
        {
            timerCounter = 0;
        }

        if (timerCounter == 6 || timerCounter == 0)
        {
            client.repaintText();
        }

        return true;
    }

    // IKeyListenerCallback
    void onKeyUp(int32_t key, int32_t flags) override
    {
    }
    void onLostFocus(gmpi::ReturnCode result) override
    {
        hide();
    }

    void onKeyDown(int32_t key, int32_t flags) override
    {
		auto text = numberEditGlyfs.text_utf32;

        // delete highlighted text
        if (selectedFrom != selectedTo)
        {
            text = text.substr(0, selectedFrom) + text.substr(selectedTo);
            cursorPos = selectedTo = selectedFrom;
        }

        switch (key)
        {
            case 0x0D: // <ENTER>
            {
                client.setEditValue(toUtf8(text));
                hide();
                return; // client may have updated text_utf32, don't overwrite that
            }
            break;

            case 0x1B: // <ESC>
            {
                hide();
                return;
            }
            break;

        case 0x08: // <BACKSPACE>
        case 0x7F: // <DEL>
        {
            if (selectedFrom == selectedTo)
            {
                if (key == 0x08)
                {
                    // delete prev character
                    if (cursorPos > 0)
                    {
                        text = text.substr(0, cursorPos - 1) + text.substr(cursorPos);
                        cursorPos--;
                    }
                }
                else // <DEL>
                {
                    // delete next character
                    if (cursorPos < text.length())
                    {
                        text = text.substr(0, cursorPos) + text.substr(cursorPos + 1);
                    }
                }
            }
            else
            {
                // delete highlighted text
                text = text.substr(0, selectedFrom) + text.substr(selectedTo);
                cursorPos = selectedTo = selectedFrom;
            }
        }
        break;

        case 0x25: // <LEFT>
        {
			const auto prevCursor = cursorPos;
            cursorPos = (std::max)(cursorPos - 1, 0);

            if (flags & gmpi::api::GG_POINTER_KEY_SHIFT)
            {
				if (prevCursor == selectedFrom)
					selectedFrom = cursorPos;
				else
					selectedTo = cursorPos;
            }
            else
            {
                selectedFrom = cursorPos;
                selectedTo = cursorPos;
            }
        }
        break;

        case 0x27: // <RIGHT>
        {
            const auto prevCursor = cursorPos;
            cursorPos = (std::min)(cursorPos + 1, (int) text.size());

            if (flags & gmpi::api::GG_POINTER_KEY_SHIFT)
            {
                if (prevCursor == selectedTo)
                    selectedTo = cursorPos;
                else
                    selectedFrom = cursorPos;
            }
            else
            {
                selectedFrom = cursorPos;
                selectedTo = cursorPos;
            }
        }
        break;

        default:
        {
            if (flags & gmpi::api::GG_POINTER_KEY_CONTROL) // <ctrl> key
            {
                switch (key)
                {
                case 'a':
                case 'A':
                {
                    // <ctrl> + A : Select All
                    selectedFrom = 0;
                    selectedTo = text.length();
                    cursorPos = text.length();
                }
                break;

                case 'v':
                case 'V':
                {
                    // <ctrl> + V : Paste
                    int test = 9;
                }
                break;

				};
            }
            else
            {
                switch (key)
                {
                case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': case '.': case '+': case '-':
                {
                    if (selectedFrom != selectedTo)
                    {
                        text = text.substr(0, selectedFrom) + (char32_t)key + text.substr(selectedTo);
                        cursorPos = selectedFrom + 1;
                    }
                    else
                    {
                        text = text.substr(0, cursorPos) + (char32_t)key + text.substr(cursorPos);
                        cursorPos++;
                    }
                    selectedFrom = selectedTo = cursorPos;
                }
                break;
                };
            }
        }
        break;
        };

        timerCounter = 0; // show cursor
        client.repaintText();

		if(numberEditGlyfs.setText(text))
		{
			client.repaintText();
		}

        /*
		if (textBefore != text)
		{
            if (textBefore.size() > 0 && text == textBefore.substr(0, textBefore.size() - 1))
            {
                // removing from back of string can be quicker than recalculating entire layout by just removing last glyf
				numberEditGlyfs.glyphs.pop_back();
            }
            else
            {
                numberEditGlyfs.glyphs.clear();
            }
		}
        */
    }
    void paste(const char* ptext, size_t psize) override
    {
        auto text = numberEditGlyfs.text_utf32;

        // delete highlighted text
        if (selectedFrom != selectedTo)
        {
            text = text.substr(0, selectedFrom) + text.substr(selectedTo);
            cursorPos = selectedTo = selectedFrom;
        }

        // convert to glyfs
		const std::string_view utf8(ptext, psize);
		const size_t expected_utfwords = simdutf::utf32_length_from_utf8(utf8.data(), utf8.size());
		std::u32string utf32;
		utf32.resize(expected_utfwords);
		[[maybe_unused]] const auto r = simdutf::convert_utf8_to_utf32(utf8.data(), utf8.size(), (char32_t*)utf32.data());

        // paste into string
        text = text.substr(0, cursorPos) + utf32 + text.substr(cursorPos);
        cursorPos += utf32.size();

        if (numberEditGlyfs.setText(text))
        {
            client.repaintText();
        }
    }

    // textFormat must be created with default alightment (top-left)
    void render(
          gmpi::drawing::Graphics& g
        , gmpi::drawing::TextFormat& textFormat
        , gmpi::drawing::Rect bounds
        , gmpi::drawing::TextAlignment textAlignment = gmpi::drawing::TextAlignment::Center
        , gmpi::drawing::ParagraphAlignment paragraphAlignment = gmpi::drawing::ParagraphAlignment::Center
    )
    {
		numberEditGlyfs.update(textFormat);

        const float textWidth = numberEditGlyfs.glyphs.empty() ? 0.0f : numberEditGlyfs.glyphs.back().right;
        const auto& textHeight = numberEditGlyfs.height;

        switch (textAlignment)
        {
        case gmpi::drawing::TextAlignment::Leading:
            break;
        case gmpi::drawing::TextAlignment::Trailing:
            bounds.left = bounds.right - textWidth;
            break;
        case gmpi::drawing::TextAlignment::Center:
            bounds.left = 0.5f * (bounds.left + bounds.right - textWidth);
            break;
        }

        switch (paragraphAlignment)
        {
        case gmpi::drawing::ParagraphAlignment::Near:
            break;
        case gmpi::drawing::ParagraphAlignment::Far:
            bounds.top = bounds.bottom - textHeight;
            break;
        case gmpi::drawing::ParagraphAlignment::Center:
            bounds.top = 0.5f * (bounds.top + bounds.bottom - textHeight);
            break;
        }

        auto brush = g.createSolidColorBrush(gmpi::drawing::Colors::White);
        g.drawTextU(numberEditGlyfs.text_utf8, textFormat, bounds, brush);

        // Highlight selection
        if (selectedFrom != selectedTo)
        {
            gmpi::drawing::Rect highlightRect = unionRect(numberEditGlyfs.getGlyfBounds(selectedFrom), numberEditGlyfs.getGlyfBounds(selectedTo - 1));
            
			highlightRect.left += bounds.left;
			highlightRect.right += bounds.left;
			highlightRect.top += bounds.top;
			highlightRect.bottom += bounds.top;

            {
                gmpi::drawing::ClipDrawingToBounds _(g, highlightRect);

                // highlight background
                g.clear(gmpi::drawing::Colors::Green);

                // highlight foreground
                brush.setColor(gmpi::drawing::Colors::Red);

                g.drawTextU(numberEditGlyfs.text_utf8, textFormat, bounds, brush);
            }
        }

        // draw a cursor
        if (cursorBlinkState(numberEditGlyfs.glyphs.size()))
        {
            float cursorX{ bounds.left};
			const auto glyfCount = numberEditGlyfs.glyphs.size();
            if (glyfCount > 0 && cursorPos > 0)
            {
                if (cursorPos >= glyfCount)
                {
                    cursorX += numberEditGlyfs.glyphs.back().right;
                }
                else
                {
                    cursorX += 0.5f * (numberEditGlyfs.glyphs[cursorPos - 1].right + numberEditGlyfs.glyphs[cursorPos].left);
                }
            }
            float top = bounds.top;
            float bottom = top + numberEditGlyfs.height;

            brush.setColor(gmpi::drawing::Colors::White);
            g.drawLine({ cursorX, top - 2 }, { cursorX, bottom + 2 }, brush, 2.0f);
        }

#if 0 // debug glyf bounds

        brush.setColor(gmpi::drawing::Colors::Gray);
        for (int i = 0 ; i < numberEditGlyfs.glyphs.size() ; ++i)
        {
			gmpi::drawing::Rect r = numberEditGlyfs.getGlyfBounds(i);
            //			r.offset(bounds.left, bounds.top);
            g.drawRectangle(r, brush, 0.5f);
        }
#endif

    }

#if 0 // todo
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (isVisible() && !getBounds().contains(event.getPosition()))
            hide();
    }

    void drawEditorText(juce::Graphics& g, juce::GlyphArrangement* editableGlyphs, juce::Path allTextPath)
    {
        g.fillPath(allTextPath);

        if (editableGlyphs == nullptr || !isVisible())
            return;

        // Highlight selection
        if (selectedFrom != selectedTo)
        {
            //_RPTN(0, "SELECTION FROM %d To %d\n", numberEdit.selectedFrom, numberEdit.selectedTo);
            const auto numbersRect = editableGlyphs->getBoundingBox(selectedFrom, selectedTo - selectedFrom, true);
            g.setColour(juce::Colour(0xff9FB0B3));
            g.fillRect(numbersRect);

            g.saveState();
            g.reduceClipRegion(numbersRect.toNearestInt());

            g.setColour(juce::Colours::black);
            g.fillPath(allTextPath);

            g.restoreState();
        }

        drawCursor(g, *editableGlyphs);
    }

    void drawCursor(juce::Graphics& g, juce::GlyphArrangement& editableGlyphs)
    {
        if (!cursorBlinkState(editableGlyphs.getNumGlyphs()))
            return;

        const auto boundingBox = editableGlyphs.getBoundingBox(0, editableGlyphs.getNumGlyphs(), true);

        float cursorX = boundingBox.getRight();
        if (cursorPos < editableGlyphs.getNumGlyphs())
        {
            auto& glyph = editableGlyphs.getGlyph(cursorPos);
            cursorX = glyph.getLeft() + 1;
        }

        g.setColour(juce::Colour(0xffE4F9FE));
        g.drawLine(cursorX, boundingBox.getY(), cursorX, boundingBox.getBottom(), 2.0f);
    }
#endif

    GMPI_QUERYINTERFACE_METHOD(gmpi::api::IKeyListenerCallback);
	GMPI_REFCOUNT;
}; 
