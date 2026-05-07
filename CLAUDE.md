# Porting Guide: Legacy SE SDK to New GMPI UI SDK

## SDK Locations
- **Old SE SDK**: `se_sdk3/` — `Drawing.h`, `mp_sdk_gui2.h`, `mp_sdk_audio.h`, `TimerManager.h`
- **New GMPI UI SDK**: `C:\SE\gmpi_ui` (referenced as `${GMPI_UI_SDK}`) — `Drawing.h`, `helpers/GmpiPluginEditor.h`, `helpers/Timer.h`
- **New GMPI Core SDK**: `C:\SE\GMPI` (referenced as `${GMPI_SDK}`) — `Core/Common.h`, `Core/Processor.h`
- **Conflict avoidance**: Use `GmpiUiDrawing.h` instead of `Drawing.h` when both old and new SDKs might be in include paths

## GUI Plugin Base Class
| Old SDK | New SDK |
|---------|---------|
| `#include "mp_sdk_gui2.h"` | `#include "helpers/GmpiPluginEditor.h"` |
| `class Foo : public gmpi_gui::MpGuiGfxBase` | `class Foo : public gmpi::editor::PluginEditor` |
| `int32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* dc)` | `ReturnCode render(drawing::api::IDeviceContext* dc)` |
| `GmpiDrawing::Graphics g(dc);` | `Graphics g(dc);` (with `using namespace gmpi::drawing;`) |
| Return `gmpi::MP_OK` | Return `ReturnCode::Ok` |
| `invalidateRect()` | `drawingHost->invalidateRect(&rect)` |

## Pin Types
| Old SDK | New SDK |
|---------|---------|
| `StringGuiPin pinFoo;` | `Pin<std::string> pinFoo;` |
| `FloatGuiPin pinFoo;` | `Pin<float> pinFoo;` |
| `IntGuiPin pinFoo;` | `Pin<int32_t> pinFoo;` |
| `BoolGuiPin pinFoo;` | `Pin<bool> pinFoo;` |
| `BlobGuiPin pinFoo;` | `Pin<std::vector<uint8_t>> pinFoo;` |
| `initializePin(pinFoo, &Foo::onUpdate);` | `pinFoo.onUpdate = [this](PinBase*) { onUpdate(); };` |
| `(std::string)pinFoo` or `pinFoo.getValue()` | `pinFoo.value` |
| Pin auto-registers in constructor via `constructingInstance` pattern — no manual init needed |

## Plugin Registration
| Old SDK | New SDK |
|---------|---------|
| `Register<Foo>::withId(L"My Plugin")` | `gmpi::Register<Foo>::withId("My Plugin")` |
| Wide string `L"..."` | Narrow string `"..."` |
| Can also use `gmpi::Register<Foo>::withXml(R"XML(...)XML")` for embedded XML |
| For invisible/placeholder GUIs: `Register<SeGuiInvisibleBase>::withId(...)` | `gmpi::Register<gmpi::editor::PluginEditor>::withId(...)` |

## Drawing API Method Names (PascalCase → camelCase)
| Old (`GmpiDrawing::`) | New (`gmpi::drawing::`) |
|------------------------|------------------------|
| `g.CreateSolidColorBrush(color)` | `g.createSolidColorBrush(color)` |
| `g.GetFactory().CreatePathGeometry()` | `g.getFactory().createPathGeometry()` |
| `g.FillGeometry(geom, brush)` | `g.fillGeometry(geom, brush)` |
| `g.FillRectangle(rect, brush)` | `g.fillRectangle(rect, brush)` |
| `g.DrawRectangle(rect, brush)` | `g.drawRectangle(rect, brush)` |
| `geometry.Open()` | `geometry.open()` |
| `sink.BeginFigure(pt, begin)` | `sink.beginFigure(pt, begin)` |
| `sink.AddArc(arc)` | `sink.addArc(arc)` |
| `sink.AddLine(pt)` | `sink.addLine(pt)` |
| `sink.EndFigure(end)` | `sink.endFigure(end)` |
| `sink.Close()` | `sink.close()` |

## Drawing Data Types
| Old (`GmpiDrawing::`) | New (`gmpi::drawing::`) |
|------------------------|------------------------|
| `GmpiDrawing::Color` | `gmpi::drawing::Color` |
| `GmpiDrawing::Point` | `gmpi::drawing::Point` |
| `GmpiDrawing::Rect` | `gmpi::drawing::Rect` |
| `GmpiDrawing::Size` | `gmpi::drawing::Size` |
| `GmpiDrawing::PointL` | `gmpi::drawing::PointL` |
| `GmpiDrawing::RectL` | `gmpi::drawing::RectL` |
| `GmpiDrawing::SizeL` | `gmpi::drawing::SizeL` |
| Binary compatible — use `reinterpret_cast` at boundaries if needed |

## Named Colors
| Old | New |
|-----|-----|
| `Color::Black` or just `Black` | `Colors::Black` |
| `Color::White` | `Colors::White` |
| `Bisque` | `Colors::Bisque` |
| All in `gmpi::drawing::Colors` namespace |

## Free Functions (replace old member functions on data types)
| Old | New |
|-----|-----|
| `rect.empty()` | `gmpi::drawing::empty(rect)` |
| `rect.getWidth()` | `gmpi::drawing::getWidth(rect)` |
| `rect.getHeight()` | `gmpi::drawing::getHeight(rect)` |
| `rect.Offset(dx, dy)` | Manual: `rect.left += dx; rect.right += dx; ...` |
| `rect.Union(other)` | `gmpi::drawing::unionRect(rect, other)` |
| `rect.Intersect(other)` | `gmpi::drawing::intersectRect(rect, other)` |
| `Color::FromHexString(s)` | `gmpi::drawing::colorFromHexString(s)` |
| `Color::FromHex(h)` | `gmpi::drawing::colorFromHex(h)` |
| New data types have NO operator overloads (`+`, `-`, `+=`) — do arithmetic manually |
| New data types use aggregate initialization: `RectL{l,t,r,b}` not `RectL(l,t,r,b)` |

## Timer
| Old | New |
|-----|-----|
| `#include "TimerManager.h"` | `#include "helpers/Timer.h"` |
| `class Foo : public TimerClient` | Same base class name |
| `bool OnTimer() override` | `bool onTimer() override` |
| `StartTimer(ms)` | `startTimer(ms)` |
| `StopTimer()` | `stopTimer()` |
| Timer.cpp must be compiled — add `${GMPI_UI_SDK}/helpers/Timer.cpp` to cmake sources |

## Input Handling
| Old | New |
|-----|-----|
| `OnMouseDown(flags, point)` | `onPointerDown(point, flags)` — note param order swap |
| `OnMouseMove(flags, point)` | `onPointerMove(point, flags)` |
| `OnMouseUp(flags, point)` | `onPointerUp(point, flags)` |
| `GmpiDrawing_API::MP1_POINT` | `gmpi::drawing::Point` |

## CMake
- Use `gmpi_plugin()` macro from `${GMPI_SDK}/gmpi_plugin.cmake` for pure new-SDK plugins
- For mixed old/new SDK, write custom CMakeLists (old `mp_sdk_common.cpp`'s `MP_GetFactory` conflicts with new `Common.cpp`)
- Timer is optional — add `${GMPI_UI_SDK}/helpers/Timer.cpp` to source files manually
- Include directories needed: `${GMPI_SDK}`, `${GMPI_SDK}/Core`, `${GMPI_UI_SDK}`
