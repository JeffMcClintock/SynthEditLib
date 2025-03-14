#pragma once
#include "helpers/Timer.h"

struct SsgNumberEditClient
{
    virtual void repaintText() = 0;
    virtual void setEditValue(std::string value) = 0;
    virtual void endEditValue() = 0;
};

class GlyphArrangement
{
public:
	struct Glyph
	{
        char c; // ASCII is ok for numbers.
        float left{};
        float right{};
	};
    float height{};

	std::vector<Glyph> glyphs;

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

            glyphs.push_back({ c, sizeCumulative.width - boundsIndividual.width, sizeCumulative.width });
        }
    }

	gmpi::drawing::Rect getGlyfBounds(int i) const
	{
		return { glyphs[i].left, 0.0f, glyphs[i].right, height};
	}
};

class SsgNumberEdit : public gmpi::TimerClient, public gmpi::api::IKeyListenerCallback // juce::Component, public juce::Timer, public juce::ModalComponentManager::Callback
{
    SsgNumberEditClient& client;
    std::string text;
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

    std::string unsavedText() const { return text; }

    void show(gmpi::api::IDialogHost* dialogHost, std::string ptext, const gmpi::drawing::Rect* bounds)
    {
        if (text != ptext)
            numberEditGlyfs.glyphs.clear();

        text = ptext;

        selectedFrom = 0;
        selectedTo = text.length();
        cursorPos = text.length();

        // grab Keyboard Focus
        gmpi::shared_ptr<gmpi::api::IUnknown> ret;
        dialogHost->createKeyListener(bounds, ret.put());
		listener = ret.as<gmpi::api::IKeyListener>();

        listener->showAsync(static_cast<gmpi::api::IKeyListenerCallback*>(this));

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
        client.repaintText();
        listener = nullptr;
    }

    void onKeyDown(int32_t key, int32_t flags) override
    {
		const auto textBefore = text;

        switch (key)
        {
            case 0x0D: // <ENTER>
            {
                client.setEditValue(text);
                hide();
            }
            break;

            case 0x1B: // <ESC>
            {
                hide();
            }
            break;

        case 0x08: // <BACKSPACE>
        {
            if (selectedFrom == selectedTo)
            {
                if (cursorPos > 0)
                {
                    text = text.substr(0, cursorPos - 1) + text.substr(cursorPos);
                    cursorPos--;
                }
            }
            else
            {
                text = text.substr(0, selectedFrom) + text.substr(selectedTo);
                cursorPos = selectedFrom;
                selectedTo = selectedFrom;
            }
        }
        break;

        case 0x2E: // <DEL>
        {
            if (selectedFrom == selectedTo)
            {
                if (cursorPos < text.length())
                {
                    text = text.substr(0, cursorPos) + text.substr(cursorPos + 1);
                }
            }
            else
            {
                text = text.substr(0, selectedFrom) + text.substr(selectedTo);
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
            // const auto k = static_cast<char>(key.getKeyCode());
            switch (key)
            {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': case '.': case '+': case '-':
            {
                if (selectedFrom != selectedTo)
                {
                    text = text.substr(0, selectedFrom) + (char)key + text.substr(selectedTo);
                    cursorPos = selectedFrom + 1;
                }
                else
                {
                    text = text.substr(0, cursorPos) + (char)key + text.substr(cursorPos);
                    cursorPos++;
                }
                selectedFrom = selectedTo = cursorPos;
            }
            break;

            case 'a':
            case 'A':
            {
                // <ctrl> + A : Select All
                if (flags & gmpi::api::GG_POINTER_KEY_CONTROL)
                //if (key.getModifiers().isCommandDown())
                {
                    selectedFrom = 0;
                    selectedTo = text.length();
                    cursorPos = text.length();
                }
            }
            break;
            };
        }
        break;
        };

        timerCounter = 0; // show cursor
        client.repaintText();

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
    }

	void setText(std::string newText)
	{
		text = newText;
		numberEditGlyfs.glyphs.clear();
		client.repaintText();
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
        if (numberEditGlyfs.glyphs.empty())
        {
            numberEditGlyfs.init(text, textFormat);
        }

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
        g.drawTextU(text, textFormat, bounds, brush);

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

                g.drawTextU(text, textFormat, bounds, brush);
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
