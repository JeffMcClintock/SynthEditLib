#pragma once
#include <chrono>
#include "ModuleView.h"

namespace SE2
{
struct pinHit
{
	int pinIndex;
	int pinID;
	int guiPin;		// index of actual GUI pin from modules point of view, including hidden pins etc.
	int dspPin;		// index of actual DSP pin from modules point of view.
	float distance; // distance to circle, or 0.f if hit lable rectangle.
	bool hitCircle;
};

class ModuleViewStruct : public ModuleView
{
	std::string lPlugNames;
	std::string rPlugNames;
	gmpi::drawing::PathGeometry outlineGeometry;
	std::vector< pinViewInfo > plugs_;
	const float clientPadding = 2.0f;
	const int plugTextHorizontalPadding = -1; // gap between plug text and plug circle outer radius.
	gmpi::drawing::Rect clipArea;
	bool muted = false;
	pinHit hoveredPin_{ -1, -1, -1, -1, 0.0f, true };
	gmpi::drawing::Rect boundsOnMouseDown;
	bool scopeIsWave{};
	std::string hoverScopeText;
	std::unique_ptr< std::vector<float> > hoverScopeWaveform;
	float movingPeaks[512] = {};
	int movingPeaksIdx = 0;
	static std::chrono::time_point<std::chrono::steady_clock> lastClickedTime;

	std::shared_ptr<sharedGraphicResources_struct> drawingResources;
	static GraphicsResourceCache<sharedGraphicResources_struct> drawingResourcesCache;
	sharedGraphicResources_struct* getDrawingResources(gmpi::drawing::Factory& factory);
	bool hasHoverScope() const;

#if 0 // TODO
	// SynthEdit-specific.  Locate resources and make SynthEdit embed them during save-as-vst.
	int32_t ClearResourceUris() override
	{
		int32_t h;
		getHandle(h);

		GmpiResourceManager::Instance()->ClearResourceUris(h);
		return gmpi::MP_OK;
	}
#endif

	gmpi::drawing::Rect GetCpuRect();
	void RenderCpu(gmpi::drawing::Graphics& g);
	void RenderHoverScope(gmpi::drawing::Graphics& g);
	bool showCpu()
	{
		return cpuInfo != nullptr;
	}
	cpu_accumulator* cpuInfo = {};
	// retains current pin value for displaying on hoverscopes. need to add some way of getting both pin index and pin ID
	// indexed on pin ID
	std::unique_ptr < std::map<int, std::vector<uint8_t> >> editorPinValues;

public:

	ModuleViewStruct(const wchar_t* typeId, ViewBase* pParent, int handle) : ModuleView(typeId, pParent, handle) {}
	ModuleViewStruct(Json::Value* context, class ViewBase* pParent, std::map<int, class ModuleView*>& guiObjectMap);

	virtual gmpi::drawing::Rect getClipArea() override;

	gmpi::drawing::PathGeometry CreateModuleOutline(gmpi::drawing::Factory& factory);
	gmpi::drawing::PathGeometry getOutline(gmpi::drawing::Factory drawingFactory) override;
	gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) override;
	int getPinDatatype(int pinIndex);
	bool getPinGuiType(int pinIndex);
	bool isMuted() override
	{
		return muted;
	}

	int32_t setPin(ModuleView* fromModule, int32_t fromPinIndex, int32_t pinId, int32_t voice, int32_t size, const void* data) override;
    int32_t pinTransmit(int32_t pinId, int32_t size, const void* data, int32_t voice) override;

	virtual void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize) override;
	virtual void arrange(gmpi::drawing::Rect finalRect) override;
	virtual void render(gmpi::drawing::Graphics& g) override;
	bool hasRenderLayers() const override;
	void renderPluginLayer(gmpi::drawing::Graphics& g, int32_t layer) override;
	float hitTestFuzzy(int32_t flags, gmpi::drawing::Point point) override;
	pinHit getPinUnderMouse(gmpi::drawing::Point point);
	int32_t OnDoubleClicked(gmpi::drawing::Point point, int32_t flags);
	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode setHover(bool mouseIsOverMe) override;

	void OnClickedButDidntDrag() override;
	void OnCableDrag(ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, ModuleView*& bestModule, int& bestPinIndex) override;
	bool EndCableDrag(gmpi::drawing::Point point, ConnectorViewBase* dragline, int32_t keyFlags) override;

	bool isVisable() override
	{
		return true;
	}
	virtual std::unique_ptr<SE2::IViewChild> createAdorner(ViewBase* pParent) override;
	void OnCpuUpdate(class cpu_accumulator* cpuInfo) override;
	gmpi::drawing::Rect calcScopeRect(int pinIdx);
	void SetHoverScopeText(const char* text) override;
	void SetHoverScopeWaveform(std::unique_ptr< std::vector<float> > data) override;

	bool isRackModule() override
	{
		return false;
	}
//	void invalidateMeasure() override;
	void invalidateRect(gmpi::drawing::Rect* r = nullptr) {};// todo
	void invalidateMyRect(gmpi::drawing::Rect localRect);
};
}
