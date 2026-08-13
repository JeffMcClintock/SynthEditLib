#pragma once
#include "helpers/NativeUi.h"
#include "experimental/primatives.h"
#include "experimental/forms.h"
#include "experimental/builders.h"
#include "experimental/theme.h"
#include "notify.h"

#ifndef _RPTN
#define _RPTN(...) ((void)0)
#endif
#ifndef _RPT0
#define _RPT0(...) ((void)0)
#endif

class CSynthEditAppBase;

// one node in the tree view that shows module categories
struct categoryState
{
    std::string path;
    std::string name;
    int level = 0;
    bool isExpanded = false;
    bool isTerminal = true;
};

struct MbTreeViewModel : public gmpi_forms::IObservableObject
{
    mutable std::vector<categoryState> moduleGroups;
    mutable int selectedIndex = -1;
    mutable int hoveredIndex = -1;

    void ToggleExpanded(int i) const
    {
        auto& isExpanded = moduleGroups[i].isExpanded;

        // todo defer to next frame?
        isExpanded = !isExpanded;
        ObjectWillChange();
    };

    void setSelectedIndex(int i)
    {
        if (selectedIndex != i)
        {
            selectedIndex = i;
            ObjectWillChange();
        }
    }

    void setHoveredIndex(int i, bool hovered)
    {
        const auto prev = hoveredIndex;

        if (hovered)
        {
            hoveredIndex = i;
        }
        else
        {
            if(hoveredIndex == i)
                hoveredIndex = -1;
        }

        if(prev != hoveredIndex)
            ObjectWillChange();
    }
};

struct MbTreeView : public gmpi::ui::builder::View
{
    gmpi::drawing::Rect bounds;
    struct MbTreeViewModel* model = {};
    std::function<void(std::string)> onSelected;

    MbTreeView(MbTreeViewModel* pmodel) : model(pmodel)
    {
        model->observers2.push_back(this);
    }

    ~MbTreeView()
    {
        auto& observers = model->observers2;
        auto it = std::find(observers.begin(), observers.end(), this);
        if (it != observers.end())
            observers.erase(it);
    }

    // ultimatly will take parent bounds into account
    gmpi::drawing::Rect getBounds() const override
    {
        return bounds;
    }

    void Render(gmpi_forms::Environment* env, gmpi::forms::primitive::Canvas& canvas) const override;
};

enum class moduleFlavor { dsp, gui, control };
struct GroupedListItem
{
    std::string id;
    std::string name;
    std::string group;
    moduleFlavor flavor = moduleFlavor::dsp;
};

struct MbListViewModel : public gmpi_forms::IObservableObject
{
    mutable std::vector < GroupedListItem > items;
    mutable int selectedIndex = -1;
    mutable int hoveredIndex = -1;
    mutable int draggedIndex = -1; // module currently being drag-inserted; -1 when not dragging

    void setSelectedIndex(int i)
    {
        if (selectedIndex != i)
        {
            selectedIndex = i;
            ObjectWillChange();
        }
    }

    void setHoveredIndex(int i, bool hovered)
    {
        const auto prev = hoveredIndex;

        if (hovered)
        {
            hoveredIndex = i;
        }
        else
        {
            hoveredIndex = -1;
        }

        if (prev != hoveredIndex)
        {
            ObjectWillChange();
//            _RPTN(0, "hovered changed %d => %d\n", prev, hoveredIndex);
        }
    }

    void setDraggedIndex(int i)
    {
        if (draggedIndex != i)
        {
            draggedIndex = i;
            ObjectWillChange();
        }
    }
};

struct MbFormState
{
    std::string filterText;
    std::string groupFilter;

    MbTreeViewModel moduleGroups;
    MbListViewModel moduleNames;

    bool formIsDirty = true;
    gmpi::ui::ThemeMode lastRenderedTheme = gmpi::ui::ThemeMode::Dark;
};

class ModuleBrowser : public gmpi::ui::Form, public Notifiable, public gmpi_forms::IObserver
{
    MbFormState state;
    std::list< std::wstring > moduleList;
    gmpi::drawing::Rect bounds = {};
    std::function<void(gmpi::drawing::Size)> pointerDragCallback;

public:
     CSynthEditAppBase* app = {};

     ModuleBrowser(CSynthEditAppBase* p_app);
    ~ModuleBrowser();

    void Init();
    void Body() override;

    void OnNotify(Notifier* sender, int lHint, void* pHint = 0) override;

    void renderVisuals() override;
    void updateFilter(std::string filterText);

    gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override;
    gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;
    gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point, int32_t flags) override;
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point, int32_t flags) override;

    // IPointerBoss
    void captureMouse(std::function<void(gmpi::drawing::Size)> callback) override;

    // gmpi_forms::IObserver
    void OnModelWillChange() override;
};
