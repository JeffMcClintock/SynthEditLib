// Compiled into EditorLib so both SynthEdit2 (GUI) and SynthEditCL (headless)
// link it. EditorLib doesn't use precompiled headers, so we don't include
// pch.h here — none of the code below needs WinRT/UWP types anyway. The
// vcxproj entry for this file is removed; SynthEdit2 picks up the symbols
// through the EditorLib link.
#include "SynthEditAppBase.h"
#include "ModuleBrowser.h"
#include "ModuleDragAndDropManager.h"
#include "Notify_msg.h"
#include "experimental/theme.h"

#ifndef _RPT0
#define _RPT0(...) ((void)0)
#endif

using namespace gmpi;
using namespace gmpi::drawing;

ModuleBrowser::ModuleBrowser(CSynthEditAppBase* p_app)
{
    app = p_app;

    // manually hook up category tree so that expanding a node recreates entire UI
    // eventually this relationship should be automatic, and the rebuild should be restricted to the one view
    state.moduleGroups.observers2.push_back(this);

    // keys Not working.
    setKeyboardHandler(
        [this](std::string glyph)
        {
            if (glyph == "\x1b") // <ESC> to cancel drag
            {
                if (auto m = app->getModuleDragAndDropManager())
                    m->EndDrag();
            }
        }
    );
}

ModuleBrowser::~ModuleBrowser()
{
    app->UnRegisterObserver(this);
    clear();
}

void ModuleBrowser::OnModelWillChange()
{
    redraw();
}

void ModuleBrowser::OnNotify(Notifier* sender, int lHint, void* pHint)
{
    if (OM_UPDATE_MODULE_BROWSER == lHint)
    {
        moduleList.clear();
        const bool includePrefabs = true;
        app->ExportModules(moduleList, includePrefabs);

        state.moduleGroups.moduleGroups.clear();
        state.formIsDirty = true;

        renderVisuals();
    }

    if (OM_DRAG_NEW_MODULE == lHint)
    {
        // Highlight the module being dragged. pHint is the module id c-str
        // while dragging, nullptr when the drag ends/cancels.
        int idx = -1;
        if (pHint)
        {
            const std::string draggedId(static_cast<const char*>(pHint));
            for (int i = 0; i < (int)state.moduleNames.items.size(); ++i)
            {
                if (state.moduleNames.items[i].id == draggedId)
                {
                    idx = i;
                    break;
                }
            }
        }
        state.moduleNames.setDraggedIndex(idx);
        return;
    }

    if (lHint != OM_UPDATE_BROWSER_FILTER)
        return;

    const std::wstring searchTextW(reinterpret_cast<wchar_t*>(pHint));
    state.filterText = WStringToUtf8(searchTextW);

    renderVisuals();
}

void ModuleBrowser::Init()
{
    app->RegisterObserver(this);

    const bool includePrefabs = true;
    app->ExportModules(moduleList, includePrefabs);

    renderVisuals();
}

void ModuleBrowser::updateFilter(std::string filterText)
{
    if (moduleList.empty())
        return;

    std::transform(filterText.begin(), filterText.end(), filterText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool initGroups = state.moduleGroups.moduleGroups.empty();

    state.moduleNames.items.clear();
    state.moduleNames.ObjectWillChange();

    std::vector<std::string> groupNames;
    std::vector<std::string> groupNames_lowercase;

    groupNames.push_back("All");
    groupNames_lowercase.push_back("all");

    int level = 0;
    if (initGroups)
        state.moduleGroups.moduleGroups.push_back({ "/all", "All", level, true, false });

    std::string currentGroupName;
    std::string currentGroupPath;
    std::string currentGroupPathDisplay;

    for (const auto& mod : moduleList)
    {
        auto p = mod.find(L'\r'); // modules have id and name seperated by \r
        if (p != std::string::npos)
        {
#ifndef _DEBUG
            if (currentGroupPath.find("/all/Debug") != 0)
#endif
            {
                const auto moduleName = WStringToUtf8(mod.substr(0, p));

                auto moduleName_lowercase = moduleName;
                std::transform(moduleName_lowercase.begin(), moduleName_lowercase.end(), moduleName_lowercase.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (state.groupFilter.empty() || currentGroupPath.find(state.groupFilter) == 0 || state.groupFilter == "/all")
                {
                    if (moduleName_lowercase.find(filterText) != std::string::npos || groupNames_lowercase.back().find(filterText) != std::string::npos)
                    {
                        auto p2 = mod.find_last_of(L'\r'); // name and flavor seperated by \r
                        if (p2 != p)
                        {
                            const auto id = WStringToUtf8(mod.substr(p + 1, p2 - p - 1));

                            // Skip non-creatable editor objects (connection lines should not be listed)
                            if (id == "Line")
                                continue;

                            const auto flavorStr = mod[p2 + 1];
                            moduleFlavor flavor = moduleFlavor::dsp;
                            if (flavorStr == L'1')
                                flavor = moduleFlavor::gui;
                            if (flavorStr == L'2')
                                flavor = moduleFlavor::control;

                            state.moduleNames.items.push_back({ id, moduleName, currentGroupPathDisplay, flavor });
                        }
                        else
                        {
                            assert(false);
                            //const auto id = WStringToUtf8(mod.substr(p + 1));
                            //state.moduleNames.items.push_back({ id, moduleName, currentGroupPathDisplay, moduleFlavor::dsp });
                        }
                    }
                }
            }
        }
        else
        {
            if (mod.find(L'+') == 0) // new group
            {
                ++level;
                if (initGroups && state.moduleGroups.moduleGroups.back().level < level)
                    state.moduleGroups.moduleGroups.back().isTerminal = false; // parent is not terminal

                currentGroupName = WStringToUtf8(mod.substr(2, p));
                groupNames.push_back(currentGroupName);

                auto currentGroupName_lower(currentGroupName);
                std::transform(currentGroupName_lower.begin(), currentGroupName_lower.end(), currentGroupName_lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                groupNames_lowercase.push_back(currentGroupName);
            }
            else if (mod.find(L'-') == 0) // end group
            {
                --level;
                groupNames.pop_back();
                groupNames_lowercase.pop_back();

                auto it = currentGroupPath.find_last_of('/');
                if (it != std::string::npos)
                    currentGroupPath = currentGroupPath.substr(0, it);
            }

            assert(mod.find(L'-') == 0 || mod.find(L'+') == 0);

            std::string indentSpaces;
            currentGroupPath.clear();
            currentGroupPathDisplay.clear();
            for (const auto& s : groupNames_lowercase)
            {
                // omit redundant 'all' on displaypath
                if (!currentGroupPath.empty())
                {
                    if (!currentGroupPathDisplay.empty())
                        currentGroupPathDisplay += "-";
                    currentGroupPathDisplay += s;
                }

                currentGroupPath += "/" + s;
                indentSpaces += " ";
            }

            if (initGroups && mod.find(L'+') == 0) // new group
            {
#ifndef _DEBUG
                if (currentGroupPath.find("/all/Debug") != 0)
#endif
                {
                    state.moduleGroups.moduleGroups.push_back({ currentGroupPath, currentGroupName, level, false, true });
                }
            }
        }
    }

    // sort state.moduleNames by modulegroup, then name.
    std::sort(state.moduleNames.items.begin(), state.moduleNames.items.end(), [](const GroupedListItem& a, const GroupedListItem& b)
        {
            if (a.group < b.group)
                return true;
            if (a.group > b.group)
                return false;

            return a.name < b.name;
        });
}

struct ListView : public gmpi::ui::builder::View
{
    gmpi::drawing::Rect bounds;
    struct MbListViewModel* model = {};
    std::function<void(const std::pair<std::string, std::string>&)> onSelected;
    std::function<void(const std::string& moduleId)> onDoubleClicked;

    // ultimatly will take parent bounds into account
    gmpi::drawing::Rect getBounds() const override
    {
        return bounds;
    }
    void Render(gmpi_forms::Environment* env, gmpi::forms::primitive::Canvas& canvas) const override;
};

// experiment
// kind of smart pointer that automatically creates and adds object ('me')
struct ListView2
{
    ListView* me = {};

    ListView2()
    {
        me = new ListView();
        auto& result = *gmpi::ui::builder::ThreadLocalCurrentBuilder;
        result.push_back(std::unique_ptr<gmpi::ui::builder::View>(me));
    }

    ListView* operator->() const
    {
        return me;
    }
};

struct ListView3
{
    struct init
    {
        gmpi::drawing::Rect bounds = {};
        MbListViewModel* model = {};
        std::function<void(const std::pair<std::string, std::string>&)> onSelected = {};
        std::function<void(const std::string& moduleId)> onDoubleClicked = {};
    };

    ListView* me = {};

    ListView3(ListView3::init i)
    {
        auto& result = *gmpi::ui::builder::ThreadLocalCurrentBuilder;
        me = new ListView();
        result.push_back(std::unique_ptr<gmpi::ui::builder::View>(me));
        me->bounds = i.bounds;
        me->model = i.model;
        me->onSelected = i.onSelected;
        me->onDoubleClicked = i.onDoubleClicked;

        // redraw module list when hover etc changes
        i.model->observers2.push_back(me);
    }
/* a bit confusing, access inner object.
    auto operator->() const
    {
        return me;
    }
*/
};

/* SYNTAX
    // Current
    TreeView2 detailview(&state.moduleGroups);
    detailview->bounds = listArea2;

    // Alternate (more uniform)
    TreeView2 detailview();
    detailview->model = &state.moduleGroups;
    detailview->bounds = listArea2;
*/

struct TreeView2
{
    MbTreeView* me = {};

    TreeView2(MbTreeViewModel* pmodel)
    {
        me = new MbTreeView(pmodel);
        auto& result = *gmpi::ui::builder::ThreadLocalCurrentBuilder;
        result.push_back(std::unique_ptr<gmpi::ui::builder::View>(me));
    }

    auto operator->() const
    {
        return me;
    }
};

/* SYNTAX

    TreeView3 detailview({
            .bounds = listArea2,
            .model = &state.moduleGroups,
        });
*/

struct TreeView3
{
    struct init
    {
        gmpi::drawing::Rect bounds;
        MbTreeViewModel* model = {};
        std::function<void(std::string)> onSelected = {};
    };

    MbTreeView* me = {};

    TreeView3(TreeView3::init i)
    {
        auto& result = *gmpi::ui::builder::ThreadLocalCurrentBuilder;
        me = new MbTreeView(i.model);
        result.push_back(std::unique_ptr<gmpi::ui::builder::View>(me));
        me->bounds = i.bounds;
        me->onSelected = i.onSelected;
    }

    auto operator->() const
    {
        return me;
    }
};

void ModuleBrowser::Body()
{
    auto contentArea = bounds;
    contentArea.right -= 12; // for scrollbar

    constexpr float middleSeparationwidth = 2.0f;
    const gmpi::drawing::Rect treeArea{
        bounds.left,
        bounds.top,
        bounds.left + getWidth(contentArea) * 0.5f - middleSeparationwidth,
        bounds.bottom
    };

#if 0
    // background rectangle
    auto& canvas = *gmpi::ui::builder::ThreadLocalCurrentBuilder;
    {
        auto style2 = new gmpi::forms::primitive::ShapeStyle();
        style2->strokeColor = gmpi::drawing::Colors::TransparentBlack;
        style2->fillColor = colorFromHex(0x003E3E3Eu);

        canvas.add(style2);

        auto backGroundRect = new gmpi::forms::primitive::Rectangle(style2, bounds);
        canvas.add(backGroundRect);
    }
#endif
    // List of modules area at right
    state.moduleNames.observers2.clear();

    {
        contentArea.left = floorf(contentArea.right - getWidth(contentArea) * 0.5f);
        auto scrollArea = contentArea;
        scrollArea.right = bounds.right; // let ScrollPortal place the scrollbar flush against the panel's right edge

        // scroller arround the details view.
        gmpi::ui::ScrollPortal _(scrollArea);

    #if 0
        ListView2 detailview2;
        detailview2->bounds = contentArea;
        detailview2->model = &state.moduleNames;

        detailview2->onSelected = [this](const std::pair<std::string, std::string>& key_text) {
            app->VO_Notify(OM_DRAG _NEW_MODULE, (void*)key_text.first.c_str());
            };
    #else
        ListView3 detailview2({
            .bounds = contentArea,
            .model = &state.moduleNames,
            .onSelected = [this](const std::pair<std::string, std::string>& key_text) {
                if (auto m = app->getModuleDragAndDropManager())
                    m->BeginDrag(key_text.first);
            },
            .onDoubleClicked = [this](const std::string& moduleId) {
                if (auto m = app->getModuleDragAndDropManager())
                    m->InsertAtViewCenter(moduleId);
            }
            });
    #endif
    }

#if 0
    // manually allocate and add
    auto detailview = new gmpi_form_builder::ListView();
    {
        result.push_back(std::unique_ptr<gmpi::ui::builder::View>(detailview));
        detailview->bounds = treeArea;
        detailview->items = &state.modules.items;

        detailview->onSelected = [this](const std::pair<std::string, std::string>& key_text)
            {
                app->VO_Notify(OM_DRAG _NEW_MODULE, (void*)key_text.first.c_str());
            };
    }
#endif

    // categories tree view, at left.
    {
        // scroller around the tree view (named so it has its own scroll state, independent of the module list)
        gmpi::ui::ScrollPortal _(treeArea, "tree");

        // inspired by CLAY. Better?
        TreeView3 detailview3({
            .bounds = treeArea,
            .model = &state.moduleGroups,
            .onSelected = [this](std::string id) {

                // get the scroll of the details view (right-side module list).
                const auto scroll = dynamic_cast<gmpi_forms::State<float>*>(env.findState("ScrollPortal.scroll"));
                if(scroll)
                    scroll->set(0.0f); // reset scrolling on module list to zero

                state.groupFilter = id;
                updateFilter(state.filterText);
                redraw();
            }
            });
    }
}

void ModuleBrowser::renderVisuals()
{
    updateFilter(state.filterText);

    Form::renderVisuals();
}

struct TextChunk
{
    std::string group;
    std::string text; // several lines of text
    int lineCount;
    moduleFlavor flavor;
};

// more granular approach
void ListView::Render(gmpi_forms::Environment* env, gmpi::forms::primitive::Canvas& canvas) const
{
 //   _RPTN(0, "\nListView::Render ================================\n");

    constexpr float itemHeight = 13;
    constexpr float itemMargin = 2;
    constexpr auto lineHeight = itemHeight + itemMargin;

    gmpi::drawing::Rect textBoxArea = getBounds();
    textBoxArea.bottom = textBoxArea.top + itemHeight;

    // styles for text on list of modules (color-coded by type)
    const bool isDark = gmpi::ui::themeModeStorage() == gmpi::ui::ThemeMode::Dark;
    auto styleDsp = new gmpi::forms::primitive::TextBoxStyle(
        isDark ? colorFromHex(0xF2F2F2u) : colorFromHex(0x111111u), Colors::TransparentBlack);
    styleDsp->setLineSpacing(itemHeight + itemMargin);
    canvas.add(styleDsp);

    auto styleGui = new gmpi::forms::primitive::TextBoxStyle(
        isDark ? colorFromHex(0xD5E0F2u) : colorFromHex(0x2B4070u), Colors::TransparentBlack);
    styleGui->setLineSpacing(itemHeight + itemMargin);
    canvas.add(styleGui);

    auto styleControl = new gmpi::forms::primitive::TextBoxStyle(
        isDark ? colorFromHex(0xDBEFD5u) : colorFromHex(0x2B6020u), Colors::TransparentBlack);
    styleControl->setLineSpacing(itemHeight + itemMargin);
    canvas.add(styleControl);

    const float headerHeight = 8;
    const float headerMargin = 2;
    const auto headerTotalHeight = headerHeight + headerMargin;
    auto groupHeaderStyle = new gmpi::forms::primitive::TextBoxStyle(gmpi::ui::currentTheme().labelText, Colors::TransparentBlack);
    groupHeaderStyle->bodyHeight = headerHeight;
    groupHeaderStyle->setLineSpacing(headerHeight + headerMargin);
    canvas.add(groupHeaderStyle);

    auto styleHovered = new gmpi::forms::primitive::TextBoxStyle(Colors::White, Colors::DimGray);
    canvas.add(styleHovered);

    auto styleDragged = new gmpi::forms::primitive::TextBoxStyle(Colors::Black, Colors::Yellow);
    canvas.add(styleDragged);

    auto& canvas2 = canvas;

    // consolidate several lines of text into one (to save rendering/conversion overhead)
    std::vector<TextChunk> textChunks;
    {
        std::string currentGroup;
        moduleFlavor currentFlavor = moduleFlavor::dsp;
        int i = 0;
        for (const auto& s : model->items)
        {
            if (0 == (i & 7) || s.group != currentGroup || s.flavor != currentFlavor)
            {
                textChunks.push_back({ s.group, s.name, 1, s.flavor });
                currentGroup = s.group;
                currentFlavor = s.flavor;
                i = 1;
            }
            else
            {
                textChunks.back().text += '\n' + s.name;
                textChunks.back().lineCount++;
            }
        }
    }

    textBoxArea = offsetRect(textBoxArea, { -textBoxArea.left, -textBoxArea .top}); // get local bounds
    auto chunkArea = textBoxArea;

    {
        std::string currentGroup;
        int index = 0;
        for (auto& chunk : textChunks)
        {
            // Group header
            if (chunk.group != currentGroup/* || chunk.flavor != currentFlavor*/)
            {
                chunkArea.bottom = chunkArea.top + headerTotalHeight;

                canvas2.add(
                    new gmpi::forms::primitive::TextBox(groupHeaderStyle, chunkArea, chunk.group)
                );
                chunkArea.top = chunkArea.bottom;
                currentGroup = chunk.group;
            }

            const auto chunkHeight = lineHeight * chunk.lineCount;
            chunkArea.bottom = chunkArea.top + chunkHeight;

            // items (in groups of 8)
            gmpi::forms::primitive::TextBoxStyle* s;
            switch (chunk.flavor)
            {
            default:
            case moduleFlavor::dsp:
                s = styleDsp;
                break;
            case moduleFlavor::gui:
                s = styleGui;
                break;
            case moduleFlavor::control:
                s = styleControl;
                break;
            }

            canvas2.add(
                new gmpi::forms::primitive::TextBox(s, chunkArea, chunk.text)
            );

            // mouse detector for this chunk of module names.
            auto clickDetector = new gmpi::forms::primitive::RectangleMouseTarget(chunkArea);

#ifdef _DEBUG
            clickDetector->debugName = std::to_string(index);
#endif

            auto lmodel = model; // minimize use of 'this' to access member variable 'model'
            clickDetector->onPointerDown_callback = [this, lmodel, index, chunkArea, lineHeight](const gmpi::forms::primitive::PointerEvent* e)
                {
                    const int i = index + static_cast<int>((e->position.y - chunkArea.top) / lineHeight);
                    lmodel->setSelectedIndex(i); // dosn't do much.
                    const auto moduleId = lmodel->items[i].id;

                    if ((e->flags & static_cast<int32_t>(gmpi::api::PointerFlags::Double)) && onDoubleClicked)
                    {
                        onDoubleClicked(moduleId); // immediate insert at view centre
                    }
                    else
                    {
                        onSelected({ moduleId, moduleId }); // kicks off module-insert drag
                    }
                };

            // handle hovering
            clickDetector->onPointerMove_callback = [lmodel, index, chunkArea, lineHeight](gmpi::drawing::Point p)
                {
                    const int i = index + static_cast<int>((p.y - chunkArea.top) / lineHeight);
 //                   _RPTN(0, "lmodel->setHoveredIndex( %d\n", i);
                    lmodel->setHoveredIndex(i, true);
                };

            clickDetector->onHover_callback = [lmodel, clickDetector](bool hovered)
                {
//                    _RPTN(0, "onHover_callback: start idx %s, hovered=%d\n", clickDetector->debugName.c_str(), (int)hovered);
                    // when view is re rendered, the old scrollview looses the mouse and we call in here causing flickering. even though mouse is over new scrollviewer.
                    if (!hovered)
                        lmodel->setHoveredIndex(-1, true);
                };

            canvas2.add(clickDetector);

            // highlighted module name
            if (model->hoveredIndex >= index && model->hoveredIndex < index + chunk.lineCount)
            {
                const int offset = model->hoveredIndex - index;
                auto highlightedArea = chunkArea;
                highlightedArea.top += lineHeight * offset;
                highlightedArea.bottom = highlightedArea.top + lineHeight;

                const auto& ModuleName = model->items[model->hoveredIndex].name;
                canvas2.add(
                    new gmpi::forms::primitive::TextBox(styleHovered, highlightedArea, ModuleName)
                );
            }

            // module currently being drag-inserted (yellow until drop or cancel)
            if (model->draggedIndex >= index && model->draggedIndex < index + chunk.lineCount)
            {
                const int offset = model->draggedIndex - index;
                auto highlightedArea = chunkArea;
                highlightedArea.top += lineHeight * offset;
                highlightedArea.bottom = highlightedArea.top + lineHeight;

                const auto& ModuleName = model->items[model->draggedIndex].name;
                canvas2.add(
                    new gmpi::forms::primitive::TextBox(styleDragged, highlightedArea, ModuleName)
                );
            }

            chunkArea.top += chunkHeight;
            index += chunk.lineCount;
        }
    }
}

void MbTreeView::Render(gmpi_forms::Environment* env, gmpi::forms::primitive::Canvas& canvas) const
{
    const float itemHeight = 13;
    const float itemMargin = 2;
    gmpi::drawing::Rect textBoxArea = getBounds();
    textBoxArea.bottom = textBoxArea.top + itemHeight;

    // default text style (use transparent background since the tree view has its own background rectangle)
    // setLineSpacing must not exceed itemHeight, otherwise CoreText can't fit a line in single-item bounds
    auto style = new gmpi::forms::primitive::TextBoxStyle(gmpi::ui::currentTheme().controlText, Colors::TransparentBlack);
    style->setLineSpacing(itemHeight);
    canvas.add(style);

    auto styleSelected = new gmpi::forms::primitive::TextBoxStyle(Colors::White, Colors::DodgerBlue);
    styleSelected->setLineSpacing(itemHeight);
    canvas.add(styleSelected);

    auto styleHovered = new gmpi::forms::primitive::TextBoxStyle(Colors::White, Colors::DimGray);
    styleHovered->setLineSpacing(itemHeight);
    canvas.add(styleHovered);

    auto styleExpander = new gmpi::forms::primitive::ShapeStyle();
    styleExpander->fillColor = gmpi::ui::currentTheme().controlText;
    canvas.add(styleExpander);
    auto styleExpanderHovered = new gmpi::forms::primitive::ShapeStyle();
    styleExpanderHovered->fillColor = Colors::Cyan;
    canvas.add(styleExpanderHovered);

    auto styleExpanderShapeClosed = new gmpi::forms::primitive::PathHolder();
    styleExpanderShapeClosed->InitPath = [](gmpi::drawing::PathGeometry& path)
        {
            auto sink = path.open();

            sink.beginFigure({ 2.5f, 1.5f }, FigureBegin::Filled);

            sink.addLine({ 7.5f, 5.0f });
            sink.addLine({ 2.5f, 8.5f });

            sink.endFigure(FigureEnd::Closed);
            sink.close();
        };
    canvas.add(styleExpanderShapeClosed);

    auto styleExpanderShapeOpen = new gmpi::forms::primitive::PathHolder();
    styleExpanderShapeOpen->InitPath = [](gmpi::drawing::PathGeometry& path)
        {
            auto sink = path.open();

            // triangle
            sink.beginFigure({ 2.5f, 8.5f }, FigureBegin::Filled);
            sink.addLine({ 7.5f, 2.5f });
            sink.addLine({ 7.5f, 8.5f });

            sink.endFigure(FigureEnd::Closed);
            sink.close();
        };
    canvas.add(styleExpanderShapeOpen);

    // background rectangle
    {
        auto style2 = new gmpi::forms::primitive::ShapeStyle();
        style2->strokeColor = gmpi::drawing::Colors::TransparentBlack;
        style2->fillColor = gmpi::ui::currentTheme().controlBackground;

        canvas.add(style2);

        auto backGRoundRect = new gmpi::forms::primitive::Rectangle(style2, getBounds());
        canvas.add(backGRoundRect);
    }

    auto& canvas2 = canvas;

    int showLevel = 0;
    int index = 0;
    for (auto& it : model->moduleGroups)
    {
        auto categoryPath = it.path;
        auto categoryName = it.name;

        if (showLevel > it.level)
        {
            showLevel = it.level;
        }

        textBoxArea.left = getBounds().left + (it.level) * 10; // space for +/-

        const char* prefix = {};

        // expandable nodes have a '+' or '-' button
        if (!it.isTerminal)
        {
            if (it.isExpanded)
            {
                // expand only if parent also is expanded
                if (showLevel == it.level)
                    showLevel = it.level + 1;
                prefix = "-";
            }
            else
            {
                prefix = "+";
            }
        }

        if (showLevel >= it.level)
        {
            if (prefix)
            {
                // expand toggle button
                auto toggleArea = textBoxArea;
                toggleArea.right = toggleArea.left + 10;

                auto s = styleExpander;
                if (index == model->hoveredIndex)
                {
                    s = styleExpanderHovered;
                }

                auto p = styleExpanderShapeClosed;
                if (it.isExpanded)
                {
                    p = styleExpanderShapeOpen;
                }

                canvas2.add(
                    new gmpi::forms::primitive::Shape(p, s, toggleArea)
                );

                // clicks expand treeview parent nodes.
                auto clickDetector = new gmpi::forms::primitive::RectangleMouseTarget(toggleArea);
                canvas2.add(clickDetector);
                auto lmodel = model;
                clickDetector->onPointerDown_callback = [lmodel, index](const gmpi::forms::primitive::PointerEvent*)
                    {
                        lmodel->ToggleExpanded(index);
                    };
                clickDetector->onHover_callback = [lmodel, index](bool hovered)
                    {
                        lmodel->setHoveredIndex(index, hovered);
                    };
            }

            textBoxArea.left += 10;

            auto s = style;
            if (index == model->selectedIndex)
            {
                s = styleSelected;
            }
            else if (index == model->hoveredIndex)
            {
                s = styleHovered;
            }

            canvas2.add(
                new gmpi::forms::primitive::TextBox(s, textBoxArea, categoryName)
            );

            // clicks on categories trigger refresh of filtered modulelist.
            auto clickDetector = new gmpi::forms::primitive::RectangleMouseTarget(textBoxArea);
            auto lmodel = model; // minimize use of 'this' to access member variable 'model'
            clickDetector->onPointerDown_callback = [this, categoryPath, lmodel, index](const gmpi::forms::primitive::PointerEvent*)
                {
                    lmodel->setSelectedIndex(index);
                    onSelected(categoryPath);
                };

            clickDetector->onHover_callback = [lmodel, index](bool hovered)
                {
                    lmodel->setHoveredIndex(index, hovered);
                };

            canvas2.add(clickDetector);

            textBoxArea.top += itemHeight + itemMargin;
            textBoxArea.bottom += itemHeight + itemMargin;
        }
        ++index;
    }
}

gmpi::ReturnCode ModuleBrowser::arrange(const gmpi::drawing::Rect* finalRect)
{
    bounds = *finalRect;

    state.formIsDirty = true;
    Invalidate({});

    return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode ModuleBrowser::render(gmpi::drawing::api::IDeviceContext* drawingContext)
{
    // Theme change requires full visual rebuild (new colors for backgrounds, text, etc.)
    // The mode comparison below is what decides that, and always was.
    //
    // consumeThemeChanged() used to be called here as well, to beat Form::DoUpdates
    // to the flag. It no longer has to: DoUpdates consumes against its own counter
    // through the versioned overload, so it cannot take anything this view needed.
    // The no-argument overload it used keeps ONE static shared by every caller in
    // the process, so two views watching through it would race in any case.
    const auto currentMode = gmpi::ui::themeModeStorage();
    if (currentMode != state.lastRenderedTheme)
        state.formIsDirty = true;

    if (state.formIsDirty)
    {
        state.formIsDirty = false;
        state.lastRenderedTheme = currentMode;
        renderVisuals();
    }

    gmpi::drawing::Graphics g(drawingContext);
    g.clear(gmpi::ui::currentTheme().controlBackground);

    return Form::render(drawingContext);
}

gmpi::ReturnCode ModuleBrowser::onPointerMove(gmpi::drawing::Point point, int32_t flags)
{
    if (pointerDragCallback)
    {
        const auto newPoint = transformPoint(reverseTransform, point);
        pointerDragCallback({ newPoint.x - mousePoint.x, newPoint.y - mousePoint.y });
        mousePoint = newPoint;

        // During mouse-capture drag, Windows starves WM_PAINT behind WM_MOUSEMOVE.
        // Force the host to schedule a paint each move so the scroll (or any drag-driven
        // visual) updates in real time instead of only on mouse release.
        redraw();
    }
    else
    {
        Form::onPointerMove(point, flags);
    }
    return gmpi::ReturnCode::Ok;
}

gmpi::ReturnCode ModuleBrowser::onPointerUp(gmpi::drawing::Point point, int32_t flags)
{
    bool isCaptured{};
    inputhost->getCapture(isCaptured);
    if (isCaptured)
    {
        inputhost->releaseCapture();
        pointerDragCallback = nullptr;
    }
    else
    {
        Form::onPointerUp(point, flags);
    }

    return gmpi::ReturnCode::Ok;
}

// IPointerBoss
void ModuleBrowser::captureMouse(std::function<void(gmpi::drawing::Size)> callback)
{
    pointerDragCallback = callback;
    inputhost->setCapture();
}

// IMpGraphics2
//gmpi::ReturnCode ModuleBrowser::getToolTip(MP1_POINT point, gmpi::IString* returnString) { return gmpi::ReturnCode::Ok; }

gmpi::ReturnCode ModuleBrowser::measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) { return gmpi::ReturnCode::Ok; }

