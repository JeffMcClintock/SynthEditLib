// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/ImageCache.h"
#include <algorithm>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

class PlainImageGui final : public PluginEditor, public gmpi::api::IDrawingLayer, gmpi_helper::ImageCacheClient
{
	Pin<std::string> pinFilename;
	Pin<int32_t> pinStretchMode;

	Bitmap bitmap_;
	gmpi_helper::ImageMetadata* bitmapMetadata_ = nullptr;

	enum class StretchMode { Fixed, Tiled, Stretch };

	void redraw()
	{
		if (drawingHost)
		{
			drawingHost->invalidateRect(&bounds);
		}
	}

	void onSetFilename()
	{
		bitmap_ = {};

		if (drawingHost)
		{
			gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
			drawingHost->getDrawingFactory(unknown.put());
			auto factory = unknown.as<gmpi::drawing::api::IFactory>();

			if (factory)
			{
				const auto& filename = pinFilename.value;
				bitmap_ = GetImage(factory.get(), filename.c_str(), nullptr, &bitmapMetadata_);
			}
		}

		if (drawingHost)
			drawingHost->invalidateMeasure();

		redraw();
	}

	void onSetMode()
	{
		if (drawingHost)
			drawingHost->invalidateMeasure();
	}

public:
	PlainImageGui()
	{
		pinFilename.onUpdate = [this](PinBase*) { onSetFilename(); };
		pinStretchMode.onUpdate = [this](PinBase*) { onSetMode(); };
	}

	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}

	int32_t release() override
	{
		return PluginEditor::release();
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		switch (pinStretchMode.value)
		{
		case (int)StretchMode::Fixed:
			if (!AccessPtr::get(bitmap_))
			{
				*returnDesiredSize = Size{ 10.0f, 10.0f };
			}
			else
			{
				auto fs = bitmapMetadata_->getPaddedFrameSize();
				returnDesiredSize->width = fs.width;
				returnDesiredSize->height = fs.height;
			}
			break;

		case (int)StretchMode::Tiled:
		case (int)StretchMode::Stretch:
			*returnDesiredSize = *availableSize;
			break;
		}

		return ReturnCode::Ok;
	}

	ReturnCode renderLayer(drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		if (layer != -2)
			return ReturnCode::NoSupport;

		Graphics g(drawingContext);

		if (!AccessPtr::get(bitmap_))
		{
			auto fallbackBrush = g.createSolidColorBrush(Colors::Gray);
			g.fillRectangle(bounds, fallbackBrush);
			return ReturnCode::Ok;
		}

		auto bitmapSize = bitmap_.getSize();
		Rect bitmapRect{ 0.0f, 0.0f, static_cast<float>(bitmapSize.width), static_cast<float>(bitmapSize.height) };

		switch (pinStretchMode.value)
		{
		case (int)StretchMode::Fixed:
		{
			bitmapRect.right = (std::min)(bitmapRect.right, bounds.right - bounds.left);
			bitmapRect.bottom = (std::min)(bitmapRect.bottom, bounds.bottom - bounds.top);
			g.drawBitmap(bitmap_, bitmapRect, bitmapRect);
		}
		break;

		case (int)StretchMode::Tiled:
		{
			auto brush = g.createBitmapBrush(bitmap_);
			Rect localRect{ 0.0f, 0.0f, bounds.right - bounds.left, bounds.bottom - bounds.top };
			g.fillRectangle(localRect, brush);
		}
		break;

		case (int)StretchMode::Stretch:
		{
			Rect localRect{ 0.0f, 0.0f, bounds.right - bounds.left, bounds.bottom - bounds.top };
			g.drawBitmap(bitmap_, localRect, bitmapRect);
		}
		break;
		}

		return ReturnCode::Ok;
	}

	ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};

		if ((*iid) == gmpi::api::IDrawingLayer::guid)
		{
			*returnInterface = static_cast<gmpi::api::IDrawingLayer*>(this);
			PluginEditor::addRef();
			return ReturnCode::Ok;
		}

		return PluginEditor::queryInterface(iid, returnInterface);
	}
};

namespace
{
auto r = gmpi::Register<PlainImageGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Plain Image" name="Plain Image" category="Sub-Controls">
	<GUI>
		<Pin name="Filename" datatype="string" metadata="filename"/>
		<Pin name="Stretch Mode" datatype="int" default="0" metadata="Fixed,Tiled,Stretch"/>
	</GUI>
</Plugin>
)XML");
}
